
#pragma once

/*
 * File: cch_order.hpp
 * Purpose: Generates a nested-dissection contraction order
 *          from EcoRoute's road network and CSR graph.
 * Status: VERIFIED
 * Remaining: Implement, test and verify ordering.
 */

#include "ecoroute/result.hpp"
#include "graph/road_network.hpp"
#include "graph/routing_graph.hpp"

#include <vector>

namespace ecoroute {

// Computes a RoutingKit-compatible nested-dissection order.
//
// The returned vector is a permutation of compact node indices.
// Coordinates are obtained from the physical RoadNetwork.
//
// This is a preprocessing operation, not a per-query operation.
Result<std::vector<unsigned>> computeCchNodeOrder(
    const RoadNetwork& network,
    const RoutingGraph& graph
);

} // namespace ecoroute
