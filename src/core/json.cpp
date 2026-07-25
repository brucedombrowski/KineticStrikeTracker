#include "kst/json.hpp"

#include <charconv>
#include <cstdint>
#include <system_error>

namespace kst::json {

const Value* Value::find(std::string_view key) const {
    for (const Member& m : as_object()) {
        if (m.first == key) return &m.second;
    }
    return nullptr;
}

namespace {

class Parser {
  public:
    Parser(std::string_view in, const Limits& lim) : in_(in), lim_(lim) {}

    ParseResult run() {
        if (in_.size() > lim_.max_input_bytes) {
            return fail("input exceeds max_input_bytes limit");
        }
        skip_ws();
        std::optional<Value> v = parse_value();
        if (!v) return {std::nullopt, err_};
        skip_ws();
        if (pos_ != in_.size()) {
            return fail("trailing content after JSON document");
        }
        return {std::move(v), std::nullopt};
    }

  private:
    std::string_view in_;
    const Limits& lim_;
    std::size_t pos_ = 0;
    std::size_t depth_ = 0;
    std::optional<Error> err_;

    ParseResult fail(std::string message) {
        set_error(std::move(message));
        return {std::nullopt, err_};
    }

    void set_error(std::string message) {
        if (err_) return;  // keep the first (deepest) error
        Error e;
        e.message = std::move(message);
        e.offset = pos_;
        for (std::size_t i = 0; i < pos_ && i < in_.size(); ++i) {
            if (in_[i] == '\n') {
                ++e.line;
                e.column = 1;
            } else {
                ++e.column;
            }
        }
        err_ = std::move(e);
    }

    bool eof() const { return pos_ >= in_.size(); }
    char peek() const { return in_[pos_]; }

    void skip_ws() {
        while (!eof()) {
            const char c = peek();
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++pos_;
            } else {
                break;
            }
        }
    }

    bool consume(char expected, const char* what) {
        if (eof() || peek() != expected) {
            set_error(std::string("expected ") + what);
            return false;
        }
        ++pos_;
        return true;
    }

    std::optional<Value> parse_value() {
        if (eof()) {
            set_error("unexpected end of input");
            return std::nullopt;
        }
        switch (peek()) {
            case '{': return parse_object();
            case '[': return parse_array();
            case '"': {
                std::optional<std::string> s = parse_string();
                if (!s) return std::nullopt;
                return Value(std::move(*s));
            }
            case 't': return parse_literal("true", Value(true));
            case 'f': return parse_literal("false", Value(false));
            case 'n': return parse_literal("null", Value(nullptr));
            default:  return parse_number();
        }
    }

    std::optional<Value> parse_literal(std::string_view word, Value v) {
        if (in_.substr(pos_, word.size()) != word) {
            set_error("invalid literal");
            return std::nullopt;
        }
        pos_ += word.size();
        return v;
    }

    std::optional<Value> parse_object() {
        if (++depth_ > lim_.max_depth) {
            set_error("nesting exceeds max_depth limit");
            return std::nullopt;
        }
        ++pos_;  // '{'
        Object obj;
        skip_ws();
        if (!eof() && peek() == '}') {
            ++pos_;
            --depth_;
            return Value(std::move(obj));
        }
        while (true) {
            skip_ws();
            if (eof() || peek() != '"') {
                set_error("expected object key string");
                return std::nullopt;
            }
            std::optional<std::string> key = parse_string();
            if (!key) return std::nullopt;
            for (const Member& m : obj) {
                if (m.first == *key) {
                    set_error("duplicate object key");
                    return std::nullopt;
                }
            }
            skip_ws();
            if (!consume(':', "':' after object key")) return std::nullopt;
            skip_ws();
            std::optional<Value> v = parse_value();
            if (!v) return std::nullopt;
            obj.emplace_back(std::move(*key), std::move(*v));
            if (obj.size() > lim_.max_container_members) {
                set_error("object exceeds max_container_members limit");
                return std::nullopt;
            }
            skip_ws();
            if (!eof() && peek() == ',') {
                ++pos_;
                continue;
            }
            if (!consume('}', "',' or '}' in object")) return std::nullopt;
            --depth_;
            return Value(std::move(obj));
        }
    }

    std::optional<Value> parse_array() {
        if (++depth_ > lim_.max_depth) {
            set_error("nesting exceeds max_depth limit");
            return std::nullopt;
        }
        ++pos_;  // '['
        Array arr;
        skip_ws();
        if (!eof() && peek() == ']') {
            ++pos_;
            --depth_;
            return Value(std::move(arr));
        }
        while (true) {
            skip_ws();
            std::optional<Value> v = parse_value();
            if (!v) return std::nullopt;
            arr.push_back(std::move(*v));
            if (arr.size() > lim_.max_container_members) {
                set_error("array exceeds max_container_members limit");
                return std::nullopt;
            }
            skip_ws();
            if (!eof() && peek() == ',') {
                ++pos_;
                continue;
            }
            if (!consume(']', "',' or ']' in array")) return std::nullopt;
            --depth_;
            return Value(std::move(arr));
        }
    }

    // RFC 8259 section 7 string, with full escape handling, surrogate-pair
    // decoding, and UTF-8 validation of raw bytes.
    std::optional<std::string> parse_string() {
        ++pos_;  // opening quote
        std::string out;
        while (true) {
            if (eof()) {
                set_error("unterminated string");
                return std::nullopt;
            }
            if (out.size() > lim_.max_string_bytes) {
                set_error("string exceeds max_string_bytes limit");
                return std::nullopt;
            }
            const unsigned char c = static_cast<unsigned char>(in_[pos_]);
            if (c == '"') {
                ++pos_;
                return out;
            }
            if (c == '\\') {
                ++pos_;
                if (!append_escape(out)) return std::nullopt;
                continue;
            }
            if (c < 0x20) {
                set_error("unescaped control character in string");
                return std::nullopt;
            }
            if (c < 0x80) {
                out.push_back(static_cast<char>(c));
                ++pos_;
                continue;
            }
            if (!append_utf8_sequence(out)) return std::nullopt;
        }
    }

    bool append_escape(std::string& out) {
        if (eof()) {
            set_error("truncated escape sequence");
            return false;
        }
        const char e = in_[pos_++];
        switch (e) {
            case '"':  out.push_back('"'); return true;
            case '\\': out.push_back('\\'); return true;
            case '/':  out.push_back('/'); return true;
            case 'b':  out.push_back('\b'); return true;
            case 'f':  out.push_back('\f'); return true;
            case 'n':  out.push_back('\n'); return true;
            case 'r':  out.push_back('\r'); return true;
            case 't':  out.push_back('\t'); return true;
            case 'u':  return append_unicode_escape(out);
            default:
                --pos_;
                set_error("invalid escape character");
                return false;
        }
    }

    std::optional<std::uint32_t> read_hex4() {
        if (pos_ + 4 > in_.size()) {
            set_error("truncated \\u escape");
            return std::nullopt;
        }
        std::uint32_t v = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = in_[pos_ + static_cast<std::size_t>(i)];
            std::uint32_t d = 0;
            if (c >= '0' && c <= '9') {
                d = static_cast<std::uint32_t>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                d = static_cast<std::uint32_t>(c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                d = static_cast<std::uint32_t>(c - 'A' + 10);
            } else {
                set_error("invalid hex digit in \\u escape");
                return std::nullopt;
            }
            v = (v << 4) | d;
        }
        pos_ += 4;
        return v;
    }

    bool append_unicode_escape(std::string& out) {
        std::optional<std::uint32_t> hi = read_hex4();
        if (!hi) return false;
        std::uint32_t cp = *hi;
        if (cp >= 0xD800 && cp <= 0xDBFF) {  // high surrogate: need a pair
            if (pos_ + 2 > in_.size() || in_[pos_] != '\\' ||
                in_[pos_ + 1] != 'u') {
                set_error("unpaired high surrogate in \\u escape");
                return false;
            }
            pos_ += 2;
            std::optional<std::uint32_t> lo = read_hex4();
            if (!lo) return false;
            if (*lo < 0xDC00 || *lo > 0xDFFF) {
                set_error("invalid low surrogate in \\u escape");
                return false;
            }
            cp = 0x10000 + ((cp - 0xD800) << 10) + (*lo - 0xDC00);
        } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
            set_error("unpaired low surrogate in \\u escape");
            return false;
        }
        append_codepoint_utf8(out, cp);
        return true;
    }

    static void append_codepoint_utf8(std::string& out, std::uint32_t cp) {
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }

    // Validate and copy one raw UTF-8 multi-byte sequence (REQ-12.9:
    // invalid encoding is rejected, not passed through).
    bool append_utf8_sequence(std::string& out) {
        const unsigned char b0 = static_cast<unsigned char>(in_[pos_]);
        std::size_t len = 0;
        std::uint32_t min_cp = 0;
        if ((b0 & 0xE0) == 0xC0) {
            len = 2;
            min_cp = 0x80;
        } else if ((b0 & 0xF0) == 0xE0) {
            len = 3;
            min_cp = 0x800;
        } else if ((b0 & 0xF8) == 0xF0) {
            len = 4;
            min_cp = 0x10000;
        } else {
            set_error("invalid UTF-8 leading byte in string");
            return false;
        }
        if (pos_ + len > in_.size()) {
            set_error("truncated UTF-8 sequence in string");
            return false;
        }
        std::uint32_t cp = b0 & static_cast<std::uint32_t>(0x7F >> len);
        for (std::size_t i = 1; i < len; ++i) {
            const unsigned char bi =
                static_cast<unsigned char>(in_[pos_ + i]);
            if ((bi & 0xC0) != 0x80) {
                set_error("invalid UTF-8 continuation byte in string");
                return false;
            }
            cp = (cp << 6) | (bi & 0x3Fu);
        }
        if (cp < min_cp || cp > 0x10FFFF ||
            (cp >= 0xD800 && cp <= 0xDFFF)) {
            set_error("invalid UTF-8 code point in string");
            return false;
        }
        out.append(in_.substr(pos_, len));
        pos_ += len;
        return true;
    }

    // RFC 8259 section 6 number grammar, enforced before conversion so that
    // acceptance never depends on the conversion routine's leniency.
    std::optional<Value> parse_number() {
        const std::size_t start = pos_;
        if (!eof() && peek() == '-') ++pos_;
        if (eof()) {
            set_error("truncated number");
            return std::nullopt;
        }
        if (peek() == '0') {
            ++pos_;  // leading zero must stand alone
        } else if (peek() >= '1' && peek() <= '9') {
            while (!eof() && peek() >= '0' && peek() <= '9') ++pos_;
        } else {
            set_error("invalid value");
            return std::nullopt;
        }
        if (!eof() && peek() == '.') {
            ++pos_;
            if (eof() || peek() < '0' || peek() > '9') {
                set_error("digit required after decimal point");
                return std::nullopt;
            }
            while (!eof() && peek() >= '0' && peek() <= '9') ++pos_;
        }
        if (!eof() && (peek() == 'e' || peek() == 'E')) {
            ++pos_;
            if (!eof() && (peek() == '+' || peek() == '-')) ++pos_;
            if (eof() || peek() < '0' || peek() > '9') {
                set_error("digit required in exponent");
                return std::nullopt;
            }
            while (!eof() && peek() >= '0' && peek() <= '9') ++pos_;
        }
        const std::string_view text = in_.substr(start, pos_ - start);
        double d = 0.0;
        const std::from_chars_result r =
            std::from_chars(text.data(), text.data() + text.size(), d);
        if (r.ec == std::errc::result_out_of_range) {
            pos_ = start;
            set_error("number out of representable range");
            return std::nullopt;
        }
        if (r.ec != std::errc() || r.ptr != text.data() + text.size()) {
            pos_ = start;
            set_error("invalid number");
            return std::nullopt;
        }
        return Value(d);
    }
};

}  // namespace

ParseResult parse(std::string_view input, const Limits& limits) {
    return Parser(input, limits).run();
}

}  // namespace kst::json
