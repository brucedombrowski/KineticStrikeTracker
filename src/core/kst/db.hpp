// kst::db — local SQLite persistence (REQ-4.1).
//
// Safety and determinism posture:
// - REQ-12.2: every statement is prepared with bound parameters. This
//   interface offers no way to splice a value into SQL text.
// - REQ-4.2: the database carries a schema version; the library refuses to
//   open a database whose version it does not recognise, and applies forward
//   migrations in a fixed order.
// - REQ-4.5: raw_response is append-only — enforced by SQL triggers, not by
//   convention, so a future careless call site cannot quietly violate it.
// - REQ-4.3: ingestion is idempotent via natural-key upsert.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct sqlite3;
struct sqlite3_stmt;

namespace kst::db {

// Bumped whenever migrations are added. Opening a database newer than this
// is an error (REQ-4.2).
inline constexpr int kSchemaVersion = 1;

struct Error {
    std::string message;
};

// A prepared statement with bound parameters. Values are never formatted
// into SQL text (REQ-12.2).
class Statement {
  public:
    ~Statement();
    Statement(Statement&&) noexcept;
    Statement& operator=(Statement&&) noexcept;
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    // 1-based parameter binding, mirroring SQLite's own indexing.
    Statement& bind(int index, std::int64_t value);
    Statement& bind(int index, double value);
    Statement& bind(int index, std::string_view value);
    Statement& bind_null(int index);

    // Advance one row. Returns true while rows remain.
    bool step();
    // Run to completion for statements returning no rows.
    bool execute();

    std::int64_t column_int(int index) const;
    double column_double(int index) const;
    std::string column_text(int index) const;
    bool column_is_null(int index) const;

    const std::optional<Error>& error() const { return error_; }

  private:
    friend class Database;
    explicit Statement(sqlite3_stmt* stmt) : stmt_(stmt) {}
    sqlite3_stmt* stmt_ = nullptr;
    std::optional<Error> error_;
};

class Database {
  public:
    // Opens (creating if absent) and migrates to kSchemaVersion.
    static std::optional<Database> open(const std::string& path,
                                        std::optional<Error>* error);

    ~Database();
    Database(Database&&) noexcept;
    Database& operator=(Database&&) noexcept;
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    std::optional<Statement> prepare(std::string_view sql);
    bool execute(std::string_view sql);  // no parameters, no rows

    int schema_version();
    const std::optional<Error>& error() const { return error_; }

    sqlite3* handle() const { return db_; }

  private:
    Database() = default;
    sqlite3* db_ = nullptr;
    std::optional<Error> error_;
};

}  // namespace kst::db
