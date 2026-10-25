
#pragma once

/*
 * File: routing_graph.hpp
 * Purpose: Defines a compact directed CSR graph derived from a
 *          validated physical RoadNetwork.
 * Status: VERIFIED
 * Remaining: Implement CSR construction, mapping and unit tests.
 */

#include "road_network.hpp"

#include "ecoroute/result.hpp"

#include <cstddef>
#include <optional>
#include <unordered_map>
#include <vector>

namespace ecoroute {

class RoutingGraph {
public:

    // Converts a validated physical network into compact CSR form.
    static Result<RoutingGraph> create(
        const RoadNetwork& network
    );

    // Find the compact index corresponding to an external node ID.
    // Returns an empty optional when the node does not exist.
    std::optional<unsigned> findNodeIndex(
        RoadNodeId id
    ) const;

    // Original node IDs in compact-index order.
    const std::vector<RoadNodeId>& nodeIds() const;

    // CSR adjacency offsets. Size = node count + 1.
    const std::vector<unsigned>& firstOut() const;

    // Source and destination indices for directed arcs.
    const std::vector<unsigned>& tail() const;
    const std::vector<unsigned>& head() const;

    // Original road segment IDs, aligned with CSR arc indices.
    const std::vector<RoadSegmentId>& segmentIds() const;

    std::size_t nodeCount() const;
    std::size_t arcCount() const;

private:

    // Construction must go through create().
    RoutingGraph() = default;

    // Compact index -> original road node ID.
    std::vector<RoadNodeId> node_ids_;

    // Original road node ID -> compact index.
    std::unordered_map<RoadNodeId, unsigned> node_index_;

    // CSR adjacency representation.
    std::vector<unsigned> first_out_;
    std::vector<unsigned> tail_;
    std::vector<unsigned> head_;

    // CSR arc index -> physical road segment ID.
    std::vector<RoadSegmentId> segment_ids_;
};

} // namespace ecoroute
