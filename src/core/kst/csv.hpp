// kst::csv — bounded RFC 4180 CSV reader for untrusted input (REQ-2.13).
//
// Fields are addressed BY HEADER NAME, never by position: upstream providers
// add and reorder columns, and a positional parser fails silently into
// mislabelled data rather than loudly (REQ-3.7).
//
// Bounds mirror the JSON and XML parsers (REQ-12.3): input size, row count,
// column count, and field length are explicit, and exceeding one is a clean
// error rather than exhaustion.
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kst::csv {

struct Limits {
    std::size_t max_input_bytes = 64u * 1024u * 1024u;
    std::size_t max_rows = 2'000'000;
    std::size_t max_columns = 256;
    std::size_t max_field_bytes = 64u * 1024u;
};

struct Error {
    std::string message;
    std::size_t row = 0;
};

struct ParseResult;

class Table {
  public:
    std::size_t rows() const { return rows_.size(); }
    const std::vector<std::string>& header() const { return header_; }

    // Value at (row, column-name), or empty if the column does not exist.
    std::string_view get(std::size_t row, std::string_view column) const;
    bool has_column(std::string_view column) const;

  private:
    friend struct Parser;
    friend ParseResult parse(std::string_view, const Limits&);
    std::vector<std::string> header_;
    std::vector<std::vector<std::string>> rows_;
};

struct ParseResult {
    std::optional<Table> table;
    std::optional<Error> error;
    explicit operator bool() const { return table.has_value(); }
    const Table& operator*() const { return *table; }
    const Table* operator->() const { return &*table; }
};

// First non-empty line is the header. Quoted fields, doubled quotes, and
// embedded newlines are handled per RFC 4180.
ParseResult parse(std::string_view input, const Limits& limits = {});

}  // namespace kst::csv
