
/*
 * File: road_network.cpp
 * Purpose: Implements validated construction and efficient lookup
 *          of physical road nodes and directed road segments.
 * Status: IMPLEMENTED
 */

#include "road_network.hpp"

#include <cmath>
#include <string>
#include <utility>

namespace ecoroute {

namespace {

// Geographic coordinates must be finite and within WGS84 ranges.
bool isValidCoordinate(const Coordinate& coordinate) {
    return std::isfinite(coordinate.latitude) &&
           std::isfinite(coordinate.longitude) &&
           coordinate.latitude >= -90.0 &&
           coordinate.latitude <= 90.0 &&
           coordinate.longitude >= -180.0 &&
           coordinate.longitude <= 180.0;
}

bool matchesEndpoint(const Coordinate& point, const Coordinate& node) {
    // +180 and -180 denote the same meridian.
    const double longitude_difference =
        std::fabs(std::remainder(point.longitude - node.longitude, 360.0));
    return std::fabs(point.latitude - node.latitude) <=
               GEOMETRY_ENDPOINT_TOLERANCE_DEGREES &&
           longitude_difference <= GEOMETRY_ENDPOINT_TOLERANCE_DEGREES;
}

} // namespace

Result<RoadNetwork> RoadNetwork::create(
    std::vector<RoadNode> nodes,
    std::vector<RoadSegment> segments
) {
    // Isolated-node snapshots are valid: source=self has an empty route,
    // while different isolated endpoints have no route.
    if (nodes.empty()) {
        return Result<RoadNetwork>::failure(
            ErrorCode::InvalidGraph,
            "Road network must contain at least one node."
        );
    }

    RoadNetwork network;

    // Allocate lookup-map capacity in advance.
    network.node_index_.reserve(nodes.size());
    network.segment_index_.reserve(segments.size());

    // --------------------------------------------------
    // Validate road nodes.
    // --------------------------------------------------

    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const RoadNode& node = nodes[i];

        if (!isValidCoordinate(node.location)) {
            return Result<RoadNetwork>::failure(
                ErrorCode::InvalidGraph,
                "Invalid coordinates for node " +
                    std::to_string(node.id)
            );
        }

        if (node.elevation_meters.has_value() &&
            !std::isfinite(*node.elevation_meters)) {
            return Result<RoadNetwork>::failure(
                ErrorCode::InvalidGraph,
                "Invalid elevation for node " +
                    std::to_string(node.id)
            );
        }

        // emplace inserts only if the ID is not already present.
        auto inserted = network.node_index_.emplace(node.id, i);

        if (!inserted.second) {
            return Result<RoadNetwork>::failure(
                ErrorCode::InvalidGraph,
                "Duplicate node ID: " + std::to_string(node.id)
            );
        }
    }

    // --------------------------------------------------
    // Validate directed road segments.
    // --------------------------------------------------

    for (std::size_t i = 0; i < segments.size(); ++i) {
        const RoadSegment& segment = segments[i];

        // Every segment must reference existing nodes.
        if (network.node_index_.find(segment.from) ==
                network.node_index_.end() ||
            network.node_index_.find(segment.to) ==
                network.node_index_.end()) {

            return Result<RoadNetwork>::failure(
                ErrorCode::InvalidGraph,
                "Segment " + std::to_string(segment.id) +
                    " references an unknown node."
            );
        }

        // Length must be a finite positive number.
        if (!std::isfinite(segment.length_meters) ||
            segment.length_meters <= 0.0) {

            return Result<RoadNetwork>::failure(
                ErrorCode::InvalidGraph,
                "Invalid length for segment " +
                    std::to_string(segment.id)
            );
        }

        // Speed limit is optional, but must be valid if present.
        if (segment.speed_limit_kph.has_value()) {
            double speed = *segment.speed_limit_kph;

            if (!std::isfinite(speed) || speed <= 0.0) {
                return Result<RoadNetwork>::failure(
                    ErrorCode::InvalidGraph,
                    "Invalid speed limit for segment " +
                        std::to_string(segment.id)
                );
            }
        }

        // Geometry may be absent. If present, it needs at least
        // two valid geographic points.
        if (!segment.geometry.empty()) {
            if (segment.geometry.size() < 2) {
                return Result<RoadNetwork>::failure(
                    ErrorCode::InvalidGraph,
                    "Insufficient geometry for segment " +
                        std::to_string(segment.id)
                );
            }

            for (const Coordinate& point : segment.geometry) {
                if (!isValidCoordinate(point)) {
                    return Result<RoadNetwork>::failure(
                        ErrorCode::InvalidGraph,
                        "Invalid geometry for segment " +
                            std::to_string(segment.id)
                    );
                }
            }

            const Coordinate& source =
                nodes[network.node_index_.at(segment.from)].location;
            const Coordinate& target =
                nodes[network.node_index_.at(segment.to)].location;
            if (!matchesEndpoint(segment.geometry.front(), source) ||
                !matchesEndpoint(segment.geometry.back(), target)) {
                return Result<RoadNetwork>::failure(
                    ErrorCode::InvalidGraph,
                    "Geometry does not follow directed endpoints for segment " +
                        std::to_string(segment.id)
                );
            }
        }

        auto inserted =
            network.segment_index_.emplace(segment.id, i);

        if (!inserted.second) {
            return Result<RoadNetwork>::failure(
                ErrorCode::InvalidGraph,
                "Duplicate segment ID: " +
                    std::to_string(segment.id)
            );
        }
    }

    // All validation succeeded.
    // Transfer ownership without copying the entire vectors.
    network.nodes_ = std::move(nodes);
    network.segments_ = std::move(segments);

    return Result<RoadNetwork>::success(std::move(network));
}

// --------------------------------------------------
// Lookup functions.
// --------------------------------------------------

const RoadNode* RoadNetwork::findNode(RoadNodeId id) const {
    auto it = node_index_.find(id);

    if (it == node_index_.end()) {
        return nullptr;
    }

    return &nodes_[it->second];
}

const RoadSegment* RoadNetwork::findSegment(RoadSegmentId id) const {
    auto it = segment_index_.find(id);

    if (it == segment_index_.end()) {
        return nullptr;
    }

    return &segments_[it->second];
}

// --------------------------------------------------
// Read-only accessors.
// --------------------------------------------------

const std::vector<RoadNode>& RoadNetwork::nodes() const {
    return nodes_;
}

const std::vector<RoadSegment>& RoadNetwork::segments() const {
    return segments_;
}

std::size_t RoadNetwork::nodeCount() const {
    return nodes_.size();
}

std::size_t RoadNetwork::segmentCount() const {
    return segments_.size();
}

} // namespace ecoroute
