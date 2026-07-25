#include "kst/db.hpp"

#include <sqlite3.h>

#include <array>
#include <utility>

namespace kst::db {

namespace {

// Migrations applied in order; index i takes the schema from version i to
// i+1 (REQ-4.2). Never edit a shipped migration — append a new one.
constexpr std::array<const char*, 2> kMigrations = {
    // v0 -> v1: initial schema.
    R"SQL(
    -- Raw source responses, exactly as received (REQ-2.10). Append-only
    -- (REQ-4.5), enforced below by triggers rather than by convention.
    -- Content-addressed bodies: each distinct body stored once (REQ-4.6).
    CREATE TABLE raw_body (
        sha256      TEXT PRIMARY KEY,
        body        BLOB NOT NULL,
        byte_count  INTEGER NOT NULL,
        first_seen  TEXT NOT NULL
    );

    -- One row per retrieval. Bodies dedupe; retrievals never do — knowing
    -- when a source was last confirmed unchanged is provenance (REQ-4.6).
    CREATE TABLE raw_response (
        id            INTEGER PRIMARY KEY,
        source_id     TEXT    NOT NULL,
        request_url   TEXT    NOT NULL,
        requested_at  TEXT    NOT NULL,   -- ISO 8601 UTC (REQ-3.2)
        http_status   INTEGER,
        content_type  TEXT,
        sha256        TEXT    NOT NULL REFERENCES raw_body(sha256),
        run_id        INTEGER
    );
    CREATE INDEX idx_raw_response_source ON raw_response(source_id, requested_at);
    CREATE INDEX idx_raw_response_sha    ON raw_response(sha256);

    CREATE TRIGGER raw_body_no_update
    BEFORE UPDATE ON raw_body BEGIN
        SELECT RAISE(ABORT, 'raw_body is append-only (REQ-4.5)');
    END;
    CREATE TRIGGER raw_body_no_delete
    BEFORE DELETE ON raw_body BEGIN
        SELECT RAISE(ABORT, 'raw_body is append-only (REQ-4.5)');
    END;

    CREATE TRIGGER raw_response_no_update
    BEFORE UPDATE ON raw_response BEGIN
        SELECT RAISE(ABORT, 'raw_response is append-only (REQ-4.5)');
    END;
    CREATE TRIGGER raw_response_no_delete
    BEFORE DELETE ON raw_response BEGIN
        SELECT RAISE(ABORT, 'raw_response is append-only (REQ-4.5)');
    END;

    -- One row per analysis run (REQ-4.4).
    CREATE TABLE run (
        id             INTEGER PRIMARY KEY,
        run_uid        TEXT    NOT NULL UNIQUE,
        software_version TEXT  NOT NULL,
        git_commit     TEXT,
        config_digest  TEXT,
        started_at     TEXT    NOT NULL,
        ended_at       TEXT,
        source_coverage_complete INTEGER NOT NULL DEFAULT 1  -- REQ-2.9
    );

    -- Normalised observations (REQ-3.1). observation_uid is derived
    -- deterministically from source_id + native_id (REQ-3.5), which also
    -- makes re-ingest idempotent (REQ-4.3) via UPSERT on the unique key.
    CREATE TABLE observation (
        id              INTEGER PRIMARY KEY,
        observation_uid TEXT    NOT NULL UNIQUE,
        source_id       TEXT    NOT NULL,
        source_class    TEXT    NOT NULL,   -- A..E (REQ-7.2)
        native_id       TEXT    NOT NULL,
        origin_time     TEXT    NOT NULL,   -- ISO 8601 UTC
        latitude        REAL,               -- WGS 84 (REQ-3.3)
        longitude       REAL,
        depth_km        REAL,
        depth_is_fixed  INTEGER NOT NULL DEFAULT 0,  -- agency default (REQ-5.2)
        magnitude       REAL,
        magnitude_type  TEXT,               -- never converted (REQ-3.4)
        location_uncertainty_km REAL,
        reported_event_type TEXT,
        description     TEXT,
        is_curated      INTEGER NOT NULL DEFAULT 0,  -- seed data (REQ-2.18)
        raw_response_id INTEGER REFERENCES raw_response(id),
        first_seen_run  INTEGER REFERENCES run(id),
        last_seen_run   INTEGER REFERENCES run(id),
        UNIQUE(source_id, native_id)
    );
    CREATE INDEX idx_observation_time ON observation(origin_time);
    CREATE INDEX idx_observation_pos  ON observation(latitude, longitude);

    -- Superseded observation values retained rather than overwritten
    -- (REQ-2.17): revisions are history, not corrections.
    CREATE TABLE observation_revision (
        id             INTEGER PRIMARY KEY,
        observation_id INTEGER NOT NULL REFERENCES observation(id),
        superseded_at  TEXT    NOT NULL,
        prior_values   TEXT    NOT NULL   -- JSON snapshot of replaced fields
    );

    -- Files ingested from disk, with digest for change detection (REQ-12.10).
    CREATE TABLE ingested_file (
        id           INTEGER PRIMARY KEY,
        path         TEXT NOT NULL,
        sha256       TEXT NOT NULL,
        byte_count   INTEGER NOT NULL,
        ingested_at  TEXT NOT NULL,
        run_id       INTEGER REFERENCES run(id)
    );
    CREATE INDEX idx_ingested_file_path ON ingested_file(path, ingested_at);
    )SQL",
    // v1 -> v2: depth provenance and source-declared type certainty. The
    // text formats hide these; QuakeML publishes them, and they decide
    // whether an origin is usable (issue #23, DM-2026-007).
    R"SQL(
    ALTER TABLE observation ADD COLUMN depth_type TEXT;
    ALTER TABLE observation ADD COLUMN type_certainty TEXT;
    )SQL",
};

static_assert(kMigrations.size() == static_cast<std::size_t>(kSchemaVersion),
              "migration count must equal kSchemaVersion");

}  // namespace

// --- Statement ---

Statement::~Statement() {
    if (stmt_) sqlite3_finalize(stmt_);
}

Statement::Statement(Statement&& other) noexcept
    : stmt_(std::exchange(other.stmt_, nullptr)),
      error_(std::move(other.error_)) {}

Statement& Statement::operator=(Statement&& other) noexcept {
    if (this != &other) {
        if (stmt_) sqlite3_finalize(stmt_);
        stmt_ = std::exchange(other.stmt_, nullptr);
        error_ = std::move(other.error_);
    }
    return *this;
}

Statement& Statement::bind(int index, std::int64_t value) {
    sqlite3_bind_int64(stmt_, index, value);
    return *this;
}

Statement& Statement::bind(int index, double value) {
    sqlite3_bind_double(stmt_, index, value);
    return *this;
}

Statement& Statement::bind(int index, std::string_view value) {
    // SQLITE_TRANSIENT: SQLite copies, so the caller's buffer need not
    // outlive the bind.
    sqlite3_bind_text(stmt_, index, value.data(),
                      static_cast<int>(value.size()), SQLITE_TRANSIENT);
    return *this;
}

Statement& Statement::bind_null(int index) {
    sqlite3_bind_null(stmt_, index);
    return *this;
}

bool Statement::step() {
    const int rc = sqlite3_step(stmt_);
    if (rc == SQLITE_ROW) return true;
    if (rc != SQLITE_DONE) {
        error_ = Error{sqlite3_errmsg(sqlite3_db_handle(stmt_))};
    }
    return false;
}

bool Statement::execute() {
    const int rc = sqlite3_step(stmt_);
    if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
        error_ = Error{sqlite3_errmsg(sqlite3_db_handle(stmt_))};
        return false;
    }
    return true;
}

std::int64_t Statement::column_int(int index) const {
    return sqlite3_column_int64(stmt_, index);
}

double Statement::column_double(int index) const {
    return sqlite3_column_double(stmt_, index);
}

std::string Statement::column_text(int index) const {
    const unsigned char* p = sqlite3_column_text(stmt_, index);
    if (!p) return {};
    return std::string(reinterpret_cast<const char*>(p),
                       static_cast<std::size_t>(sqlite3_column_bytes(stmt_, index)));
}

bool Statement::column_is_null(int index) const {
    return sqlite3_column_type(stmt_, index) == SQLITE_NULL;
}

// --- Database ---

Database::~Database() {
    if (db_) sqlite3_close(db_);
}

Database::Database(Database&& other) noexcept
    : db_(std::exchange(other.db_, nullptr)),
      error_(std::move(other.error_)) {}

Database& Database::operator=(Database&& other) noexcept {
    if (this != &other) {
        if (db_) sqlite3_close(db_);
        db_ = std::exchange(other.db_, nullptr);
        error_ = std::move(other.error_);
    }
    return *this;
}

std::optional<Database> Database::open(const std::string& path,
                                       std::optional<Error>* error) {
    Database db;
    const int rc = sqlite3_open_v2(
        path.c_str(), &db.db_,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX,
        nullptr);
    if (rc != SQLITE_OK) {
        if (error) {
            *error = Error{db.db_ ? sqlite3_errmsg(db.db_)
                                  : "sqlite3_open_v2 failed"};
        }
        return std::nullopt;
    }

    db.execute("PRAGMA foreign_keys = ON");
    db.execute("PRAGMA journal_mode = WAL");
    db.execute("PRAGMA synchronous = FULL");

    const int current = db.schema_version();
    if (current > kSchemaVersion) {
        if (error) {
            *error = Error{"database schema version " +
                           std::to_string(current) +
                           " is newer than this build supports (" +
                           std::to_string(kSchemaVersion) + ")"};
        }
        return std::nullopt;  // REQ-4.2: refuse rather than guess
    }

    for (int v = current; v < kSchemaVersion; ++v) {
        if (!db.execute("BEGIN")) break;
        if (!db.execute(kMigrations[static_cast<std::size_t>(v)]) ||
            !db.execute("PRAGMA user_version = " + std::to_string(v + 1))) {
            db.execute("ROLLBACK");
            if (error) *error = db.error_;
            return std::nullopt;
        }
        if (!db.execute("COMMIT")) {
            if (error) *error = db.error_;
            return std::nullopt;
        }
    }
    return db;
}

int Database::schema_version() {
    std::optional<Statement> s = prepare("PRAGMA user_version");
    if (!s || !s->step()) return 0;
    return static_cast<int>(s->column_int(0));
}

std::optional<Statement> Database::prepare(std::string_view sql) {
    sqlite3_stmt* stmt = nullptr;
    const int rc = sqlite3_prepare_v2(db_, sql.data(),
                                      static_cast<int>(sql.size()), &stmt,
                                      nullptr);
    if (rc != SQLITE_OK) {
        error_ = Error{sqlite3_errmsg(db_)};
        return std::nullopt;
    }
    return Statement(stmt);
}

bool Database::execute(std::string_view sql) {
    char* msg = nullptr;
    const std::string text(sql);
    const int rc = sqlite3_exec(db_, text.c_str(), nullptr, nullptr, &msg);
    if (rc != SQLITE_OK) {
        error_ = Error{msg ? msg : "sqlite3_exec failed"};
        if (msg) sqlite3_free(msg);
        return false;
    }
    return true;
}

}  // namespace kst::db
