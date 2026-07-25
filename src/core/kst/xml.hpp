// kst::xml — bounded, read-only XML parser for untrusted input.
//
// Exists because QuakeML carries fields the FDSN text format silently drops
// (depthType, originUncertainty, typeCertainty) and those fields decide
// whether an origin is usable. The standard library has no XML facility and
// REQ-9.4 forbids third-party runtime dependencies without a memorandum.
//
// Deliberately NOT a general XML processor. The following are rejected or
// ignored outright rather than supported, because each is an attack surface
// with no upside for reading seismic bulletins:
//   - DOCTYPE / DTDs and ALL entity declarations. Custom entities are the
//     billion-laughs amplification vector and the XXE file-disclosure
//     vector; only the five predefined entities plus numeric character
//     references are honoured (REQ-12.4).
//   - External references of any kind. This parser never opens a file or a
//     socket — it sees one buffer and nothing else.
//   - Namespace resolution. Element names are matched on local name, which
//     is what QuakeML consumers need and avoids prefix-rebinding subtleties.
//
// Bounds mirror kst::json (REQ-12.3): input size, nesting depth, name and
// text length, and child count are all explicit, and exceeding one is a
// clean error rather than exhaustion.
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kst::xml {

struct Limits {
    std::size_t max_input_bytes = 64u * 1024u * 1024u;
    std::size_t max_depth = 100;
    std::size_t max_name_bytes = 1024;
    std::size_t max_text_bytes = 1u * 1024u * 1024u;
    std::size_t max_children = 1'000'000;
};

struct Element {
    std::string name;  // local name, namespace prefix stripped
    std::string text;  // concatenated direct character data, trimmed
    std::vector<std::pair<std::string, std::string>> attributes;
    std::vector<Element> children;

    // First direct child with this local name, or nullptr.
    const Element* child(std::string_view local_name) const;
    // Depth-first search over all descendants.
    const Element* find(std::string_view local_name) const;
    // All direct children with this local name, in document order.
    std::vector<const Element*> children_named(std::string_view local_name) const;
    // Text of a named descendant, or empty.
    std::string text_of(std::string_view local_name) const;
    const std::string* attribute(std::string_view key) const;
};

struct Error {
    std::string message;
    std::size_t offset = 0;
};

struct ParseResult {
    std::optional<Element> root;
    std::optional<Error> error;
    explicit operator bool() const { return root.has_value(); }
    const Element& operator*() const { return *root; }
    const Element* operator->() const { return &*root; }
};

ParseResult parse(std::string_view input, const Limits& limits = {});

}  // namespace kst::xml
