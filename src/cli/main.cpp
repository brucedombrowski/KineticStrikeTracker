// kst — command-line interface for KineticStrikeTracker (REQ-8.1).
//
// Thin by design: parses arguments, builds adapters, calls kst_core. No
// analysis logic lives here (REQ-9.2).

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "kst/analysis.hpp"
#include "kst/db.hpp"
#include "kst/pipeline.hpp"
#include "kst/source.hpp"
#include "kst/version.hpp"

namespace {

constexpr const char* kUsage =
    "usage: kst <command> [options]\n"
    "\n"
    "commands:\n"
    "  ingest    retrieve from configured sources and store\n"
    "  analyse   correlate and assess stored observations\n"
    "  replay    re-run analysis from stored data, no network\n"
    "  query     list stored observations\n"
    "  report    write GeoJSON and text report\n"
    "\n"
    "options:\n"
    "  --db PATH          database file (default: data/kst.db)\n"
    "  --start TIME       ISO 8601 UTC start\n"
    "  --end TIME         ISO 8601 UTC end\n"
    "  --bbox S,N,W,E     region of interest, anywhere on Earth\n"
    "  --file PATH        additional local file source (repeatable)\n"
    "  --seed PATH        curated seed dataset file (repeatable)\n"
    "  --out DIR          output directory for report/geojson (default: out)\n"
    "  --firms            include NASA FIRMS thermal detections\n"
    "                     (needs FIRMS_MAP_KEY; free key at\n"
    "                      https://firms.modaps.eosdis.nasa.gov/api/map_key/)\n"
    "  --offline          skip network sources\n"
    "  --version, --help\n"
    "\n"
    "Scope is Earth; --bbox takes any region. Defaults reproduce validation\n"
    "cases VC-01/VC-02/VC-04 (June 2025 Iran) because a documented campaign\n"
    "there gives ground truth — they are a demo default, not a boundary.\n";

struct Options {
    std::string db_path = "data/kst.db";
    std::string start = "2025-06-13T00:00:00Z";
    std::string end = "2025-06-26T00:00:00Z";
    double south = 31.5, north = 36.5, west = 49.0, east = 53.5;
    std::vector<std::string> files;
    std::vector<std::string> seeds;
    std::string out_dir = "out";
    bool offline = false;
    bool firms = false;
};

bool parse_bbox(const std::string& s, Options& o) {
    double v[4];
    int n = 0;
    std::size_t pos = 0;
    while (n < 4 && pos <= s.size()) {
        std::size_t comma = s.find(',', pos);
        const std::string part =
            s.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
        try {
            v[n++] = std::stod(part);
        } catch (...) {
            return false;
        }
        if (comma == std::string::npos) break;
        pos = comma + 1;
    }
    if (n != 4) return false;
    o.south = v[0];
    o.north = v[1];
    o.west = v[2];
    o.east = v[3];
    return true;
}

std::vector<std::unique_ptr<kst::source::Adapter>> build_adapters(
    const Options& o) {
    std::vector<std::unique_ptr<kst::source::Adapter>> a;
    if (!o.offline) {
        a.push_back(kst::source::make_isc());   // strike-capable route
        a.push_back(kst::source::make_usgs());  // global reference
        if (o.firms) a.push_back(kst::source::make_firms());
    }
    for (const std::string& f : o.files) {
        a.push_back(kst::source::make_file(
            f, "file", kst::model::SourceClass::Instrumental, false));
    }
    for (const std::string& f : o.seeds) {
        a.push_back(kst::source::make_file(
            f, "seed", kst::model::SourceClass::CuratedDataset, true));
    }
    return a;
}

int cmd_ingest(const Options& o, bool quiet = false) {
    std::optional<kst::db::Error> err;
    auto db = kst::db::Database::open(o.db_path, &err);
    if (!db) {
        std::cerr << "kst: cannot open database: "
                  << (err ? err->message : "unknown") << "\n";
        return 1;
    }
    auto adapters = build_adapters(o);
    if (adapters.empty()) {
        std::cerr << "kst: no sources configured\n";
        return 1;
    }
    kst::source::Query q;
    q.start_time = o.start;
    q.end_time = o.end;
    q.min_latitude = o.south;
    q.max_latitude = o.north;
    q.min_longitude = o.west;
    q.max_longitude = o.east;

    const auto report = kst::pipeline::ingest(*db, adapters, q);
    if (!quiet) {
        for (const auto& s : report.sources) {
            if (s.ok && s.no_data) {
                std::cout << "  " << s.source_id
                          << ": no data in range (queried successfully)\n";
            } else if (s.ok) {
                std::cout << "  " << s.source_id << ": " << s.observations
                          << " observations"
                          << (s.body_was_new ? "" : "  (body deduplicated)")
                          << "\n";
            } else {
                std::cout << "  " << s.source_id << ": FAILED — " << s.error
                          << "\n";
            }
        }
        std::cout << "ingested " << report.total_observations
                  << " observations; coverage "
                  << (report.coverage_complete ? "complete" : "INCOMPLETE")
                  << "\n";
    }
    return 0;
}

int cmd_report(const Options& o, bool write_files) {
    std::optional<kst::db::Error> err;
    auto db = kst::db::Database::open(o.db_path, &err);
    if (!db) {
        std::cerr << "kst: cannot open database: "
                  << (err ? err->message : "unknown") << "\n";
        return 1;
    }
    auto observations = kst::pipeline::load_observations(*db);
    auto events = kst::analysis::correlate(observations);

    Options probe = o;
    probe.offline = true;  // coverage notes need no network
    auto adapters = build_adapters(o);
    auto coverage = kst::pipeline::coverage_notes(adapters);
    std::vector<std::string> attributions;
    for (const auto& a : adapters) {
        attributions.push_back(a->attribution() + " — " + a->data_licence());
    }

    kst::source::Query q;
    q.start_time = o.start;
    q.end_time = o.end;
    q.min_latitude = o.south;
    q.max_latitude = o.north;
    q.min_longitude = o.west;
    q.max_longitude = o.east;

    const std::string text =
        kst::pipeline::to_report(events, coverage, q, attributions);
    std::cout << text;

    if (write_files) {
        std::filesystem::create_directories(o.out_dir);
        std::ofstream(o.out_dir + "/events.geojson")
            << kst::pipeline::to_geojson(events, coverage);
        std::ofstream(o.out_dir + "/report.txt") << text;
        std::cerr << "\nwrote " << o.out_dir << "/events.geojson and "
                  << o.out_dir << "/report.txt\n";
    }
    return 0;
}

int cmd_query(const Options& o) {
    std::optional<kst::db::Error> err;
    auto db = kst::db::Database::open(o.db_path, &err);
    if (!db) {
        std::cerr << "kst: cannot open database\n";
        return 1;
    }
    for (const auto& ob : kst::pipeline::load_observations(*db)) {
        std::cout << ob.origin_time << "  " << ob.source_id << " ["
                  << kst::model::to_string(ob.source_class) << "] "
                  << ob.native_id;
        if (ob.magnitude) std::cout << "  M" << *ob.magnitude;
        if (ob.depth_km) {
            std::cout << "  " << *ob.depth_km << " km"
                      << (ob.depth_is_fixed ? "*" : "");
        }
        std::cout << "  " << ob.description << "\n";
    }
    return 0;
}

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

    Options o;
    const std::string cmd = argv[1];
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string {
            return (i + 1 < argc) ? argv[++i] : std::string();
        };
        if (a == "--db") o.db_path = next();
        else if (a == "--start") o.start = next();
        else if (a == "--end") o.end = next();
        else if (a == "--out") o.out_dir = next();
        else if (a == "--file") o.files.push_back(next());
        else if (a == "--seed") o.seeds.push_back(next());
        else if (a == "--offline") o.offline = true;
        else if (a == "--firms") o.firms = true;
        else if (a == "--bbox") {
            if (!parse_bbox(next(), o)) {
                std::cerr << "kst: --bbox expects S,N,W,E\n";
                return 2;
            }
        } else {
            std::cerr << "kst: unknown option '" << a << "'\n";
            return 2;
        }
    }

    std::filesystem::create_directories(
        std::filesystem::path(o.db_path).parent_path().empty()
            ? "."
            : std::filesystem::path(o.db_path).parent_path());

    if (cmd == "ingest") return cmd_ingest(o);
    if (cmd == "analyse" || cmd == "replay") {
        Options r = o;
        if (cmd == "replay") r.offline = true;  // REQ-2.11
        return cmd_report(r, false);
    }
    if (cmd == "report") return cmd_report(o, true);
    if (cmd == "query") return cmd_query(o);

    std::cerr << "kst: unknown command '" << cmd << "'\n\n" << kUsage;
    return 2;
}
