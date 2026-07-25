// Minimal stdlib-only test harness (REQ-9.4: no third-party test framework
// without a decision memorandum; REQ-10.2: suite runs via a single command —
// `ctest --test-dir build`).
//
// Usage: define tests with KST_TEST(name) { ...KST_CHECK(cond)... } and finish
// main() with `return kst::test::run();`. Each translation unit is one test
// executable registered with CTest.
#pragma once

#include <functional>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace kst::test {

struct Case {
    std::string name;
    std::function<void()> fn;
};

inline std::vector<Case>& registry() {
    static std::vector<Case> r;
    return r;
}

inline int& failures() {
    static int f = 0;
    return f;
}

struct Register {
    Register(std::string name, std::function<void()> fn) {
        registry().push_back({std::move(name), std::move(fn)});
    }
};

inline int run() {
    for (const auto& c : registry()) {
        const int before = failures();
        c.fn();
        std::cout << (failures() == before ? "PASS" : "FAIL") << "  " << c.name
                  << "\n";
    }
    return failures() == 0 ? 0 : 1;
}

}  // namespace kst::test

#define KST_TEST(name)                                                      \
    static void kst_test_##name();                                          \
    static const ::kst::test::Register kst_reg_##name{#name,                \
                                                      kst_test_##name};     \
    static void kst_test_##name()

#define KST_CHECK(cond)                                                     \
    do {                                                                    \
        if (!(cond)) {                                                      \
            ++::kst::test::failures();                                      \
            std::cerr << __FILE__ << ":" << __LINE__ << ": CHECK failed: "  \
                      << #cond << "\n";                                     \
        }                                                                   \
    } while (0)
