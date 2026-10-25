#pragma once

/*
 * File: route_evaluator.hpp
 * Purpose: Declares route validation and metric evaluation.
 * Status: IMPLEMENTED
 * Remaining: None for additive physical route evaluation.
 */

#include "ecoroute/result.hpp"
#include "ecoroute/types.hpp"
#include "graph/routing_graph.hpp"
#include "search/shortest_path.hpp"

#include <vector>

namespace ecoroute {

// Validates directed arc continuity and accumulates original physical metrics.
// Objective-specific integer search scores never enter the displayed totals.
Result<RouteMetrics> evaluateRouteMetrics(
    const ShortestPath& path,
    const RoutingGraph& graph,
    const std::vector<RouteMetrics>& arc_metrics,
    unsigned source,
    unsigned target
);

} // namespace ecoroute
