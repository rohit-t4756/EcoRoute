
/*
 * File: test_graph_loader.cpp
 * Purpose: Tests loading versioned graph snapshots from disk,
 *          input validation, directed connectivity and CCH routing.
 *
 * Status: VERIFIED
 * Remaining: Compile, run and verify tests.
 */

#include "graph/graph_loader.hpp"
#include "search/cch_order.hpp"
#include "search/cch_adapter.hpp"
#include "search/dijkstra.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <locale>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

using namespace ecoroute;

namespace {

namespace fs = std::filesystem;

int failures = 0;
int checks = 0;

void check(bool condition, const std::string& name) {
    ++checks;

    if (condition) {
        std::cout << "[PASS] " << name << '\n';
    } else {
        ++failures;
        std::cerr << "[FAIL] " << name << '\n';
    }
}

bool writeText(
    const fs::path& path,
    const std::string& content
) {
    std::ofstream output(path, std::ios::binary);

    if (!output) {
        return false;
    }

    output << content;
    output.close();

    return output.good();
}

// A temporary directory is created for this test.
// It is automatically deleted when the test finishes.
struct SnapshotFixture {
    fs::path directory;
    GraphSnapshotFiles files;

    SnapshotFixture()
        : directory(
            fs::temp_directory_path() /
            ("ecoroute_loader_test_" +
             std::to_string(
                 std::chrono::steady_clock::now()
                     .time_since_epoch().count()
             ))
        ),
          files{
              directory / "metadata.txt",
              directory / "nodes.csv",
              directory / "edges.csv"
          }
    {
        fs::create_directories(directory);
    }

    ~SnapshotFixture() {
        std::error_code error;
        fs::remove_all(directory, error);
    }

    bool write(
        const std::string& metadata,
        const std::string& nodes,
        const std::string& edges
    ) {
        return writeText(files.metadata_file, metadata) &&
               writeText(files.nodes_file, nodes) &&
               writeText(files.edges_file, edges);
    }
};

std::string replaceOnce(
    std::string text,
    const std::string& old_value,
    const std::string& new_value
) {
    auto position = text.find(old_value);

    if (position != std::string::npos) {
        text.replace(
            position,
            old_value.size(),
            new_value
        );
    }

    return text;
}

bool hasError(
    const Result<LoadedGraphSnapshot>& result,
    ErrorCode expected
) {
    return !result.ok() &&
           result.error().code == expected;
}

std::vector<RoadSegmentId> pathSegmentIds(
    const ShortestPath& path,
    const RoutingGraph& graph
) {
    std::vector<RoadSegmentId> ids;

    for (unsigned arc : path.arc_indices) {
        if (arc >= graph.arcCount()) {
            return {};
        }

        ids.push_back(graph.segmentIds()[arc]);
    }

    return ids;
}

// Deliberately independent bitwise CRC reference, not the loader's zlib call.
std::string crcHex(const std::string& bytes) {
    std::uint32_t checksum = 0xffffffffU;
    for (unsigned char byte : bytes) {
        checksum ^= byte;
        for (unsigned bit = 0; bit < 8; ++bit) {
            checksum = (checksum >> 1) ^ ((checksum & 1) ? 0xedb88320U : 0U);
        }
    }
    std::ostringstream output;
    output << std::hex << std::setfill('0') << std::setw(8) << (checksum ^ 0xffffffffU);
    return output.str();
}

std::string manifest(const std::string& nodes, const std::string& edges) {
    return "schema_version=2\ngraph_version=geometry-test\nnode_count=2\nedge_count=1\n"
           "nodes_crc32=" + crcHex(nodes) + "\nedges_crc32=" + crcHex(edges) + "\n";
}

struct CommaDecimal : std::numpunct<char> {
    char do_decimal_point() const override { return ','; }
};

} // namespace

int main() {

    // --------------------------------------------------
    // 1. Create a small exported road network.
    // --------------------------------------------------

    // These IDs are intentionally greater than 2^32.
    // This verifies that external 64-bit IDs are retained.

    const std::string metadata =
        "schema_version=1\n"
        "graph_version=nyc-test-001\n";

    const std::string nodes =
        "node_id,latitude,longitude,elevation_meters\n"
        "5000000001,40.700,-73.900,10.5\n"
        "5000000002,40.701,-73.899,\n"
        "5000000003,40.702,-73.898,15.0\n"
        "5000000004,40.703,-73.897,20.0\n";

    const std::string edges =
        "edge_id,from_node_id,to_node_id,"
        "length_meters,speed_limit_kph,road_class\n"

        "7000000001,5000000001,5000000002,"
        "100.0,40.0,residential\n"

        "7000000002,5000000002,5000000003,"
        "150.0,50.0,primary\n"

        "7000000003,5000000001,5000000003,"
        "400.0,50.0,\"collector,urban\"\n"

        "7000000004,5000000003,5000000004,"
        "120.0,,\n"

        // Parallel edge between the same two nodes.
        "7000000005,5000000001,5000000002,"
        "105.0,30.0,\"residential \"\"local\"\"\"\n"

        // One-way edge completing a directed cycle.
        "7000000006,5000000004,5000000002,"
        "180.0,40.0,secondary\n";

    SnapshotFixture fixture;

    if (!fixture.write(metadata, nodes, edges)) {
        std::cerr << "[FAIL] Cannot create temporary files.\n";
        return 1;
    }

    // --------------------------------------------------
    // 2. Load the snapshot from disk.
    // --------------------------------------------------

    auto loaded = loadGraphSnapshot(fixture.files);

    if (!loaded.ok()) {
        std::cerr << "[FAIL] Loading valid snapshot: "
                  << loaded.error().message << '\n';
        return 1;
    }

    const LoadedGraphSnapshot& snapshot = loaded.value();

    const RoadNetwork& network = snapshot.network;
    const RoutingGraph& graph = snapshot.routing_graph;

    check(
        snapshot.graph_version == "nyc-test-001",
        "Graph version preserved"
    );

    check(
        network.nodeCount() == 4 &&
        network.segmentCount() == 6,
        "Physical graph dimensions are correct"
    );

    check(
        graph.nodeCount() == 4 &&
        graph.arcCount() == 6,
        "CSR graph dimensions are correct"
    );

    // --------------------------------------------------
    // 3. Verify IDs and optional attributes.
    // --------------------------------------------------

    const RoadNode* node1 =
        network.findNode(5000000001ULL);

    const RoadNode* node2 =
        network.findNode(5000000002ULL);

    check(
        node1 != nullptr &&
        node1->elevation_meters.has_value() &&
        *node1->elevation_meters == 10.5,
        "64-bit node ID and elevation preserved"
    );

    check(
        node2 != nullptr &&
        !node2->elevation_meters.has_value(),
        "Missing elevation remains missing"
    );

    const RoadSegment* unknown_speed =
        network.findSegment(7000000004ULL);

    check(
        unknown_speed != nullptr &&
        !unknown_speed->speed_limit_kph.has_value() &&
        !unknown_speed->road_class.has_value(),
        "Missing speed and road class remain optional"
    );

    const RoadSegment* quoted_class =
        network.findSegment(7000000003ULL);

    check(
        quoted_class != nullptr &&
        quoted_class->road_class.has_value() &&
        *quoted_class->road_class == "collector,urban",
        "Quoted CSV field containing a comma parsed"
    );

    const RoadSegment* escaped_quotes =
        network.findSegment(7000000005ULL);

    check(
        escaped_quotes != nullptr &&
        escaped_quotes->road_class.has_value() &&
        *escaped_quotes->road_class ==
            "residential \"local\"",
        "Escaped CSV quotes parsed"
    );

    check(
        graph.findNodeIndex(5000000001ULL).has_value() &&
        network.findSegment(7000000001ULL) != nullptr,
        "External 64-bit IDs can be looked up"
    );

    // --------------------------------------------------
    // 4. Run our real CCH engine on the loaded graph.
    // --------------------------------------------------

    auto source = graph.findNodeIndex(5000000001ULL);
    auto target = graph.findNodeIndex(5000000004ULL);
    auto middle = graph.findNodeIndex(5000000002ULL);

    if (!source || !target || !middle) {
        std::cerr << "[FAIL] Compact node mapping.\n";
        return 1;
    }

    // Weights correspond to original segment IDs,
    // but must be stored in exact CSR arc order.
    std::vector<unsigned> weights;

    for (RoadSegmentId id : graph.segmentIds()) {
        switch (id) {
            case 7000000001ULL: weights.push_back(5); break;
            case 7000000002ULL: weights.push_back(6); break;
            case 7000000003ULL: weights.push_back(50); break;
            case 7000000004ULL: weights.push_back(7); break;
            case 7000000005ULL: weights.push_back(20); break;
            case 7000000006ULL: weights.push_back(25); break;

            default:
                std::cerr << "[FAIL] Unexpected edge ID.\n";
                return 1;
        }
    }

    auto order = computeCchNodeOrder(network, graph);

    if (!order.ok()) {
        std::cerr << "[FAIL] Nested dissection: "
                  << order.error().message << '\n';
        return 1;
    }

    auto cch_result = CchAdapter::create(
        graph,
        std::move(order.value()),
        weights
    );

    if (!cch_result.ok()) {
        std::cerr << "[FAIL] CCH construction: "
                  << cch_result.error().message << '\n';
        return 1;
    }

    CchAdapter cch = std::move(cch_result.value());

    auto cch_path = cch.shortestPath(*source, *target);

    auto reference = dijkstraShortestPath(
        graph, weights, *source, *target
    );

    const std::vector<RoadSegmentId> expected = {
        7000000001ULL,
        7000000002ULL,
        7000000004ULL
    };

    check(
        cch_path.ok() &&
        reference.ok() &&
        cch_path.value().total_cost == 18 &&
        reference.value().total_cost == 18 &&
        pathSegmentIds(cch_path.value(), graph) == expected &&
        pathSegmentIds(reference.value(), graph) == expected,
        "CCH and Dijkstra find expected loaded-graph route"
    );

    // No edge leads back to the starting node.
    auto reverse = cch.shortestPath(*middle, *source);

    check(
        !reverse.ok() &&
        reverse.error().code == ErrorCode::NoRoute,
        "One-way connectivity is preserved"
    );

    // --------------------------------------------------
    // 5. Test malformed and inconsistent snapshots.
    // --------------------------------------------------

    auto expectRejected = [&](
        const std::string& name,
        const std::string& meta,
        const std::string& node_data,
        const std::string& edge_data,
        ErrorCode expected_error
    ) {
        if (!fixture.write(meta, node_data, edge_data)) {
            check(false, name + " (file write)");
            return;
        }

        auto result = loadGraphSnapshot(fixture.files);

        check(
            hasError(result, expected_error),
            name
        );
    };

    expectRejected(
        "Unsupported schema version rejected",
        replaceOnce(
            metadata, "schema_version=1", "schema_version=3"
        ),
        nodes, edges,
        ErrorCode::InvalidGraph
    );

    expectRejected(
        "Missing graph version rejected",
        "schema_version=1\n",
        nodes, edges,
        ErrorCode::InvalidGraph
    );

    expectRejected(
        "Duplicate node ID rejected",
        metadata,
        nodes +
            "5000000002,40.710,-73.910,11.0\n",
        edges,
        ErrorCode::InvalidGraph
    );

    expectRejected(
        "Duplicate edge ID rejected",
        metadata, nodes,
        edges +
            "7000000001,5000000002,5000000004,"
            "100.0,40.0,primary\n",
        ErrorCode::InvalidGraph
    );

    expectRejected(
        "Missing edge endpoint rejected",
        metadata, nodes,
        replaceOnce(
            edges,
            "5000000003,5000000004",
            "5000000003,9999999999"
        ),
        ErrorCode::InvalidGraph
    );

    expectRejected(
        "Out-of-range coordinate rejected",
        metadata,
        replaceOnce(
            nodes, "40.700,-73.900", "95.000,-73.900"
        ),
        edges,
        ErrorCode::InvalidGraph
    );

    expectRejected(
        "Negative road length rejected",
        metadata, nodes,
        replaceOnce(
            edges, ",400.0,50.0", ",-400.0,50.0"
        ),
        ErrorCode::InvalidGraph
    );

    expectRejected(
        "Zero speed rejected",
        metadata, nodes,
        replaceOnce(
            edges, ",150.0,50.0", ",150.0,0.0"
        ),
        ErrorCode::InvalidGraph
    );

    expectRejected(
        "Nonfinite elevation rejected",
        metadata,
        replaceOnce(
            nodes, "40.703,-73.897,20.0",
            "40.703,-73.897,nan"
        ),
        edges,
        ErrorCode::InvalidGraph
    );

    expectRejected(
        "Incorrect CSV header rejected",
        metadata,
        replaceOnce(
            nodes, "node_id,latitude",
            "id,latitude"
        ),
        edges,
        ErrorCode::InvalidGraph
    );

    expectRejected(
        "Unclosed quoted CSV field rejected",
        metadata, nodes,
        replaceOnce(
            edges, "\"collector,urban\"",
            "\"collector,urban"
        ),
        ErrorCode::InvalidGraph
    );

    // --------------------------------------------------
    // 6. Missing files and UTF-8 BOM handling.
    // --------------------------------------------------

    if (!fixture.write(metadata, nodes, edges)) {
        std::cerr << "[FAIL] Resetting snapshot files.\n";
        return 1;
    }

    std::error_code remove_error;
    fs::remove(fixture.files.edges_file, remove_error);

    auto missing_file = loadGraphSnapshot(fixture.files);

    check(
        !remove_error &&
        hasError(missing_file, ErrorCode::InvalidArgument),
        "Missing edges file rejected"
    );

    std::string nodes_with_bom =
        std::string("\xEF\xBB\xBF") + nodes;

    if (!fixture.write(metadata, nodes_with_bom, edges)) {
        std::cerr << "[FAIL] Writing BOM test files.\n";
        return 1;
    }

    auto bom_result = loadGraphSnapshot(fixture.files);

    check(
        bom_result.ok() &&
        bom_result.value().network.nodeCount() == 4,
        "UTF-8 BOM in CSV header supported"
    );

    check(fixture.write(std::string("\xEF\xBB\xBF") + metadata, nodes, edges) &&
          loadGraphSnapshot(fixture.files).ok(), "UTF-8 BOM in metadata supported");

    for (const auto& bad_id : {"-1", "+1", "18446744073709551616", "1x", "1.0"}) {
        expectRejected(std::string("Invalid integer ID rejected: ") + bad_id,
            metadata, replaceOnce(nodes, "5000000001,", std::string(bad_id) + ","),
            edges, ErrorCode::InvalidGraph);
    }
    for (const auto& bad_number : {"nan", "inf", "1e999", "1e-999", "0x1p2", "40.7x"}) {
        expectRejected(std::string("Invalid floating number rejected: ") + bad_number,
            metadata, replaceOnce(nodes, "40.700", bad_number), edges,
            ErrorCode::InvalidGraph);
    }
    expectRejected("Text after closing quote rejected", metadata, nodes,
        replaceOnce(edges, "\"collector,urban\"", "\"collector,urban\"x"),
        ErrorCode::InvalidGraph);
    expectRejected("Duplicate metadata key rejected", metadata + "graph_version=other\n",
        nodes, edges, ErrorCode::InvalidGraph);

    // CSV decimal parsing is locale independent.
    const std::locale original_locale;
    std::locale::global(std::locale(original_locale, new CommaDecimal));
    const bool locale_ok = fixture.write(metadata, nodes, edges) &&
                           loadGraphSnapshot(fixture.files).ok();
    std::locale::global(original_locale);
    check(locale_ok, "Decimal point parsing independent of global locale");

    // v2 geometry, exact-byte integrity, and count checks.
    const std::string shape_nodes =
        "node_id,latitude,longitude,elevation_meters\n"
        "1,40.700,-73.900,\n2,40.701,-73.899,\n";
    const std::string shape_header =
        "edge_id,from_node_id,to_node_id,length_meters,speed_limit_kph,road_class,geometry_wkt\n";
    const std::string shape_edges = shape_header +
        "9,1,2,150,30,primary,\"LINESTRING (-73.900 40.700,-73.8995 40.7008,-73.899 40.701)\"\n";
    check(crcHex("123456789") == "cbf43926", "CRC reference matches published check vector");
    check(fixture.write(manifest(shape_nodes, shape_edges), shape_nodes, shape_edges),
          "Write schema v2 fixture");
    auto shape = loadGraphSnapshot(fixture.files);
    check(shape.ok() && shape.value().network.findSegment(9)->geometry.size() == 3 &&
          shape.value().network.findSegment(9)->geometry[1].longitude == -73.8995 &&
          shape.value().network.findSegment(9)->geometry[1].latitude == 40.7008,
          "Directed WKT geometry loaded with longitude latitude conversion");
    expectRejected("Modified data fails manifest integrity", manifest(shape_nodes, shape_edges),
        shape_nodes, replaceOnce(shape_edges, ",150,", ",151,"), ErrorCode::InvalidGraph);
    expectRejected("Wrong manifest count rejected",
        replaceOnce(manifest(shape_nodes, shape_edges), "node_count=2", "node_count=3"),
        shape_nodes, shape_edges, ErrorCode::InvalidGraph);
    expectRejected("Oversized manifest rejected before allocation",
        replaceOnce(manifest(shape_nodes, shape_edges), "node_count=2", "node_count=4294967296"),
        shape_nodes, shape_edges, ErrorCode::ResourceExhausted);
    expectRejected("Missing v2 manifest rejected", "schema_version=2\ngraph_version=test\n",
        shape_nodes, shape_edges, ErrorCode::InvalidGraph);
    expectRejected("Malformed CRC rejected",
        replaceOnce(manifest(shape_nodes, shape_edges), "nodes_crc32=", "nodes_crc32=x"),
        shape_nodes, shape_edges, ErrorCode::InvalidGraph);
    for (const auto& bad_shape : {
             "LINESTRING (-73.899 40.701,-73.900 40.700)",
             "LINESTRING (-73.900 40.700,-73.899 40.702)",
             "LINESTRING (-73.900 40.700)", "LINESTRING EMPTY",
             "LINESTRING Z (-73.900 40.700 0,-73.899 40.701 0)",
             "LINESTRING (-73.900 40.700,nan 40.701)",
             "LINESTRING (-73.900 40.700,-73.899 40.701) trailing"}) {
        const std::string invalid_edges = shape_header + "9,1,2,150,30,primary,\"" + bad_shape + "\"\n";
        expectRejected(std::string("Invalid WKT rejected: ") + bad_shape,
            manifest(shape_nodes, invalid_edges), shape_nodes, invalid_edges, ErrorCode::InvalidGraph);
    }
    const std::string blank_edges = shape_header + "9,1,2,150,30,primary,\n";
    check(fixture.write(manifest(shape_nodes, blank_edges), shape_nodes, blank_edges) &&
          loadGraphSnapshot(fixture.files).ok(), "Missing v2 geometry remains optional");

    // CRLF, BOM, blank rows and absence of final newline are part of the checksum.
    std::string byte_nodes = std::string("\xEF\xBB\xBF") + shape_nodes;
    for (std::size_t p = 0; (p = byte_nodes.find('\n', p)) != std::string::npos; p += 2) {
        byte_nodes.insert(p, "\r");
    }
    byte_nodes += "\r\n";
    std::string byte_edges = shape_edges.substr(0, shape_edges.size() - 1);
    check(fixture.write(manifest(byte_nodes, byte_edges), byte_nodes, byte_edges) &&
          loadGraphSnapshot(fixture.files).ok(), "Integrity covers exact BOM CRLF and EOF bytes");

    // --------------------------------------------------
    // Final result.
    // --------------------------------------------------

    std::cout << "\nChecks executed: "
              << checks << '\n';

    if (failures == 0) {
        std::cout << "All GraphLoader tests passed.\n";
        return 0;
    }

    std::cerr << failures
              << " GraphLoader test(s) failed.\n";

    return 1;
}
