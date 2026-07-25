#include "kst/source.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "kst/http.hpp"
#include "kst/json.hpp"
#include "kst/csv.hpp"
#include "kst/xml.hpp"

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
    // 204 No Content: the service answered and reported nothing in range.
    // That is an observation about the world, not a fault (REQ-2.9).
    f.no_data = (r->status == 204) || r->body.empty();
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
        if (f.ok() && !f.no_data) f.observations = parse(f.raw_body, &f.error);
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
        // QuakeML, NOT the pipe-delimited text format. The text format
        // silently drops depthType, originUncertainty, and typeCertainty —
        // precisely the three fields that determine whether an origin is
        // usable for attribution (evidence: issue #23, DM-2026-007).
        //
        // ISC also rejects the ISO 8601 'Z' designator that FDSN permits and
        // REQ-3.2 requires internally, so it is stripped at this boundary;
        // the value is still UTC, only the spelling differs.
        auto drop_z = [](std::string t) {
            if (!t.empty() && (t.back() == 'Z' || t.back() == 'z')) t.pop_back();
            return t;
        };
        std::string url =
            "https://www.isc.ac.uk/fdsnws/event/1/query?format=xml"
            "&starttime=" + url_encode(drop_z(q.start_time)) +
            "&endtime=" + url_encode(drop_z(q.end_time)) +
            "&minlatitude=" + num(q.min_latitude) +
            "&maxlatitude=" + num(q.max_latitude) +
            "&minlongitude=" + num(q.min_longitude) +
            "&maxlongitude=" + num(q.max_longitude);
        Fetch f = http_fetch(url);
        if (f.ok() && !f.no_data) f.observations = parse(f.raw_body, &f.error);
        return f;
    }

    std::vector<Observation> parse(std::string_view body,
                                   std::string* error) const override {
        std::vector<Observation> out;
        auto doc = xml::parse(body);
        if (!doc) {
            if (error) *error = "ISC QuakeML parse failed: " + doc.error->message;
            return out;
        }
        const xml::Element* params = doc->find("eventParameters");
        if (!params) {
            if (error) *error = "ISC QuakeML has no eventParameters";
            return out;
        }
        for (const xml::Element* ev : params->children_named("event")) {
            const xml::Element* origin = ev->child("origin");
            if (!origin) continue;

            Observation o;
            o.source_id = id();
            o.source_class = source_class();
            if (const std::string* pid = ev->attribute("publicID")) {
                o.native_id = *pid;
            }
            if (o.native_id.empty()) continue;
            o.observation_uid = model::observation_uid(o.source_id, o.native_id);

            // QuakeML wraps scalars in a <value> child: <time><value>…
            const xml::Element* time_el = origin->child("time");
            std::string ts = time_el ? time_el->text_of("value") : std::string();
            if (!ts.empty() && ts.back() != 'Z') ts += "Z";
            if (!model::parse_iso8601_ms(ts)) continue;  // REQ-3.7
            o.origin_time = ts;

            auto num_of = [](const xml::Element* e,
                             std::string_view k) -> std::optional<double> {
                if (!e) return std::nullopt;
                const xml::Element* c = e->find(k);
                if (!c) return std::nullopt;
                const std::string t = c->find("value") ? c->text_of("value") : c->text;
                if (t.empty()) return std::nullopt;
                try {
                    return std::stod(t);
                } catch (...) {
                    return std::nullopt;
                }
            };

            if (auto v = num_of(origin, "latitude");
                v && model::latitude_valid(*v)) {
                o.latitude = *v;
            }
            if (auto v = num_of(origin, "longitude")) {
                o.longitude = model::normalise_longitude(*v);
            }
            // QuakeML publishes depth in METRES.
            if (auto v = num_of(origin, "depth")) o.depth_km = *v / 1000.0;

            // The authoritative answer to OQ-09: depthType distinguishes a
            // solved depth ("from location", "constrained by depth phases")
            // from one an operator or default supplied. No value-guessing.
            const std::string dtype = origin->text_of("depthType");
            // REQ-5.2: the discriminant applies ONLY where the source says
            // the depth was solved. An ABSENT depthType is not a claim that
            // it was — silence is treated as unknown provenance, not as
            // permission.
            o.depth_is_fixed = dtype.empty() || dtype == "operator assigned" ||
                               dtype == "constrained by prior knowledge" ||
                               dtype == "other";
            o.depth_type = dtype;

            // Location uncertainty, metres to kilometres. The ellipse
            // semi-major axis is the honest figure: it is the worst case
            // (REQ-7.5, REQ-8.7).
            if (const xml::Element* unc = origin->child("originUncertainty")) {
                if (auto v = num_of(unc, "maxHorizontalUncertainty")) {
                    o.location_uncertainty_km = *v / 1000.0;
                } else if (auto h = num_of(unc, "horizontalUncertainty")) {
                    o.location_uncertainty_km = *h / 1000.0;
                }
            }

            if (const xml::Element* ci = origin->child("creationInfo")) {
                o.author = ci->text_of("author");
            }
            if (const xml::Element* mag = ev->child("magnitude")) {
                if (auto v = num_of(mag, "mag")) o.magnitude = *v;
                o.magnitude_type = mag->text_of("type");
            }
            if (const xml::Element* desc = ev->child("description")) {
                o.description = desc->text_of("text");
            }
            // Direct children only: a depth-first search would find
            // <description><type>Flinn-Engdahl region</type></description>
            // first and silently mislabel every event (REQ-3.6).
            if (const xml::Element* et = ev->child("type")) {
                o.reported_event_type = et->text;
            }
            // ISC's own confidence in the event type — 'suspected' vs
            // 'known'. Retained as published (REQ-3.6).
            if (const xml::Element* tc = ev->child("typeCertainty")) {
                o.type_certainty = tc->text;
            }
            out.push_back(std::move(o));
        }
        if (out.empty() && error) *error = "ISC response contained no events";
        return out;
    }
};

// --- NASA FIRMS thermal anomalies (REQ-2.15) ---

class FirmsAdapter final : public Adapter {
  public:
    explicit FirmsAdapter(std::string product) : product_(std::move(product)) {}

    std::string id() const override { return "firms"; }
    SourceClass source_class() const override { return SourceClass::Instrumental; }
    std::string data_licence() const override {
        return "NASA FIRMS — open, free of restrictions on use; attribution "
               "requested";
    }
    std::string attribution() const override {
        return "NASA FIRMS (Fire Information for Resource Management System), "
               "LANCE/ESDIS — " + product_;
    }

    Fetch fetch(const Query& q) const override {
        Fetch f;
        const char* key = std::getenv("FIRMS_MAP_KEY");
        if (!key || !*key) {
            f.error =
                "FIRMS_MAP_KEY not set. Request a free key at "
                "https://firms.modaps.eosdis.nasa.gov/api/map_key/ then export "
                "FIRMS_MAP_KEY=<key>";
            return f;
        }
        // FIRMS area order is west,south,east,north — deliberately different
        // from our bbox order, so it is spelled out rather than passed through.
        const std::string area =
            num(q.min_longitude) + "," + num(q.min_latitude) + "," +
            num(q.max_longitude) + "," + num(q.max_latitude);

        // The area API accepts a 1-5 day range from a start date. Longer
        // windows must be walked a chunk at a time; the caller sees one Fetch.
        const auto start = model::parse_iso8601_ms(q.start_time);
        const auto end = model::parse_iso8601_ms(q.end_time);
        if (!start || !end || *end < *start) {
            f.error = "FIRMS: invalid time range";
            return f;
        }
        constexpr std::int64_t kDayMs = 86400000;
        constexpr int kChunkDays = 5;   // FIRMS rejects anything above 5

        std::string merged;
        bool have_header = false;
        int requests = 0;
        for (std::int64_t t = *start; t <= *end; t += kDayMs * kChunkDays) {
            const std::string day = model::format_iso8601_ms(t).substr(0, 10);
            const std::int64_t remaining = (*end - t) / kDayMs + 1;
            const int span = static_cast<int>(
                std::min<std::int64_t>(kChunkDays, std::max<std::int64_t>(1, remaining)));
            const std::string url =
                "https://firms.modaps.eosdis.nasa.gov/api/area/csv/" +
                url_encode(key) + "/" + url_encode(product_) + "/" + area + "/" +
                std::to_string(span) + "/" + day;
            Fetch part = http_fetch(url);
            if (!part.ok()) {
                // Partial coverage is recorded, not silently swallowed (REQ-2.9).
                f.error = part.error + " (at " + day + ")";
                break;
            }
            if (part.raw_body.rfind("Invalid MAP_KEY", 0) == 0) {
                f.error = "FIRMS rejected the MAP_KEY";
                return f;
            }
            if (f.requested_at.empty()) f.requested_at = part.requested_at;
            f.request_url = url;   // last request; each is logged by the caller
            std::string_view body = part.raw_body;
            if (have_header) {
                const std::size_t nl = body.find('\n');
                if (nl != std::string_view::npos) body.remove_prefix(nl + 1);
            }
            have_header = true;
            merged.append(body);
            if (++requests > 200) break;   // guard against absurd spans
        }
        f.raw_body = merged;
        f.sha256 = http::sha256_hex(merged);
        f.content_type = "text/csv";
        f.http_status = 200;
        if (!merged.empty()) {
            std::string perr;
            f.observations = parse(merged, &perr);
            if (f.error.empty()) f.error = perr;
        }
        return f;
    }

    std::vector<Observation> parse(std::string_view body,
                                   std::string* error) const override {
        std::vector<Observation> out;
        auto t = csv::parse(body);
        if (!t) {
            if (error) *error = "FIRMS CSV parse failed: " + t.error->message;
            return out;
        }
        if (!t->has_column("latitude") || !t->has_column("acq_date")) {
            if (error) *error = "FIRMS CSV missing expected columns";
            return out;
        }
        // Brightness column differs by instrument; both are retained as the
        // published value and never converted (REQ-3.4).
        const bool viirs = t->has_column("bright_ti4");
        for (std::size_t r = 0; r < t->rows(); ++r) {
            auto num_of = [&](const char* col) -> std::optional<double> {
                const std::string_view v = t->get(r, col);
                if (v.empty()) return std::nullopt;
                try {
                    return std::stod(std::string(v));
                } catch (...) {
                    return std::nullopt;
                }
            };
            Observation o;
            o.source_id = id();
            o.source_class = source_class();

            const auto lat = num_of("latitude");
            const auto lon = num_of("longitude");
            const std::string date(t->get(r, "acq_date"));
            std::string hhmm(t->get(r, "acq_time"));   // "HHMM", may lack a zero
            if (!lat || !lon || date.size() != 10) continue;   // REQ-3.7
            while (hhmm.size() < 4) hhmm.insert(hhmm.begin(), '0');
            o.origin_time = date + "T" + hhmm.substr(0, 2) + ":" +
                            hhmm.substr(2, 2) + ":00.000Z";
            if (!model::parse_iso8601_ms(o.origin_time)) continue;

            if (!model::latitude_valid(*lat)) continue;
            o.latitude = *lat;
            o.longitude = model::normalise_longitude(*lon);

            // A satellite detection has no depth. Leaving it unset is correct
            // and keeps the depth discriminant from firing on it (REQ-5.2).
            const std::string sat(t->get(r, "satellite"));
            const std::string inst(t->get(r, "instrument"));
            o.author = inst.empty() ? sat : (inst + "/" + sat);
            o.native_id = date + "_" + hhmm + "_" + std::string(t->get(r, "latitude")) +
                          "_" + std::string(t->get(r, "longitude")) + "_" + o.author;
            o.observation_uid = model::observation_uid(o.source_id, o.native_id);

            // NOT 'explosion'. FIRMS observes radiant heat, not a cause.
            o.reported_event_type = "thermal anomaly";
            o.type_certainty = std::string(t->get(r, "confidence"));

            // Pixel footprint as a location uncertainty: VIIRS I-band is
            // nominally 375 m, MODIS 1 km, both degrading off-nadir. Reporting
            // the scan-scaled footprint is more honest than reporting nothing
            // (REQ-7.8).
            const double base_km = viirs ? 0.375 : 1.0;
            const auto scan = num_of("scan");
            o.location_uncertainty_km = base_km * (scan && *scan > 0 ? *scan : 1.0);

            const auto frp = num_of("frp");
            const auto bright = num_of(viirs ? "bright_ti4" : "brightness");
            std::string desc = "thermal anomaly";
            if (frp) desc += ", FRP " + std::to_string(static_cast<int>(*frp)) + " MW";
            if (bright) {
                desc += ", brightness " +
                        std::to_string(static_cast<int>(*bright)) + " K";
            }
            const std::string dn(t->get(r, "daynight"));
            if (!dn.empty()) desc += (dn == "N" ? ", night overpass" : ", day overpass");
            o.description = desc;
            out.push_back(std::move(o));
        }
        return out;
    }

  private:
    std::string product_;
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

std::unique_ptr<Adapter> make_firms(std::string product) {
    return std::make_unique<FirmsAdapter>(std::move(product));
}

std::unique_ptr<Adapter> make_file(std::string path, std::string source_id,
                                   SourceClass cls, bool curated) {
    return std::make_unique<FileAdapter>(std::move(path), std::move(source_id),
                                         cls, curated);
}

}  // namespace kst::source
