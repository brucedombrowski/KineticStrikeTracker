// kst — command-line interface for KineticStrikeTracker.
//
// REQ-8.1: the CLI provides ingest, analyse, replay, query, and report.
// All verbs are stubs until their Phase 2 backlog issues land; a stub exits
// with status 2 and says so rather than pretending to work.

#include <cstring>
#include <iostream>
#include <string_view>

#include "kst/version.hpp"

namespace {

constexpr std::string_view kUsage =
    "usage: kst <command> [options]\n"
    "\n"
    "commands (REQ-8.1):\n"
    "  ingest    fetch and land observations from configured sources\n"
    "  analyse   correlate, discriminate, and assess stored observations\n"
    "  replay    re-run an analysis offline from stored raw responses\n"
    "  query     inspect stored observations and events\n"
    "  report    emit GeoJSON and human-readable reports\n"
    "\n"
    "options:\n"
    "  --version show version and exit\n"
    "  --help    show this text and exit\n";

constexpr std::string_view kVerbs[] = {"ingest", "analyse", "replay", "query",
                                       "report"};

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2 || std::strcmp(argv[1], "--help") == 0) {
        std::cout << kUsage;
        return argc < 2 ? 2 : 0;
    }
    if (std::strcmp(argv[1], "--version") == 0) {
        std::cout << kst::version_string() << "\n";
        return 0;
    }
    for (std::string_view verb : kVerbs) {
        if (verb == argv[1]) {
            std::cerr << "kst: '" << verb
                      << "' is not implemented yet (see the Phase 2 backlog)\n";
            return 2;
        }
    }
    std::cerr << "kst: unknown command '" << argv[1] << "'\n\n" << kUsage;
    return 2;
}
