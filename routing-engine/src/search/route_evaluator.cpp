/*
 * File: route_evaluator.cpp
 * Purpose: Validates directed routes and accumulates physical metrics.
 * Status: IMPLEMENTED
 * Remaining: Full geometry assembly belongs to the runtime response layer.
 */

#include "route_evaluator.hpp"

#include <cmath>

namespace ecoroute {
namespace {

bool validMetrics(const RouteMetrics& metrics) {
    for (double value : {metrics.duration_seconds, metrics.distance_meters,
                         metrics.fuel_milliliters, metrics.electricity_wh,
                         metrics.co2e_grams}) {
        if (!std::isfinite(value) || value < 0.0) return false;
    }
    return true;
}

} // namespace

Result<RouteMetrics> evaluateRouteMetrics(
    const ShortestPath& path, const RoutingGraph& graph,
    const std::vector<RouteMetrics>& arc_metrics,
    unsigned source, unsigned target) {
    if (source >= graph.nodeCount() || target >= graph.nodeCount()) {
        return Result<RouteMetrics>::failure(ErrorCode::InvalidArgument,
            "Route evaluation endpoint is outside the graph.");
    }
    if (arc_metrics.size() != graph.arcCount()) {
        return Result<RouteMetrics>::failure(ErrorCode::InvalidMetric,
            "Route evaluation metrics must match the CSR arc count.");
    }
    RouteMetrics total{};
    unsigned current = source;
    for (unsigned arc : path.arc_indices) {
        if (arc >= graph.arcCount() || graph.tail()[arc] != current) {
            return Result<RouteMetrics>::failure(ErrorCode::InvalidGraph,
                "Route contains a disconnected or invalid directed arc.");
        }
        const auto& metrics = arc_metrics[arc];
        if (!validMetrics(metrics)) {
            return Result<RouteMetrics>::failure(ErrorCode::InvalidMetric,
                "Invalid physical metrics in reconstructed path.");
        }
        total.duration_seconds += metrics.duration_seconds;
        total.distance_meters += metrics.distance_meters;
        total.fuel_milliliters += metrics.fuel_milliliters;
        total.electricity_wh += metrics.electricity_wh;
        total.co2e_grams += metrics.co2e_grams;
        if (!validMetrics(total)) {
            return Result<RouteMetrics>::failure(ErrorCode::InvalidMetric,
                "Accumulated route metrics overflowed.");
        }
        current = graph.head()[arc];
    }
    if (current != target) {
        return Result<RouteMetrics>::failure(ErrorCode::InvalidGraph,
            "Reconstructed route does not reach destination.");
    }
    return Result<RouteMetrics>::success(total);
}

} // namespace ecoroute
