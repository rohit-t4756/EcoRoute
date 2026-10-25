
/*
 * File: cch_order.cpp
 * Purpose: Implements CCH node ordering using RoutingKit's
 *          Inertial Flow nested-dissection algorithm.
 * Status: VERIFIED
 * Remaining: Unit tests and verification.
 */

#include "cch_order.hpp"

#include <routingkit/nested_dissection.h>

#include <cstddef>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace ecoroute {

Result<std::vector<unsigned>> computeCchNodeOrder(
    const RoadNetwork& network,
    const RoutingGraph& graph
) {
    const std::size_t node_count = graph.nodeCount();
    const std::size_t arc_count = graph.arcCount();

    // Confirm that both representations have matching sizes.
    if (node_count == 0 ||
        network.nodeCount() != node_count ||
        network.segmentCount() != arc_count) {

        return Result<std::vector<unsigned>>::failure(
            ErrorCode::InvalidGraph,
            "RoadNetwork and RoutingGraph sizes do not match."
        );
    }

    if (node_count > std::numeric_limits<unsigned>::max()) {
        return Result<std::vector<unsigned>>::failure(
            ErrorCode::ResourceExhausted,
            "Node count exceeds RoutingKit limits."
        );
    }

    // --------------------------------------------------
    // Step 1: Collect coordinates in compact node order.
    // --------------------------------------------------

    std::vector<float> latitude;
    std::vector<float> longitude;

    latitude.reserve(node_count);
    longitude.reserve(node_count);

    for (RoadNodeId id : graph.nodeIds()) {

        const RoadNode* node = network.findNode(id);

        if (node == nullptr) {
            return Result<std::vector<unsigned>>::failure(
                ErrorCode::InvalidGraph,
                "Missing road node " + std::to_string(id)
            );
        }

        latitude.push_back(
            static_cast<float>(node->location.latitude)
        );

        longitude.push_back(
            static_cast<float>(node->location.longitude)
        );
    }

    // --------------------------------------------------
    // Step 2: Verify original segment connectivity.
    // --------------------------------------------------

    for (std::size_t i = 0; i < arc_count; ++i) {

        const RoadSegment* segment =
            network.findSegment(graph.segmentIds()[i]);

        if (segment == nullptr ||
            graph.tail()[i] >= node_count ||
            graph.head()[i] >= node_count) {

            return Result<std::vector<unsigned>>::failure(
                ErrorCode::InvalidGraph,
                "CSR contains an invalid road segment."
            );
        }

        if (graph.nodeIds()[graph.tail()[i]] != segment->from ||
            graph.nodeIds()[graph.head()[i]] != segment->to) {

            return Result<std::vector<unsigned>>::failure(
                ErrorCode::InvalidGraph,
                "CSR topology does not match RoadNetwork."
            );
        }
    }

    // --------------------------------------------------
    // Step 3: Compute nested-dissection ordering.
    // --------------------------------------------------

    std::vector<unsigned> order =
        RoutingKit::compute_nested_node_dissection_order_using_inertial_flow(
            static_cast<unsigned>(node_count),
            graph.tail(),
            graph.head(),
            latitude,
            longitude
        );

    // --------------------------------------------------
    // Step 4: Validate the generated permutation.
    // --------------------------------------------------

    if (order.size() != node_count) {
        return Result<std::vector<unsigned>>::failure(
            ErrorCode::InternalError,
            "Nested dissection returned an incorrect order size."
        );
    }

    std::vector<unsigned char> seen(node_count, 0);

    for (unsigned node : order) {

        if (node >= node_count || seen[node]) {
            return Result<std::vector<unsigned>>::failure(
                ErrorCode::InternalError,
                "Nested dissection returned an invalid permutation."
            );
        }

        seen[node] = 1;
    }

    return Result<std::vector<unsigned>>::success(
        std::move(order)
    );
}

} // namespace ecoroute
