
/*
 * File: test_dijkstra.cpp
 * Purpose: Tests shortest-path correctness, exact arc reconstruction,
 *          directed connections, invalid inputs and cost limits.
 * Status: VERIFIED
 * Remaining: Compile, run and verify all tests.
 */

#include "graph/road_network.hpp"
#include "graph/routing_graph.hpp"
#include "search/dijkstra.hpp"

#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

using namespace ecoroute;

void check(bool condition, const std::string& name, int& failures) {
    if (condition) {
        std::cout << "[PASS] " << name << '\n';
    } else {
        std::cerr << "[FAIL] " << name << '\n';
        ++failures;
    }
}

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

// Build a RoutingGraph using the same construction
// pipeline as the actual engine.
Result<RoutingGraph> makeGraph(
    std::vector<RoadNode> nodes,
    std::vector<RoadSegment> segments
) {
    auto network = RoadNetwork::create(nodes, segments);

    if (!network.ok()) {
        return Result<RoutingGraph>::failure(
            network.error().code,
            network.error().message
        );
    }

    return RoutingGraph::create(network.value());
}

// Verify the entire reconstructed route, including exact
// segment IDs, direction, destination and total cost.
bool pathMatches(
    const Result<ShortestPath>& result,
    const RoutingGraph& graph,
    const std::vector<unsigned>& weights,
    unsigned source,
    unsigned target,
    unsigned expected_cost,
    const std::vector<RoadSegmentId>& expected_segments
) {
    if (!result.ok()) {
        return false;
    }

    const ShortestPath& path = result.value();

    std::vector<RoadSegmentId> actual_segments;
    std::uint64_t calculated_cost = 0;
    unsigned current = source;

    for (unsigned arc : path.arc_indices) {
        if (arc >= graph.arcCount() ||
            graph.tail()[arc] != current) {
            return false;
        }

        actual_segments.push_back(graph.segmentIds()[arc]);
        calculated_cost += weights[arc];
        current = graph.head()[arc];
    }

    return current == target &&
           path.total_cost == expected_cost &&
           calculated_cost == expected_cost &&
           actual_segments == expected_segments;
}

bool hasError(
    const Result<ShortestPath>& result,
    ErrorCode expected
) {
    return !result.ok() && result.error().code == expected;
}

int main() {
    int failures = 0;

    // Six nodes, including one isolated node (60).
    std::vector<RoadNode> nodes = {
        {10, {40.700, -73.900}, std::nullopt},
        {20, {40.701, -73.899}, std::nullopt},
        {30, {40.702, -73.898}, std::nullopt},
        {40, {40.703, -73.897}, std::nullopt},
        {50, {40.704, -73.896}, std::nullopt},
        {60, {40.705, -73.895}, std::nullopt}
    };

    // Directed segments deliberately supplied out of order.
    std::vector<RoadSegment> segments = {
        makeSegment(105, 20, 40),
        makeSegment(101, 10, 20),
        makeSegment(103, 10, 30),
        makeSegment(102, 20, 30),
        makeSegment(104, 30, 40),
        makeSegment(106, 10, 20), // Parallel road
        makeSegment(107, 40, 50),
        makeSegment(108, 30, 30), // Self-loop
        makeSegment(109, 50, 20)  // Directed cycle
    };

    auto graph_result = makeGraph(nodes, segments);

    if (!graph_result.ok()) {
        std::cerr << "[FAIL] Graph construction: "
                  << graph_result.error().message << '\n';
        return 1;
    }

    const RoutingGraph& graph = graph_result.value();

    // Specify weights by external segment ID, not CSR position.
    // This also tests our ID-to-arc cost alignment.
    const std::unordered_map<RoadSegmentId, unsigned> costs = {
        {101, 3},
        {102, 4},
        {103, 20},
        {104, 2},
        {105, 20},
        {106, 10},
        {107, 1},
        {108, 0},
        {109, 5}
    };

    std::vector<unsigned> weights;

    for (RoadSegmentId id : graph.segmentIds()) {
        weights.push_back(costs.at(id));
    }

    // Compact node indices.
    unsigned a = *graph.findNodeIndex(10);
    unsigned b = *graph.findNodeIndex(20);
    unsigned c = *graph.findNodeIndex(30);
    unsigned d = *graph.findNodeIndex(40);
    unsigned e = *graph.findNodeIndex(50);
    unsigned isolated = *graph.findNodeIndex(60);

    // --------------------------------------------------
    // 1. Normal shortest paths.
    // --------------------------------------------------

    check(
        pathMatches(
            dijkstraShortestPath(graph, weights, a, d),
            graph, weights, a, d, 9,
            {101, 102, 104}
        ),
        "Shortest route and exact arc sequence",
        failures
    );

    check(
        pathMatches(
            dijkstraShortestPath(graph, weights, a, e),
            graph, weights, a, e, 10,
            {101, 102, 104, 107}
        ),
        "Multi-edge route reconstruction",
        failures
    );

    // --------------------------------------------------
    // 2. Parallel edges.
    // --------------------------------------------------

    check(
        pathMatches(
            dijkstraShortestPath(graph, weights, a, b),
            graph, weights, a, b, 3, {101}
        ),
        "Cheaper parallel edge selected",
        failures
    );

    // Make the other parallel edge cheaper.
    auto changed_weights = weights;

    for (std::size_t i = 0; i < graph.arcCount(); ++i) {
        if (graph.segmentIds()[i] == 106) {
            changed_weights[i] = 1;
        }
    }

    check(
        pathMatches(
            dijkstraShortestPath(graph, changed_weights, a, d),
            graph, changed_weights, a, d, 7,
            {106, 102, 104}
        ),
        "Changed weights select different parallel edge",
        failures
    );

    // --------------------------------------------------
    // 3. Zero-weight edges and self-loop.
    // --------------------------------------------------

    auto zero_weights = weights;

    for (std::size_t i = 0; i < graph.arcCount(); ++i) {
        RoadSegmentId id = graph.segmentIds()[i];

        if (id == 101 || id == 102 || id == 104) {
            zero_weights[i] = 0;
        }
    }

    check(
        pathMatches(
            dijkstraShortestPath(graph, zero_weights, a, d),
            graph, zero_weights, a, d, 0,
            {101, 102, 104}
        ),
        "Zero-cost path handled correctly",
        failures
    );

    check(
        pathMatches(
            dijkstraShortestPath(graph, weights, c, c),
            graph, weights, c, c, 0, {}
        ),
        "Source equals destination",
        failures
    );

    // --------------------------------------------------
    // 4. Directed and unreachable routes.
    // --------------------------------------------------

    check(
        hasError(
            dijkstraShortestPath(graph, weights, d, a),
            ErrorCode::NoRoute
        ),
        "Reverse route not invented",
        failures
    );

    check(
        hasError(
            dijkstraShortestPath(graph, weights, a, isolated),
            ErrorCode::NoRoute
        ),
        "Unreachable destination detected",
        failures
    );

    check(
        pathMatches(
            dijkstraShortestPath(graph, weights, isolated, isolated),
            graph, weights, isolated, isolated, 0, {}
        ),
        "Isolated node routes to itself",
        failures
    );

    // --------------------------------------------------
    // 5. Invalid input validation.
    // --------------------------------------------------

    check(
        hasError(
            dijkstraShortestPath(graph, weights, 999, d),
            ErrorCode::InvalidArgument
        ),
        "Invalid source rejected",
        failures
    );

    check(
        hasError(
            dijkstraShortestPath(graph, weights, a, 999),
            ErrorCode::InvalidArgument
        ),
        "Invalid target rejected",
        failures
    );

    auto short_weights = weights;
    short_weights.pop_back();

    check(
        hasError(
            dijkstraShortestPath(graph, short_weights, a, d),
            ErrorCode::InvalidMetric
        ),
        "Incorrect weight-array size rejected",
        failures
    );

    auto invalid_weights = weights;
    invalid_weights[0] = 2147483647u;

    check(
        hasError(
            dijkstraShortestPath(graph, invalid_weights, a, d),
            ErrorCode::InvalidMetric
        ),
        "CCH infinity value rejected as an edge weight",
        failures
    );

    // --------------------------------------------------
    // 6. Large accumulated path costs.
    // --------------------------------------------------

    auto large_graph_result = makeGraph(
        {
            {1, {40.70, -73.90}, std::nullopt},
            {2, {40.71, -73.89}, std::nullopt},
            {3, {40.72, -73.88}, std::nullopt}
        },
        {
            makeSegment(201, 1, 2),
            makeSegment(202, 2, 3)
        }
    );

    if (!large_graph_result.ok()) {
        std::cerr << "[FAIL] Large-cost graph construction\n";
        return 1;
    }

    const RoutingGraph& large_graph = large_graph_result.value();

    // Total = 2,147,483,646, just below CCH infinity.
    std::vector<unsigned> boundary_weights = {
        1073741823u,
        1073741823u
    };

    check(
        pathMatches(
            dijkstraShortestPath(
                large_graph, boundary_weights, 0, 2
            ),
            large_graph, boundary_weights, 0, 2,
            2147483646u, {201, 202}
        ),
        "Largest supported finite path cost",
        failures
    );

    // Each edge is valid individually, but their sum
    // exceeds the supported CCH path-cost range.
    std::vector<unsigned> oversized_weights = {
        1500000000u,
        1500000000u
    };

    check(
        hasError(
            dijkstraShortestPath(
                large_graph, oversized_weights, 0, 2
            ),
            ErrorCode::ResourceExhausted
        ),
        "Oversized accumulated path cost rejected",
        failures
    );

    // --------------------------------------------------
    // Final result.
    // --------------------------------------------------

    if (failures == 0) {
        std::cout << "\nAll Dijkstra tests passed.\n";
        return 0;
    }

    std::cerr << failures << " Dijkstra test(s) failed.\n";
    return 1;
}
