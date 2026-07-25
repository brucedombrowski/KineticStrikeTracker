// kst::json tests — REQ-12.3 (bounds), REQ-12.9 (adversarial inputs),
// REQ-1.2 (deterministic acceptance and ordering).
#include <fstream>
#include <sstream>
#include <string>

#include "kst/json.hpp"
#include "kst_test.hpp"

using kst::json::Limits;
using kst::json::parse;
using kst::json::Value;

namespace {
bool ok(std::string_view s) { return static_cast<bool>(parse(s)); }
bool rejected(std::string_view s) { return !parse(s); }
}  // namespace

// --- Valid documents ---

KST_TEST(scalars) {
    KST_CHECK(parse("null")->is_null());
    KST_CHECK(parse("true")->as_bool() == true);
    KST_CHECK(parse("false")->as_bool() == false);
    KST_CHECK(parse("0")->as_number() == 0.0);
    KST_CHECK(parse("-1.5")->as_number() == -1.5);
    KST_CHECK(parse("1e3")->as_number() == 1000.0);
    KST_CHECK(parse("  \"hi\"  ")->as_string() == "hi");
}

KST_TEST(escapes_and_unicode) {
    KST_CHECK(parse(R"("a\"b\\c\/d\n")")->as_string() == "a\"b\\c/d\n");
    KST_CHECK(parse(R"("A")")->as_string() == "A");
    // Surrogate pair: U+1F600
    KST_CHECK(parse(R"("😀")")->as_string() == "\xF0\x9F\x98\x80");
    // Raw multi-byte UTF-8 passes through intact
    KST_CHECK(parse("\"\xE2\x82\xAC\"")->as_string() == "\xE2\x82\xAC");
}

KST_TEST(object_preserves_document_order) {
    auto r = parse(R"({"z":1,"a":2,"m":3})");
    KST_CHECK(static_cast<bool>(r));
    const auto& obj = r.value->as_object();
    KST_CHECK(obj.size() == 3);
    KST_CHECK(obj[0].first == "z" && obj[1].first == "a" &&
              obj[2].first == "m");
    const Value* a = r.value->find("a");
    KST_CHECK(a != nullptr && a->as_number() == 2.0);
    KST_CHECK(r.value->find("missing") == nullptr);
}

KST_TEST(nesting_within_limit) {
    std::string s;
    for (int i = 0; i < 64; ++i) s += "[";
    s += "1";
    for (int i = 0; i < 64; ++i) s += "]";
    KST_CHECK(ok(s));  // exactly at default max_depth
}

// --- Adversarial documents (REQ-12.9) ---

KST_TEST(truncated_inputs) {
    KST_CHECK(rejected(""));
    KST_CHECK(rejected("{"));
    KST_CHECK(rejected(R"({"a":)"));
    KST_CHECK(rejected("[1,"));
    KST_CHECK(rejected("\"unterminated"));
    KST_CHECK(rejected("tru"));
    KST_CHECK(rejected("-"));
    KST_CHECK(rejected(R"("\u00)"));
}

KST_TEST(trailing_content) {
    KST_CHECK(rejected("{} x"));
    KST_CHECK(rejected("1 2"));
    KST_CHECK(rejected("null,"));
}

KST_TEST(depth_bomb_is_clean_error_not_crash) {
    std::string s(200'000, '[');
    auto r = parse(s);
    KST_CHECK(!r);
    KST_CHECK(r.error->message.find("max_depth") != std::string::npos);
}

KST_TEST(size_limits_enforced) {
    Limits tiny;
    tiny.max_input_bytes = 8;
    KST_CHECK(!parse("[1,2,3,4,5]", tiny));

    Limits short_strings;
    short_strings.max_string_bytes = 4;
    KST_CHECK(!parse(R"("aaaaaaaaaa")", short_strings));

    Limits few_members;
    few_members.max_container_members = 2;
    KST_CHECK(!parse("[1,2,3]", few_members));
    KST_CHECK(!parse(R"({"a":1,"b":2,"c":3})", few_members));
}

KST_TEST(duplicate_keys_rejected) {
    auto r = parse(R"({"a":1,"a":2})");
    KST_CHECK(!r);
    KST_CHECK(r.error->message.find("duplicate") != std::string::npos);
}

KST_TEST(bad_numbers) {
    KST_CHECK(rejected("01"));       // leading zero
    KST_CHECK(rejected("+1"));       // leading plus
    KST_CHECK(rejected("1."));       // digit required after point
    KST_CHECK(rejected(".5"));       // digit required before point
    KST_CHECK(rejected("1e"));       // digit required in exponent
    KST_CHECK(rejected("0x10"));     // no hex
    KST_CHECK(rejected("NaN"));
    KST_CHECK(rejected("Infinity"));
    KST_CHECK(rejected("1e999"));    // out of range is an error, not inf
}

KST_TEST(bad_strings) {
    KST_CHECK(rejected("\"a\x01ize\""));      // raw control char
    KST_CHECK(rejected("\"\xFF\""));          // invalid UTF-8 lead byte
    KST_CHECK(rejected("\"\xC3(\""));         // invalid continuation
    KST_CHECK(rejected("\"\xC0\xAF\""));      // overlong encoding
    KST_CHECK(rejected("\"\xED\xA0\x80\""));  // raw surrogate half
    KST_CHECK(rejected(R"("\uD800")"));       // unpaired high surrogate
    KST_CHECK(rejected(R"("\uDC00")"));       // unpaired low surrogate
    KST_CHECK(rejected(R"("\uD800A")")); // wrong low surrogate
    KST_CHECK(rejected(R"("\q")"));           // invalid escape
}

KST_TEST(error_reports_position) {
    auto r = parse("{\n  \"a\": tru\n}");
    KST_CHECK(!r);
    KST_CHECK(r.error->line == 2);
}

// --- Real fixture: committed USGS FDSN response (REQ-10.4) ---

KST_TEST(parses_committed_usgs_evidence) {
    std::ifstream f(std::string(KST_SOURCE_DIR) +
                    "/requirements/evidence/usgs-iran-jun2025.json");
    KST_CHECK(f.good());
    std::stringstream ss;
    ss << f.rdbuf();
    auto r = parse(ss.str());
    KST_CHECK(static_cast<bool>(r));
    if (!r) return;
    const Value* type = r.value->find("type");
    KST_CHECK(type != nullptr && type->as_string() == "FeatureCollection");
    const Value* features = r.value->find("features");
    KST_CHECK(features != nullptr && features->as_array().size() == 2);
    const Value* mag =
        features->as_array()[0].find("properties")->find("mag");
    KST_CHECK(mag != nullptr && mag->as_number() == 4.4);
}

int main() { return kst::test::run(); }
