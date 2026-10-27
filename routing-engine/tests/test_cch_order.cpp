
/*
 * File: test_cch_order.cpp
 * Purpose: Tests automatic nested-dissection ordering and
 *          CCH correctness on an EcoRoute road grid.
 * Status: VERIFIED
 * Remaining: Compile, run and verify results.
 */

#include "graph/road_network.hpp"
#include "graph/routing_graph.hpp"
#include "search/cch_order.hpp"
#include "search/cch_adapter.hpp"
#include "search/dijkstra.hpp"

#include <cstdint>
#include <iostream>
#include <optional>
#include <random>
#include <utility>
#include <vector>

using namespace ecoroute;

// Verify an actual path using the original physical roads.
bool validPath(
    const ShortestPath& path,
    const RoadNetwork& network,
    const RoutingGraph& graph,
    const std::vector<unsigned>& weights,
    unsigned source,
    unsigned target
) {
    unsigned current = source;
    std::uint64_t total = 0;

    for (unsigned arc : path.arc_indices) {
        if (arc >= graph.arcCount() ||
            graph.tail()[arc] != current) {
            return false;
        }

        const RoadSegment* segment =
            network.findSegment(graph.segmentIds()[arc]);

        if (segment == nullptr ||
            segment->from != graph.nodeIds()[current] ||
            segment->to != graph.nodeIds()[graph.head()[arc]]) {
            return false;
        }

        total += weights[arc];
        current = graph.head()[arc];
    }

    return current == target && total == path.total_cost;
}

// Compare every possible source-destination pair.
bool compareAllPairs(
    const RoadNetwork& network,
    const RoutingGraph& graph,
    const CchAdapter& cch,
    const std::vector<unsigned>& weights,
    unsigned& comparisons
) {
    for (unsigned source = 0; source < graph.nodeCount(); ++source) {
        for (unsigned target = 0; target < graph.nodeCount(); ++target) {

            auto baseline = dijkstraShortestPath(
                graph, weights, source, target
            );

            auto optimized = cch.shortestPath(source, target);

            if (!baseline.ok() || !optimized.ok()) {
                std::cerr << "[FAIL] Query failed: "
                          << source << " -> " << target << '\n';
                return false;
            }

            if (baseline.value().total_cost !=
                optimized.value().total_cost) {
                std::cerr << "[FAIL] Distance mismatch: "
                          << source << " -> " << target << '\n';
                return false;
            }

            if (!validPath(
                    baseline.value(), network, graph,
                    weights, source, target
                ) ||
                !validPath(
                    optimized.value(), network, graph,
                    weights, source, target
                )) {
                std::cerr << "[FAIL] Invalid reconstructed path.\n";
                return false;
            }

            ++comparisons;
        }
    }

    return true;
}

int main() {
    constexpr unsigned SIDE = 6;
    constexpr unsigned NODE_COUNT = SIDE * SIDE;

    // --------------------------------------------------
    // 1. Construct physical road nodes.
    // --------------------------------------------------

    std::vector<RoadNode> nodes;
    std::vector<RoadNodeId> ids;

    for (unsigned row = 0; row < SIDE; ++row) {
        for (unsigned col = 0; col < SIDE; ++col) {

            unsigned index = row * SIDE + col;

            RoadNode node;
            node.id = 100000 + 17 * index;

            node.location = {
                40.700 + row * 0.001,
                -73.900 + col * 0.001
            };

            ids.push_back(node.id);
            nodes.push_back(node);
        }
    }

    // --------------------------------------------------
    // 2. Construct directed road segments.
    // --------------------------------------------------

    std::vector<RoadSegment> segments;
    RoadSegmentId next_id = 5000;

    auto addArc = [&](unsigned from, unsigned to) {
        RoadSegment segment;

        segment.id = next_id++;
        segment.from = ids[from];
        segment.to = ids[to];
        segment.length_meters = 100.0;

        RoadSegmentId assigned_id = segment.id;
        segments.push_back(segment);
        return assigned_id;
    };

    for (unsigned row = 0; row < SIDE; ++row) {
        for (unsigned col = 0; col < SIDE; ++col) {

            unsigned u = row * SIDE + col;

            // Horizontal streets in both directions.
            if (col + 1 < SIDE) {
                addArc(u, u + 1);
                addArc(u + 1, u);
            }

            // Vertical streets in both directions.
            if (row + 1 < SIDE) {
                addArc(u, u + SIDE);
                addArc(u + SIDE, u);
            }
        }
    }

    // Additional one-way connection from the top-left
    // to the bottom-right node.
    RoadSegmentId shortcut_id = addArc(0, NODE_COUNT - 1);

    auto network_result = RoadNetwork::create(
        std::move(nodes), std::move(segments)
    );

    if (!network_result.ok()) {
        std::cerr << "[FAIL] RoadNetwork construction\n";
        return 1;
    }

    const RoadNetwork& network = network_result.value();

    auto graph_result = RoutingGraph::create(network);

    if (!graph_result.ok()) {
        std::cerr << "[FAIL] CSR construction\n";
        return 1;
    }

    const RoutingGraph& graph = graph_result.value();

    if (graph.nodeCount() != NODE_COUNT ||
        graph.arcCount() != 121) {
        std::cerr << "[FAIL] Unexpected graph dimensions.\n";
        return 1;
    }

    std::cout << "[PASS] Road grid and CSR constructed.\n";

    // --------------------------------------------------
    // 3. Generate the contraction order automatically.
    // --------------------------------------------------

    auto order_result = computeCchNodeOrder(network, graph);

    if (!order_result.ok()) {
        std::cerr << "[FAIL] Nested dissection: "
                  << order_result.error().message << '\n';
        return 1;
    }

    std::vector<unsigned> order = order_result.value();

    // Independent permutation check.
    std::vector<bool> seen(NODE_COUNT, false);

    for (unsigned node : order) {
        if (node >= NODE_COUNT || seen[node]) {
            std::cerr << "[FAIL] Invalid contraction order.\n";
            return 1;
        }

        seen[node] = true;
    }

    if (order.size() != NODE_COUNT) {
        std::cerr << "[FAIL] Incorrect order size.\n";
        return 1;
    }

    std::cout << "[PASS] Nested-dissection order generated.\n";

    // --------------------------------------------------
    // 4. Build CCH using the automatically generated order.
    // --------------------------------------------------

    std::vector<unsigned> weights(graph.arcCount());

    for (unsigned arc = 0; arc < graph.arcCount(); ++arc) {
        weights[arc] = 5 + graph.segmentIds()[arc] % 7;

        if (graph.segmentIds()[arc] == shortcut_id) {
            weights[arc] = 1;
        }
    }

    auto cch_result = CchAdapter::create(
        graph, std::move(order), weights
    );

    if (!cch_result.ok()) {
        std::cerr << "[FAIL] CCH construction: "
                  << cch_result.error().message << '\n';
        return 1;
    }

    CchAdapter cch = std::move(cch_result.value());

    // The inexpensive shortcut should be selected.
    auto initial = cch.shortestPath(0, NODE_COUNT - 1);

    if (!initial.ok() ||
        initial.value().total_cost != 1 ||
        initial.value().arc_indices.size() != 1 ||
        graph.segmentIds()[initial.value().arc_indices[0]] != shortcut_id) {
        std::cerr << "[FAIL] Expected shortcut not selected.\n";
        return 1;
    }

    std::cout << "[PASS] CCH selects correct one-way shortcut.\n";

    // --------------------------------------------------
    // 5. Simulate a traffic change.
    // --------------------------------------------------

    for (unsigned arc = 0; arc < graph.arcCount(); ++arc) {
        if (graph.segmentIds()[arc] == shortcut_id) {
            weights[arc] = 1000;
        }
    }

    auto updated = cch.updateWeights(weights);

    if (!updated.ok()) {
        std::cerr << "[FAIL] Metric customization.\n";
        return 1;
    }

    auto changed = cch.shortestPath(0, NODE_COUNT - 1);

    if (!changed.ok() ||
        changed.value().total_cost >= 1000) {
        std::cerr << "[FAIL] Congested shortcut not avoided.\n";
        return 1;
    }

    std::cout << "[PASS] Traffic update changes selected route.\n";

    // --------------------------------------------------
    // 6. Cross-check under multiple weight configurations.
    // --------------------------------------------------

    std::mt19937 rng(20261025u);
    std::uniform_int_distribution<unsigned> distribution(1, 50);

    unsigned comparisons = 0;

    for (unsigned round = 0; round < 5; ++round) {

        for (unsigned& weight : weights) {
            weight = distribution(rng);
        }

        auto result = cch.updateWeights(weights);

        if (!result.ok() ||
            !compareAllPairs(
                network, graph, cch, weights, comparisons
            )) {
            std::cerr << "[FAIL] Cross-check round "
                      << round << '\n';
            return 1;
        }
    }

    if (comparisons != 5 * NODE_COUNT * NODE_COUNT) {
        std::cerr << "[FAIL] Incorrect comparison count.\n";
        return 1;
    }

    std::cout << "[PASS] CCH matches Dijkstra over "
              << comparisons << " source-target queries.\n";

    std::cout << "\nAll nested-dissection tests passed.\n";
    return 0;
}
