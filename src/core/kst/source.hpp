// kst::source — the pluggable source-adapter interface (REQ-2.1).
//
// A new source is added by implementing Adapter and registering it; the
// analysis core is not modified. Adapters own all network access (REQ-9.3);
// everything downstream sees only normalised Observations.
#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "kst/observation.hpp"
#include "kst/xml.hpp"

namespace kst::source {

// A time-and-space window to retrieve (REQ-1.1: whole-Earth model, each
// query scoped by an explicit region of interest).
struct Query {
    std::string start_time;  // ISO 8601 UTC
    std::string end_time;
    double min_latitude = -90.0;
    double max_latitude = 90.0;
    double min_longitude = -180.0;
    double max_longitude = 180.0;
};

// What an adapter produced for one retrieval, before persistence.
struct Fetch {
    std::vector<model::Observation> observations;
    std::string request_url;      // empty for file sources
    std::string requested_at;     // ISO 8601 UTC
    std::string raw_body;         // retained verbatim (REQ-2.10)
    std::string sha256;
    std::string content_type;
    long http_status = 0;
    std::string error;            // non-empty means this source failed (REQ-2.9)
    bool ok() const { return error.empty(); }
};

class Adapter {
  public:
    virtual ~Adapter() = default;

    virtual std::string id() const = 0;
    virtual model::SourceClass source_class() const = 0;

    // Licence or terms of the retrieved data, surfaced in outputs (REQ-2.12).
    virtual std::string data_licence() const = 0;
    virtual std::string attribution() const = 0;

    virtual Fetch fetch(const Query& q) const = 0;

    // Parse an already-retrieved body. Separating this from fetch() is what
    // makes offline replay possible (REQ-2.11) and lets tests run without
    // network (REQ-10.4).
    virtual std::vector<model::Observation> parse(
        std::string_view body, std::string* error) const = 0;
};

// --- Built-in adapters ---

// USGS FDSN event service, GeoJSON (REQ-2.2).
std::unique_ptr<Adapter> make_usgs();

// ISC FDSN event service, text format. Aggregates member agencies including
// the CTBTO IDC — the route that reaches strike magnitudes (REQ-2.4, ASM-08).
std::unique_ptr<Adapter> make_isc();

// Local files: GeoJSON, FDSN text, or curated seed JSON (REQ-2.13, REQ-2.18).
// Granted no more trust than a network response (REQ-2.14).
std::unique_ptr<Adapter> make_file(std::string path, std::string source_id,
                                   model::SourceClass cls, bool curated);

}  // namespace kst::source
