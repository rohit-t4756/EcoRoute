/*
 * File: weight_builder.cpp
 * Purpose: Converts physical objectives into integer CCH weights, with an
 *          explicit graph-wide scale and conservative quantization bound.
 * Status: IMPLEMENTED
 * Remaining: Calibrate normalization and benchmark ranking on real networks.
 */

#include "weight_builder.hpp"
#include <routingkit/constants.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace ecoroute {
namespace {

bool nonnegativeFinite(double value) {
    return std::isfinite(value) && value >= 0.0;
}

bool validMetrics(const RouteMetrics& metrics) {
    return nonnegativeFinite(metrics.duration_seconds) &&
        nonnegativeFinite(metrics.distance_meters) &&
        nonnegativeFinite(metrics.fuel_milliliters) &&
        nonnegativeFinite(metrics.electricity_wh) &&
        nonnegativeFinite(metrics.co2e_grams);
}

Result<std::vector<long double>> objectives(
    const std::vector<RouteMetrics>& metrics, double preference,
    const WeightBuilderOptions& options) {
    if (!std::isfinite(preference) || preference < 0.0 || preference > 1.0) {
        return Result<std::vector<long double>>::failure(ErrorCode::InvalidArgument,
            "Time preference must be between 0 and 1.");
    }
    if (!std::isfinite(options.reference_time_seconds) || options.reference_time_seconds <= 0.0 ||
        !std::isfinite(options.reference_co2e_grams) || options.reference_co2e_grams <= 0.0 ||
        options.integer_scale == 0) {
        return Result<std::vector<long double>>::failure(ErrorCode::InvalidArgument,
            "Invalid weight normalization configuration.");
    }
    std::vector<long double> values;
    values.reserve(metrics.size());
    for (std::size_t arc = 0; arc < metrics.size(); ++arc) {
        const auto& metric = metrics[arc];
        if (!validMetrics(metric)) {
            return Result<std::vector<long double>>::failure(ErrorCode::InvalidMetric,
                "Invalid physical metrics at arc " + std::to_string(arc));
        }
        // Skip inactive dimensions so 0 * overflow cannot poison an endpoint
        // objective. Extended intermediates also protect tiny positive costs.
        long double objective = 0.0L;
        if (preference > 0.0) {
            objective += static_cast<long double>(preference) * metric.duration_seconds /
                         options.reference_time_seconds;
        }
        if (preference < 1.0) {
            objective += (1.0L - static_cast<long double>(preference)) * metric.co2e_grams /
                         options.reference_co2e_grams;
        }
        if (!std::isfinite(objective) || objective < 0.0L) {
            return Result<std::vector<long double>>::failure(ErrorCode::InvalidMetric,
                "Invalid normalized objective at arc " + std::to_string(arc));
        }
        values.push_back(objective);
    }
    return Result<std::vector<long double>>::success(std::move(values));
}

Result<std::vector<unsigned>> convert(const std::vector<long double>& values,
                                      long double scale) {
    std::vector<unsigned> weights;
    weights.reserve(values.size());
    for (std::size_t arc = 0; arc < values.size(); ++arc) {
        const long double scaled = values[arc] * scale;
        if (!std::isfinite(scaled)) {
            return Result<std::vector<unsigned>>::failure(ErrorCode::ResourceExhausted,
                "Nonfinite scaled weight at arc " + std::to_string(arc));
        }
        long double rounded = std::round(scaled);
        if (values[arc] > 0.0L && rounded == 0.0L) rounded = 1.0L;
        if (rounded >= RoutingKit::inf_weight) {
            return Result<std::vector<unsigned>>::failure(ErrorCode::ResourceExhausted,
                "CCH weight exceeds RoutingKit limit at arc " + std::to_string(arc));
        }
        weights.push_back(static_cast<unsigned>(rounded));
    }
    return Result<std::vector<unsigned>>::success(std::move(weights));
}

} // namespace

Result<std::vector<unsigned>> buildCchArcWeights(
    const std::vector<RouteMetrics>& arc_metrics, double time_preference,
    const WeightBuilderOptions& options) {
    auto values = objectives(arc_metrics, time_preference, options);
    if (!values.ok()) return Result<std::vector<unsigned>>::failure(values.error().code, values.error().message);
    return convert(values.value(), options.integer_scale);
}

Result<CchWeightSet> buildGraphSafeCchArcWeights(
    const std::vector<RouteMetrics>& arc_metrics, double time_preference,
    const WeightBuilderOptions& options, std::size_t node_count) {
    if (node_count == 0) {
        return Result<CchWeightSet>::failure(ErrorCode::InvalidArgument,
            "Graph-safe weights require at least one node.");
    }
    auto values = objectives(arc_metrics, time_preference, options);
    if (!values.ok()) return Result<CchWeightSet>::failure(values.error().code, values.error().message);

    long double sum = 0.0L;
    long double maximum = 0.0L;
    std::size_t positive_arcs = 0;
    for (long double value : values.value()) {
        sum += value;
        maximum = std::max(maximum, value);
        if (value > 0.0L) ++positive_arcs;
    }
    const std::size_t path_arcs = std::min(node_count - 1, positive_arcs);
    if (path_arcs >= RoutingKit::inf_weight - 1u) {
        return Result<CchWeightSet>::failure(ErrorCode::ResourceExhausted,
            "Too many positive arcs to guarantee finite CCH path costs.");
    }

    // A simple path uses each arc at most once and at most n-1 arcs. Retain
    // the tighter of these two inexpensive conservative upper bounds.
    const long double path_bound = std::min(sum, maximum * path_arcs);
    // Also bound a single edge, including self-loops on a one-node graph.
    const long double bound = std::max(path_bound, maximum);
    const std::size_t rounding_arcs = std::max<std::size_t>(path_arcs, maximum > 0.0L ? 1 : 0);
    long double scale = options.integer_scale;
    if (bound > 0.0L) {
        const long double budget = static_cast<long double>(RoutingKit::inf_weight - 1u) - rounding_arcs;
        scale = std::min(scale, budget / bound);
    }
    // Report and use the same representable scale, rounded down when reduced.
    double effective = static_cast<double>(scale);
    if (static_cast<long double>(effective) > scale) {
        effective = std::nextafter(effective, 0.0);
    }
    if (!std::isfinite(effective) || effective <= 0.0) {
        return Result<CchWeightSet>::failure(ErrorCode::ResourceExhausted,
            "The graph-wide objective scale is not representable.");
    }
    auto weights = convert(values.value(), effective);
    if (!weights.ok()) return Result<CchWeightSet>::failure(weights.error().code, weights.error().message);

    CchWeightSet result;
    result.arc_weights = std::move(weights.value());
    result.effective_integer_scale = effective;
    // Positive-to-one rounding has error less than one integer unit per arc.
    // Zero-cost arcs are exact and do not contribute to this bound.
    result.maximum_path_objective_error = static_cast<double>(path_arcs) / effective;
    if (!std::isfinite(result.maximum_path_objective_error)) {
        return Result<CchWeightSet>::failure(ErrorCode::ResourceExhausted,
            "The graph-wide quantization error bound is not representable.");
    }
    return Result<CchWeightSet>::success(std::move(result));
}

} // namespace ecoroute
