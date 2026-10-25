
/*
 * File: routing_graph.cpp
 * Purpose: Converts a validated physical road network into
 *          a compact directed CSR graph with external ID mappings.
 * Status: VERIFIED
 * Remaining: Unit tests and correctness verification.
 */

#include "routing_graph.hpp"

#include <limits>
#include <utility>

namespace ecoroute {

Result<RoutingGraph> RoutingGraph::create(
    const RoadNetwork& network
) {
    const auto& nodes = network.nodes();
    const auto& segments = network.segments();

    // RoutingKit uses unsigned integers for node and arc indices.
    // Reject graphs that cannot fit into this representation.
    const std::size_t max_index =
        std::numeric_limits<unsigned>::max();

    if (nodes.size() > max_index ||
        segments.size() > max_index) {

        return Result<RoutingGraph>::failure(
            ErrorCode::ResourceExhausted,
            "Road network exceeds supported CSR index limits."
        );
    }

    RoutingGraph graph;

    // --------------------------------------------------
    // Step 1: Map external node IDs to compact indices.
    // --------------------------------------------------

    graph.node_ids_.reserve(nodes.size());
    graph.node_index_.reserve(nodes.size());

    for (std::size_t i = 0; i < nodes.size(); ++i) {

        RoadNodeId id = nodes[i].id;
        unsigned index = static_cast<unsigned>(i);

        graph.node_ids_.push_back(id);
        graph.node_index_.emplace(id, index);
    }

    // --------------------------------------------------
    // Step 2: Count outgoing arcs for every node.
    // --------------------------------------------------

    // first_out[i + 1] initially stores the outgoing
    // degree of node i.
    graph.first_out_.assign(nodes.size() + 1, 0);

    for (const RoadSegment& segment : segments) {

        unsigned source = graph.node_index_.at(segment.from);

        ++graph.first_out_[source + 1];
    }

    // --------------------------------------------------
    // Step 3: Convert degrees into CSR prefix sums.
    // --------------------------------------------------

    for (std::size_t i = 1; i < graph.first_out_.size(); ++i) {
        graph.first_out_[i] += graph.first_out_[i - 1];
    }

    // Now outgoing arcs of node u occupy:
    //
    // [first_out[u], first_out[u + 1])
    //
    // The final entry equals the total number of arcs.

    // --------------------------------------------------
    // Step 4: Allocate the compact arc arrays.
    // --------------------------------------------------

    const std::size_t arc_count = segments.size();

    graph.tail_.resize(arc_count);
    graph.head_.resize(arc_count);
    graph.segment_ids_.resize(arc_count);

    // Tracks the next available position for each node.
    // Keep first_out unchanged because it defines CSR ranges.
    std::vector<unsigned> next_out = graph.first_out_;

    // --------------------------------------------------
    // Step 5: Place every directed segment into CSR order.
    // --------------------------------------------------

    for (const RoadSegment& segment : segments) {

        unsigned source = graph.node_index_.at(segment.from);
        unsigned target = graph.node_index_.at(segment.to);

        unsigned position = next_out[source]++;

        graph.tail_[position] = source;
        graph.head_[position] = target;
        graph.segment_ids_[position] = segment.id;
    }

    return Result<RoutingGraph>::success(std::move(graph));
}

// --------------------------------------------------
// External node ID -> compact node index.
// --------------------------------------------------

std::optional<unsigned> RoutingGraph::findNodeIndex(
    RoadNodeId id
) const {
    auto it = node_index_.find(id);

    if (it == node_index_.end()) {
        return std::nullopt;
    }

    return it->second;
}

// --------------------------------------------------
// Read-only accessors.
// --------------------------------------------------

const std::vector<RoadNodeId>& RoutingGraph::nodeIds() const {
    return node_ids_;
}

const std::vector<unsigned>& RoutingGraph::firstOut() const {
    return first_out_;
}

const std::vector<unsigned>& RoutingGraph::tail() const {
    return tail_;
}

const std::vector<unsigned>& RoutingGraph::head() const {
    return head_;
}

const std::vector<RoadSegmentId>& RoutingGraph::segmentIds() const {
    return segment_ids_;
}

std::size_t RoutingGraph::nodeCount() const {
    return node_ids_.size();
}

std::size_t RoutingGraph::arcCount() const {
    return head_.size();
}

} // namespace ecoroute
