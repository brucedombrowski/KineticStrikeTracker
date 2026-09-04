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

// One sub-interval of a requested window, and whether the source actually
// returned data for it (REQ-1.6). Only an adapter that subdivides its request
// can report these; a single-request adapter reports one interval spanning
// the query. The distinction they enable is the one REQ-8.6 turns on: a
// window is only as observed as its emptiest part, and "the source answered"
// is not the same fact as "the source answered for every day we asked about".
struct CoverageInterval {
    std::string start;            // ISO 8601 UTC date, inclusive
    std::string end;              // ISO 8601 UTC date, inclusive
    bool returned_data = false;
    std::string note;             // e.g. which products were empty
};

// One HTTP retrieval, exactly as it came back. An adapter that satisfies a
// long window by walking it in chunks performs many of these, and REQ-2.10
// requires every one of them to be persisted with its own URL, timestamp and
// status: a merged body digested as a whole matches no artifact the service
// ever served, so it cannot be verified against the source. REQ-4.6 adds that
// deduplicating the body must not discard the retrieval history.
struct Retrieval {
    std::string request_url;
    std::string requested_at;   // ISO 8601 UTC
    std::string raw_body;       // unmodified as received (REQ-2.10)
    std::string sha256;
    std::string content_type;
    long http_status = 0;
    // Parsed from this body alone, so each observation is traceable to the
    // exact response it came from (REQ-7.4).
    std::vector<model::Observation> observations;
};

// What an adapter produced for one logical fetch, before persistence.
struct Fetch {
    std::vector<model::Observation> observations;
    std::string request_url;      // empty for file sources
    std::string requested_at;     // ISO 8601 UTC
    std::string raw_body;         // retained verbatim (REQ-2.10)
    std::string sha256;
    std::string content_type;
    long http_status = 0;
    std::string error;            // non-empty means this source failed (REQ-2.9)
    // The service answered and had nothing in range. Distinct from failure:
    // "no events here" and "we could not look" are different facts, and
    // conflating them is exactly what REQ-1.6's coverage model forbids.
    bool no_data = false;
    // Chronological, one entry per sub-request. Empty for adapters that
    // retrieve the whole window at once.
    std::vector<CoverageInterval> coverage;
    // Every retrieval made to satisfy this fetch, in the order made. One
    // entry for a single-request adapter; one per chunk-and-product for a
    // chunking one. This is what gets persisted (REQ-2.10, REQ-4.6).
    std::vector<Retrieval> retrievals;
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

// Remove a credential from text destined for storage or output (REQ-10.6).
// Every occurrence is replaced, so a key repeated in a URL and again in an
// error message cannot survive in one of them. The surrounding text keeps its
// shape: a reader should still be able to see what was requested and reissue
// it with their own key.
std::string redact_secret(std::string text, const std::string& secret);

// --- Built-in adapters ---

// USGS FDSN event service, GeoJSON (REQ-2.2).
std::unique_ptr<Adapter> make_usgs();

// ISC FDSN event service, text format. Aggregates member agencies including
// the CTBTO IDC — the route that reaches strike magnitudes (REQ-2.4, ASM-08).
std::unique_ptr<Adapter> make_isc();

// NASA FIRMS thermal anomaly detections (REQ-2.15). Space-based, so it sees
// regions where no seismic network reports — the coverage gap that made the
// Minab strike invisible. Requires a free MAP_KEY from
// https://firms.modaps.eosdis.nasa.gov/api/map_key/ supplied via the
// FIRMS_MAP_KEY environment variable; never committed (REQ-10.6).
//
// A thermal anomaly is NOT an explosion. FIRMS detects fire, which correlates
// with strikes but also with agriculture, wildfire, industry, and — heavily in
// this region — gas flaring. The adapter therefore reports 'thermal anomaly',
// never 'explosion', and lets discrimination do its job.
// REQ-2.15 names VIIRS 375 m *and* MODIS. Querying one product alone makes
// the run hostage to one satellite: NASA's VIIRS_SNPP stream returned zero
// rows for 11-15 July 2026 across the whole Gulf while NOAA-20, NOAA-21 and
// MODIS all had data, and the run reported complete coverage (issue #27).
// Products are queried together and merged under one source id, because they
// share an upstream origin and must not count as independent (REQ-7.3).
std::unique_ptr<Adapter> make_firms(
    std::vector<std::string> products = {"VIIRS_SNPP", "MODIS"});

// Local files: GeoJSON, FDSN text, or curated seed JSON (REQ-2.13, REQ-2.18).
// Granted no more trust than a network response (REQ-2.14).
std::unique_ptr<Adapter> make_file(std::string path, std::string source_id,
                                   model::SourceClass cls, bool curated);

}  // namespace kst::source
