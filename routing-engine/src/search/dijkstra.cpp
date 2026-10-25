
/*
 * File: dijkstra.cpp
 * Purpose: Implements the reference Dijkstra shortest-path algorithm
 *          on EcoRoute's directed CSR graph.
 * Status: VERIFIED
 * Remaining: Unit tests, correctness verification and CCH comparison.
 */

#include "dijkstra.hpp"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <limits>
#include <queue>
#include <string>
#include <utility>
#include <vector>

namespace ecoroute {

Result<ShortestPath> dijkstraShortestPath(
    const RoutingGraph& graph,
    const std::vector<unsigned>& weights,
    unsigned source,
    unsigned target
) {
    const std::size_t node_count = graph.nodeCount();
    const std::size_t arc_count = graph.arcCount();

    // RoutingKit reserves 2147483647 as infinity.
    // Finite input weights and final path costs must be smaller.
    constexpr unsigned kCchInfinity = 2147483647u;

    // Use 64-bit distances internally to avoid unsigned overflow.
    constexpr std::uint64_t INF =
        std::numeric_limits<std::uint64_t>::max();

    // No valid arc index can equal this sentinel because
    // RoutingGraph limits the number of arcs to unsigned max.
    constexpr unsigned NO_ARC =
        std::numeric_limits<unsigned>::max();

    // --------------------------------------------------
    // Step 1: Validate inputs.
    // --------------------------------------------------

    if (source >= node_count || target >= node_count) {
        return Result<ShortestPath>::failure(
            ErrorCode::InvalidArgument,
            "Source or target node index is out of range."
        );
    }

    if (weights.size() != arc_count) {
        return Result<ShortestPath>::failure(
            ErrorCode::InvalidMetric,
            "Weight count does not match CSR arc count."
        );
    }

    for (std::size_t i = 0; i < weights.size(); ++i) {
        if (weights[i] >= kCchInfinity) {
            return Result<ShortestPath>::failure(
                ErrorCode::InvalidMetric,
                "Unsupported weight at arc " +
                    std::to_string(i)
            );
        }
    }

    // --------------------------------------------------
    // Step 2: Initialize distances and predecessors.
    // --------------------------------------------------

    std::vector<std::uint64_t> distance(node_count, INF);

    // predecessor_arc[v] stores the CSR arc used to
    // reach node v along the current shortest path.
    std::vector<unsigned> predecessor_arc(node_count, NO_ARC);

    // Each queue entry contains:
    // {distance from source, node index}
    using QueueItem = std::pair<std::uint64_t, unsigned>;

    // std::greater creates a min-priority queue.
    std::priority_queue<
        QueueItem,
        std::vector<QueueItem>,
        std::greater<QueueItem>
    > pq;

    distance[source] = 0;
    pq.push({0, source});

    const auto& first_out = graph.firstOut();
    const auto& head = graph.head();
    const auto& tail = graph.tail();

    // --------------------------------------------------
    // Step 3: Run Dijkstra.
    // --------------------------------------------------

    while (!pq.empty()) {

        auto [current_distance, u] = pq.top();
        pq.pop();

        // Ignore outdated queue entries.
        if (current_distance != distance[u]) {
            continue;
        }

        // The target now has its optimal distance.
        if (u == target) {
            break;
        }

        // Visit every outgoing CSR arc of node u.
        for (unsigned arc = first_out[u];
             arc < first_out[u + 1];
             ++arc) {

            unsigned v = head[arc];
            unsigned weight = weights[arc];

            // Protect against overflow in distance addition.
            if (current_distance > INF - weight) {
                continue;
            }

            std::uint64_t new_distance =
                current_distance + weight;

            // Relax this directed edge.
            if (new_distance < distance[v]) {

                distance[v] = new_distance;

                // Remember the exact arc, not just its source node.
                predecessor_arc[v] = arc;

                pq.push({new_distance, v});
            }
        }
    }

    // --------------------------------------------------
    // Step 4: Handle unreachable or oversized paths.
    // --------------------------------------------------

    if (distance[target] == INF) {
        return Result<ShortestPath>::failure(
            ErrorCode::NoRoute,
            "No directed path exists between source and target."
        );
    }

    if (distance[target] >= kCchInfinity) {
        return Result<ShortestPath>::failure(
            ErrorCode::ResourceExhausted,
            "Shortest-path cost exceeds the supported CCH range."
        );
    }

    // --------------------------------------------------
    // Step 5: Reconstruct the exact shortest path.
    // --------------------------------------------------

    ShortestPath path;
    path.total_cost = static_cast<unsigned>(distance[target]);

    unsigned current = target;

    while (current != source) {

        unsigned arc = predecessor_arc[current];

        // Defensive check: a reachable node should always
        // have a predecessor unless it is the source.
        if (arc == NO_ARC) {
            return Result<ShortestPath>::failure(
                ErrorCode::InternalError,
                "Shortest-path predecessor information is incomplete."
            );
        }

        path.arc_indices.push_back(arc);

        // Follow this arc backward to its source node.
        current = tail[arc];
    }

    // Predecessors were followed from target to source.
    // Reverse them into travel order.
    std::reverse(
        path.arc_indices.begin(),
        path.arc_indices.end()
    );

    return Result<ShortestPath>::success(std::move(path));
}

} // namespace ecoroute
