#include "kst/pipeline.hpp"

#include <cstdio>
#include <sstream>

#include "kst/version.hpp"

namespace kst::pipeline {

namespace {

// Fixed formatting so successive runs produce byte-identical output
// (REQ-8.4). Never rely on locale or default stream precision.
std::string fixed(double v, int decimals) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", decimals, v);
    return buf;
}

// REQ-12.6: neutralise source-derived text for the output context. JSON
// strings need escaping of quotes, backslashes, and control characters.
std::string json_escape(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char ch : s) {
        const auto c = static_cast<unsigned char>(ch);
        switch (ch) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char esc[8];
                    std::snprintf(esc, sizeof(esc), "\\u%04x", c);
                    out += esc;
                } else {
                    out.push_back(ch);
                }
        }
    }
    return out;
}

// REQ-12.6: strip control characters, including terminal escape sequences,
// from text destined for a terminal or log.
std::string terminal_safe(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char ch : s) {
        const auto c = static_cast<unsigned char>(ch);
        if (c < 0x20 || c == 0x7F) {
            out.push_back(' ');
        } else {
            out.push_back(ch);
        }
    }
    return out;
}

}  // namespace

IngestReport ingest(db::Database& database,
                    const std::vector<std::unique_ptr<source::Adapter>>& adapters,
                    const source::Query& query) {
    IngestReport report;
    for (const auto& adapter : adapters) {
        SourceResult sr;
        sr.source_id = adapter->id();
        source::Fetch f = adapter->fetch(query);
        if (!f.ok()) {
            sr.ok = false;
            sr.error = f.error;
            report.coverage_complete = false;  // REQ-2.9
            report.sources.push_back(std::move(sr));
            continue;
        }
        sr.sha256 = f.sha256;

        // Content-addressed body: stored once (REQ-4.6).
        if (auto exists = database.prepare(
                "SELECT COUNT(*) FROM raw_body WHERE sha256 = ?")) {
            exists->bind(1, f.sha256);
            if (exists->step() && exists->column_int(0) > 0) {
                sr.body_was_new = false;
            }
        }
        if (sr.body_was_new) {
            if (auto ins = database.prepare(
                    "INSERT INTO raw_body(sha256,body,byte_count,first_seen) "
                    "VALUES(?,?,?,?)")) {
                ins->bind(1, f.sha256)
                    .bind(2, f.raw_body)
                    .bind(3, static_cast<std::int64_t>(f.raw_body.size()))
                    .bind(4, f.requested_at);
                ins->execute();
            }
        }
        // The retrieval itself is always recorded, dedup or not (REQ-4.6).
        if (auto ins = database.prepare(
                "INSERT INTO raw_response(source_id,request_url,requested_at,"
                "http_status,content_type,sha256) VALUES(?,?,?,?,?,?)")) {
            ins->bind(1, sr.source_id)
                .bind(2, f.request_url)
                .bind(3, f.requested_at)
                .bind(4, static_cast<std::int64_t>(f.http_status))
                .bind(5, f.content_type)
                .bind(6, f.sha256);
            ins->execute();
        }

        for (const model::Observation& o : f.observations) {
            auto up = database.prepare(
                "INSERT INTO observation(observation_uid,source_id,source_class,"
                "native_id,origin_time,latitude,longitude,depth_km,"
                "depth_is_fixed,magnitude,magnitude_type,reported_event_type,"
                "description,is_curated,location_uncertainty_km,depth_type,"
                "type_certainty,author) "
                "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?) "
                "ON CONFLICT(source_id,native_id) DO UPDATE SET "
                "origin_time=excluded.origin_time,latitude=excluded.latitude,"
                "longitude=excluded.longitude,depth_km=excluded.depth_km,"
                "depth_is_fixed=excluded.depth_is_fixed,"
                "magnitude=excluded.magnitude,"
                "magnitude_type=excluded.magnitude_type,"
                "reported_event_type=excluded.reported_event_type,"
                "description=excluded.description,"
                "location_uncertainty_km=excluded.location_uncertainty_km,"
                "depth_type=excluded.depth_type,"
                "type_certainty=excluded.type_certainty,author=excluded.author");
            if (!up) continue;
            up->bind(1, o.observation_uid)
                .bind(2, o.source_id)
                .bind(3, model::to_string(o.source_class))
                .bind(4, o.native_id)
                .bind(5, o.origin_time);
            o.latitude ? up->bind(6, *o.latitude) : up->bind_null(6);
            o.longitude ? up->bind(7, *o.longitude) : up->bind_null(7);
            o.depth_km ? up->bind(8, *o.depth_km) : up->bind_null(8);
            up->bind(9, static_cast<std::int64_t>(o.depth_is_fixed ? 1 : 0));
            o.magnitude ? up->bind(10, *o.magnitude) : up->bind_null(10);
            up->bind(11, o.magnitude_type)
                .bind(12, o.reported_event_type)
                .bind(13, o.description)
                .bind(14, static_cast<std::int64_t>(o.is_curated ? 1 : 0));
            o.location_uncertainty_km ? up->bind(15, *o.location_uncertainty_km)
                                      : up->bind_null(15);
            up->bind(16, o.depth_type).bind(17, o.type_certainty).bind(18, o.author);
            if (up->execute()) ++sr.observations;
        }
        report.total_observations += sr.observations;
        report.sources.push_back(std::move(sr));
    }
    return report;
}

std::vector<model::Observation> load_observations(db::Database& database) {
    std::vector<model::Observation> out;
    auto q = database.prepare(
        "SELECT observation_uid,source_id,source_class,native_id,origin_time,"
        "latitude,longitude,depth_km,depth_is_fixed,magnitude,magnitude_type,"
        "reported_event_type,description,is_curated,"
        "location_uncertainty_km,depth_type,type_certainty,author FROM observation "
        "ORDER BY origin_time, observation_uid");  // canonical (REQ-6.3)
    if (!q) return out;
    while (q->step()) {
        model::Observation o;
        o.observation_uid = q->column_text(0);
        o.source_id = q->column_text(1);
        o.source_class =
            model::source_class_from_string(q->column_text(2))
                .value_or(model::SourceClass::Instrumental);
        o.native_id = q->column_text(3);
        o.origin_time = q->column_text(4);
        if (!q->column_is_null(5)) o.latitude = q->column_double(5);
        if (!q->column_is_null(6)) o.longitude = q->column_double(6);
        if (!q->column_is_null(7)) o.depth_km = q->column_double(7);
        o.depth_is_fixed = q->column_int(8) != 0;
        if (!q->column_is_null(9)) o.magnitude = q->column_double(9);
        o.magnitude_type = q->column_text(10);
        o.reported_event_type = q->column_text(11);
        o.description = q->column_text(12);
        o.is_curated = q->column_int(13) != 0;
        if (!q->column_is_null(14)) o.location_uncertainty_km = q->column_double(14);
        o.depth_type = q->column_text(15);
        o.type_certainty = q->column_text(16);
        o.author = q->column_text(17);
        out.push_back(std::move(o));
    }
    return out;
}

std::vector<Coverage> coverage_notes(
    const std::vector<std::unique_ptr<source::Adapter>>& adapters) {
    std::vector<Coverage> out;
    for (const auto& a : adapters) {
        Coverage c;
        c.source_id = a->id();
        if (c.source_id == "usgs") {
            c.note =
                "Global catalog. Magnitude of completeness over the Middle "
                "East is approximately M4.0 — well above the M2.0-M2.5 at "
                "which munition-scale events couple. Cannot see strikes.";
        } else if (c.source_id == "isc") {
            c.note =
                "Aggregates member agencies including the CTBTO IDC, which "
                "does detect surface events at munition scale. Reviewed "
                "bulletin lags roughly 24 months; contributed preliminary "
                "data arrives sooner.";
        } else {
            c.note = "Coverage not characterised.";
        }
        out.push_back(std::move(c));
    }
    return out;
}

std::string to_geojson(const std::vector<analysis::Event>& events,
                       const std::vector<Coverage>& coverage) {
    std::ostringstream o;
    o << "{\n  \"type\": \"FeatureCollection\",\n";
    o << "  \"generator\": \"" << json_escape(kst::version_string()) << "\",\n";
    o << "  \"coverage\": [\n";
    for (std::size_t i = 0; i < coverage.size(); ++i) {
        o << "    {\"source\": \"" << json_escape(coverage[i].source_id)
          << "\", \"note\": \"" << json_escape(coverage[i].note) << "\"}"
          << (i + 1 < coverage.size() ? "," : "") << "\n";
    }
    o << "  ],\n  \"features\": [\n";
    for (std::size_t i = 0; i < events.size(); ++i) {
        const analysis::Event& e = events[i];
        o << "    {\n      \"type\": \"Feature\",\n";
        o << "      \"geometry\": {\"type\": \"Point\", \"coordinates\": ["
          << fixed(e.longitude, 4) << ", " << fixed(e.latitude, 4) << "]},\n";
        o << "      \"properties\": {\n";
        o << "        \"event_uid\": \"" << json_escape(e.event_uid) << "\",\n";
        o << "        \"origin_time\": \"" << json_escape(e.origin_time) << "\",\n";
        o << "        \"classification\": \""
          << analysis::to_string(e.discrimination.classification) << "\",\n";
        o << "        \"confidence\": {\"occurrence\": \""
          << e.confidence.occurrence_band() << "\", \"location\": \""
          << e.confidence.location_band() << "\", \"characterisation\": \""
          << e.confidence.characterisation_band() << "\"},\n";
        o << "        \"independent_sources\": "
          << e.confidence.independent_sources << ",\n";
        o << "        \"constituents\": [\n";
        for (std::size_t j = 0; j < e.constituents.size(); ++j) {
            const auto& c = e.constituents[j];
            o << "          {\"source\": \"" << json_escape(c.source_id)
              << "\", \"class\": \"" << model::to_string(c.source_class)
              << "\", \"native_id\": \"" << json_escape(c.native_id) << "\"";
            if (c.magnitude) {
                o << ", \"magnitude\": " << fixed(*c.magnitude, 1)
                  << ", \"magnitude_type\": \"" << json_escape(c.magnitude_type)
                  << "\"";
            }
            if (c.depth_km) {
                o << ", \"depth_km\": " << fixed(*c.depth_km, 1)
                  << ", \"depth_is_fixed\": " << (c.depth_is_fixed ? "true" : "false")
                  << ", \"depth_type\": \"" << json_escape(c.depth_type) << "\"";
            }
            // Uncertainty is the field that decides whether a position means
            // anything (REQ-7.8, REQ-8.7) — it belongs in the payload.
            if (c.location_uncertainty_km) {
                o << ", \"location_uncertainty_km\": "
                  << fixed(*c.location_uncertainty_km, 1);
            }
            if (!c.type_certainty.empty()) {
                o << ", \"type_certainty\": \"" << json_escape(c.type_certainty) << "\"";
            }
            o << ", \"author\": \"" << json_escape(c.author) << "\"}"
              << (j + 1 < e.constituents.size() ? "," : "") << "\n";
        }
        o << "        ],\n        \"discrimination_reasons\": [\n";
        for (std::size_t j = 0; j < e.discrimination.reasons.size(); ++j) {
            o << "          \"" << json_escape(e.discrimination.reasons[j]) << "\""
              << (j + 1 < e.discrimination.reasons.size() ? "," : "") << "\n";
        }
        o << "        ]\n      }\n    }" << (i + 1 < events.size() ? "," : "")
          << "\n";
    }
    o << "  ]\n}\n";
    return o.str();
}

std::string to_report(const std::vector<analysis::Event>& events,
                      const std::vector<Coverage>& coverage,
                      const source::Query& query,
                      const std::vector<std::string>& attributions) {
    std::ostringstream o;
    o << "Kinetic Strike Tracker — candidate event report\n";
    o << "===============================================\n\n";
    o << "Region:  " << fixed(query.min_latitude, 2) << " to "
      << fixed(query.max_latitude, 2) << " N, " << fixed(query.min_longitude, 2)
      << " to " << fixed(query.max_longitude, 2) << " E\n";
    o << "Window:  " << query.start_time << "  to  " << query.end_time << "\n";
    o << "Events:  " << events.size() << " candidate\n\n";

    // REQ-8.6: limitations stated in every report, not footnoted.
    o << "DETECTION LIMITATIONS — read before interpreting\n";
    o << "------------------------------------------------\n";
    for (const Coverage& c : coverage) {
        o << "  " << c.source_id << ": " << terminal_safe(c.note) << "\n";
    }
    o << "\n  Absence of a detection is NOT evidence that no strike occurred.\n";
    o << "  Events below a source's detection threshold are invisible to it,\n";
    o << "  and this report can only reflect what its sources published.\n\n";

    for (const analysis::Event& e : events) {
        o << "------------------------------------------------------------\n";
        o << e.origin_time << "   [" << analysis::to_string(e.discrimination.classification)
          << "]\n";
        o << "  position (derived): " << fixed(e.latitude, 3) << ", "
          << fixed(e.longitude, 3) << "\n";
        o << "  confidence: occurrence=" << e.confidence.occurrence_band()
          << "  location=" << e.confidence.location_band()
          << "  characterisation=" << e.confidence.characterisation_band()
          << "\n";
        o << "  independent sources: " << e.confidence.independent_sources
          << " (instrumental " << e.confidence.instrumental_sources << ")\n";
        o << "  observed constituents:\n";
        for (const auto& c : e.constituents) {
            o << "    - " << c.source_id << " [" << model::to_string(c.source_class)
              << "] " << c.native_id;
            if (c.magnitude) {
                o << "  M" << fixed(*c.magnitude, 1) << " " << c.magnitude_type;
            }
            if (c.depth_km) {
                o << "  depth " << fixed(*c.depth_km, 1) << " km";
                if (!c.depth_type.empty()) {
                    o << " [" << terminal_safe(c.depth_type) << "]";
                } else {
                    o << (c.depth_is_fixed ? " (agency default)" : " (constrained)");
                }
            }
            if (c.location_uncertainty_km) {
                o << "  location +/-" << fixed(*c.location_uncertainty_km, 0)
                  << " km";
            }
            if (c.type_certainty == "suspected") o << "  [type: suspected]";
            if (!c.author.empty()) o << "  author=" << terminal_safe(c.author);
            if (!c.description.empty()) {
                o << "\n      " << terminal_safe(c.description);
            }
            o << "\n";
        }
        if (!e.discrimination.reasons.empty()) {
            o << "  discrimination reasoning:\n";
            for (const auto& r : e.discrimination.reasons) {
                o << "    * " << terminal_safe(r) << "\n";
            }
        }
        o << "\n";
    }

    o << "------------------------------------------------------------\n";
    o << "Assessments are candidate findings derived from open sources with\n";
    o << "stated confidence. They are not verified determinations of fact.\n\n";
    o << "Data attribution:\n";
    for (const std::string& a : attributions) {
        o << "  - " << terminal_safe(a) << "\n";
    }
    return o.str();
}

}  // namespace kst::pipeline
