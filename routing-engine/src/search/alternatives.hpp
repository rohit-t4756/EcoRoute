
#pragma once

/*
 * File: alternative_routes.hpp
 * Purpose: Defines generation of a small internal route pool
 *          around the user's selected time/emissions preference.
 *
 * Status: VERIFIED
 * Remaining: Real-network candidate quality and prefetch calibration.
 *
 * Product behavior:
 * - Fastest route: time preference = 1.0.
 * - Selected route: exact user-selected preference.
 * - Eco route: time preference = 0.0.
 * - Optional nearby preferences warm the candidate pool.
 * - Frontend displays up to THREE unique relevant routes.
 *
 * Caching is deliberately outside this C++ component.
 */

#include "ecoroute/result.hpp"
#include "ecoroute/types.hpp"

#include "cost/weight_builder.hpp"
#include "search/shortest_path.hpp"

#include <cstddef>
#include <functional>
#include <vector>

namespace ecoroute {

class RoutingGraph;
class CchAdapter;

struct ObjectiveScore {
    double time_preference = 0.0;
    unsigned total_cost = 0;
    double integer_scale = 0.0;
    double maximum_path_objective_error = 0.0;
};

struct ObjectivePath {
    ShortestPath path;
    double integer_scale = 0.0;
    double maximum_path_objective_error = 0.0;
};

// Optional runtime hook for immutable, cached objective metrics. The provider
// must use the exact requested preference and the supplied graph/arc metrics.
using ObjectiveQuery = std::function<Result<ObjectivePath>(double)>;

// One unique path generated for one or more preferences.
struct RouteCandidate {

    // Original CSR arc indices. total_cost belongs to the first objective;
    // use objective_scores for every preference associated with this path.
    ShortestPath path;

    // Actual physical metrics summed along the path.
    RouteMetrics totals{};

    // Each sampled preference that generated this path.
    // Multiple preferences may select the same path.
    std::vector<double> matching_preferences;

    std::vector<ObjectiveScore> objective_scores;
};

struct AlternativeRouteOptions {

    // Full CSR topology validation is needed for public callers. The Engine
    // has already bound its immutable graph at construction, so can skip it.
    bool verify_graph_identity = true;

    // User-selected time importance, in [0, 1].
    double selected_time_preference = 0.5;

    // Sample nearby preferences for possible caching.
    // Example: selected 0.60, step 0.05 ->
    // neighbors 0.55 and 0.65.
    double neighboring_preference_step = 0.05;

    // Number of sample steps on EACH side.
    // Default: one lower and one higher.
    unsigned neighboring_steps_each_side = 1;

    // Maximum number of distinct paths in the pool.
    // Includes the fastest, selected and eco paths.
    std::size_t max_pool_routes = 5;

    // Detour bound for additional nearby candidates.
    //
    // The three requested preference routes are retained
    // even when an extreme eco route exceeds this bound.
    //
    // The caller should surface excessive detours
    // appropriately to the user.
    double max_detour_factor = 1.5;
};

// Generates an internal pool of CCH route candidates.
//
// Preference schedule:
//   1.0
//   selected_time_preference
//   0.0
//   nearby sampled preferences
//
// The implementation must:
//   1. Validate inputs and options.
//   2. Deduplicate repeated preference values.
//   3. Build integer weights for each preference.
//   4. Customize CCH and run shortest-path queries.
//   5. Deduplicate identical arc paths.
//   6. Accumulate original physical route metrics.
//   7. Preserve the fastest, selected and eco results.
//   8. Filter dominated/unreasonable EXTRA candidates.
//   9. Respect max_pool_routes.
//
// Each default-provider metric/query operation is synchronized by CchAdapter.
// Immutable metrics supplied by the runtime hook can run independently.
//
// Redis caching is not implemented here.

Result<std::vector<RouteCandidate>> generateAlternativeRoutes(
    CchAdapter& cch,
    const RoutingGraph& graph,
    const std::vector<RouteMetrics>& arc_metrics,
    unsigned source,
    unsigned target,
    const WeightBuilderOptions& weight_options,
    const AlternativeRouteOptions& options = {},
    const ObjectiveQuery& query_objective = {}
);

} // namespace ecoroute
