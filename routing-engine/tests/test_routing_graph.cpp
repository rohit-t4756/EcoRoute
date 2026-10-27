
/*
 * File: test_routing_graph.cpp
 * Purpose: Verifies directed CSR construction, adjacency offsets,
 *          external ID mappings and preservation of road segments.
 * Status: VERIFIED
 * Remaining: Compile, execute and verify all test cases.
 */

#include "graph/road_network.hpp"
#include "graph/routing_graph.hpp"

#include <iostream>
#include <optional>
#include <string>
#include <vector>

using namespace ecoroute;

// Simple test helper.
void check(bool condition, const std::string& name, int& failures) {
    if (condition) {
        std::cout << "[PASS] " << name << '\n';
    } else {
        std::cerr << "[FAIL] " << name << '\n';
        ++failures;
    }
}

// Create nodes with nonconsecutive external IDs.
std::vector<RoadNode> makeNodes() {
    return {
        {500,  {40.700, -73.900}, std::nullopt},
        {100,  {40.701, -73.899}, std::nullopt},
        {900,  {40.702, -73.898}, std::nullopt},
        {300,  {40.703, -73.897}, std::nullopt},
        {700,  {40.704, -73.896}, std::nullopt},
        {1100, {40.705, -73.895}, std::nullopt}
    };
}

// Create a valid directed segment.
RoadSegment makeSegment(
    RoadSegmentId id,
    RoadNodeId from,
    RoadNodeId to
) {
    RoadSegment segment;
    segment.id = id;
    segment.from = from;
    segment.to = to;
    segment.length_meters = 100.0;

    return segment;
}

// Deliberately provide segments in an unsorted order.
std::vector<RoadSegment> makeSegments() {
    return {
        makeSegment(65, 300, 500),
        makeSegment(62, 500, 100),
        makeSegment(63, 100, 900),
        makeSegment(66, 500, 100),   // Parallel edge
        makeSegment(64, 900, 900),   // Self-loop
        makeSegment(61, 100, 300),
        makeSegment(67, 700, 500),
        makeSegment(68, 300, 1100)
    };
}

int main() {
    int failures = 0;

    // Step 1: Construct the validated physical network.
    auto network_result = RoadNetwork::create(
        makeNodes(), makeSegments()
    );

    if (!network_result.ok()) {
        std::cerr << "[FAIL] Could not construct RoadNetwork: "
                  << network_result.error().message << '\n';
        return 1;
    }

    // Step 2: Convert the physical network to CSR.
    auto graph_result = RoutingGraph::create(
        network_result.value()
    );

    if (!graph_result.ok()) {
        std::cerr << "[FAIL] CSR construction failed: "
                  << graph_result.error().message << '\n';
        return 1;
    }

    const RoutingGraph& graph = graph_result.value();

    // ----------------------------------------------
    // 1. Node and arc counts.
    // ----------------------------------------------

    check(
        graph.nodeCount() == 6,
        "Correct node count",
        failures
    );

    check(
        graph.arcCount() == 8,
        "Correct directed arc count",
        failures
    );

    // ----------------------------------------------
    // 2. External node ID mapping.
    // ----------------------------------------------

    const std::vector<RoadNodeId> expected_nodes = {
        500, 100, 900, 300, 700, 1100
    };

    check(
        graph.nodeIds() == expected_nodes,
        "Original node IDs preserved",
        failures
    );

    auto index = graph.findNodeIndex(900);

    check(
        index.has_value() && *index == 2,
        "External node ID maps to correct index",
        failures
    );

    check(
        !graph.findNodeIndex(999999).has_value(),
        "Unknown node lookup returns empty optional",
        failures
    );

    // ----------------------------------------------
    // 3. CSR adjacency offsets.
    // ----------------------------------------------

    // Expected outgoing degrees:
    // Node 0: 2
    // Node 1: 2
    // Node 2: 1
    // Node 3: 2
    // Node 4: 1
    // Node 5: 0

    const std::vector<unsigned> expected_first_out = {
        0, 2, 4, 5, 7, 8, 8
    };

    check(
        graph.firstOut() == expected_first_out,
        "CSR prefix sums are correct",
        failures
    );

    // ----------------------------------------------
    // 4. CSR arc arrays.
    // ----------------------------------------------

    const std::vector<unsigned> expected_tail = {
        0, 0, 1, 1, 2, 3, 3, 4
    };

    const std::vector<unsigned> expected_head = {
        1, 1, 2, 3, 2, 0, 5, 0
    };

    const std::vector<RoadSegmentId> expected_segments = {
        62, 66, 63, 61, 64, 65, 68, 67
    };

    check(
        graph.tail() == expected_tail,
        "CSR source indices are correct",
        failures
    );

    check(
        graph.head() == expected_head,
        "CSR destination indices are correct",
        failures
    );

    check(
        graph.segmentIds() == expected_segments,
        "Original segment IDs match CSR arc positions",
        failures
    );

    // ----------------------------------------------
    // 5. Validate every adjacency range.
    // ----------------------------------------------

    bool valid_ranges = true;

    for (unsigned u = 0; u < graph.nodeCount(); ++u) {
        unsigned begin = graph.firstOut()[u];
        unsigned end = graph.firstOut()[u + 1];

        if (begin > end || end > graph.arcCount()) {
            valid_ranges = false;
            break;
        }

        for (unsigned arc = begin; arc < end; ++arc) {
            if (graph.tail()[arc] != u) {
                valid_ranges = false;
                break;
            }
        }
    }

    check(
        valid_ranges,
        "Every CSR adjacency range contains its source arcs",
        failures
    );

    // ----------------------------------------------
    // 6. Parallel edges.
    // ----------------------------------------------

    check(
        graph.head()[0] == 1 &&
        graph.head()[1] == 1 &&
        graph.segmentIds()[0] != graph.segmentIds()[1],
        "Parallel directed edges preserved",
        failures
    );

    // ----------------------------------------------
    // 7. Self-loop.
    // ----------------------------------------------

    check(
        graph.tail()[4] == graph.head()[4],
        "Self-loop edge preserved",
        failures
    );

    // ----------------------------------------------
    // 8. Sink node (no outgoing connections).
    // ----------------------------------------------

    check(
        graph.firstOut()[5] == graph.firstOut()[6],
        "Node without outgoing edges handled",
        failures
    );

    // ----------------------------------------------
    // 9. No reverse edges are invented.
    // ----------------------------------------------

    // Node 0 -> node 1 exists, but node 1 -> node 0
    // was not provided in the input.

    bool has_reverse_edge = false;

    for (unsigned arc = graph.firstOut()[1];
         arc < graph.firstOut()[2];
         ++arc) {

        if (graph.head()[arc] == 0) {
            has_reverse_edge = true;
        }
    }

    check(
        !has_reverse_edge,
        "No nonexistent reverse road added",
        failures
    );

    // ----------------------------------------------
    // Final result.
    // ----------------------------------------------

    if (failures == 0) {
        std::cout << "\nAll CSR tests passed.\n";
        return 0;
    }

    std::cerr << failures << " CSR test(s) failed.\n";
    return 1;
}
