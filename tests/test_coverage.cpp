// Regression for issue #27: a data gap inside a requested window must not be
// reported as complete coverage.
//
// NASA's VIIRS_SNPP stream returned zero rows for 11-15 July 2026 across the
// whole Gulf while NOAA-20, NOAA-21 and MODIS all had data. The chunk came
// back HTTP 200 with a valid CSV header and no rows — a well-formed "no
// detections" that was indistinguishable, in the output, from a genuinely
// quiet interval. The run printed "coverage complete" over a five-day hole
// that coincided with the largest strike wave in the window.

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "kst/pipeline.hpp"
#include "kst/source.hpp"
#include "kst_test.hpp"

namespace {

kst::source::CoverageInterval iv(const char* start, const char* end,
                                 bool returned_data) {
    kst::source::CoverageInterval c;
    c.start = start;
    c.end = end;
    c.returned_data = returned_data;
    return c;
}

std::string read_fixture(const char* name) {
    std::ifstream in(std::string(KST_SOURCE_DIR) + "/tests/fixtures/" + name,
                     std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

}  // namespace

// The failure that shipped: twelve chunks answer, one in the middle does not.
KST_TEST(gap_mid_window_is_not_complete_coverage) {
    const std::vector<kst::source::CoverageInterval> window = {
        iv("2026-07-01", "2026-07-05", true),
        iv("2026-07-06", "2026-07-10", true),
        iv("2026-07-11", "2026-07-15", false),   // the five-day hole
        iv("2026-07-16", "2026-07-20", true),
        iv("2026-07-21", "2026-07-25", true),
    };
    const auto gaps = kst::pipeline::coverage_gaps(window);
    KST_CHECK(gaps.size() == 1);
    KST_CHECK(gaps.front().start == "2026-07-11");
    KST_CHECK(gaps.front().end == "2026-07-15");
}

// A source that answered for everything it was asked has no gap.
KST_TEST(fully_observed_window_has_no_gap) {
    const std::vector<kst::source::CoverageInterval> window = {
        iv("2026-07-01", "2026-07-05", true),
        iv("2026-07-06", "2026-07-10", true),
    };
    KST_CHECK(kst::pipeline::coverage_gaps(window).empty());
}

// Nothing anywhere is "nothing in range", not a hole. Reporting it as a gap
// would make incomplete coverage the permanent state of every quiet region
// and destroy the signal the flag carries.
KST_TEST(uniformly_empty_window_is_no_data_not_a_gap) {
    const std::vector<kst::source::CoverageInterval> window = {
        iv("2026-07-01", "2026-07-05", false),
        iv("2026-07-06", "2026-07-10", false),
    };
    KST_CHECK(kst::pipeline::coverage_gaps(window).empty());
}

// Several separate holes are all reported, in canonical order.
KST_TEST(multiple_gaps_are_all_reported_in_order) {
    const std::vector<kst::source::CoverageInterval> window = {
        iv("2026-07-01", "2026-07-05", false),
        iv("2026-07-06", "2026-07-10", true),
        iv("2026-07-11", "2026-07-15", false),
    };
    const auto gaps = kst::pipeline::coverage_gaps(window);
    KST_CHECK(gaps.size() == 2);
    KST_CHECK(gaps[0].start == "2026-07-01");
    KST_CHECK(gaps[1].start == "2026-07-11");
}

// An adapter with no sub-intervals to report is unchanged by the rule.
KST_TEST(single_request_adapter_reports_no_gaps) {
    KST_CHECK(kst::pipeline::coverage_gaps({}).empty());
}

// The body that started it: HTTP 200, valid CSV, header and nothing else.
// It must parse cleanly to zero observations — the adapter was never wrong
// about this, which is why the gap needed recording elsewhere.
KST_TEST(header_only_csv_parses_to_zero_observations_without_error) {
    const std::string body = read_fixture("firms-header-only.csv");
    KST_CHECK(!body.empty());
    auto firms = kst::source::make_firms({"VIIRS_SNPP"});
    std::string error;
    const auto obs = firms->parse(body, &error);
    KST_CHECK(obs.empty());
    KST_CHECK(error.empty());
}

int main() { return kst::test::run(); }
