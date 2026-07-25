#include <cctype>
#include <string_view>

#include "kst/version.hpp"
#include "kst_test.hpp"

KST_TEST(version_is_semver_like) {
    // MAJOR.MINOR.PATCH — digits and exactly two dots (REQ-10.7).
    std::string_view v = kst::kVersion;
    KST_CHECK(!v.empty());
    int dots = 0;
    bool ok = true;
    for (char c : v) {
        if (c == '.') {
            ++dots;
        } else if (!std::isdigit(static_cast<unsigned char>(c))) {
            ok = false;
        }
    }
    KST_CHECK(ok);
    KST_CHECK(dots == 2);
}

KST_TEST(version_string_carries_project_and_version) {
    std::string_view s = kst::version_string();
    KST_CHECK(s.find("KineticStrikeTracker") != std::string_view::npos);
    KST_CHECK(s.find(kst::kVersion) != std::string_view::npos);
}

int main() { return kst::test::run(); }
