
/*
 * File: alternatives.cpp
 * Purpose: Generates fastest, selected-preference and eco routes,
 *          with optional nearby preference samples.
 *
 * Status: VERIFIED
 * Remaining: Real-network candidate quality and prefetch benchmarking.
 *
 * Note:
 * The default provider changes/queries the CCH metric atomically. Runtime
 * callers may supply a provider backed by independent cached metrics.
 */

#include "alternatives.hpp"

#include "graph/routing_graph.hpp"
#include "search/cch_adapter.hpp"
#include "search/route_evaluator.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace ecoroute {

namespace {



// One objective we want to query.
struct PreferenceRequest {
    double value;
    bool essential;
};

bool validNonnegative(double value) {
    return std::isfinite(value) && value >= 0.0;
}

bool validMetrics(const RouteMetrics& m) {
    return validNonnegative(m.duration_seconds) &&
           validNonnegative(m.distance_meters) &&
           validNonnegative(m.fuel_milliliters) &&
           validNonnegative(m.electricity_wh) &&
           validNonnegative(m.co2e_grams);
}

// Add a preference only if we haven't already scheduled it.
void addPreference(
    std::vector<PreferenceRequest>& requests,
    double preference,
    bool essential
) {
    for (auto& request : requests) {
        if (request.value == preference) {

            request.essential =
                request.essential || essential;

            return;
        }
    }

    requests.push_back({preference, essential});
}

// A dominates B if A is no worse in time and emissions,
// and strictly better in at least one.
bool dominates(
    const RouteCandidate& a,
    const RouteCandidate& b
) {
    bool no_worse =
        a.totals.duration_seconds <=
            b.totals.duration_seconds &&
        a.totals.co2e_grams <=
            b.totals.co2e_grams;

    bool strictly_better =
        a.totals.duration_seconds <
            b.totals.duration_seconds ||
        a.totals.co2e_grams <
            b.totals.co2e_grams;

    return no_worse && strictly_better;
}

// Find the closest preference represented by this route.
double preferenceDistance(
    const RouteCandidate& candidate,
    double selected
) {
    double best = 2.0;

    for (double preference : candidate.matching_preferences) {
        best = std::min(
            best,
            std::fabs(preference - selected)
        );
    }

    return best;
}

} // namespace

// --------------------------------------------------
// Generate the internal route candidate pool.
// --------------------------------------------------

Result<std::vector<RouteCandidate>> generateAlternativeRoutes(
    CchAdapter& cch,
    const RoutingGraph& graph,
    const std::vector<RouteMetrics>& arc_metrics,
    unsigned source,
    unsigned target,
    const WeightBuilderOptions& weight_options,
    const AlternativeRouteOptions& options,
    const ObjectiveQuery& query_objective
) {
    using CandidateResult =
        Result<std::vector<RouteCandidate>>;

    // --------------------------------------------------
    // 1. Validate inputs.
    // --------------------------------------------------

    if (source >= graph.nodeCount() ||
        target >= graph.nodeCount()) {

        return CandidateResult::failure(
            ErrorCode::InvalidArgument,
            "Source or target node index is invalid."
        );
    }

    if (options.verify_graph_identity && !cch.matchesGraph(graph)) {
        return CandidateResult::failure(ErrorCode::InvalidGraph,
            "CCH topology does not match the supplied CSR graph.");
    }

    if (arc_metrics.size() != graph.arcCount()) {
        return CandidateResult::failure(
            ErrorCode::InvalidArgument,
            "Physical metrics must match CSR arc count."
        );
    }

    for (const RouteMetrics& metrics : arc_metrics) {
        if (!validMetrics(metrics)) {
            return CandidateResult::failure(
                ErrorCode::InvalidMetric,
                "Invalid physical segment metrics."
            );
        }
    }

    if (!std::isfinite(options.selected_time_preference) ||
        options.selected_time_preference < 0.0 ||
        options.selected_time_preference > 1.0) {

        return CandidateResult::failure(
            ErrorCode::InvalidArgument,
            "Selected preference must be in [0, 1]."
        );
    }

    if (!std::isfinite(options.neighboring_preference_step) ||
        options.neighboring_preference_step <= 0.0 ||
        options.neighboring_preference_step > 1.0) {

        return CandidateResult::failure(
            ErrorCode::InvalidArgument,
            "Neighboring preference step must be in (0, 1]."
        );
    }

    if (options.neighboring_steps_each_side > 10u) {
        return CandidateResult::failure(
            ErrorCode::InvalidArgument,
            "Too many neighboring preference samples."
        );
    }

    // We reserve capacity for up to three essential paths.
    if (options.max_pool_routes < 3) {
        return CandidateResult::failure(
            ErrorCode::InvalidArgument,
            "Candidate pool must allow at least three routes."
        );
    }

    if (!std::isfinite(options.max_detour_factor) ||
        options.max_detour_factor < 1.0) {

        return CandidateResult::failure(
            ErrorCode::InvalidArgument,
            "Detour factor must be finite and at least 1."
        );
    }

    // --------------------------------------------------
    // 2. Build the preference schedule.
    // --------------------------------------------------

    std::vector<PreferenceRequest> requests;

    // Essential objectives are evaluated first.
    addPreference(requests, 1.0, true);

    addPreference(
        requests,
        options.selected_time_preference,
        true
    );

    addPreference(requests, 0.0, true);

    // Record where optional preferences begin.
    const std::size_t essential_count = requests.size();

    // If the pool can contain only essential paths,
    // avoid unnecessary neighboring CCH customizations.
    if (options.max_pool_routes > essential_count) {

        for (unsigned step = 1;
             step <= options.neighboring_steps_each_side;
             ++step) {

            double offset =
                static_cast<double>(step) *
                options.neighboring_preference_step;

            double lower =
                options.selected_time_preference - offset;

            double higher =
                options.selected_time_preference + offset;

            if (lower >= 0.0) {
                addPreference(requests, lower, false);
            }

            if (higher <= 1.0) {
                addPreference(requests, higher, false);
            }
        }
    }

    // --------------------------------------------------
    // 3. Generate CCH routes for these preferences.
    // --------------------------------------------------

    std::vector<RouteCandidate> candidates;

    // Tracks which paths correspond to essential results.
    std::vector<bool> essential_candidate;

    for (const PreferenceRequest& request : requests) {

        auto query = [&]() -> Result<ObjectivePath> {
            if (query_objective) return query_objective(request.value);
            auto weights = buildGraphSafeCchArcWeights(
                arc_metrics, request.value, weight_options, graph.nodeCount());
            if (!weights.ok()) {
                return Result<ObjectivePath>::failure(weights.error().code, weights.error().message);
            }
            auto path = cch.shortestPathWithWeights(
                source, target, std::move(weights.value().arc_weights));
            if (!path.ok()) return Result<ObjectivePath>::failure(path.error().code, path.error().message);
            return Result<ObjectivePath>::success({std::move(path.value()),
                weights.value().effective_integer_scale,
                weights.value().maximum_path_objective_error});
        }();

        if (!query.ok()) {
            return CandidateResult::failure(
                query.error().code,
                "CCH route query failed: " +
                    query.error().message
            );
        }

        if (!std::isfinite(query.value().integer_scale) || query.value().integer_scale <= 0.0 ||
            std::isnan(query.value().maximum_path_objective_error) ||
            query.value().maximum_path_objective_error < 0.0) {
            return CandidateResult::failure(ErrorCode::InvalidMetric,
                "Objective provider returned invalid numerical scale/error metadata.");
        }
        const ObjectiveScore score{request.value, query.value().path.total_cost,
            query.value().integer_scale, query.value().maximum_path_objective_error};

        // Check whether this exact arc path already exists.
        auto existing = std::find_if(
            candidates.begin(),
            candidates.end(),
            [&](const RouteCandidate& candidate) {
                return candidate.path.arc_indices ==
                       query.value().path.arc_indices;
            }
        );

        if (existing != candidates.end()) {

            // Different preferences can produce the same route.
            existing->matching_preferences.push_back(
                request.value
            );
            existing->objective_scores.push_back(score);

            std::size_t index =
                static_cast<std::size_t>(
                    existing - candidates.begin()
                );

            essential_candidate[index] =
                essential_candidate[index] ||
                request.essential;

            continue;
        }

        // New unique route: calculate its physical totals.
        auto totals = evaluateRouteMetrics(
            query.value().path,
            graph,
            arc_metrics,
            source,
            target
        );

        if (!totals.ok()) {
            return CandidateResult::failure(
                totals.error().code,
                totals.error().message
            );
        }

        RouteCandidate candidate;

        candidate.path = std::move(query.value().path);
        candidate.totals = std::move(totals.value());

        candidate.matching_preferences.push_back(
            request.value
        );
        candidate.objective_scores.push_back(score);

        candidates.push_back(std::move(candidate));
        essential_candidate.push_back(request.essential);
    }

    if (candidates.empty()) {
        return CandidateResult::failure(
            ErrorCode::NoRoute,
            "No route candidates were generated."
        );
    }

    // --------------------------------------------------
    // 4. Determine the fastest-route baseline.
    // --------------------------------------------------

    // Quantization can make the time-only result slightly slower in physical
    // seconds than another discovered path. Use the best observed duration
    // for optional detour filtering, without claiming an exact real optimum.
    double fastest_time = candidates.front().totals.duration_seconds;
    for (const auto& candidate : candidates) {
        fastest_time = std::min(fastest_time, candidate.totals.duration_seconds);
    }

    // --------------------------------------------------
    // 5. Preserve all essential unique routes.
    // --------------------------------------------------

    std::vector<RouteCandidate> selected_routes;
    selected_routes.reserve(
        std::min(options.max_pool_routes, candidates.size())
    );

    for (std::size_t i = 0; i < candidates.size(); ++i) {
        if (essential_candidate[i]) {
            selected_routes.push_back(candidates[i]);
        }
    }

    // --------------------------------------------------
    // 6. Filter optional neighboring candidates.
    // --------------------------------------------------

    std::vector<std::size_t> useful_extras;

    for (std::size_t i = 0; i < candidates.size(); ++i) {

        if (essential_candidate[i]) {
            continue;
        }

        const RouteCandidate& candidate = candidates[i];

        // Apply the detour limit to OPTIONAL paths only.
        //
        // An extreme eco route is kept even if it is
        // slower than this threshold.
        if ((fastest_time == 0.0 && candidate.totals.duration_seconds > 0.0) ||
            (fastest_time > 0.0 && candidate.totals.duration_seconds / fastest_time >
                options.max_detour_factor)) {

            continue;
        }

        bool dominated = false;

        for (std::size_t j = 0; j < candidates.size(); ++j) {

            if (i == j) {
                continue;
            }

            if (dominates(candidates[j], candidate)) {
                dominated = true;
                break;
            }
        }

        if (!dominated) {
            useful_extras.push_back(i);
        }
    }

    // Prefer extras whose sampled preferences are closest
    // to the user's selected preference.
    std::stable_sort(
        useful_extras.begin(),
        useful_extras.end(),
        [&](std::size_t a, std::size_t b) {
            return preferenceDistance(
                candidates[a],
                options.selected_time_preference
            ) < preferenceDistance(
                candidates[b],
                options.selected_time_preference
            );
        }
    );

    // --------------------------------------------------
    // 7. Fill remaining pool slots with useful extras.
    // --------------------------------------------------

    for (std::size_t index : useful_extras) {

        if (selected_routes.size() >= options.max_pool_routes) {
            break;
        }

        selected_routes.push_back(candidates[index]);
    }

    // Essential routes remain first, followed by extras.
    // The frontend will choose at most three unique paths
    // corresponding to fastest, selected and eco.
    return CandidateResult::success(
        std::move(selected_routes)
    );
}

} // namespace ecoroute

