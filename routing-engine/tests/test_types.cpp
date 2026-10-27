
/*
 * File: test_types.cpp
 * Purpose: Tests the shared data structures defined in types.hpp.
 * Status: VERIFIED
 * Remaining: None for the current test cases
 */

#include "ecoroute/types.hpp"
#include <iostream>
#include <string>

using namespace ecoroute;

// Reports whether a test passed and counts failures.
void check(bool condition, const std::string& name, int& failures) {
    if (condition) {
        std::cout << "[PASS] " << name << '\n';
    } else {
        std::cerr << "[FAIL] " << name << '\n';
        failures++;
    }
}

int main() {
    int failures = 0;

    // Test 1: Geographic coordinates.
    Coordinate point{40.7580, -73.9855};
    check(point.latitude == 40.7580, "Latitude storage", failures);
    check(point.longitude == -73.9855, "Longitude storage", failures);

    // Test 2: RouteRequest default values.
    RouteRequest request;
    check(request.urgency_preference == 0.5, "Default urgency", failures);
    check(request.max_extra_time_ratio == 0.25, "Default delay ratio", failures);
    check(request.max_extra_time_seconds == 600.0, "Default absolute delay", failures);
    check(request.max_routes == 3, "Default visible route count", failures);

    // Test 3: Assign user-provided routing information.
    request.origin = point;
    request.destination = {40.7061, -74.0086};
    request.vehicle_profile_id = "petrol_compact";
    request.departure_time_unix_seconds = 1800000000;

    check(request.origin.latitude == point.latitude, "Origin assignment", failures);
    check(request.destination.longitude == -74.0086, "Destination assignment", failures);
    check(request.vehicle_profile_id == "petrol_compact", "Vehicle profile", failures);
    check(request.departure_time_unix_seconds == 1800000000, "Departure timestamp", failures);

    // Test 4: Store estimated route metrics.
    RouteMetrics metrics;
    metrics.duration_seconds = 1200.0;
    metrics.distance_meters = 8500.0;
    metrics.co2e_grams = 1800.0;
    metrics.fuel_milliliters = 750.0;

    check(metrics.duration_seconds == 1200.0, "Travel time", failures);
    check(metrics.distance_meters == 8500.0, "Travel distance", failures);
    check(metrics.co2e_grams == 1800.0, "Estimated emissions", failures);
    check(metrics.electricity_wh == 0.0, "Default electricity usage", failures);

    // Test 5: Construct a route containing road segments and geometry.
    Route route;
    route.id = "route-1";
    route.road_segment_ids = {101, 102, 103};
    route.geometry = {point, request.destination};
    route.metrics = metrics;
    route.extra_time_seconds = 60.0;

    check(route.road_segment_ids.size() == 3, "Road segment count", failures);
    check(route.road_segment_ids[1] == 102, "Road segment order", failures);
    check(route.geometry.size() == 2, "Geometry count", failures);
    check(route.metrics.co2e_grams == 1800.0, "Route metrics assignment", failures);

    // Test 6: Store routes in a complete engine response.
    RouteResponse response;
    response.routes.push_back(route);
    response.recommended_route_id = "route-1";
    response.graph_version = "nyc-v1";
    response.traffic_version = "traffic-v1";
    response.cost_model_version = "model-v1";

    check(response.routes.size() == 1, "Response route count", failures);
    check(response.routes[0].id == "route-1", "Stored route ID", failures);
    check(response.recommended_route_id == "route-1", "Recommendation ID", failures);
    check(response.graph_version == "nyc-v1", "Graph version", failures);

    // Test 7: Engine configuration and initial status.
    EngineOptions options;
    options.graph_file = "nyc_graph.bin";

    EngineStatus status;
    check(options.graph_file == "nyc_graph.bin", "Graph file configuration", failures);
    check(!status.ready, "Engine initially not ready", failures);

    std::cout << "\nTotal failures: " << failures << '\n';
    return failures == 0 ? 0 : 1;
}
