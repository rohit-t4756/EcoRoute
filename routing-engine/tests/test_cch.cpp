/*
 * File: test_cch.cpp
 * Purpose: Verifies RoutingKit CCH against independent Dijkstra
 *          across directed routes and changing edge weights.
 * Status: IN_PROGRESS
 * Remaining: Verify execution through CMake and CTest.
 */


#include <routingkit/customizable_contraction_hierarchy.h>

#include <functional>
#include <iostream>
#include <queue>
#include <random>
#include <utility>
#include <vector>

// Ordinary Dijkstra, independent of RoutingKit's search algorithm.
unsigned dijkstra(
    unsigned source,
    unsigned target,
    unsigned node_count,
    const std::vector<unsigned>& tail,
    const std::vector<unsigned>& head,
    const std::vector<unsigned>& weights
) {
    const unsigned INF = RoutingKit::inf_weight;

    std::vector<std::vector<std::pair<unsigned, unsigned>>> adj(node_count);

    for (unsigned i = 0; i < tail.size(); ++i) {
        adj[tail[i]].push_back({head[i], weights[i]});
    }

    std::vector<unsigned> dist(node_count, INF);
    dist[source] = 0;

    using State = std::pair<unsigned, unsigned>;

    std::priority_queue<
        State,
        std::vector<State>,
        std::greater<State>
    > pq;

    pq.push({0, source});

    while (!pq.empty()) {
        auto [current_dist, u] = pq.top();
        pq.pop();

        if (current_dist != dist[u]) {
            continue;
        }

        if (u == target) {
            return current_dist;
        }

        for (const auto& [v, cost] : adj[u]) {
            unsigned new_dist = current_dist + cost;

            if (new_dist < dist[v]) {
                dist[v] = new_dist;
                pq.push({new_dist, v});
            }
        }
    }

    return INF;
}

int main() {
    const unsigned NODE_COUNT = 7;

    // Directed graph containing:
    // - Multiple possible routes
    // - A pair of parallel edges (0 -> 1)
    // - A one-way connection into node 6
    // - Unreachable source-destination pairs

    std::vector<unsigned> tail = {
        0, 0, 1, 1, 2, 2, 3,
        3, 4, 4, 5, 4, 0
    };

    std::vector<unsigned> head = {
        1, 2, 2, 3, 3, 4, 4,
        5, 5, 1, 0, 6, 1
    };

    std::vector<unsigned> weights(tail.size(), 1);

    std::vector<unsigned> order = {0, 1, 2, 3, 4, 5, 6};

    // Preprocessing happens only once.
    RoutingKit::CustomizableContractionHierarchy cch(
        order, tail, head
    );

    RoutingKit::CustomizableContractionHierarchyMetric metric(
        cch, weights
    );

    RoutingKit::CustomizableContractionHierarchyQuery query(metric);

    // Fixed seed gives reproducible test results.
    std::mt19937 rng(20261024);
    std::uniform_int_distribution<unsigned> weight_distribution(1, 40);

    unsigned passed = 0;

    // 25 different weight configurations.
    for (unsigned round = 0; round < 25; ++round) {

        // Simulate changes in road traversal costs.
        for (unsigned& w : weights) {
            w = weight_distribution(rng);
        }

        // Update the hierarchy metric, not its topology.
        metric.customize();

        // Verify every possible source-target pair.
        for (unsigned source = 0; source < NODE_COUNT; ++source) {
            for (unsigned target = 0; target < NODE_COUNT; ++target) {

                unsigned expected = dijkstra(
                    source, target, NODE_COUNT,
                    tail, head, weights
                );

                query.reset()
                     .add_source(source)
                     .add_target(target)
                     .run();

                unsigned actual = query.get_distance();

                if (expected != actual) {
                    std::cerr
                        << "[FAIL] Round " << round
                        << ", source " << source
                        << ", target " << target
                        << ", Dijkstra " << expected
                        << ", CCH " << actual << '\n';

                    return 1;
                }

                // For reachable pairs, verify that the
                // reconstructed path is valid and has the
                // same cost reported by CCH.
                if (actual != RoutingKit::inf_weight) {
                    std::vector<unsigned> arcs = query.get_arc_path();

                    unsigned current = source;
                    unsigned total_cost = 0;

                    for (unsigned edge_id : arcs) {
                        if (edge_id >= tail.size() ||
                            tail[edge_id] != current) {
                            std::cerr << "[FAIL] Invalid path edge.\n";
                            return 1;
                        }

                        current = head[edge_id];
                        total_cost += weights[edge_id];
                    }

                    if (current != target || total_cost != actual) {
                        std::cerr << "[FAIL] Path cost or destination mismatch.\n";
                        return 1;
                    }
                }

                ++passed;
            }
        }
    }

    std::cout << "[PASS] All CCH distances match Dijkstra.\n";
    std::cout << "[PASS] Path reconstruction is consistent.\n";
    std::cout << "[PASS] 25 metric customizations verified.\n";
    std::cout << "[PASS] " << passed << " source-target checks passed.\n";

    return 0;
}
