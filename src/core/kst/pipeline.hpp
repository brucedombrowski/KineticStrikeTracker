// kst::pipeline — ingest, analyse, and report. The only place that combines
// adapters, persistence, and analysis.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "kst/analysis.hpp"
#include "kst/db.hpp"
#include "kst/source.hpp"

namespace kst::pipeline {

// Per-source outcome of an ingest run. A source failure is recorded and the
// run continues (REQ-2.9); the resulting analysis is marked as having
// incomplete coverage.
struct SourceResult {
    std::string source_id;
    int observations = 0;
    bool ok = true;
    std::string error;
    std::string sha256;
    bool body_was_new = true;  // false when any body was new (REQ-4.6)
    int retrievals = 0;        // HTTP requests made to satisfy this fetch
    int bodies_stored = 0;     // of those, how many bodies were not already held
    bool no_data = false;      // queried successfully, nothing in range
    // Per-sub-interval observation record, for adapters that subdivide their
    // request. Empty for single-request adapters.
    std::vector<source::CoverageInterval> coverage;
    std::vector<source::CoverageInterval> gaps;   // the unobserved subset
};

struct IngestReport {
    std::vector<SourceResult> sources;
    int total_observations = 0;
    bool coverage_complete = true;
};

// A window is only as observed as its emptiest part. A source that returned
// data for some sub-intervals and nothing for others has a hole in the middle
// of a window it was otherwise answering: that is a coverage gap, and the run
// must not claim complete coverage (REQ-1.6, REQ-8.6).
//
// A source that returned nothing for *every* sub-interval is a different fact.
// It answered, and there was nothing in range — that is no_data, not a gap.
// Treating it as one would make "coverage incomplete" the permanent condition
// of every quiet region and destroy the signal the flag exists to carry.
//
// Pure and side-effect free so the rule can be tested without a network.
std::vector<source::CoverageInterval> coverage_gaps(
    const std::vector<source::CoverageInterval>& intervals);

// Retrieve from each adapter and persist. Raw bodies are content-addressed;
// every retrieval is recorded (REQ-4.6). Observation upsert is idempotent
// (REQ-4.3).
IngestReport ingest(db::Database& database,
                    const std::vector<std::unique_ptr<source::Adapter>>& adapters,
                    const source::Query& query);

// Load every stored observation, in a canonical order (REQ-6.3).
std::vector<model::Observation> load_observations(db::Database& database);

// Coverage statement for a region (REQ-1.6, REQ-8.6). Honest about the
// detection floor: what a source can see, not merely what it returned.
struct Coverage {
    std::string source_id;
    std::string note;
    // Sub-intervals of the stored window that this source did not observe.
    // Read back from the database so a report — and an offline replay
    // (REQ-2.11) — can state them without repeating the retrieval.
    std::vector<source::CoverageInterval> gaps;
};
std::vector<Coverage> coverage_notes(
    const std::vector<std::unique_ptr<source::Adapter>>& adapters,
    db::Database* database = nullptr);

// --- Output (REQ-8.2, REQ-8.3, REQ-8.4) ---

// RFC 7946 GeoJSON. Stable key order and fixed numeric formatting so that
// successive runs diff byte-for-byte (REQ-8.4).
std::string to_geojson(const std::vector<analysis::Event>& events,
                       const std::vector<Coverage>& coverage);

// Human-readable report. Always states detection limitations (REQ-8.6) and
// distinguishes observed from derived quantities (REQ-8.7).
std::string to_report(const std::vector<analysis::Event>& events,
                      const std::vector<Coverage>& coverage,
                      const source::Query& query,
                      const std::vector<std::string>& attributions);

}  // namespace kst::pipeline
