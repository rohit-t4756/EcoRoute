
#pragma once

/*
 * File: cch_adapter.hpp
 * Purpose: Provides EcoRoute's interface to RoutingKit CCH
 *          for preprocessing, weight customization and queries.
 * Status: VERIFIED
 * Remaining: Large-network throughput and persistence benchmarks.
 */

#include "ecoroute/result.hpp"
#include "graph/routing_graph.hpp"
#include "search/shortest_path.hpp"

#include <memory>
#include <vector>

namespace ecoroute {

class CchAdapter {
public:

    // Builds a CCH hierarchy for the supplied directed graph.
    //
    // node_order: A permutation of [0, nodeCount).
    // arc_weights: Integer weights in CSR arc order.
    static Result<CchAdapter> create(
        const RoutingGraph& graph,
        std::vector<unsigned> node_order,
        std::vector<unsigned> arc_weights
    );

    // Replaces the arc weights and re-customizes CCH.
    // The graph topology and contraction order remain unchanged.
    Result<void> updateWeights(
        std::vector<unsigned> arc_weights
    );

    // Creates an independent metric/query sharing the immutable hierarchy.
    Result<CchAdapter> withWeights(std::vector<unsigned> arc_weights) const;

    // A metric replacement and its query form one indivisible operation.
    // Separate updateWeights/shortestPath calls do not form a transaction.
    Result<ShortestPath> shortestPathWithWeights(
        unsigned source, unsigned target, std::vector<unsigned> arc_weights
    );

    // Checks the CSR arc ordering used by this hierarchy.
    bool matchesGraph(const RoutingGraph& graph) const;

    // Returns a shortest path using original CSR arc indices.
    // Calls and updates are synchronized; a reusable query workspace serializes
    // searches on one adapter. Independent adapters can search in parallel.
    // Source and target are compact RoutingGraph node indices.
    Result<ShortestPath> shortestPath(
        unsigned source,
        unsigned target
    ) const;

    // Owns internal RoutingKit objects through a stable allocation.
    ~CchAdapter();

    // Moving is allowed, copying is prohibited.
    CchAdapter(CchAdapter&&) noexcept;
    CchAdapter& operator=(CchAdapter&&) noexcept;

    CchAdapter(const CchAdapter&) = delete;
    CchAdapter& operator=(const CchAdapter&) = delete;

private:

    // Internal implementation containing RoutingKit's hierarchy
    // and its metric. Defined in cch_adapter.cpp.
    struct Impl;

    std::unique_ptr<Impl> impl_;

    explicit CchAdapter(std::unique_ptr<Impl> impl);
};

} // namespace ecoroute
