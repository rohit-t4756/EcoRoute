#include "cost/traffic_snapshot.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>

using namespace ecoroute;
namespace {
int failures = 0;
void check(bool condition, const char* name) {
    if (!condition) { std::cerr << "[FAIL] " << name << '\n'; ++failures; }
}
TrafficMetadata metadata() { return {"graph-v1", "traffic-v1", 100, 200}; }
void write(const std::filesystem::path& file, const std::string& contents) {
    std::ofstream stream(file, std::ios::binary);
    stream << contents;
}
}

int main() {
    const RoadSegmentId big_id = 9007199254740993ULL;
    auto network = RoadNetwork::create(
        {{30, {0, 0.02}, {}}, {10, {0, 0}, {}}, {20, {0, 0.01}, {}}},
        {{big_id, 10, 20, 100, 36, {}, {}}, {7, 20, 30, 100, 36, {}, {}},
         {9, 10, 20, 120, {}, {}, {}}});
    if (!network.ok()) return 1;
    auto graph_result = RoutingGraph::create(network.value());
    if (!graph_result.ok()) return 1;
    const auto& graph = graph_result.value();
    std::vector<EdgeTrafficFactor> factors{{7, 3}, {big_id, 2}, {9, 4}};
    auto traffic = TrafficSnapshot::create(graph, "graph-v1", metadata(), factors);
    check(traffic.ok(), "Complete snapshot accepted");
    if (!traffic.ok()) return 1;
    for (std::size_t arc = 0; arc < graph.arcCount(); ++arc) {
        const auto id = graph.segmentIds()[arc];
        check(traffic.value().multipliers()[arc] == (id == 7 ? 3 : id == 9 ? 4 : 2),
              "External directed IDs align to CSR, including parallel and 64-bit IDs");
    }
    check(traffic.value().isAlignedWith(graph, "graph-v1"), "Alignment accepted");
    check(!traffic.value().isAlignedWith(graph, "other"), "Wrong graph version rejected");
    check(!traffic.value().validateTime(99).ok(), "Future snapshot rejected");
    check(traffic.value().validateTime(100).ok(), "Inclusive validity start");
    check(traffic.value().validateTime(199).ok(), "Last valid second");
    check(!traffic.value().validateTime(200).ok(), "Exclusive validity end");
    check(!traffic.value().validateTime(-1).ok(), "Negative departure rejected");
    auto bad_metadata = metadata();
    bad_metadata.graph_version = "other";
    auto mismatch = TrafficSnapshot::create(graph, "graph-v1", bad_metadata, factors);
    check(!mismatch.ok() && mismatch.error().code == ErrorCode::VersionMismatch, "Version mismatch explicit");
    bad_metadata = metadata(); bad_metadata.valid_until_unix_seconds = 100;
    check(!TrafficSnapshot::create(graph, "graph-v1", bad_metadata, factors).ok(), "Empty interval rejected");
    bad_metadata = metadata(); bad_metadata.traffic_version = std::string("bad\0version", 11);
    check(!TrafficSnapshot::create(graph, "graph-v1", bad_metadata, factors).ok(), "Control byte in version rejected");
    auto missing = factors; missing.pop_back();
    check(!TrafficSnapshot::create(graph, "graph-v1", metadata(), missing).ok(), "Missing rejects by default");
    auto fallback = TrafficSnapshot::create(graph, "graph-v1", metadata(), missing,
                                            {MissingTrafficPolicy::FreeFlow, 100});
    check(fallback.ok() && fallback.value().missingFactorCount() == 1, "Explicit missing fallback recorded");
    if (fallback.ok()) {
        for (std::size_t arc = 0; arc < graph.arcCount(); ++arc)
            if (graph.segmentIds()[arc] == 9)
                check(fallback.value().multipliers()[arc] == 1, "Missing factor uses free-flow exactly");
    }
    auto duplicate = factors; duplicate[2] = duplicate[0];
    check(!TrafficSnapshot::create(graph, "graph-v1", metadata(), duplicate).ok(), "Duplicate ID rejected");
    auto unknown = factors; unknown[2].segment_id = 999;
    check(!TrafficSnapshot::create(graph, "graph-v1", metadata(), unknown).ok(), "Unknown ID rejected");
    for (double multiplier : {0.99, 101.0, std::numeric_limits<double>::infinity(),
                              std::numeric_limits<double>::quiet_NaN()}) {
        auto invalid = factors; invalid[0].travel_time_multiplier = multiplier;
        check(!TrafficSnapshot::create(graph, "graph-v1", metadata(), invalid,
              {MissingTrafficPolicy::FreeFlow, 100}).ok(), "Invalid factors never silently clamped or replaced");
    }

    const auto directory = std::filesystem::temp_directory_path() /
        ("ecoroute-traffic-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    const TrafficSnapshotFiles files{directory / "metadata.txt", directory / "factors.csv"};
    const std::string valid_metadata = "schema_version=1\ngraph_version=graph-v1\ntraffic_version=traffic-v1\n"
        "valid_from_unix_seconds=100\nvalid_until_unix_seconds=200\n";
    write(files.metadata_file, "\xEF\xBB\xBF" + valid_metadata);
    write(files.factors_file, "edge_id,travel_time_multiplier\n7,3\n9007199254740993,2\n9,4\n");
    check(loadTrafficSnapshot(files, graph, "graph-v1").ok(), "Actual files and UTF-8 BOM load");
    write(files.metadata_file, valid_metadata + "traffic_version=duplicate\n");
    check(!loadTrafficSnapshot(files, graph, "graph-v1").ok(), "Duplicate metadata key rejected");
    write(files.metadata_file, valid_metadata);
    for (const auto& row : {"-1,2", "7,nan", "7,2junk", "7,2,extra", "7,\"2\""}) {
        write(files.factors_file, std::string("edge_id,travel_time_multiplier\n") + row + "\n");
        check(!loadTrafficSnapshot(files, graph, "graph-v1").ok(), "Malformed CSV rejected");
    }
    write(files.factors_file, "wrong,header\n");
    check(!loadTrafficSnapshot(files, graph, "graph-v1").ok(), "Wrong header rejected");
    std::filesystem::remove(files.factors_file);
    const auto missing_file = loadTrafficSnapshot(files, graph, "graph-v1");
    check(!missing_file.ok() && missing_file.error().code == ErrorCode::FileError, "Missing file distinguished");
    std::filesystem::remove(files.metadata_file);
    std::filesystem::remove(directory);
    std::cout << "TrafficSnapshotTests failures: " << failures << '\n';
    return failures == 0 ? 0 : 1;
}