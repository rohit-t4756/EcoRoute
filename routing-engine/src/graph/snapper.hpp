#pragma once

/*
 * File: snapper.hpp
 * Purpose: Indexes graph nodes for bounded nearest-node endpoint snapping.
 * Status: IMPLEMENTED
 * Remaining: Directed-edge projection, legal access and component-aware snapping.
 */

#include "graph/road_network.hpp"
#include "graph/routing_graph.hpp"

#include <array>
#include <cstddef>
#include <limits>
#include <vector>

namespace ecoroute {

enum class SnapRole { Any, Source, Target };

struct SnapResult {
    unsigned node_index = 0;
    RoadNodeId node_id = 0;
    Coordinate location;
    double distance_meters = 0.0;
};

class NodeSnapper {
public:
    static Result<NodeSnapper> create(
        const RoadNetwork& network,
        const RoutingGraph& graph
    );

    // Source/Target require an outgoing/incoming non-self arc. Any includes
    // isolated nodes and is useful when resolving a zero-length request.
    // Eligibility does not establish connectivity between the chosen endpoints.
    Result<SnapResult> snap(
        const Coordinate& coordinate,
        double max_distance_meters,
        SnapRole role = SnapRole::Any
    ) const;

private:
    static constexpr std::size_t no_node = std::numeric_limits<std::size_t>::max();
    struct Entry {
        SnapResult result;
        std::array<double, 3> unit_position{};
        bool outgoing = false;
        bool incoming = false;
        unsigned axis = 0;
        std::size_t left = no_node;
        std::size_t right = no_node;
    };

    NodeSnapper() = default;
    std::size_t build(std::size_t begin, std::size_t end, unsigned depth);
    void nearest(std::size_t entry, const std::array<double, 3>& position,
                 SnapRole role, double& best_squared, std::size_t& best) const;
    std::vector<Entry> entries_;
    std::size_t root_ = no_node;
};

} // namespace ecoroute
