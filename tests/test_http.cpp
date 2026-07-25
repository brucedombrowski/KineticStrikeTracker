// kst::http tests. These run WITHOUT network access (REQ-10.4): they cover
// the digest, the scheme guard, and limit plumbing. Live retrieval is
// exercised separately by adapter tests against committed fixtures.
#include <string>

#include "kst/http.hpp"
#include "kst_test.hpp"

using kst::http::Limits;
using kst::http::sha256_hex;

KST_TEST(sha256_known_vectors) {
    // NIST/RFC 6234 test vectors.
    KST_CHECK(sha256_hex("") ==
              "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    KST_CHECK(sha256_hex("abc") ==
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

KST_TEST(sha256_matches_committed_evidence_digest) {
    // The digest recorded when requirements/evidence/ was captured. If this
    // fails, either the digest routine changed or the evidence file did —
    // both are things we want to hear about (REQ-12.10).
    std::string path = std::string(KST_SOURCE_DIR) +
                       "/requirements/evidence/usgs-iran-jun2025.json";
    FILE* f = std::fopen(path.c_str(), "rb");
    KST_CHECK(f != nullptr);
    if (!f) return;
    std::string data;
    char buf[4096];
    std::size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) data.append(buf, n);
    std::fclose(f);
    KST_CHECK(sha256_hex(data) ==
              "3aaef6ef5fbb348697993fa94fe848527fe3b4f71c629602f0dba6e9c1406e90");
}

KST_TEST(non_https_refused_without_touching_network) {
    for (const char* url : {"http://example.com/", "ftp://example.com/",
                            "file:///etc/passwd", "example.com"}) {
        auto r = kst::http::get(url);
        KST_CHECK(!r);
        KST_CHECK(r.error->message.find("non-HTTPS") != std::string::npos);
    }
}

KST_TEST(limits_have_safe_defaults) {
    Limits d;
    KST_CHECK(d.max_response_bytes > 0);
    KST_CHECK(d.max_redirects > 0 && d.max_redirects <= 10);
    KST_CHECK(d.connect_timeout_seconds > 0);
    KST_CHECK(d.total_timeout_seconds >= d.connect_timeout_seconds);
}

int main() { return kst::test::run(); }
