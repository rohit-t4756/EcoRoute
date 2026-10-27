#include "ecoroute/engine.hpp"
#include <atomic>
#include <cmath>
#include <iostream>
#include <limits>
#include <thread>
#include <vector>
using namespace ecoroute;
int main() {
    int failures = 0;
    auto check = [&](bool condition, const char* name) {
        if (!condition) { std::cerr << "FAILED: " << name << '\n'; ++failures; }
    };
    auto options = loadEngineOptions(std::string(ECOROUTE_SOURCE_DIR) + "/config/engine.example.conf");
    check(options.ok(), "configuration"); if (!options.ok()) return 1;
    auto engine = Engine::create(options.value());
    check(engine.ok(), "initialize engine"); if (!engine.ok()) { std::cerr << engine.error().message; return 1; }
    RouteRequest request;
    request.vehicle_profile_id = "petrol_demo";
    request.departure_time_unix_seconds = 1800000000;
    auto result = engine.value().routeNodes(1, 4, request);
    check(result.ok(), "public route"); if (!result.ok()) { std::cerr << result.error().message; return 1; }
    check(result.value().routes.size() == 2, "unique essential routes");
    check(result.value().routes[0].road_segment_ids == std::vector<RoadSegmentId>{101, 102}, "CSR mapping");
    check(std::fabs(result.value().routes[0].metrics.duration_seconds - 72) < 1e-10, "independent time");
    check(std::fabs(result.value().routes[0].metrics.fuel_milliliters - 140) < 1e-10, "independent units");
    for (const char* profile : {"petrol_demo", "diesel_demo", "hybrid_demo", "electric_demo"}) {
        request.vehicle_profile_id = profile;
        check(engine.value().routeNodes(1, 4, request).ok(), "all vehicle categories");
    }
    request.vehicle_profile_id = "petrol_demo";
    request.urgency_preference = std::nextafter(1.0, 0.0);
    result = engine.value().routeNodes(1, 4, request);
    bool exact = false;
    if (result.ok()) for (const auto& route : result.value().routes)
        for (const auto& score : route.objective_scores) exact |= score.preference == request.urgency_preference;
    check(exact, "exact near-endpoint preference");
    request.max_routes = 1;
    result = engine.value().routeNodes(1, 4, request);
    check(result.ok() && result.value().routes.size() == 1 &&
        result.value().routes[0].id == result.value().recommended_route_id, "selected role under cap");
    request.max_routes = 3;
    result = engine.value().routeNodes(99, 99, request);
    check(result.ok() && result.value().routes.size() == 1 &&
        result.value().routes[0].metrics.distance_meters == 0, "isolated source to self");
    result = engine.value().routeNodes(4, 1, request);
    check(!result.ok() && result.error().code == ErrorCode::NoRoute, "one-way no route");
    request.origin = {40, -74}; request.destination = {40.01, -73.99};
    check(engine.value().route(request).ok(), "coordinate route");
    request.origin.latitude = std::numeric_limits<double>::quiet_NaN();
    check(!engine.value().route(request).ok(), "invalid coordinate");
    request.vehicle_profile_id = "unknown";
    result = engine.value().routeNodes(1, 4, request);
    check(!result.ok() && result.error().code == ErrorCode::UnsupportedProfile, "unknown profile");
    request.vehicle_profile_id = "petrol_demo";
    request.departure_time_unix_seconds = 2000000000;
    check(!engine.value().routeNodes(1, 4, request).ok(), "expired snapshot rejected");
    request.departure_time_unix_seconds = 1800000000;
    request.urgency_preference = 1;
    std::atomic<int> concurrent_failures{0};
    std::vector<std::thread> readers;
    for (int t = 0; t < 4; ++t) readers.emplace_back([&] {
        for (int i = 0; i < 40; ++i) {
            auto route = engine.value().routeNodes(1, 4, request);
            if (!route.ok()) { ++concurrent_failures; continue; }
            const auto& response = route.value();
            const double expected = response.traffic_version == "toy-traffic-v1" ? 72.0 : 180.0;
            if (std::fabs(response.routes.front().metrics.duration_seconds - expected) > 1e-8)
                ++concurrent_failures;
        }
    });
    auto update = engine.value().updateTraffic(std::string(ECOROUTE_SOURCE_DIR) + "/fixtures/toy_traffic_updated");
    for (auto& thread : readers) thread.join();
    check(update.ok() && concurrent_failures == 0, "atomic publication under queries");
    result = engine.value().routeNodes(1, 4, request);
    check(result.ok() && result.value().traffic_version == "toy-traffic-v2" &&
        result.value().routes.front().road_segment_ids == std::vector<RoadSegmentId>{103, 104}, "update invalidates cache");
    check(!engine.value().updateTraffic("missing-traffic-directory").ok() &&
        engine.value().status().traffic_version == "toy-traffic-v2", "failed update retains snapshot");
    std::cout << (failures ? "FAILED" : "PASS") << ": public engine integration\n";
    return failures ? 1 : 0;
}