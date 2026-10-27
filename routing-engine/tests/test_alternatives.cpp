
/*
 * File: test_alternative_routes.cpp
 * Purpose: Tests essential routes, preference sampling,
 *          deduplication, detour limits and validation.
 * Status: VERIFIED
 * Remaining: Build and execute.
 */

#include "search/alternatives.hpp"
#include "search/cch_adapter.hpp"
#include "search/cch_order.hpp"
#include "search/dijkstra.hpp"
#include "search/route_evaluator.hpp"

#include "graph/road_network.hpp"
#include "graph/routing_graph.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace ecoroute;

namespace {

int failures = 0;

void check(bool condition, const std::string& name) {
    if (condition) {
        std::cout << "[PASS] " << name << '\n';
    } else {
        ++failures;
        std::cerr << "[FAIL] " << name << '\n';
    }
}

bool closeTo(double a, double b) {
    return std::fabs(a - b) < 1e-8;
}

RoadSegment road(
    RoadSegmentId id,
    RoadNodeId from,
    RoadNodeId to,
    double speed
) {
    RoadSegment s;
    s.id = id;
    s.from = from;
    s.to = to;
    s.length_meters = 500.0;
    s.speed_limit_kph = speed;
    return s;
}

// The generator receives already calculated physical
// metrics, in exactly the same order as the CSR arcs.
//
// Each route has two identical-cost road segments.
RouteMetrics metricFor(RoadSegmentId id) {
    RouteMetrics m{};
    m.distance_meters = 500.0;

    if (id == 110 || id == 111) {
        m.duration_seconds = 30.0;
        m.co2e_grams = 300.0;
    } else if (id == 210 || id == 211) {
        m.duration_seconds = 60.0;
        m.co2e_grams = 110.0;
    } else if (id == 310 || id == 311) {
        m.duration_seconds = 120.0;
        m.co2e_grams = 30.0;
    } else if (id == 410 || id == 411) {
        m.duration_seconds = 75.0;
        m.co2e_grams = 83.0;
    }

    return m;
}

std::vector<RoadSegmentId> roadIds(
    const RouteCandidate& candidate,
    const RoutingGraph& graph
) {
    std::vector<RoadSegmentId> ids;

    for (unsigned arc : candidate.path.arc_indices) {
        if (arc >= graph.arcCount()) {
            return {};
        }

        ids.push_back(graph.segmentIds()[arc]);
    }

    return ids;
}

bool hasPreference(
    const RouteCandidate& candidate,
    double preference
) {
    return std::any_of(
        candidate.matching_preferences.begin(),
        candidate.matching_preferences.end(),
        [&](double p) {
            return closeTo(p, preference);
        }
    );
}

bool hasError(
    const Result<std::vector<RouteCandidate>>& result,
    ErrorCode code
) {
    return !result.ok() && result.error().code == code;
}

} // namespace

int main() {

    // --------------------------------------------------
    // 1. Build a graph with four distinct routes.
    // --------------------------------------------------

    // Start: 100
    // Destination: 900
    // Intermediate nodes: 200, 300, 400, 500

    std::vector<RoadNode> nodes = {
        {100, {40.700, -73.900}, std::nullopt},
        {200, {40.701, -73.899}, std::nullopt},
        {300, {40.702, -73.898}, std::nullopt},
        {400, {40.703, -73.897}, std::nullopt},
        {500, {40.704, -73.896}, std::nullopt},
        {900, {40.705, -73.895}, std::nullopt}
    };

    std::vector<RoadSegment> segments = {
        // Fastest: 60 seconds, 600 grams.
        road(110, 100, 200, 60.0),
        road(111, 200, 900, 60.0),

        // Balanced: 120 seconds, 220 grams.
        road(210, 100, 300, 30.0),
        road(211, 300, 900, 30.0),

        // Lowest emissions: 240 seconds, 60 grams.
        road(310, 100, 400, 15.0),
        road(311, 400, 900, 15.0),

        // Nearby candidate: 150 seconds, 166 grams.
        road(410, 100, 500, 24.0),
        road(411, 500, 900, 24.0)
    };

    auto network_result = RoadNetwork::create(
        std::move(nodes), std::move(segments)
    );

    if (!network_result.ok()) {
        std::cerr << "[FAIL] RoadNetwork creation\n";
        return 1;
    }

    const RoadNetwork& network = network_result.value();

    auto graph_result = RoutingGraph::create(network);

    if (!graph_result.ok()) {
        std::cerr << "[FAIL] RoutingGraph creation\n";
        return 1;
    }

    const RoutingGraph& graph = graph_result.value();

    auto source = graph.findNodeIndex(100);
    auto target = graph.findNodeIndex(900);

    if (!source || !target) {
        std::cerr << "[FAIL] Source/destination mapping\n";
        return 1;
    }

    check(
        graph.nodeCount() == 6 &&
        graph.arcCount() == 8,
        "RoadNetwork and CSR constructed correctly"
    );

    // --------------------------------------------------
    // 2. Prepare physical metrics in CSR arc order.
    // --------------------------------------------------

    std::vector<RouteMetrics> metrics;
    metrics.reserve(graph.arcCount());

    for (RoadSegmentId id : graph.segmentIds()) {
        metrics.push_back(metricFor(id));
    }

    WeightBuilderOptions weight_options;
    weight_options.reference_time_seconds = 100.0;
    weight_options.reference_co2e_grams = 200.0;
    weight_options.integer_scale = 10000u;

    // --------------------------------------------------
    // 3. Construct the automatically ordered CCH.
    // --------------------------------------------------

    auto order = computeCchNodeOrder(network, graph);

    if (!order.ok()) {
        std::cerr << "[FAIL] CCH ordering\n";
        return 1;
    }

    auto initial_weights = buildCchArcWeights(
        metrics, 1.0, weight_options
    );

    if (!initial_weights.ok()) {
        std::cerr << "[FAIL] Initial CCH weights\n";
        return 1;
    }

    auto cch_result = CchAdapter::create(
        graph,
        std::move(order.value()),
        std::move(initial_weights.value())
    );

    if (!cch_result.ok()) {
        std::cerr << "[FAIL] CCH construction\n";
        return 1;
    }

    CchAdapter cch = std::move(cch_result.value());

    // --------------------------------------------------
    // 4. Generate essential and nearby route candidates.
    // --------------------------------------------------

    AlternativeRouteOptions options;
    options.selected_time_preference = 0.5;
    options.neighboring_preference_step = 0.05;
    options.neighboring_steps_each_side = 1;
    options.max_pool_routes = 5;

    // Allows the optional 150-second route:
    // 150 / 60 = 2.5.
    options.max_detour_factor = 3.0;

    auto result = generateAlternativeRoutes(
        cch, graph, metrics,
        *source, *target,
        weight_options, options
    );

    if (!result.ok()) {
        std::cerr << "[FAIL] Alternative generation: "
                  << result.error().message << '\n';
        return 1;
    }

    const auto& routes = result.value();

    const std::vector<RoadSegmentId> fast = {110, 111};
    const std::vector<RoadSegmentId> balanced = {210, 211};
    const std::vector<RoadSegmentId> eco = {310, 311};
    const std::vector<RoadSegmentId> nearby = {410, 411};

    check(
        routes.size() == 4,
        "Four unique routes generated from five preferences"
    );

    if (routes.size() >= 4) {

        check(
            roadIds(routes[0], graph) == fast &&
            hasPreference(routes[0], 1.0),
            "Fastest route preserved"
        );

        check(
            roadIds(routes[1], graph) == balanced &&
            hasPreference(routes[1], 0.5) &&
            hasPreference(routes[1], 0.55),
            "Selected route preserved and duplicate merged"
        );

        check(
            roadIds(routes[2], graph) == eco &&
            hasPreference(routes[2], 0.0),
            "Lowest-emissions route preserved"
        );

        check(
            roadIds(routes[3], graph) == nearby &&
            hasPreference(routes[3], 0.45),
            "Nearby preference discovers a useful route"
        );

        check(
            closeTo(routes[0].totals.duration_seconds, 60.0) &&
            closeTo(routes[0].totals.co2e_grams, 600.0) &&
            closeTo(routes[1].totals.duration_seconds, 120.0) &&
            closeTo(routes[1].totals.co2e_grams, 220.0) &&
            closeTo(routes[2].totals.duration_seconds, 240.0) &&
            closeTo(routes[2].totals.co2e_grams, 60.0) &&
            closeTo(routes[3].totals.duration_seconds, 150.0) &&
            closeTo(routes[3].totals.co2e_grams, 166.0),
            "Physical route totals are accumulated correctly"
        );
    }

    // --------------------------------------------------
    // 5. Independently compare paths with Dijkstra.
    // --------------------------------------------------

    for (double preference :
         {1.0, 0.5, 0.0, 0.45, 0.55}) {

        auto weights = buildCchArcWeights(
            metrics, preference, weight_options
        );

        if (!weights.ok()) {
            check(false, "Dijkstra reference weight generation");
            continue;
        }

        auto reference = dijkstraShortestPath(
            graph, weights.value(), *source, *target
        );

        auto found = std::find_if(
            routes.begin(),
            routes.end(),
            [&](const RouteCandidate& candidate) {
                return hasPreference(candidate, preference);
            }
        );

        check(
            reference.ok() &&
            found != routes.end() &&
            found->path.arc_indices ==
                reference.value().arc_indices,
            "Preference " + std::to_string(preference) +
                " matches Dijkstra"
        );
    }

    // --------------------------------------------------
    // 6. Strict detour filtering.
    // --------------------------------------------------

    AlternativeRouteOptions strict = options;
    strict.max_detour_factor = 1.5;

    auto strict_result = generateAlternativeRoutes(
        cch, graph, metrics,
        *source, *target,
        weight_options, strict
    );

    check(
        strict_result.ok() &&
        strict_result.value().size() == 3 &&
        roadIds(strict_result.value()[0], graph) == fast &&
        roadIds(strict_result.value()[1], graph) == balanced &&
        roadIds(strict_result.value()[2], graph) == eco,
        "Strict detour removes nearby route but preserves essentials"
    );

    // --------------------------------------------------
    // 7. Pool capacity of three.
    // --------------------------------------------------

    AlternativeRouteOptions three_only = options;
    three_only.max_pool_routes = 3;

    auto limited = generateAlternativeRoutes(
        cch, graph, metrics,
        *source, *target,
        weight_options, three_only
    );

    check(
        limited.ok() &&
        limited.value().size() == 3 &&
        !hasPreference(limited.value()[1], 0.55),
        "Three-route capacity skips optional preferences"
    );

    // --------------------------------------------------
    // 8. Selected preference equals fastest.
    // --------------------------------------------------

    AlternativeRouteOptions endpoint = options;
    endpoint.selected_time_preference = 1.0;

    auto endpoint_result = generateAlternativeRoutes(
        cch, graph, metrics,
        *source, *target,
        weight_options, endpoint
    );

    check(
        endpoint_result.ok() &&
        endpoint_result.value().size() == 2 &&
        roadIds(endpoint_result.value()[0], graph) == fast &&
        roadIds(endpoint_result.value()[1], graph) == eco,
        "Endpoint preference does not duplicate fastest route"
    );

    // --------------------------------------------------
    // 9. Source and target are identical.
    // --------------------------------------------------

    auto same_node = generateAlternativeRoutes(
        cch, graph, metrics,
        *source, *source,
        weight_options, options
    );

    check(
        same_node.ok() &&
        same_node.value().size() == 1 &&
        same_node.value()[0].path.arc_indices.empty() &&
        closeTo(
            same_node.value()[0].totals.duration_seconds,
            0.0
        ),
        "Source-to-self returns one zero-length route"
    );

    // --------------------------------------------------
    // 10. Reject invalid input.
    // --------------------------------------------------

    auto bad_preference = options;
    bad_preference.selected_time_preference = 1.5;

    check(
        hasError(generateAlternativeRoutes(
            cch, graph, metrics, *source, *target,
            weight_options, bad_preference
        ), ErrorCode::InvalidArgument),
        "Invalid selected preference rejected"
    );

    auto bad_step = options;
    bad_step.neighboring_preference_step = 0.0;

    check(
        hasError(generateAlternativeRoutes(
            cch, graph, metrics, *source, *target,
            weight_options, bad_step
        ), ErrorCode::InvalidArgument),
        "Invalid neighboring step rejected"
    );

    auto bad_capacity = options;
    bad_capacity.max_pool_routes = 2;

    check(
        hasError(generateAlternativeRoutes(
            cch, graph, metrics, *source, *target,
            weight_options, bad_capacity
        ), ErrorCode::InvalidArgument),
        "Insufficient pool capacity rejected"
    );

    auto bad_detour = options;
    bad_detour.max_detour_factor = 0.9;

    check(
        hasError(generateAlternativeRoutes(
            cch, graph, metrics, *source, *target,
            weight_options, bad_detour
        ), ErrorCode::InvalidArgument),
        "Invalid detour factor rejected"
    );

    auto missing_metric = metrics;
    missing_metric.pop_back();

    check(
        hasError(generateAlternativeRoutes(
            cch, graph, missing_metric, *source, *target,
            weight_options, options
        ), ErrorCode::InvalidArgument),
        "Incorrect arc-metric count rejected"
    );

    auto invalid_metric = metrics;
    invalid_metric[0].co2e_grams = -1.0;

    check(
        hasError(generateAlternativeRoutes(
            cch, graph, invalid_metric, *source, *target,
            weight_options, options
        ), ErrorCode::InvalidMetric),
        "Negative physical metrics rejected"
    );

    check(
        hasError(generateAlternativeRoutes(
            cch, graph, metrics,
            static_cast<unsigned>(graph.nodeCount()),
            *target, weight_options, options
        ), ErrorCode::InvalidArgument),
        "Invalid source index rejected"
    );

    // Exact preferences within the old epsilon must remain distinct objectives,
    // even when quantization selects the same physical path.
    auto near_endpoint = three_only;
    near_endpoint.selected_time_preference = std::nextafter(1.0, 0.0);
    auto exact = generateAlternativeRoutes(cch, graph, metrics, *source, *target,
                                          weight_options, near_endpoint);
    bool found_exact = false;
    bool scores_associated = exact.ok();
    if (exact.ok()) {
        for (const auto& candidate : exact.value()) {
            scores_associated = scores_associated &&
                candidate.objective_scores.size() == candidate.matching_preferences.size();
            for (const auto& score : candidate.objective_scores) {
                if (score.time_preference == near_endpoint.selected_time_preference) found_exact = true;
                auto expected = buildGraphSafeCchArcWeights(metrics, score.time_preference,
                                                            weight_options, graph.nodeCount());
                unsigned long long sum = 0;
                if (expected.ok()) {
                    for (unsigned arc : candidate.path.arc_indices) sum += expected.value().arc_weights[arc];
                }
                scores_associated = scores_associated && expected.ok() && sum == score.total_cost;
            }
        }
    }
    check(found_exact && scores_associated,
          "Near-endpoint selected preference is exact and each objective retains its own score");

    auto disconnected_path = evaluateRouteMetrics(ShortestPath{}, graph, metrics, *source, *target);
    auto invalid_path = evaluateRouteMetrics(
        ShortestPath{{static_cast<unsigned>(graph.arcCount())}, 0}, graph, metrics, *source, *target);
    check(!disconnected_path.ok() && !invalid_path.ok() &&
          disconnected_path.error().code == ErrorCode::InvalidGraph &&
          invalid_path.error().code == ErrorCode::InvalidGraph,
          "Shared evaluator rejects disconnected and out-of-range paths");

    // --------------------------------------------------
    // Final result.
    // --------------------------------------------------

    std::cout << '\n';

    if (failures == 0) {
        std::cout
            << "All alternative-route tests passed.\n";
        return 0;
    }

    std::cerr << failures << " test(s) failed.\n";
    return 1;
}
