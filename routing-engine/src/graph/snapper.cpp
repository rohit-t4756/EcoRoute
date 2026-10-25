/*
 * File: snapper.cpp
 * Purpose: Performs nearest-node queries using a balanced unit-sphere k-d tree.
 * Status: IMPLEMENTED
 * Remaining: Directed-edge projection and access/turn-aware snapping.
 */

#include "snapper.hpp"

#include <algorithm>
#include <cmath>
#include <new>
#include <utility>

namespace ecoroute {
namespace {

constexpr double pi = 3.14159265358979323846;
constexpr double earth_radius_meters = 6371008.8;

bool validCoordinate(const Coordinate& point) {
    return std::isfinite(point.latitude) && std::isfinite(point.longitude) &&
           point.latitude >= -90.0 && point.latitude <= 90.0 &&
           point.longitude >= -180.0 && point.longitude <= 180.0;
}

std::array<double, 3> unitPosition(const Coordinate& point) {
    // Canonicalize equivalent WGS84 positions, including zero-radius queries.
    if (point.latitude == 90.0) return {0.0, 0.0, 1.0};
    if (point.latitude == -90.0) return {0.0, 0.0, -1.0};
    const double latitude = point.latitude * pi / 180.0;
    const double longitude = (point.longitude == 180.0 ? -180.0 : point.longitude) * pi / 180.0;
    const double latitude_cosine = std::cos(latitude);
    return {latitude_cosine * std::cos(longitude),
            latitude_cosine * std::sin(longitude), std::sin(latitude)};
}

} // namespace

Result<NodeSnapper> NodeSnapper::create(const RoadNetwork& network, const RoutingGraph& graph) {
    if (network.nodeCount() != graph.nodeCount() || network.segmentCount() != graph.arcCount()) {
        return Result<NodeSnapper>::failure(ErrorCode::InvalidGraph, "Snapper network/CSR dimensions differ.");
    }
    try {
        NodeSnapper snapper;
        snapper.entries_.resize(graph.nodeCount());
        for (std::size_t index = 0; index < graph.nodeCount(); ++index) {
            const auto* node = network.findNode(graph.nodeIds()[index]);
            if (!node) {
                return Result<NodeSnapper>::failure(ErrorCode::InvalidGraph, "Snapper cannot resolve a CSR node.");
            }
            auto& entry = snapper.entries_[index];
            entry.result = {static_cast<unsigned>(index), node->id, node->location, 0.0};
            entry.unit_position = unitPosition(node->location);
        }
        for (std::size_t arc = 0; arc < graph.arcCount(); ++arc) {
            const auto* segment = network.findSegment(graph.segmentIds()[arc]);
            const unsigned tail = graph.tail()[arc];
            const unsigned head = graph.head()[arc];
            if (!segment || segment->from != graph.nodeIds()[tail] || segment->to != graph.nodeIds()[head]) {
                return Result<NodeSnapper>::failure(ErrorCode::InvalidGraph, "Snapper network/CSR edge mapping differs.");
            }
            if (tail != head) {
                snapper.entries_[tail].outgoing = true;
                snapper.entries_[head].incoming = true;
            }
        }
        snapper.root_ = snapper.build(0, snapper.entries_.size(), 0);
        return Result<NodeSnapper>::success(std::move(snapper));
    } catch (const std::bad_alloc&) {
        return Result<NodeSnapper>::failure(ErrorCode::ResourceExhausted, "Cannot allocate node snapping index.");
    }
}

std::size_t NodeSnapper::build(std::size_t begin, std::size_t end, unsigned depth) {
    if (begin == end) return no_node;
    const std::size_t middle = begin + (end - begin) / 2;
    const unsigned axis = depth % 3;
    std::nth_element(entries_.begin() + begin, entries_.begin() + middle,
                     entries_.begin() + end, [axis](const Entry& left, const Entry& right) {
        if (left.unit_position[axis] != right.unit_position[axis]) {
            return left.unit_position[axis] < right.unit_position[axis];
        }
        return left.result.node_id < right.result.node_id;
    });
    entries_[middle].axis = axis;
    entries_[middle].left = build(begin, middle, depth + 1);
    entries_[middle].right = build(middle + 1, end, depth + 1);
    return middle;
}

void NodeSnapper::nearest(std::size_t index, const std::array<double, 3>& position,
                         SnapRole role, double& best_squared, std::size_t& best) const {
    if (index == no_node) return;
    const auto& entry = entries_[index];
    double squared = 0.0;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const double delta = entry.unit_position[axis] - position[axis];
        squared += delta * delta;
    }
    const bool eligible = role == SnapRole::Any ||
        (role == SnapRole::Source && entry.outgoing) ||
        (role == SnapRole::Target && entry.incoming);
    if (eligible && squared <= best_squared &&
        (best == no_node || squared < best_squared ||
         entry.result.node_id < entries_[best].result.node_id)) {
        best = index;
        best_squared = squared;
    }
    const double plane_delta = position[entry.axis] - entry.unit_position[entry.axis];
    const std::size_t near_child = plane_delta < 0.0 ? entry.left : entry.right;
    const std::size_t far_child = plane_delta < 0.0 ? entry.right : entry.left;
    nearest(near_child, position, role, best_squared, best);
    if (plane_delta * plane_delta <= best_squared) {
        nearest(far_child, position, role, best_squared, best);
    }
}

Result<SnapResult> NodeSnapper::snap(const Coordinate& coordinate,
                                  double max_distance_meters, SnapRole role) const {
    if (!validCoordinate(coordinate) || !std::isfinite(max_distance_meters) ||
        max_distance_meters < 0.0 ||
        (role != SnapRole::Any && role != SnapRole::Source && role != SnapRole::Target)) {
        return Result<SnapResult>::failure(ErrorCode::InvalidArgument, "Invalid coordinate, snap radius or role.");
    }
    // Chord distance is monotonic in great-circle distance up to half a sphere.
    // Working in 3D avoids longitude wrap and polar singularities.
    const double angle = std::min(pi, max_distance_meters / earth_radius_meters);
    const double chord = 2.0 * std::sin(angle / 2.0);
    double best_squared = chord * chord;
    std::size_t best = no_node;
    nearest(root_, unitPosition(coordinate), role, best_squared, best);
    if (best == no_node) {
        return Result<SnapResult>::failure(ErrorCode::SnapFailed, "No eligible graph node within the snap radius.");
    }
    SnapResult result = entries_[best].result;
    result.distance_meters = 2.0 * earth_radius_meters *
        std::asin(std::min(1.0, std::sqrt(best_squared) / 2.0));
    return Result<SnapResult>::success(result);
}

} // namespace ecoroute
