// kst::analysis — correlation, discrimination, and confidence.
//
// Pure computation over Observations: no I/O, no network, no clock
// (REQ-9.2, REQ-9.3). Every function here is a deterministic function of its
// inputs (REQ-1.2), which is what makes REQ-10.3's run-twice test meaningful.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "kst/observation.hpp"

namespace kst::analysis {

// Candidate source hypotheses (REQ-5.1).
enum class Classification {
    NaturalEarthquake,
    SurfaceExplosion,   // consistent with a surface/near-surface source
    IndustrialBlast,    // quarry/mine pattern
    ReportedOnly,       // reported, with no instrumental constituent to characterise it
    Indeterminate,
};

const char* to_string(Classification c);

// Association windows (REQ-6.2). Defaults differ by source class because a
// news report may localise only to a city and a day, while a seismic origin
// is localised to kilometres and seconds.
struct Windows {
    double instrumental_km = 35.0;
    double instrumental_seconds = 90.0;
    double reporting_km = 100.0;
    double reporting_seconds = 86400.0;
};

// A discrimination verdict with the specific reasons that produced it.
// Reasons are emitted so a reader can audit the call (REQ-5.6).
struct Discrimination {
    Classification classification = Classification::Indeterminate;
    std::vector<std::string> reasons;
    bool depth_discriminant_applied = false;  // REQ-5.2/5.7
};

// Confidence factors, stored individually. The scalar presentation is
// derived, never stored, so numeric-vs-ordinal remains a view concern
// (REQ-7.7, DM-2026-003).
struct Confidence {
    int independent_sources = 0;       // REQ-7.3
    int instrumental_sources = 0;
    int reporting_sources = 0;
    bool has_government_source = false;
    bool has_disconfirming_evidence = false;  // REQ-7.6
    bool any_source_type_suspected = false;  // source's own hedge
    double best_location_uncertainty_km = -1.0;

    // Separate axes — they are genuinely independent (REQ-7.5).
    std::string occurrence_band() const;
    std::string location_band() const;
    std::string characterisation_band() const;
};

// A correlated candidate event: constituent observations plus the verdicts.
// Constituents are retained, never collapsed (REQ-6.4).
struct Event {
    std::string event_uid;
    std::string origin_time;      // earliest constituent, ISO 8601 UTC
    double latitude = 0.0;
    double longitude = 0.0;
    std::vector<model::Observation> constituents;
    std::vector<std::string> association_reasons;  // REQ-6.4
    // How each non-instrumental constituent related to the instrumental
    // partition: attached, reported-only, or ambiguous (DM-2026-009 R2).
    // Empty when the event has no report constituent.
    std::vector<std::string> report_association;
    Discrimination discrimination;
    Confidence confidence;
};

// Diurnal-regularity discriminant (REQ-5.12, DM-2026-008). Industry blasts
// on a shift schedule; war does not. Derived from the data rather than from
// the manually maintained site registry of REQ-5.5.
struct DiurnalRule {
    double cell_degrees = 0.1;
    int min_events = 30;
    double concentration = 0.90;  // fraction inside the busiest 10-hour window
    int window_hours = 10;
    int min_empty_hours = 6;
    // The window must sit in DAYLIGHT to be a working-hours signature.
    // Local solar time is derived from longitude (hour = UTC + lon/15), which
    // needs no timezone database and cannot go stale.
    double daylight_start = 5.0;   // local solar
    double daylight_end = 19.0;
};

// Per-cell verdict, retained so the reasoning can be reported (REQ-5.6).
struct DiurnalCell {
    long key = 0;
    int count = 0;
    int busiest_start_hour = 0;      // UTC
    double window_centre_local = 0;  // local solar hour, for reporting
    double concentration = 0.0;
    int empty_hours = 0;
    bool in_daylight = false;
    bool industrial = false;
};

// Identify cells whose explosion-typed events follow a working-hours
// signature. Deterministic: depends only on the observations and the rule.
std::map<long, DiurnalCell> diurnal_cells(
    const std::vector<model::Observation>& observations,
    const DiurnalRule& rule = {});

// Apply the discrimination rules to a single observation (REQ-5.1-5.7).
Discrimination discriminate(const model::Observation& o);

// Correlate observations into candidate events. Order-independent: the
// result does not depend on input order (REQ-6.3). Observations of the same
// physical event from different catalogs count once as instrumental
// corroboration, not twice (REQ-6.5).
std::vector<Event> correlate(std::vector<model::Observation> observations,
                             const Windows& w = {},
                             const DiurnalRule& diurnal = {});

}  // namespace kst::analysis
