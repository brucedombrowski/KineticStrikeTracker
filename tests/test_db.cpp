// kst::db tests — REQ-4.2 (schema version), REQ-4.3 (idempotence),
// REQ-4.5 (append-only raw store, trigger-enforced), REQ-12.2 (parameter
// binding defeats injection).
#include <cstdio>
#include <unistd.h>
#include <optional>
#include <string>

#include "kst/db.hpp"
#include "kst_test.hpp"

using kst::db::Database;
using kst::db::Error;

namespace {

std::string temp_path(const char* tag) {
    return std::string("/tmp/kst_test_") + tag + "_" +
           std::to_string(::getpid()) + ".db";
}

struct TempDb {
    std::string path;
    explicit TempDb(const char* tag) : path(temp_path(tag)) {
        std::remove(path.c_str());
    }
    ~TempDb() {
        std::remove(path.c_str());
        std::remove((path + "-wal").c_str());
        std::remove((path + "-shm").c_str());
    }
};

}  // namespace

KST_TEST(opens_and_migrates_to_current_schema) {
    TempDb t("migrate");
    std::optional<Error> err;
    auto db = Database::open(t.path, &err);
    KST_CHECK(static_cast<bool>(db));
    if (!db) return;
    KST_CHECK(db->schema_version() == kst::db::kSchemaVersion);
}

KST_TEST(reopen_is_stable_no_double_migration) {
    TempDb t("reopen");
    std::optional<Error> err;
    { auto db = Database::open(t.path, &err); KST_CHECK(static_cast<bool>(db)); }
    auto db2 = Database::open(t.path, &err);
    KST_CHECK(static_cast<bool>(db2));
    if (db2) KST_CHECK(db2->schema_version() == kst::db::kSchemaVersion);
}

KST_TEST(refuses_database_from_a_newer_build) {
    TempDb t("newer");
    std::optional<Error> err;
    {
        auto db = Database::open(t.path, &err);
        KST_CHECK(static_cast<bool>(db));
        if (db) {
            db->execute("PRAGMA user_version = " +
                        std::to_string(kst::db::kSchemaVersion + 7));
        }
    }
    auto db2 = Database::open(t.path, &err);
    KST_CHECK(!db2);  // REQ-4.2: refuse, do not guess
    KST_CHECK(err && err->message.find("newer") != std::string::npos);
}

KST_TEST(raw_response_is_append_only) {
    TempDb t("append");
    std::optional<Error> err;
    auto db = Database::open(t.path, &err);
    KST_CHECK(static_cast<bool>(db));
    if (!db) return;
    auto ins = db->prepare(
        "INSERT INTO raw_response(source_id,request_url,requested_at,"
        "http_status,content_type,body,sha256,byte_count) "
        "VALUES(?,?,?,?,?,?,?,?)");
    KST_CHECK(static_cast<bool>(ins));
    if (!ins) return;
    ins->bind(1, "usgs").bind(2, "https://example.test/q")
        .bind(3, "2026-07-25T00:00:00.000Z").bind(4, std::int64_t{200})
        .bind(5, "application/json").bind(6, "{}").bind(7, "deadbeef")
        .bind(8, std::int64_t{2});
    KST_CHECK(ins->execute());

    // Triggers must reject both, regardless of caller intent (REQ-4.5).
    KST_CHECK(!db->execute("UPDATE raw_response SET body = 'x' WHERE id = 1"));
    KST_CHECK(!db->execute("DELETE FROM raw_response WHERE id = 1"));

    auto count = db->prepare("SELECT COUNT(*) FROM raw_response");
    KST_CHECK(static_cast<bool>(count) && count->step());
    if (count) KST_CHECK(count->column_int(0) == 1);
}

KST_TEST(observation_upsert_is_idempotent) {
    TempDb t("idem");
    std::optional<Error> err;
    auto db = Database::open(t.path, &err);
    KST_CHECK(static_cast<bool>(db));
    if (!db) return;
    const char* kSql =
        "INSERT INTO observation(observation_uid,source_id,source_class,"
        "native_id,origin_time,magnitude,magnitude_type) VALUES(?,?,?,?,?,?,?) "
        "ON CONFLICT(source_id,native_id) DO UPDATE SET "
        "magnitude=excluded.magnitude, magnitude_type=excluded.magnitude_type";
    for (int i = 0; i < 3; ++i) {  // same record three times
        auto s = db->prepare(kSql);
        KST_CHECK(static_cast<bool>(s));
        if (!s) return;
        s->bind(1, "usgs:ak123").bind(2, "usgs").bind(3, "A")
            .bind(4, "ak123").bind(5, "2025-06-20T17:49:14.000Z")
            .bind(6, 4.9).bind(7, "mww");
        KST_CHECK(s->execute());
    }
    auto count = db->prepare("SELECT COUNT(*) FROM observation");
    KST_CHECK(static_cast<bool>(count) && count->step());
    if (count) KST_CHECK(count->column_int(0) == 1);  // REQ-4.3
}

KST_TEST(bound_parameters_are_not_sql) {
    TempDb t("inject");
    std::optional<Error> err;
    auto db = Database::open(t.path, &err);
    KST_CHECK(static_cast<bool>(db));
    if (!db) return;
    // A classic payload bound as a value must be stored verbatim, never
    // executed (REQ-12.2).
    const std::string payload = "'); DROP TABLE observation; --";
    auto s = db->prepare(
        "INSERT INTO observation(observation_uid,source_id,source_class,"
        "native_id,origin_time,description) VALUES(?,?,?,?,?,?)");
    KST_CHECK(static_cast<bool>(s));
    if (!s) return;
    s->bind(1, "x:1").bind(2, "x").bind(3, "D").bind(4, "1")
        .bind(5, "2026-01-01T00:00:00.000Z").bind(6, payload);
    KST_CHECK(s->execute());

    auto q = db->prepare("SELECT description FROM observation WHERE native_id = ?");
    KST_CHECK(static_cast<bool>(q));
    if (!q) return;
    q->bind(1, "1");
    KST_CHECK(q->step());
    KST_CHECK(q->column_text(0) == payload);  // stored, not executed

    auto still = db->prepare("SELECT COUNT(*) FROM observation");
    KST_CHECK(static_cast<bool>(still) && still->step());  // table survives
}

int main() { return kst::test::run(); }
