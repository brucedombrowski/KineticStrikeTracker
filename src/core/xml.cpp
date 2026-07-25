#include "kst/xml.hpp"

#include <algorithm>
#include <cstdint>

namespace kst::xml {

const Element* Element::child(std::string_view n) const {
    for (const Element& c : children) {
        if (c.name == n) return &c;
    }
    return nullptr;
}

const Element* Element::find(std::string_view n) const {
    for (const Element& c : children) {
        if (c.name == n) return &c;
        if (const Element* d = c.find(n)) return d;
    }
    return nullptr;
}

std::vector<const Element*> Element::children_named(std::string_view n) const {
    std::vector<const Element*> out;
    for (const Element& c : children) {
        if (c.name == n) out.push_back(&c);
    }
    return out;
}

std::string Element::text_of(std::string_view n) const {
    const Element* e = find(n);
    return e ? e->text : std::string();
}

const std::string* Element::attribute(std::string_view key) const {
    for (const auto& [k, v] : attributes) {
        if (k == key) return &v;
    }
    return nullptr;
}

namespace {

bool is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

bool is_name_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' ||
           c == ':' || static_cast<unsigned char>(c) >= 0x80;
}

bool is_name_char(char c) {
    return is_name_start(c) || (c >= '0' && c <= '9') || c == '-' || c == '.';
}

class Parser {
  public:
    Parser(std::string_view in, const Limits& lim) : in_(in), lim_(lim) {}

    ParseResult run() {
        if (in_.size() > lim_.max_input_bytes) {
            return fail("input exceeds max_input_bytes limit");
        }
        skip_prolog();
        if (err_) return {std::nullopt, err_};
        if (eof() || peek() != '<') return fail("no root element");
        Element root;
        if (!parse_element(root, 0)) return {std::nullopt, err_};
        return {std::move(root), std::nullopt};
    }

  private:
    std::string_view in_;
    const Limits& lim_;
    std::size_t pos_ = 0;
    std::optional<Error> err_;

    bool eof() const { return pos_ >= in_.size(); }
    char peek() const { return in_[pos_]; }
    bool starts_with(std::string_view s) const {
        return in_.compare(pos_, s.size(), s) == 0;
    }

    ParseResult fail(std::string m) {
        set_error(std::move(m));
        return {std::nullopt, err_};
    }

    void set_error(std::string m) {
        if (!err_) err_ = Error{std::move(m), pos_};
    }

    void skip_space() {
        while (!eof() && is_space(peek())) ++pos_;
    }

    // Prolog handling. A DOCTYPE is refused rather than skipped: accepting
    // one would mean deciding what to do with entity declarations, and the
    // safe answer is to not be in that business at all.
    void skip_prolog() {
        while (true) {
            skip_space();
            if (eof()) return;
            if (starts_with("<?")) {
                const std::size_t end = in_.find("?>", pos_);
                if (end == std::string_view::npos) {
                    set_error("unterminated processing instruction");
                    return;
                }
                pos_ = end + 2;
                continue;
            }
            if (starts_with("<!--")) {
                if (!skip_comment()) return;
                continue;
            }
            if (starts_with("<!DOCTYPE") || starts_with("<!ENTITY")) {
                set_error(
                    "DOCTYPE and entity declarations are refused: they are the "
                    "entity-expansion and external-entity attack vectors and "
                    "are not needed to read seismic bulletins");
                return;
            }
            return;
        }
    }

    bool skip_comment() {
        const std::size_t end = in_.find("-->", pos_);
        if (end == std::string_view::npos) {
            set_error("unterminated comment");
            return false;
        }
        pos_ = end + 3;
        return true;
    }

    std::string parse_name() {
        const std::size_t start = pos_;
        if (eof() || !is_name_start(peek())) {
            set_error("expected element or attribute name");
            return {};
        }
        while (!eof() && is_name_char(peek())) ++pos_;
        std::string_view raw = in_.substr(start, pos_ - start);
        if (raw.size() > lim_.max_name_bytes) {
            set_error("name exceeds max_name_bytes limit");
            return {};
        }
        // Strip namespace prefix: match on local name (documented in header).
        const std::size_t colon = raw.rfind(':');
        if (colon != std::string_view::npos) raw.remove_prefix(colon + 1);
        return std::string(raw);
    }

    // Only the five predefined entities and numeric character references.
    // Anything else is an error, not a silent pass-through — a stray '&'
    // in a bulletin is malformed input we want to hear about (REQ-3.7).
    bool decode_entity(std::string& out) {
        const std::size_t semi = in_.find(';', pos_);
        if (semi == std::string_view::npos || semi - pos_ > 12) {
            set_error("unterminated or over-long entity reference");
            return false;
        }
        const std::string_view name = in_.substr(pos_ + 1, semi - pos_ - 1);
        if (name == "lt")        out.push_back('<');
        else if (name == "gt")   out.push_back('>');
        else if (name == "amp")  out.push_back('&');
        else if (name == "quot") out.push_back('"');
        else if (name == "apos") out.push_back('\'');
        else if (!name.empty() && name[0] == '#') {
            std::uint32_t cp = 0;
            const bool hex = name.size() > 1 && (name[1] == 'x' || name[1] == 'X');
            for (std::size_t i = hex ? 2 : 1; i < name.size(); ++i) {
                const char c = name[i];
                std::uint32_t d;
                if (c >= '0' && c <= '9') d = static_cast<std::uint32_t>(c - '0');
                else if (hex && c >= 'a' && c <= 'f') d = static_cast<std::uint32_t>(c - 'a' + 10);
                else if (hex && c >= 'A' && c <= 'F') d = static_cast<std::uint32_t>(c - 'A' + 10);
                else {
                    set_error("invalid numeric character reference");
                    return false;
                }
                cp = cp * (hex ? 16u : 10u) + d;
                if (cp > 0x10FFFF) {
                    set_error("character reference out of range");
                    return false;
                }
            }
            append_utf8(out, cp);
        } else {
            set_error("undefined entity reference '" + std::string(name) +
                      "' (custom entities are not supported)");
            return false;
        }
        pos_ = semi + 1;
        return true;
    }

    static void append_utf8(std::string& out, std::uint32_t cp) {
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

    bool parse_attributes(Element& e, bool& self_closing) {
        while (true) {
            skip_space();
            if (eof()) {
                set_error("unterminated start tag");
                return false;
            }
            if (peek() == '>') {
                ++pos_;
                self_closing = false;
                return true;
            }
            if (starts_with("/>")) {
                pos_ += 2;
                self_closing = true;
                return true;
            }
            std::string key = parse_name();
            if (err_) return false;
            skip_space();
            if (eof() || peek() != '=') {
                set_error("expected '=' after attribute name");
                return false;
            }
            ++pos_;
            skip_space();
            if (eof() || (peek() != '"' && peek() != '\'')) {
                set_error("expected quoted attribute value");
                return false;
            }
            const char quote = peek();
            ++pos_;
            std::string value;
            while (true) {
                if (eof()) {
                    set_error("unterminated attribute value");
                    return false;
                }
                if (peek() == quote) {
                    ++pos_;
                    break;
                }
                if (peek() == '&') {
                    if (!decode_entity(value)) return false;
                    continue;
                }
                value.push_back(in_[pos_++]);
                if (value.size() > lim_.max_text_bytes) {
                    set_error("attribute value exceeds max_text_bytes limit");
                    return false;
                }
            }
            e.attributes.emplace_back(std::move(key), std::move(value));
        }
    }

    bool parse_element(Element& e, std::size_t depth) {
        if (depth > lim_.max_depth) {
            set_error("nesting exceeds max_depth limit");
            return false;
        }
        ++pos_;  // '<'
        e.name = parse_name();
        if (err_) return false;

        bool self_closing = false;
        if (!parse_attributes(e, self_closing)) return false;
        if (self_closing) return true;

        std::string text;
        while (true) {
            if (eof()) {
                set_error("unterminated element '" + e.name + "'");
                return false;
            }
            if (peek() == '<') {
                if (starts_with("</")) {
                    pos_ += 2;
                    const std::string close = parse_name();
                    if (err_) return false;
                    if (close != e.name) {
                        set_error("mismatched closing tag: expected '" + e.name +
                                  "', found '" + close + "'");
                        return false;
                    }
                    skip_space();
                    if (eof() || peek() != '>') {
                        set_error("unterminated closing tag");
                        return false;
                    }
                    ++pos_;
                    break;
                }
                if (starts_with("<!--")) {
                    if (!skip_comment()) return false;
                    continue;
                }
                if (starts_with("<![CDATA[")) {
                    const std::size_t end = in_.find("]]>", pos_);
                    if (end == std::string_view::npos) {
                        set_error("unterminated CDATA section");
                        return false;
                    }
                    text.append(in_.substr(pos_ + 9, end - pos_ - 9));
                    pos_ = end + 3;
                    continue;
                }
                if (starts_with("<?")) {
                    const std::size_t end = in_.find("?>", pos_);
                    if (end == std::string_view::npos) {
                        set_error("unterminated processing instruction");
                        return false;
                    }
                    pos_ = end + 2;
                    continue;
                }
                if (e.children.size() >= lim_.max_children) {
                    set_error("element exceeds max_children limit");
                    return false;
                }
                Element child;
                if (!parse_element(child, depth + 1)) return false;
                e.children.push_back(std::move(child));
                continue;
            }
            if (peek() == '&') {
                if (!decode_entity(text)) return false;
                continue;
            }
            text.push_back(in_[pos_++]);
            if (text.size() > lim_.max_text_bytes) {
                set_error("text exceeds max_text_bytes limit");
                return false;
            }
        }

        const std::size_t b = text.find_first_not_of(" \t\n\r");
        if (b != std::string::npos) {
            const std::size_t f = text.find_last_not_of(" \t\n\r");
            e.text = text.substr(b, f - b + 1);
        }
        return true;
    }
};

}  // namespace

ParseResult parse(std::string_view input, const Limits& limits) {
    return Parser(input, limits).run();
}

}  // namespace kst::xml
