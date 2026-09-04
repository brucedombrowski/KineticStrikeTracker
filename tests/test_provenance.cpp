// Regression for issue #28: every retrieval is persisted, and deduplicating a
// body must not discard the history of having fetched it.
//
// The FIRMS adapter satisfies a long window by walking it in 5-day chunks —
// fourteen requests for a two-month window, and more once several products
// are queried. All of them used to collapse into a single raw_response row
// whose request_url named only the last chunk, whose requested_at came from
// the first, whose http_status was hardcoded, and whose sha256 digested a
// concatenation NASA never served. REQ-2.10 requires each retrieval stored
// with its own URL, timestamp and status; REQ-4.6 adds that deduplication
// must not discard retrieval history.

#include <cstdio>
#include <fstream>
#include <optional>
#include <string>
#include <unistd.h>

#include "kst/db.hpp"
#include "kst/pipeline.hpp"
#include "kst/source.hpp"
#include "kst_test.hpp"

namespace {

struct TempFile {
    std::string path;
    explicit TempFile(const char* tag, const std::string& body)
        : path(std::string("/tmp/kst_prov_") + tag + "_" +
               std::to_string(::getpid()) + ".json") {
        std::ofstream(path, std::ios::binary) << body;
    }
    ~TempFile() { std::remove(path.c_str()); }
};

struct TempDb {
    std::string path;
    explicit TempDb(const char* tag)
        : path(std::string("/tmp/kst_prov_") + tag + "_" +
               std::to_string(::getpid()) + ".db") {
        std::remove(path.c_str());
    }
    ~TempDb() {
        std::remove(path.c_str());
        std::remove((path + "-wal").c_str());
        std::remove((path + "-shm").c_str());
    }
};

const char* kSeed = R"([
  {"id":"t1","time":"2026-07-01T00:00:00.000Z","latitude":26.85,
   "longitude":56.36,"location_uncertainty_km":5.0,"event_type":"explosion",
   "description":"fixture","author":"test"}
])";

std::int64_t count(kst::db::Database& db, const std::string& sql) {
    auto q = db.prepare(sql);
    if (!q || !q->step()) return -1;
    return q->column_int(0);
}

}  // namespace

// A retrieval whose body is already held still records that it happened.
KST_TEST(repeat_ingest_adds_a_retrieval_but_not_a_body) {
    TempFile f("dedup", kSeed);
    TempDb t("dedup");
    std::optional<kst::db::Error> err;
    auto db = kst::db::Database::open(t.path, &err);
    KST_CHECK(static_cast<bool>(db));
    if (!db) return;

    kst::source::Query q;
    q.start_time = "2026-07-01T00:00:00Z";
    q.end_time = "2026-07-02T00:00:00Z";

    for (int i = 0; i < 3; ++i) {
        std::vector<std::unique_ptr<kst::source::Adapter>> adapters;
        adapters.push_back(kst::source::make_file(
            f.path, "seed", kst::model::SourceClass::CuratedDataset, true));
        const auto report = kst::pipeline::ingest(*db, adapters, q);
        KST_CHECK(report.sources.size() == 1);
        if (!report.sources.empty()) {
            KST_CHECK(report.sources.front().retrievals == 1);
            // Only the first ingest stores the body; all three record the fetch.
            KST_CHECK(report.sources.front().bodies_stored == (i == 0 ? 1 : 0));
        }
    }

    KST_CHECK(count(*db, "SELECT COUNT(*) FROM raw_body") == 1);
    KST_CHECK(count(*db, "SELECT COUNT(*) FROM raw_response") == 3);
    // Idempotent upsert: still one observation (REQ-4.3).
    KST_CHECK(count(*db, "SELECT COUNT(*) FROM observation") == 1);
}

// Every observation names the response it was parsed from (REQ-7.4).
KST_TEST(observation_resolves_to_the_response_it_came_from) {
    TempFile f("link", kSeed);
    TempDb t("link");
    std::optional<kst::db::Error> err;
    auto db = kst::db::Database::open(t.path, &err);
    KST_CHECK(static_cast<bool>(db));
    if (!db) return;

    std::vector<std::unique_ptr<kst::source::Adapter>> adapters;
    adapters.push_back(kst::source::make_file(
        f.path, "seed", kst::model::SourceClass::CuratedDataset, true));
    kst::source::Query q;
    kst::pipeline::ingest(*db, adapters, q);

    KST_CHECK(count(*db, "SELECT COUNT(*) FROM observation "
                         "WHERE raw_response_id IS NULL") == 0);
    // and it joins to a real row whose digest matches the stored body
    KST_CHECK(count(*db,
                    "SELECT COUNT(*) FROM observation o "
                    "JOIN raw_response r ON r.id = o.raw_response_id "
                    "JOIN raw_body b ON b.sha256 = r.sha256") == 1);
}

// An adapter reports its retrievals through the interface, so the fix is not
// specific to FIRMS (issue #28, criterion 5).
KST_TEST(adapter_interface_carries_retrievals) {
    TempFile f("iface", kSeed);
    auto file = kst::source::make_file(
        f.path, "seed", kst::model::SourceClass::CuratedDataset, true);
    kst::source::Query q;
    const kst::source::Fetch fetch = file->fetch(q);
    KST_CHECK(fetch.ok());
    KST_CHECK(fetch.retrievals.size() == 1);
    if (!fetch.retrievals.empty()) {
        const auto& rt = fetch.retrievals.front();
        KST_CHECK(!rt.sha256.empty());
        KST_CHECK(!rt.raw_body.empty());
        // The body is stored as received, not stripped or merged.
        KST_CHECK(rt.raw_body == std::string(kSeed));
        KST_CHECK(rt.observations.size() == 1);
    }
}

// REQ-10.6: a credential must never reach the database or an output. The
// FIRMS MAP_KEY travels in the request path, and request URLs are persisted.
KST_TEST(map_key_never_survives_into_a_stored_url) {
    const std::string key = "60edc4e2f57e157563f10c302e3e0cb0";
    const std::string url =
        "https://firms.modaps.eosdis.nasa.gov/api/area/csv/" + key +
        "/VIIRS_SNPP_NRT/54,24.5,59,27.5/5/2026-07-11";
    const std::string safe = kst::source::redact_secret(url, key);
    KST_CHECK(safe.find(key) == std::string::npos);
    // The shape survives, so the request stays legible and reissuable.
    KST_CHECK(safe.find("VIIRS_SNPP_NRT/54,24.5,59,27.5/5/2026-07-11") !=
              std::string::npos);
    KST_CHECK(safe.find("<FIRMS_MAP_KEY>") != std::string::npos);
}

// A key quoted twice — a URL echoed inside an error message — must not
// survive in either copy.
KST_TEST(every_occurrence_of_a_secret_is_removed) {
    const std::string key = "deadbeefcafe";
    const std::string text = "GET " + key + " failed; retrying " + key;
    const std::string safe = kst::source::redact_secret(text, key);
    KST_CHECK(safe.find(key) == std::string::npos);
}

// An unset key must not turn every string into a mask.
KST_TEST(empty_secret_leaves_text_untouched) {
    KST_CHECK(kst::source::redact_secret("https://example/x", "") ==
              "https://example/x");
}

int main() { return kst::test::run(); }
