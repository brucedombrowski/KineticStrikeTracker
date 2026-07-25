// kst::json — bounded, deterministic JSON parser for untrusted input.
//
// Why in-project: the C++ standard library has no JSON facility, and REQ-9.4
// forbids third-party runtime dependencies without a decision memorandum.
//
// Threat model (ASM-06): every byte parsed here is potentially hostile.
// - REQ-12.3: explicit bounds on input size, nesting depth, string length,
//   and container member count; violation is a clean error, never memory or
//   stack exhaustion.
// - REQ-12.9: exercised against truncated, malformed, deeply nested,
//   oversized, and invalid-encoding fixtures.
//
// Determinism (REQ-1.2):
// - Object members are stored in document order in a vector — never an
//   unordered container.
// - Duplicate object keys are rejected outright: silently keeping the first
//   or last would make meaning depend on a policy invisible to the reader.
// - Grammar is strict RFC 8259: no extensions, no leniency, so acceptance
//   is a pure function of the bytes.
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace kst::json {

// Documented parse bounds (REQ-12.3). Defaults sized for the largest
// expected upstream responses (USGS caps a query at 20 000 events).
struct Limits {
    std::size_t max_input_bytes = 32u * 1024u * 1024u;  // 32 MiB
    std::size_t max_depth = 64;                          // nesting levels
    std::size_t max_string_bytes = 1u * 1024u * 1024u;   // 1 MiB per string
    std::size_t max_container_members = 1'000'000;       // per array/object
};

class Value;
using Array = std::vector<Value>;
using Member = std::pair<std::string, Value>;
using Object = std::vector<Member>;  // document order preserved

class Value {
  public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Value() : v_(nullptr) {}
    explicit Value(std::nullptr_t) : v_(nullptr) {}
    explicit Value(bool b) : v_(b) {}
    explicit Value(double n) : v_(n) {}
    explicit Value(std::string s) : v_(std::move(s)) {}
    explicit Value(Array a) : v_(std::move(a)) {}
    explicit Value(Object o) : v_(std::move(o)) {}

    Type type() const { return static_cast<Type>(v_.index()); }
    bool is_null() const { return type() == Type::Null; }

    bool as_bool() const { return std::get<bool>(v_); }
    double as_number() const { return std::get<double>(v_); }
    const std::string& as_string() const { return std::get<std::string>(v_); }
    const Array& as_array() const { return std::get<Array>(v_); }
    const Object& as_object() const { return std::get<Object>(v_); }

    // First member with the given key, or nullptr. Linear scan: objects from
    // the sources this system parses are small; determinism beats asymptotics.
    const Value* find(std::string_view key) const;

  private:
    std::variant<std::nullptr_t, bool, double, std::string, Array, Object> v_;
};

struct Error {
    std::string message;
    std::size_t offset = 0;  // byte offset into the input
    std::size_t line = 1;    // 1-based
    std::size_t column = 1;  // 1-based, in bytes
};

struct ParseResult {
    std::optional<Value> value;
    std::optional<Error> error;
    explicit operator bool() const { return value.has_value(); }
    // Convenience for the success path; caller must have checked success.
    const Value& operator*() const { return *value; }
    const Value* operator->() const { return &*value; }
};

// Parse a complete JSON document. Trailing non-whitespace is an error.
ParseResult parse(std::string_view input, const Limits& limits = {});

}  // namespace kst::json
