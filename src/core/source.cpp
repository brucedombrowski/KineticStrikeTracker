#include "kst/source.hpp"

#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "kst/http.hpp"
#include "kst/json.hpp"

namespace kst::source {

namespace {

using model::Observation;
using model::SourceClass;

std::string url_encode(std::string_view s) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    for (char ch : s) {
        const auto c = static_cast<unsigned char>(ch);
        if (std::isalnum(c) != 0 || ch == '-' || ch == '_' || ch == '.' ||
            ch == '~') {
            out.push_back(ch);
        } else {
            out.push_back('%');
            out.push_back(kHex[c >> 4]);
            out.push_back(kHex[c & 0x0F]);
        }
    }
    return out;
}

std::string num(double v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%g", v);
    return buf;
}

// Agencies publish a default depth when depth is not resolvable. Treating
// such a value as a measurement is the systematic false-positive generator
// REQ-5.2 warns about, so it is flagged at normalisation time.
bool looks_like_fixed_depth(double depth_km) {
    return depth_km == 0.0 || depth_km == 10.0 || depth_km == 33.0 ||
           depth_km == 35.0;
}

Fetch http_fetch(const std::string& url) {
    Fetch f;
    f.request_url = url;
    auto r = http::get(url);
    if (!r) {
        f.error = r.error->message;
        f.http_status = r.error->status;
        return f;
    }
    f.raw_body = r->body;
    f.sha256 = r->sha256;
    f.requested_at = r->requested_at;
    f.content_type = r->content_type;
    f.http_status = r->status;
    return f;
}

// --- USGS: FDSN event service, GeoJSON (REQ-2.2) ---

class UsgsAdapter final : public Adapter {
  public:
    std::string id() const override { return "usgs"; }
    SourceClass source_class() const override { return SourceClass::Instrumental; }
    std::string data_licence() const override {
        return "Public domain (US Government work)";
    }
    std::string attribution() const override {
        return "U.S. Geological Survey Earthquake Hazards Program";
    }

    Fetch fetch(const Query& q) const override {
        std::string url =
            "https://earthquake.usgs.gov/fdsnws/event/1/query?format=geojson"
            "&orderby=time-asc&limit=20000"
            "&starttime=" + url_encode(q.start_time) +
            "&endtime=" + url_encode(q.end_time) +
            "&minlatitude=" + num(q.min_latitude) +
            "&maxlatitude=" + num(q.max_latitude) +
            "&minlongitude=" + num(q.min_longitude) +
            "&maxlongitude=" + num(q.max_longitude);
        Fetch f = http_fetch(url);
        if (f.ok()) f.observations = parse(f.raw_body, &f.error);
        return f;
    }

    std::vector<Observation> parse(std::string_view body,
                                   std::string* error) const override {
        std::vector<Observation> out;
        auto doc = json::parse(body);
        if (!doc) {
            if (error) *error = "USGS GeoJSON parse failed: " + doc.error->message;
            return out;
        }
        const json::Value* features = doc->find("features");
        if (!features || features->type() != json::Value::Type::Array) {
            if (error) *error = "USGS response has no features array";
            return out;
        }
        for (const json::Value& feat : features->as_array()) {
            const json::Value* props = feat.find("properties");
            const json::Value* geom = feat.find("geometry");
            const json::Value* idv = feat.find("id");
            if (!props || !geom || !idv) continue;
            const json::Value* coords = geom->find("coordinates");
            if (!coords || coords->as_array().size() < 3) continue;

            Observation o;
            o.source_id = id();
            o.source_class = source_class();
            o.native_id = idv->as_string();
            o.observation_uid = model::observation_uid(o.source_id, o.native_id);

            if (const json::Value* t = props->find("time");
                t && t->type() == json::Value::Type::Number) {
                o.origin_time = model::format_iso8601_ms(
                    static_cast<std::int64_t>(t->as_number()));
            }
            const auto& c = coords->as_array();
            if (c[0].type() == json::Value::Type::Number) {
                o.longitude = model::normalise_longitude(c[0].as_number());
            }
            if (c[1].type() == json::Value::Type::Number) {
                const double lat = c[1].as_number();
                if (model::latitude_valid(lat)) o.latitude = lat;
            }
            if (c[2].type() == json::Value::Type::Number) {
                o.depth_km = c[2].as_number();
                o.depth_is_fixed = looks_like_fixed_depth(*o.depth_km);
            }
            if (const json::Value* m = props->find("mag");
                m && m->type() == json::Value::Type::Number) {
                o.magnitude = m->as_number();
            }
            if (const json::Value* mt = props->find("magType");
                mt && mt->type() == json::Value::Type::String) {
                o.magnitude_type = mt->as_string();
            }
            if (const json::Value* ty = props->find("type");
                ty && ty->type() == json::Value::Type::String) {
                o.reported_event_type = ty->as_string();
            }
            if (const json::Value* pl = props->find("place");
                pl && pl->type() == json::Value::Type::String) {
                o.description = pl->as_string();
            }
            if (const json::Value* net = props->find("net");
                net && net->type() == json::Value::Type::String) {
                o.author = net->as_string();
            }
            if (o.origin_time.empty()) continue;  // REQ-3.7: reject, don't guess
            out.push_back(std::move(o));
        }
        return out;
    }
};

// --- ISC: FDSN event service, pipe-delimited text (REQ-2.4) ---

class IscAdapter final : public Adapter {
  public:
    std::string id() const override { return "isc"; }
    SourceClass source_class() const override { return SourceClass::Instrumental; }
    std::string data_licence() const override {
        return "ISC Bulletin — free for research and non-commercial use; "
               "cite the International Seismological Centre";
    }
    std::string attribution() const override {
        return "International Seismological Centre, On-line Bulletin "
               "(includes contributed agency data: IDC/CTBTO, NEIC, others)";
    }

    Fetch fetch(const Query& q) const override {
        // ISC rejects the ISO 8601 'Z' designator that FDSN permits and that
        // REQ-3.2 requires internally, so it is stripped at the boundary —
        // the value is still UTC, only the spelling differs.
        auto drop_z = [](std::string t) {
            if (!t.empty() && (t.back() == 'Z' || t.back() == 'z')) t.pop_back();
            return t;
        };
        std::string url =
            "https://www.isc.ac.uk/fdsnws/event/1/query?format=text"
            "&starttime=" + url_encode(drop_z(q.start_time)) +
            "&endtime=" + url_encode(drop_z(q.end_time)) +
            "&minlatitude=" + num(q.min_latitude) +
            "&maxlatitude=" + num(q.max_latitude) +
            "&minlongitude=" + num(q.min_longitude) +
            "&maxlongitude=" + num(q.max_longitude);
        Fetch f = http_fetch(url);
        if (f.ok()) f.observations = parse(f.raw_body, &f.error);
        return f;
    }

    // #EventID|Time|Latitude|Longitude|Depth/km|Author|Catalog|Contributor|
    // ContributorID|MagType|Magnitude|MagAuthor|EventLocationName|EventType
    std::vector<Observation> parse(std::string_view body,
                                   std::string* error) const override {
        std::vector<Observation> out;
        std::size_t start = 0;
        bool any_row = false;
        while (start < body.size()) {
            std::size_t nl = body.find('\n', start);
            if (nl == std::string_view::npos) nl = body.size();
            std::string_view line = body.substr(start, nl - start);
            start = nl + 1;
            if (line.empty() || line.front() == '#') continue;

            std::vector<std::string_view> f;
            std::size_t p = 0;
            while (p <= line.size()) {
                std::size_t bar = line.find('|', p);
                if (bar == std::string_view::npos) {
                    f.push_back(line.substr(p));
                    break;
                }
                f.push_back(line.substr(p, bar - p));
                p = bar + 1;
            }
            if (f.size() < 13) continue;
            any_row = true;

            auto trim = [](std::string_view s) {
                while (!s.empty() && (s.front() == ' ' || s.front() == '\r'))
                    s.remove_prefix(1);
                while (!s.empty() && (s.back() == ' ' || s.back() == '\r'))
                    s.remove_suffix(1);
                return s;
            };
            auto to_d = [&](std::string_view s) -> std::optional<double> {
                s = trim(s);
                if (s.empty()) return std::nullopt;
                try {
                    return std::stod(std::string(s));
                } catch (...) {
                    return std::nullopt;
                }
            };

            Observation o;
            o.source_id = id();
            o.source_class = source_class();
            o.native_id = std::string(trim(f[0]));
            if (o.native_id.empty()) continue;
            o.observation_uid = model::observation_uid(o.source_id, o.native_id);

            std::string ts(trim(f[1]));
            if (!ts.empty() && ts.back() != 'Z') ts += "Z";  // ISC omits it
            if (!model::parse_iso8601_ms(ts)) continue;      // REQ-3.7
            o.origin_time = ts;

            if (auto v = to_d(f[2]); v && model::latitude_valid(*v)) o.latitude = *v;
            if (auto v = to_d(f[3])) o.longitude = model::normalise_longitude(*v);
            if (auto v = to_d(f[4])) {
                o.depth_km = *v;
                o.depth_is_fixed = looks_like_fixed_depth(*v);
            }
            o.author = std::string(trim(f[5]));
            o.magnitude_type = std::string(trim(f[9]));
            if (auto v = to_d(f[10])) o.magnitude = *v;
            o.description = std::string(trim(f[12]));
            if (f.size() >= 14) o.reported_event_type = std::string(trim(f[13]));
            out.push_back(std::move(o));
        }
        if (!any_row && error) *error = "ISC response contained no data rows";
        return out;
    }
};

// --- Local files (REQ-2.13, REQ-2.14, REQ-2.18) ---

class FileAdapter final : public Adapter {
  public:
    FileAdapter(std::string path, std::string source_id, SourceClass cls,
                bool curated)
        : path_(std::move(path)),
          source_id_(std::move(source_id)),
          class_(cls),
          curated_(curated) {}

    std::string id() const override { return source_id_; }
    SourceClass source_class() const override { return class_; }
    std::string data_licence() const override {
        return "Local file — licence as declared by its origin";
    }
    std::string attribution() const override {
        return curated_ ? "Curated seed dataset (hand-entered)" : path_;
    }

    Fetch fetch(const Query&) const override {
        Fetch f;
        f.request_url = "file://" + path_;
        // REQ-12.5: canonicalise and confirm the file resolves to a regular
        // file before opening it.
        std::error_code ec;
        const auto canonical = std::filesystem::weakly_canonical(path_, ec);
        if (ec || !std::filesystem::is_regular_file(canonical, ec)) {
            f.error = "not a regular file: " + path_;
            return f;
        }
        std::ifstream in(canonical, std::ios::binary);
        if (!in) {
            f.error = "cannot open file: " + path_;
            return f;
        }
        std::ostringstream ss;
        ss << in.rdbuf();
        f.raw_body = ss.str();
        f.sha256 = http::sha256_hex(f.raw_body);  // REQ-12.10
        f.content_type = "application/octet-stream";
        f.observations = parse(f.raw_body, &f.error);
        return f;
    }

    // Dispatch on content, not on file extension: an extension is an
    // attacker-controllable hint, the bytes are the fact (REQ-2.14).
    std::vector<Observation> parse(std::string_view body,
                                   std::string* error) const override {
        std::size_t i = 0;
        while (i < body.size() && (body[i] == ' ' || body[i] == '\n' ||
                                   body[i] == '\r' || body[i] == '\t')) {
            ++i;
        }
        std::vector<Observation> out;
        if (i < body.size() && (body[i] == '{' || body[i] == '[')) {
            out = parse_json(body, error);
        } else {
            IscAdapter isc;
            out = isc.parse(body, error);
            for (Observation& o : out) rebrand(o);
        }
        return out;
    }

  private:
    void rebrand(Observation& o) const {
        o.source_id = source_id_;
        o.source_class = class_;
        o.is_curated = curated_;
        o.observation_uid = model::observation_uid(o.source_id, o.native_id);
    }

    std::vector<Observation> parse_json(std::string_view body,
                                        std::string* error) const {
        std::vector<Observation> out;
        auto doc = json::parse(body);
        if (!doc) {
            if (error) *error = "file JSON parse failed: " + doc.error->message;
            return out;
        }
        // GeoJSON FeatureCollection in USGS shape, or a bare array of
        // curated observation objects.
        if (doc->type() == json::Value::Type::Object && doc->find("features")) {
            UsgsAdapter usgs;
            out = usgs.parse(body, error);
            for (Observation& o : out) rebrand(o);
            return out;
        }
        if (doc->type() != json::Value::Type::Array) {
            if (error) *error = "file JSON is neither a FeatureCollection nor an array";
            return out;
        }
        for (const json::Value& v : doc->as_array()) {
            if (v.type() != json::Value::Type::Object) continue;
            Observation o;
            auto str = [&](const char* k) -> std::string {
                const json::Value* p = v.find(k);
                return (p && p->type() == json::Value::Type::String) ? p->as_string()
                                                                     : std::string();
            };
            auto dbl = [&](const char* k) -> std::optional<double> {
                const json::Value* p = v.find(k);
                if (p && p->type() == json::Value::Type::Number) return p->as_number();
                return std::nullopt;
            };
            o.native_id = str("id");
            o.origin_time = str("time");
            if (o.native_id.empty() || !model::parse_iso8601_ms(o.origin_time)) {
                continue;  // REQ-3.7
            }
            if (auto la = dbl("latitude"); la && model::latitude_valid(*la))
                o.latitude = *la;
            if (auto lo = dbl("longitude"))
                o.longitude = model::normalise_longitude(*lo);
            if (auto d = dbl("depth_km")) {
                o.depth_km = *d;
                o.depth_is_fixed = looks_like_fixed_depth(*d);
            }
            o.magnitude = dbl("magnitude");
            o.magnitude_type = str("magnitude_type");
            o.description = str("description");
            o.reported_event_type = str("event_type");
            o.author = str("author");
            rebrand(o);
            out.push_back(std::move(o));
        }
        return out;
    }

    std::string path_;
    std::string source_id_;
    SourceClass class_;
    bool curated_;
};

}  // namespace

std::unique_ptr<Adapter> make_usgs() { return std::make_unique<UsgsAdapter>(); }
std::unique_ptr<Adapter> make_isc() { return std::make_unique<IscAdapter>(); }

std::unique_ptr<Adapter> make_file(std::string path, std::string source_id,
                                   SourceClass cls, bool curated) {
    return std::make_unique<FileAdapter>(std::move(path), std::move(source_id),
                                         cls, curated);
}

}  // namespace kst::source
