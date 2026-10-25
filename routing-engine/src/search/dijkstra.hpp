
#pragma once

/*
 * File: dijkstra.hpp
 * Purpose: Declares the reference shortest-path algorithm
 *          operating on EcoRoute's directed CSR graph.
 * Status: VERIFIED
 * Remaining: Implement algorithm and verify through tests.
 */

#include "graph/routing_graph.hpp"
#include "shortest_path.hpp"

#include "ecoroute/result.hpp"

#include <vector>

namespace ecoroute {

// Finds the minimum-cost path from source to target.
//
// Parameters:
// - graph: Validated directed CSR graph.
// - weights: Nonnegative integer costs, one per CSR arc.
// - source: Compact source-node index.
// - target: Compact destination-node index.
//
// Returns:
// - ShortestPath containing CSR arc indices and total cost.
// - An error for invalid inputs, weights or unreachable targets.
//
// Edge costs must use one consistent unit and remain within
// the supported integer range.
Result<ShortestPath> dijkstraShortestPath(
    const RoutingGraph& graph,
    const std::vector<unsigned>& weights,
    unsigned source,
    unsigned target
);

} // namespace ecoroute
