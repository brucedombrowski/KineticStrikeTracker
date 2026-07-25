// kst::xml tests — REQ-12.3 (bounds), REQ-12.9 (adversarial input),
// and the QuakeML fields that motivated the parser (issue #23).
#include <fstream>
#include <sstream>
#include <string>

#include "kst/xml.hpp"
#include "kst_test.hpp"

using kst::xml::Limits;
using kst::xml::parse;

KST_TEST(basic_structure_and_text) {
    auto r = parse("<a><b>hello</b><c x='1'/></a>");
    KST_CHECK(static_cast<bool>(r));
    if (!r) return;
    KST_CHECK(r->name == "a");
    KST_CHECK(r->children.size() == 2);
    KST_CHECK(r->text_of("b") == "hello");
    const auto* c = r->child("c");
    KST_CHECK(c && c->attribute("x") && *c->attribute("x") == "1");
}

KST_TEST(namespace_prefix_stripped) {
    auto r = parse("<q:root xmlns:q='urn:x'><q:leaf>v</q:leaf></q:root>");
    KST_CHECK(static_cast<bool>(r));
    if (r) {
        KST_CHECK(r->name == "root");
        KST_CHECK(r->text_of("leaf") == "v");
    }
}

KST_TEST(predefined_entities_and_char_refs) {
    auto r = parse("<a>&lt;&amp;&gt;&quot;&apos;&#65;&#x42;</a>");
    KST_CHECK(static_cast<bool>(r));
    if (r) KST_CHECK(r->text == "<&>\"'AB");
}

// The reason this parser refuses DTDs: a custom entity is the
// billion-laughs amplification vector and the XXE disclosure vector.
KST_TEST(doctype_and_custom_entities_refused) {
    auto r = parse("<!DOCTYPE a [<!ENTITY x 'boom'>]><a>&x;</a>");
    KST_CHECK(!r);
    KST_CHECK(r.error->message.find("DOCTYPE") != std::string::npos);

    auto r2 = parse("<a>&undefined;</a>");
    KST_CHECK(!r2);
    KST_CHECK(r2.error->message.find("entity") != std::string::npos);
}

KST_TEST(billion_laughs_cannot_expand) {
    // Even framed as a plain document, undefined entities never expand.
    auto r = parse("<lol>&lol9;&lol9;&lol9;</lol>");
    KST_CHECK(!r);
}

KST_TEST(malformed_documents_rejected) {
    KST_CHECK(!parse(""));
    KST_CHECK(!parse("<a>"));                 // unterminated
    KST_CHECK(!parse("<a></b>"));             // mismatched close
    KST_CHECK(!parse("<a><b></a></b>"));      // improper nesting
    KST_CHECK(!parse("<a x=unquoted/>"));     // unquoted attribute
    KST_CHECK(!parse("<a><!-- unterminated"));
    KST_CHECK(!parse("<a><![CDATA[unterminated"));
}

KST_TEST(depth_bomb_is_clean_error) {
    std::string s;
    for (int i = 0; i < 5000; ++i) s += "<a>";
    auto r = parse(s);
    KST_CHECK(!r);
    KST_CHECK(r.error->message.find("max_depth") != std::string::npos);
}

KST_TEST(limits_enforced) {
    Limits tiny;
    tiny.max_input_bytes = 8;
    KST_CHECK(!parse("<a><b>xxxxxxxxxx</b></a>", tiny));

    Limits shallow;
    shallow.max_depth = 2;
    KST_CHECK(static_cast<bool>(parse("<a><b/></a>", shallow)));
    KST_CHECK(!parse("<a><b><c><d/></c></b></a>", shallow));

    Limits short_text;
    short_text.max_text_bytes = 4;
    KST_CHECK(!parse("<a>aaaaaaaaaa</a>", short_text));
}

KST_TEST(cdata_preserved_verbatim) {
    auto r = parse("<a><![CDATA[<not>markup</not>]]></a>");
    KST_CHECK(static_cast<bool>(r));
    if (r) KST_CHECK(r->text == "<not>markup</not>");
}

// The committed QuakeML evidence, and the three fields the text format
// omits — the whole reason this parser exists (issue #23, DM-2026-007).
KST_TEST(parses_committed_quakeml_evidence) {
    std::ifstream f(std::string(KST_SOURCE_DIR) +
                    "/requirements/evidence/isc-iran-jun2025-quakeml.xml");
    KST_CHECK(f.good());
    std::stringstream ss;
    ss << f.rdbuf();
    auto r = parse(ss.str());
    KST_CHECK(static_cast<bool>(r));
    if (!r) return;

    const auto* params = r->find("eventParameters");
    KST_CHECK(params != nullptr);
    if (!params) return;
    const auto events = params->children_named("event");
    KST_CHECK(events.size() == 15);

    int operator_assigned = 0, suspected = 0, huge_ellipse = 0;
    for (const auto* ev : events) {
        if (ev->text_of("typeCertainty") == "suspected") ++suspected;
        const auto* origin = ev->child("origin");
        if (!origin) continue;
        if (origin->text_of("depthType") == "operator assigned") {
            ++operator_assigned;
        }
        const auto* unc = origin->child("originUncertainty");
        if (unc) {
            const std::string mx = unc->text_of("maxHorizontalUncertainty");
            if (!mx.empty() && std::stod(mx) / 1000.0 > 100.0) ++huge_ellipse;
        }
    }
    // Measured facts from the committed evidence (DM-2026-007).
    KST_CHECK(operator_assigned == 14);
    KST_CHECK(suspected == 14);
    KST_CHECK(huge_ellipse == 13);  // every IDC origin
}

int main() { return kst::test::run(); }
