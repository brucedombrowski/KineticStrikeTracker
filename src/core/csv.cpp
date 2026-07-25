#include "kst/csv.hpp"

namespace kst::csv {

bool Table::has_column(std::string_view column) const {
    for (const std::string& h : header_) {
        if (h == column) return true;
    }
    return false;
}

std::string_view Table::get(std::size_t row, std::string_view column) const {
    if (row >= rows_.size()) return {};
    for (std::size_t i = 0; i < header_.size(); ++i) {
        if (header_[i] == column) {
            return i < rows_[row].size() ? std::string_view(rows_[row][i])
                                         : std::string_view();
        }
    }
    return {};
}

struct Parser {
    std::string_view in;
    const Limits& lim;
    std::size_t pos = 0;

    // One RFC 4180 record. Returns false at end of input.
    bool read_row(std::vector<std::string>& out, std::string* err) {
        out.clear();
        if (pos >= in.size()) return false;
        while (true) {
            std::string field;
            if (pos < in.size() && in[pos] == '"') {
                ++pos;
                while (true) {
                    if (pos >= in.size()) {
                        if (err) *err = "unterminated quoted field";
                        return false;
                    }
                    if (in[pos] == '"') {
                        if (pos + 1 < in.size() && in[pos + 1] == '"') {
                            field.push_back('"');
                            pos += 2;
                            continue;
                        }
                        ++pos;
                        break;
                    }
                    field.push_back(in[pos++]);
                    if (field.size() > lim.max_field_bytes) {
                        if (err) *err = "field exceeds max_field_bytes limit";
                        return false;
                    }
                }
            } else {
                while (pos < in.size() && in[pos] != ',' && in[pos] != '\n' &&
                       in[pos] != '\r') {
                    field.push_back(in[pos++]);
                    if (field.size() > lim.max_field_bytes) {
                        if (err) *err = "field exceeds max_field_bytes limit";
                        return false;
                    }
                }
            }
            out.push_back(std::move(field));
            if (out.size() > lim.max_columns) {
                if (err) *err = "row exceeds max_columns limit";
                return false;
            }
            if (pos < in.size() && in[pos] == ',') {
                ++pos;
                continue;
            }
            if (pos < in.size() && in[pos] == '\r') ++pos;
            if (pos < in.size() && in[pos] == '\n') ++pos;
            return true;
        }
    }
};

ParseResult parse(std::string_view input, const Limits& limits) {
    if (input.size() > limits.max_input_bytes) {
        return {std::nullopt, Error{"input exceeds max_input_bytes limit", 0}};
    }
    Parser p{input, limits, 0};
    Table t;
    std::string err;
    std::vector<std::string> row;

    // Skip leading blank lines, then take the header.
    while (p.read_row(row, &err)) {
        if (!(row.size() == 1 && row[0].empty())) break;
    }
    if (!err.empty()) return {std::nullopt, Error{err, 0}};
    if (row.empty()) return {std::nullopt, Error{"no header row", 0}};
    t.header_ = row;

    std::size_t n = 1;
    while (p.read_row(row, &err)) {
        if (!err.empty()) return {std::nullopt, Error{err, n}};
        // Trailing blank line is not a record.
        if (row.size() == 1 && row[0].empty()) continue;
        t.rows_.push_back(row);
        if (t.rows_.size() > limits.max_rows) {
            return {std::nullopt, Error{"input exceeds max_rows limit", n}};
        }
        ++n;
    }
    if (!err.empty()) return {std::nullopt, Error{err, n}};
    return {std::move(t), std::nullopt};
}

}  // namespace kst::csv
