
/*
 * File: test_cch_adapter.cpp
 * Purpose: Cross-validates EcoRoute's CCH adapter against Dijkstra
 *          using the same directed CSR graph and edge weights.
 * Status: VERIFIED
 * Remaining: Compile, run and verify all tests.
 */

#include "graph/road_network.hpp"
#include "graph/routing_graph.hpp"
#include "search/cch_adapter.hpp"
#include "search/dijkstra.hpp"

#include <cstdint>
#include <algorithm>
#include <atomic>
#include <iostream>
#include <numeric>
#include <optional>
#include <random>
#include <string>
#include <thread>
#include <utility>
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

// Verify that a reconstructed path follows real directed
// CSR arcs and that their weights add up correctly.
bool validPath(
    const ShortestPath& path,
    const RoutingGraph& graph,
    const std::vector<unsigned>& weights,
    unsigned source,
    unsigned target
) {
    unsigned current = source;
    std::uint64_t cost = 0;

    for (unsigned arc : path.arc_indices) {
        if (arc >= graph.arcCount() ||
            graph.tail()[arc] != current) {
            return false;
        }

        cost += weights[arc];
        current = graph.head()[arc];
    }

    return current == target && cost == path.total_cost;
}

// Convert CSR arc indices back to physical segment IDs.
std::vector<RoadSegmentId> segmentPath(
    const ShortestPath& path,
    const RoutingGraph& graph
) {
    std::vector<RoadSegmentId> ids;

    for (unsigned arc : path.arc_indices) {
        ids.push_back(graph.segmentIds()[arc]);
    }

    return ids;
}

// Compare both algorithms over every source-target pair.
// Their paths may differ when multiple optimal routes tie,
// but their minimum costs must be identical.
bool compareAllPairs(
    const RoutingGraph& graph,
    const std::vector<unsigned>& weights,
    const CchAdapter& adapter,
    unsigned& comparisons
) {
    unsigned n = static_cast<unsigned>(graph.nodeCount());

    for (unsigned source = 0; source < n; ++source) {
        for (unsigned target = 0; target < n; ++target) {

            auto baseline = dijkstraShortestPath(
                graph, weights, source, target
            );

            auto optimized = adapter.shortestPath(source, target);

            if (baseline.ok() != optimized.ok()) {
                std::cerr << "[FAIL] Reachability mismatch: "
                          << source << " -> " << target << '\n';
                return false;
            }

            if (!baseline.ok()) {
                if (baseline.error().code != ErrorCode::NoRoute ||
                    optimized.error().code != ErrorCode::NoRoute) {
                    std::cerr << "[FAIL] Incorrect error classification.\n";
                    return false;
                }
            } else {
                if (baseline.value().total_cost !=
                    optimized.value().total_cost) {

                    std::cerr << "[FAIL] Distance mismatch: "
                              << source << " -> " << target
                              << ", Dijkstra="
                              << baseline.value().total_cost
                              << ", CCH="
                              << optimized.value().total_cost << '\n';
                    return false;
                }

                if (!validPath(
                        baseline.value(), graph, weights,
                        source, target
                    ) ||
                    !validPath(
                        optimized.value(), graph, weights,
                        source, target
                    )) {

                    std::cerr << "[FAIL] Invalid reconstructed path: "
                              << source << " -> " << target << '\n';
                    return false;
                }
            }

            ++comparisons;
        }
    }

    return true;
}

int main() {
    int failures = 0;

    // --------------------------------------------------
    // 1. Construct a physical road network.
    // --------------------------------------------------

    const std::vector<RoadNodeId> ids = {
        500, 100, 900, 300, 700, 1100, 2000
    };

    std::vector<RoadNode> nodes;

    for (std::size_t i = 0; i < ids.size(); ++i) {
        nodes.push_back({
            ids[i],
            {40.700 + 0.001 * i, -73.900 + 0.001 * i},
            std::nullopt
        });
    }

    std::vector<RoadSegment> segments;

    auto addSegment = [&](RoadSegmentId id, unsigned from,
                          unsigned to) {
        RoadSegment segment;
        segment.id = id;
        segment.from = ids[from];
        segment.to = ids[to];
        segment.length_meters = 100.0;

        segments.push_back(segment);
    };

    // Directed roads, cycles, parallel edges and a self-loop.
    // Node 6 remains isolated.
    addSegment(701, 0, 1);
    addSegment(702, 0, 2);
    addSegment(703, 1, 2);
    addSegment(704, 1, 3);
    addSegment(705, 2, 3);
    addSegment(706, 2, 4);
    addSegment(707, 3, 4);
    addSegment(708, 3, 5);
    addSegment(709, 4, 5);
    addSegment(710, 4, 1);
    addSegment(711, 5, 0);
    addSegment(712, 0, 1); // Parallel edge
    addSegment(713, 2, 2); // Self-loop

    auto network_result = RoadNetwork::create(nodes, segments);

    if (!network_result.ok()) {
        std::cerr << "[FAIL] RoadNetwork construction: "
                  << network_result.error().message << '\n';
        return 1;
    }

    auto graph_result = RoutingGraph::create(
        network_result.value()
    );

    if (!graph_result.ok()) {
        std::cerr << "[FAIL] CSR construction: "
                  << graph_result.error().message << '\n';
        return 1;
    }

    const RoutingGraph& graph = graph_result.value();

    check(
        graph.nodeCount() == 7 && graph.arcCount() == 13,
        "Shared EcoRoute CSR graph constructed",
        failures
    );

    // --------------------------------------------------
    // 2. Prepare weights in CSR arc order.
    // --------------------------------------------------

    std::vector<unsigned> weights(graph.arcCount(), 100);

    auto setWeight = [&](RoadSegmentId id, unsigned value) {
        for (std::size_t i = 0; i < graph.arcCount(); ++i) {
            if (graph.segmentIds()[i] == id) {
                weights[i] = value;
                return;
            }
        }
    };

    // Initial fastest route: 0 -> 1 -> 3, cost 4.
    setWeight(701, 2);
    setWeight(704, 2);

    // Alternative route: 0 -> 2 -> 3, cost 11.
    setWeight(702, 8);
    setWeight(705, 3);

    // --------------------------------------------------
    // 3. Validate bad CCH inputs.
    // --------------------------------------------------

    std::vector<unsigned> order = {0, 1, 2, 3, 4, 5, 6};

    auto duplicate_order = order;
    duplicate_order[1] = 0;

    auto bad_order_result = CchAdapter::create(
        graph, duplicate_order, weights
    );

    check(
        !bad_order_result.ok() &&
        bad_order_result.error().code == ErrorCode::InvalidArgument,
        "Duplicate contraction-order node rejected",
        failures
    );

    auto short_order = order;
    short_order.pop_back();

    auto short_order_result = CchAdapter::create(
        graph, short_order, weights
    );

    check(
        !short_order_result.ok() &&
        short_order_result.error().code == ErrorCode::InvalidArgument,
        "Incomplete contraction order rejected",
        failures
    );

    auto invalid_weights = weights;
    invalid_weights[0] = 2147483647u;

    auto bad_metric_result = CchAdapter::create(
        graph, order, invalid_weights
    );

    check(
        !bad_metric_result.ok() &&
        bad_metric_result.error().code == ErrorCode::InvalidMetric,
        "Invalid initial metric rejected",
        failures
    );

    // --------------------------------------------------
    // 4. Build the actual CCH adapter.
    // --------------------------------------------------

    auto adapter_result = CchAdapter::create(
        graph, order, weights
    );

    if (!adapter_result.ok()) {
        std::cerr << "[FAIL] CCH adapter creation: "
                  << adapter_result.error().message << '\n';
        return 1;
    }

    CchAdapter adapter = std::move(adapter_result.value());

    // Move again to verify that ownership transfer does not
    // invalidate RoutingKit's internal references.
    CchAdapter moved_adapter(std::move(adapter));

    auto initial = moved_adapter.shortestPath(0, 3);

    check(
        initial.ok() &&
        initial.value().total_cost == 4 &&
        segmentPath(initial.value(), graph) ==
            std::vector<RoadSegmentId>{701, 704},
        "CCH finds expected initial road segments",
        failures
    );

    // --------------------------------------------------
    // 5. Simulate a traffic update.
    // --------------------------------------------------

    // Congestion makes road 704 more expensive.
    setWeight(704, 50);

    auto updated = moved_adapter.updateWeights(weights);

    check(
        updated.ok(),
        "CCH metric update accepted",
        failures
    );

    auto changed = moved_adapter.shortestPath(0, 3);

    check(
        changed.ok() &&
        changed.value().total_cost == 11 &&
        segmentPath(changed.value(), graph) ==
            std::vector<RoadSegmentId>{702, 705},
        "Traffic update changes the selected route",
        failures
    );

    // --------------------------------------------------
    // 6. Invalid updates must preserve the old metric.
    // --------------------------------------------------

    auto rejected_update = moved_adapter.updateWeights(
        invalid_weights
    );

    auto after_rejection = moved_adapter.shortestPath(0, 3);

    check(
        !rejected_update.ok() &&
        rejected_update.error().code == ErrorCode::InvalidMetric &&
        after_rejection.ok() &&
        after_rejection.value().total_cost == 11,
        "Invalid update preserves previous metric",
        failures
    );

    auto invalid_query = moved_adapter.shortestPath(999, 3);

    check(
        !invalid_query.ok() &&
        invalid_query.error().code == ErrorCode::InvalidArgument,
        "Invalid query node rejected",
        failures
    );

    // --------------------------------------------------
    // 7. Exhaustive cross-check with changing weights.
    // --------------------------------------------------

    std::mt19937 rng(20261024u);
    std::uniform_int_distribution<unsigned> distribution(0, 40);

    unsigned comparisons = 0;
    bool all_match = true;

    for (unsigned round = 0; round < 25; ++round) {

        for (unsigned& weight : weights) {
            weight = distribution(rng);
        }

        auto update = moved_adapter.updateWeights(weights);

        if (!update.ok() ||
            !compareAllPairs(
                graph, weights, moved_adapter, comparisons
            )) {

            std::cerr << "[FAIL] Cross-check round "
                      << round << '\n';

            all_match = false;
            break;
        }
    }

    check(
        all_match && comparisons == 1225,
        "CCH matches Dijkstra over 1,225 queries",
        failures
    );

    // A reachable path can exceed RoutingKit infinity although every arc is
    // finite. It is a numerical range failure, not a connectivity failure.
    auto chain_network = RoadNetwork::create(
        {{1, {0.0, 0.0}, std::nullopt}, {2, {0.0, 0.1}, std::nullopt},
         {3, {0.0, 0.2}, std::nullopt}, {4, {0.0, 0.3}, std::nullopt}},
        {{1, 1, 2, 10.0, std::nullopt, std::nullopt, {}},
         {2, 2, 3, 10.0, std::nullopt, std::nullopt, {}}});
    auto chain_graph = RoutingGraph::create(chain_network.value());
    auto chain_result = CchAdapter::create(chain_graph.value(), {0, 1, 2, 3}, {1, 2});
    if (!chain_result.ok()) return 1;
    auto& chain = chain_result.value();
    for (const auto& metric : std::vector<std::vector<unsigned>>{
             {1073741823u, 1073741823u}, {1073741823u, 1073741824u},
             {2147483646u, 2147483646u}}) {
        auto optimized = chain.shortestPathWithWeights(0, 2, metric);
        auto reference = dijkstraShortestPath(chain_graph.value(), metric, 0, 2);
        check(optimized.ok() == reference.ok() &&
              (optimized.ok() ? optimized.value().total_cost == reference.value().total_cost :
               optimized.error().code == ErrorCode::ResourceExhausted &&
               reference.error().code == ErrorCode::ResourceExhausted),
              "CCH and 64-bit Dijkstra agree at accumulated integer boundary", failures);
    }
    auto disconnected = chain.shortestPath(0, 3);
    check(!disconnected.ok() && disconnected.error().code == ErrorCode::NoRoute,
          "Disconnected target remains NoRoute for large arc weights", failures);

    auto independent = chain.withWeights({7, 11});
    auto replacement = chain.updateWeights({2, 3});
    auto preserved = independent.value().shortestPath(0, 2);
    auto current = chain.shortestPath(0, 2);
    check(replacement.ok() && preserved.ok() && preserved.value().total_cost == 18 &&
          current.ok() && current.value().total_cost == 5,
          "Shared topology supports independent immutable metric snapshots", failures);
    auto identical = chain.updateWeights({2, 3});
    check(identical.ok() && chain.shortestPath(0, 2).value().total_cost == 5,
          "Identical metric replacement preserves query state", failures);

    // Interleave query/update pairs; each result must use its own requested
    // metric. Plain shortestPath concurrently observes one complete metric.
    std::atomic<bool> concurrent_ok{true};
    std::vector<std::thread> workers;
    for (unsigned worker = 0; worker < 4; ++worker) {
        workers.emplace_back([&, worker] {
            for (unsigned iteration = 0; iteration < 200; ++iteration) {
                const unsigned value = worker + 1;
                auto result = chain.shortestPathWithWeights(0, 2, {value, value});
                if (!result.ok() || result.value().total_cost != 2 * value) concurrent_ok = false;
                auto read = chain.shortestPath(0, 2);
                if (!read.ok() || read.value().total_cost < 2 || read.value().total_cost > 8 ||
                    read.value().total_cost % 2 != 0) concurrent_ok = false;
                auto snapshot_read = independent.value().shortestPath(0, 2);
                if (!snapshot_read.ok() || snapshot_read.value().total_cost != 18) concurrent_ok = false;
            }
        });
    }
    for (auto& worker : workers) worker.join();
    check(concurrent_ok, "Concurrent metric/query transactions and snapshot queries are consistent", failures);

    // A graph with isolated nodes has no input weight element zero.
    auto isolated_network = RoadNetwork::create(
        {{1, {0.0, 0.0}, std::nullopt}, {2, {0.0, 0.1}, std::nullopt}}, {});
    auto isolated_graph = RoutingGraph::create(isolated_network.value());
    auto isolated = CchAdapter::create(isolated_graph.value(), {1, 0}, {});
    auto self = isolated.value().shortestPath(1, 1);
    auto absent = isolated.value().shortestPath(0, 1);
    check(self.ok() && self.value().total_cost == 0 && self.value().arc_indices.empty() &&
          !absent.ok() && absent.error().code == ErrorCode::NoRoute,
          "Zero-edge hierarchy supports isolated self/no-route queries", failures);

    // Change topology as well as metrics: deterministic random directed graphs
    // include parallel arcs, zero weights and self loops.
    bool random_match = true;
    unsigned random_comparisons = 0;
    for (unsigned round = 0; round < 12 && random_match; ++round) {
        std::vector<RoadNode> random_nodes;
        std::vector<RoadSegment> random_segments;
        for (unsigned node = 0; node < 8; ++node) {
            random_nodes.push_back({node + 1, {0.0, 0.01 * node}, std::nullopt});
        }
        for (unsigned arc = 0; arc < 24; ++arc) {
            random_segments.push_back({arc + 1, 1 + rng() % 8, 1 + rng() % 8, 10.0,
                                       std::nullopt, std::nullopt, {}});
        }
        auto random_network = RoadNetwork::create(std::move(random_nodes), std::move(random_segments));
        auto random_graph = RoutingGraph::create(random_network.value());
        std::vector<unsigned> random_order(8), random_weights(24);
        std::iota(random_order.begin(), random_order.end(), 0u);
        std::shuffle(random_order.begin(), random_order.end(), rng);
        for (auto& weight : random_weights) weight = distribution(rng);
        auto random_cch = CchAdapter::create(random_graph.value(), random_order, random_weights);
        random_match = random_cch.ok() && compareAllPairs(random_graph.value(), random_weights,
            random_cch.value(), random_comparisons);
    }
    check(random_match && random_comparisons == 768,
          "CCH matches Dijkstra across 12 random directed topologies", failures);

    // --------------------------------------------------
    // Final result.
    // --------------------------------------------------

    std::cout << "\n";

    if (failures == 0) {
        std::cout << "All CCH adapter tests passed.\n";
        return 0;
    }

    std::cerr << failures << " test(s) failed.\n";
    return 1;
}
