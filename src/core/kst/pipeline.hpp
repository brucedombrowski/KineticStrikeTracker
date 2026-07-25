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
    bool body_was_new = true;  // false when dedup matched (REQ-4.6)
};

struct IngestReport {
    std::vector<SourceResult> sources;
    int total_observations = 0;
    bool coverage_complete = true;
};

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
};
std::vector<Coverage> coverage_notes(
    const std::vector<std::unique_ptr<source::Adapter>>& adapters);

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
