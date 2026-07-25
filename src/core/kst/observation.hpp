// kst::model — the common Observation model every source normalises into
// (REQ-3.1). Source-independent by construction: nothing here names a
// particular catalog.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace kst::model {

// Confidence weighting classes, highest to lowest (REQ-7.2).
enum class SourceClass {
    Instrumental,   // A — geophysical or space-based measurement
    Government,     // B — official statement
    CuratedDataset, // C — documented coding methodology
    News,           // D — established media reporting
    Social,         // E — unattributed user-generated
};

const char* to_string(SourceClass c);
std::optional<SourceClass> source_class_from_string(std::string_view s);

// What kind of source produced an event, as reported (REQ-3.6: as published,
// never inferred here — inference is the discriminator's job).
struct Observation {
    std::string observation_uid;  // deterministic, REQ-3.5
    std::string source_id;        // adapter identity, e.g. "usgs", "isc"
    SourceClass source_class = SourceClass::Instrumental;
    std::string native_id;        // the source's own identifier
    std::string origin_time;      // ISO 8601 UTC, ms precision (REQ-3.2)

    std::optional<double> latitude;   // WGS 84 decimal degrees (REQ-3.3)
    std::optional<double> longitude;
    std::optional<double> depth_km;
    bool depth_is_fixed = false;      // agency default, not a measurement (REQ-5.2)

    std::optional<double> magnitude;
    std::string magnitude_type;       // native, never converted (REQ-3.4)
    std::optional<double> location_uncertainty_km;

    // Depth provenance, as published. QuakeML depthType distinguishes a
    // solved depth from an operator-assigned or default one — the field that
    // answers OQ-09 (DM-2026-007). Empty when the source does not say.
    std::string depth_type;

    std::string reported_event_type;  // as published
    // Source's own certainty in the event type ("known" / "suspected").
    std::string type_certainty;
    std::string description;
    std::string author;               // contributing agency where known
    bool is_curated = false;          // hand-entered seed data (REQ-2.18)
};

// RFC 9562 section 5.5 name-based UUIDv5 over (source_id, native_id).
// Deterministic by construction: re-ingesting a record yields the same
// identifier (REQ-3.5), which is why time-ordered UUID versions are excluded
// (DM-2026-005).
std::string observation_uid(std::string_view source_id,
                            std::string_view native_id);

// --- Time handling (REQ-3.2) ---

// Parse an ISO 8601 timestamp to milliseconds since the Unix epoch. Accepts
// a trailing 'Z' or an explicit offset; a timestamp with neither is rejected
// rather than assumed local.
std::optional<std::int64_t> parse_iso8601_ms(std::string_view text);

// Canonical UTC rendering with millisecond precision.
std::string format_iso8601_ms(std::int64_t epoch_ms);

// --- Geometry (REQ-3.3) ---

// Normalise longitude into [-180, 180]; sources may publish [0, 360].
double normalise_longitude(double lon);
bool latitude_valid(double lat);

// Great-circle distance in kilometres (spherical Earth; adequate for the
// association windows of REQ-6.2, which are themselves approximate).
double haversine_km(double lat1, double lon1, double lat2, double lon2);

}  // namespace kst::model
