
#pragma once

/*
 * File: weight_builder.hpp
 * Purpose: Converts per-segment physical metrics into
 *          nonnegative integer CCH optimization weights.
 *
 * Status: VERIFIED
 * Remaining: Calibrate normalization and assess real-network ranking precision.
 *
 * Important:
 * - Input metrics must follow RoutingGraph CSR arc order.
 * - Time and emissions are normalized before combination.
 * - All output weights must be compatible with RoutingKit.
 * - Road class affects weights only through the upstream
 *   cost model, once calibrated support is introduced.
 */

#include "ecoroute/result.hpp"
#include "ecoroute/types.hpp"

#include <vector>
#include <cstddef>

namespace ecoroute {

// --------------------------------------------------
// CCH weight-conversion configuration.
// --------------------------------------------------

struct WeightBuilderOptions {

    // Reference travel time used for normalization.
    // Must be finite and strictly positive.
    double reference_time_seconds = 0.0;

    // Reference emissions used for normalization.
    // Must be finite and strictly positive.
    double reference_co2e_grams = 0.0;

    // Scaling factor for converting the normalized
    // floating-point objective into an integer.
    //
    // Must be positive and chosen with RoutingKit's
    // finite-distance limit in mind.
    unsigned integer_scale = 1000000u;
};

// --------------------------------------------------
// Generate weights for one optimization preference.
// --------------------------------------------------

// Parameters:
//
// arc_metrics:
//   Physical metrics for every directed CSR arc.
//
// time_preference:
//   1.0 = optimize travel time only.
//   0.0 = optimize estimated CO2e only.
//   0.5 = equal weight after normalization.
//
// options:
//   Reference quantities and integer scaling.
//
// Returns:
//   One nonnegative integer weight per CSR arc.
//
// The fixed-scale implementation:
//   - Validate physical metrics.
//   - Reject invalid objective parameters.
//   - Reject nonfinite or unrepresentable costs.
//   - Preserve genuine zero-cost edges.
//   - Round positive costs consistently.
//   - Enforce RoutingKit's per-edge weight limit.
//
// NOTE:
// Per-edge validation does not guarantee that every
// possible route sum fits RoutingKit's distance range.
// Use buildGraphSafeCchArcWeights for a graph-wide finite-path guarantee.

Result<std::vector<unsigned>> buildCchArcWeights(
    const std::vector<RouteMetrics>& arc_metrics,
    double time_preference,
    const WeightBuilderOptions& options
);

// A common graph-wide scale preserves the relative arc objective. Every
// nonnegative shortest path has a simple representative with <= node_count-1
// arcs. We bound that sum, including rounding, below RoutingKit infinity.
// This conservative bound may reduce precision more than a particular route
// needs; the reported error is <= (node_count-1)/effective_integer_scale.
struct CchWeightSet {
    std::vector<unsigned> arc_weights;
    double effective_integer_scale = 0.0;
    double maximum_path_objective_error = 0.0;
};

Result<CchWeightSet> buildGraphSafeCchArcWeights(
    const std::vector<RouteMetrics>& arc_metrics,
    double time_preference,
    const WeightBuilderOptions& options,
    std::size_t node_count
);

} // namespace ecoroute
