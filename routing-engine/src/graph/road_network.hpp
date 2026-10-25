
#pragma once

/*
 * File: road_network.hpp
 * Purpose: Defines the physical directed road network used by
 *          the EcoRoute engine, independently of routing algorithms.
 * Status: IMPLEMENTED
 */

#include "ecoroute/result.hpp"
#include "ecoroute/types.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace ecoroute {

// Unique identifier of a road node within a graph version.
using RoadNodeId = std::uint64_t;

// Export rounding tolerance, in degrees per coordinate (about 0.11 m
// in latitude). Detailed shapes must follow the directed endpoints.
inline constexpr double GEOMETRY_ENDPOINT_TOLERANCE_DEGREES = 1e-6;

// Represents a geographic road-network node.
struct RoadNode {
    RoadNodeId id = 0;
    Coordinate location;

    // Empty when elevation data is unavailable.
    std::optional<double> elevation_meters;
};

// Represents a road that can be traversed from 'from' to 'to'.
struct RoadSegment {
    RoadSegmentId id = 0;

    RoadNodeId from = 0;
    RoadNodeId to = 0;

    double length_meters = 0.0;

    // Empty when no reliable speed limit is available.
    std::optional<double> speed_limit_kph;

    // Optional road classification supplied by the data pipeline.
    // Its exact vocabulary will be agreed upon with the team.
    std::optional<std::string> road_class;

    // Geographic shape in the permitted travel direction.
    // An empty vector means detailed geometry is unavailable. Nonempty
    // shapes start at 'from' and end at 'to' within the tolerance above.
    std::vector<Coordinate> geometry;
};

// Owns and provides read-only access to a validated road network.
class RoadNetwork {
public:

    // Creates a network only if nodes and segments are valid.
    static Result<RoadNetwork> create(
        std::vector<RoadNode> nodes,
        std::vector<RoadSegment> segments
    );

    // Returns nullptr when an ID is not present.
    const RoadNode* findNode(RoadNodeId id) const;
    const RoadSegment* findSegment(RoadSegmentId id) const;

    // Read-only access to stored data.
    const std::vector<RoadNode>& nodes() const;
    const std::vector<RoadSegment>& segments() const;

    std::size_t nodeCount() const;
    std::size_t segmentCount() const;

private:

    // Construction is restricted to the validating factory.
    RoadNetwork() = default;

    std::vector<RoadNode> nodes_;
    std::vector<RoadSegment> segments_;

    // External IDs mapped to their vector positions.
    std::unordered_map<RoadNodeId, std::size_t> node_index_;
    std::unordered_map<RoadSegmentId, std::size_t> segment_index_;
};

} // namespace ecoroute
