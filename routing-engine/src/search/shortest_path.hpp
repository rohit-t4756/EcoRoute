
#pragma once

/*
 * File: shortest_path.hpp
 * Purpose: Defines the common shortest-path result returned by
 *          routing algorithms such as Dijkstra and CCH.
 * Status: VERIFIED
 * Remaining: Verify through routing algorithm tests.
 */

#include <vector>

namespace ecoroute {

struct ShortestPath {

    // Indices into RoutingGraph's CSR arc arrays.
    // These are not external database segment IDs.
    std::vector<unsigned> arc_indices;

    // Total path weight, in the same units as the input weights.
    unsigned total_cost = 0;
};

} // namespace ecoroute
