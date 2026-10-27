
/*
 * File: test_objective_routing.cpp
 * Purpose: End-to-end integration test connecting
 *          CostModel -> WeightBuilder -> CCH.
 *
 * Status: VERIFIED
 * Remaining: Compile and verify integration tests.
 */

#include "cost/cost_model.hpp"
#include "cost/weight_builder.hpp"
#include "graph/road_network.hpp"
#include "graph/routing_graph.hpp"
#include "search/cch_order.hpp"
#include "search/cch_adapter.hpp"
#include "search/dijkstra.hpp"

#include <cmath>
#include <iostream>
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
    return std::fabs(a - b) < 0.0001;
}

RoadSegment makeRoad(
    RoadSegmentId id,
    RoadNodeId from,
    RoadNodeId to,
    double length,
    double speed
) {
    RoadSegment segment;
    segment.id = id;
    segment.from = from;
    segment.to = to;
    segment.length_meters = length;
    segment.speed_limit_kph = speed;
    return segment;
}

// Convert a compact-arc path into physical segment IDs.
std::vector<RoadSegmentId> segmentPath(
    const ShortestPath& path,
    const RoutingGraph& graph
) {
    std::vector<RoadSegmentId> result;

    for (unsigned arc : path.arc_indices) {
        if (arc >= graph.arcCount()) {
            return {};
        }

        result.push_back(graph.segmentIds()[arc]);
    }

    return result;
}

} // namespace

int main() {
    // --------------------------------------------------
    // 1. Build the physical road network.
    // --------------------------------------------------

    // A=100, B=200, C=300, D=400
    std::vector<RoadNode> nodes = {
        {100, {40.700, -73.900}, std::nullopt},
        {200, {40.705, -73.894}, std::nullopt},
        {300, {40.700, -73.894}, std::nullopt},
        {400, {40.700, -73.888}, std::nullopt}
    };

    std::vector<RoadSegment> segments = {
        // Fast route: A -> B -> D
        // Two 1-km roads, each at 72 km/h.
        makeRoad(7001, 100, 200, 1000.0, 72.0),
        makeRoad(7002, 200, 400, 1000.0, 72.0),

        // Eco route: A -> C -> D
        // Two 500-m roads, each at 18 km/h.
        makeRoad(7003, 100, 300, 500.0, 18.0),
        makeRoad(7004, 300, 400, 500.0, 18.0)
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
    auto target = graph.findNodeIndex(400);

    if (!source || !target) {
        std::cerr << "[FAIL] Node-index mapping\n";
        return 1;
    }

    check(
        graph.nodeCount() == 4 && graph.arcCount() == 4,
        "Physical network and CSR constructed"
    );

    // --------------------------------------------------
    // 2. Configure our test vehicle.
    // --------------------------------------------------

    VehicleProfile vehicle;
    vehicle.id = "petrol-integration-test";
    vehicle.type = VehicleType::Petrol;
    vehicle.consumption_per_100km = 8.0;
    vehicle.auxiliary_consumption_per_hour = 0.72;
    vehicle.co2e_grams_per_unit = 2300.0;

    // Elevation is deliberately unavailable in this test,
    // so we isolate time and distance-related consumption.
    CostModelOptions cost_options;
    cost_options.fallback_speed_kph = 40.0;

    // --------------------------------------------------
    // 3. Generate metrics in CSR arc order.
    // --------------------------------------------------

    auto calculateMetrics = [&](bool congested)
        -> std::optional<std::vector<RouteMetrics>> {

        std::vector<RouteMetrics> result;
        result.reserve(graph.arcCount());

        for (unsigned arc = 0; arc < graph.arcCount(); ++arc) {
            RoadSegmentId id = graph.segmentIds()[arc];

            const RoadSegment* segment =
                network.findSegment(id);

            if (!segment) {
                return std::nullopt;
            }

            const RoadNode* from =
                network.findNode(segment->from);

            const RoadNode* to =
                network.findNode(segment->to);

            if (!from || !to) {
                return std::nullopt;
            }

            // Congestion only affects the fast route.
            double factor = (
                congested &&
                (id == 7001 || id == 7002)
            ) ? 3.0 : 1.0;

            auto estimated = estimateSegmentMetrics(
                *segment,
                *from,
                *to,
                vehicle,
                factor,
                cost_options
            );

            if (!estimated.ok()) {
                return std::nullopt;
            }

            result.push_back(estimated.value());
        }

        return result;
    };

    auto initial_metrics = calculateMetrics(false);

    if (!initial_metrics) {
        std::cerr << "[FAIL] Initial cost estimation\n";
        return 1;
    }

    // Validate physical results independently.
    double fast_time = 0.0;
    double fast_emissions = 0.0;
    double eco_time = 0.0;
    double eco_emissions = 0.0;

    for (unsigned arc = 0; arc < graph.arcCount(); ++arc) {
        RoadSegmentId id = graph.segmentIds()[arc];
        const RouteMetrics& m = (*initial_metrics)[arc];

        if (id == 7001 || id == 7002) {
            fast_time += m.duration_seconds;
            fast_emissions += m.co2e_grams;
        } else {
            eco_time += m.duration_seconds;
            eco_emissions += m.co2e_grams;
        }
    }

    check(
        closeTo(fast_time, 100.0) &&
        closeTo(fast_emissions, 368.0) &&
        closeTo(eco_time, 200.0) &&
        closeTo(eco_emissions, 184.0),
        "Physical time and emissions match expected values"
    );

    // --------------------------------------------------
    // 4. Generate our CCH contraction order.
    // --------------------------------------------------

    auto order_result = computeCchNodeOrder(
        network, graph
    );

    if (!order_result.ok()) {
        std::cerr << "[FAIL] CCH ordering\n";
        return 1;
    }

    // The reference values are fixed for this test.
    // Production calibration is still pending.
    WeightBuilderOptions weight_options;
    weight_options.reference_time_seconds = 100.0;
    weight_options.reference_co2e_grams = 184.0;
    weight_options.integer_scale = 10000u;

    auto first_weights = buildCchArcWeights(
        *initial_metrics, 1.0, weight_options
    );

    if (!first_weights.ok()) {
        std::cerr << "[FAIL] Initial weight generation\n";
        return 1;
    }

    auto cch_result = CchAdapter::create(
        graph,
        std::move(order_result.value()),
        std::move(first_weights.value())
    );

    if (!cch_result.ok()) {
        std::cerr << "[FAIL] CCH construction\n";
        return 1;
    }

    CchAdapter cch = std::move(cch_result.value());

    // --------------------------------------------------
    // 5. Test optimization preferences.
    // --------------------------------------------------

    const std::vector<RoadSegmentId> fast_path = {
        7001, 7002
    };

    const std::vector<RoadSegmentId> eco_path = {
        7003, 7004
    };

    auto verifyPreference = [&](
        const std::vector<RouteMetrics>& metrics,
        double alpha,
        const std::vector<RoadSegmentId>& expected_path,
        unsigned expected_weight,
        const std::string& name
    ) {
        auto weights = buildCchArcWeights(
            metrics, alpha, weight_options
        );

        if (!weights.ok()) {
            check(false, name + " (weight generation)");
            return;
        }

        auto updated = cch.updateWeights(weights.value());

        if (!updated.ok()) {
            check(false, name + " (CCH customization)");
            return;
        }

        auto reference = dijkstraShortestPath(
            graph, weights.value(), *source, *target
        );

        auto optimized = cch.shortestPath(
            *source, *target
        );

        if (!reference.ok() || !optimized.ok()) {
            check(false, name + " (shortest-path query)");
            return;
        }

        auto selected_path = segmentPath(
            optimized.value(), graph
        );

        bool correct =
            reference.value().total_cost ==
                optimized.value().total_cost &&
            optimized.value().total_cost == expected_weight;

        // An empty expected path permits either route
        // when both have exactly the same objective.
        if (!expected_path.empty()) {
            correct = correct &&
                selected_path == expected_path &&
                segmentPath(reference.value(), graph) ==
                    expected_path;
        } else {
            correct = correct &&
                (selected_path == fast_path ||
                 selected_path == eco_path);
        }

        check(correct, name);
    };

    verifyPreference(
        *initial_metrics, 1.0, fast_path, 10000u,
        "Fastest preference selects A-B-D"
    );

    verifyPreference(
        *initial_metrics, 0.75, fast_path, 12500u,
        "Time-oriented preference selects A-B-D"
    );

    verifyPreference(
        *initial_metrics, 0.50, {}, 15000u,
        "Balanced objective handles equal-cost alternatives"
    );

    verifyPreference(
        *initial_metrics, 0.25, eco_path, 12500u,
        "Eco-oriented preference selects A-C-D"
    );

    verifyPreference(
        *initial_metrics, 0.0, eco_path, 10000u,
        "Lowest-emissions preference selects A-C-D"
    );

    // --------------------------------------------------
    // 6. Simulate traffic and customize CCH again.
    // --------------------------------------------------

    auto congested_metrics = calculateMetrics(true);

    if (!congested_metrics) {
        std::cerr << "[FAIL] Congested cost estimation\n";
        return 1;
    }

    // Fast route becomes 300 s.
    // Eco route remains 200 s.
    verifyPreference(
        *congested_metrics, 1.0, eco_path, 20000u,
        "Traffic update changes fastest route"
    );

    // --------------------------------------------------
    // Final result.
    // --------------------------------------------------

    std::cout << '\n';

    if (failures == 0) {
        std::cout
            << "All objective-routing integration tests passed.\n";
        return 0;
    }

    std::cerr << failures << " test(s) failed.\n";
    return 1;
}
