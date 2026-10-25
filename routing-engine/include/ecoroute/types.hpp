#pragma once

/*
 * File: types.hpp
 * Purpose: Defines the shared input, output and configuration structures
 *          used by the EcoRoute routing engine.
 * Status: IMPLEMENTED
 * Remaining: None for the current data structures
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ecoroute {

// Identifies a directed road segment within a specific graph version.
using RoadSegmentId = std::uint64_t;

// Represents a geographic position in decimal degrees (WGS84).
struct Coordinate {
    double latitude = 0.0;
    double longitude = 0.0;
};

// Information provided by the backend when requesting routes.
struct RouteRequest {
    Coordinate origin;
    Coordinate destination;

    // Selects a vehicle model defined in the engine configuration.
    std::string vehicle_profile_id;

    // UTC Unix timestamp in seconds; the backend must provide a valid time.
    std::int64_t departure_time_unix_seconds = 0;

    // 1.0 prioritizes time; 0.0 prioritizes emissions.
    double urgency_preference = 0.5;

    // Advisory detour thresholds. Essential routes are retained and flagged.
    // Maximum preferred delay relative to the fastest route.
    // 0.25 means up to 25% additional travel time.
    double max_extra_time_ratio = 0.25;

    // Absolute maximum permitted delay, in seconds.
    double max_extra_time_seconds = 600.0;

    // Upper limit; fewer routes may be returned if alternatives are unsuitable.
    std::uint32_t max_routes = 3;
};

// Stores the estimated properties of one complete route.
struct RouteMetrics {
    double duration_seconds = 0.0;
    double distance_meters = 0.0;

    // Model-estimated operational CO2-equivalent emissions.
    double co2e_grams = 0.0;

    // Applicable consumption fields; unused fields remain zero.
    double fuel_milliliters = 0.0;
    double electricity_wh = 0.0;
};

// Search diagnostics are objective-specific and never substitute for physical metrics.
struct RouteObjectiveScore {
    double preference = 0.0;
    std::uint64_t integer_cost = 0;
    double integer_scale = 0.0;
    double maximum_path_objective_error = 0.0;
};

// Represents one calculated route from origin to destination.
struct Route {
    std::string id;

    // Directed road segments traversed, in travel order.
    std::vector<RoadSegmentId> road_segment_ids;

    // Geographic coordinates used for drawing the route on a map.
    std::vector<Coordinate> geometry;

    RouteMetrics metrics;

    // Differences compared with the minimum-time route.
    double extra_time_seconds = 0.0;
    double co2e_saved_grams = 0.0;

    // A physical path can fulfill several roles without duplicate route cards.
    std::vector<std::string> roles;
    std::vector<double> matching_preferences;
    std::vector<RouteObjectiveScore> objective_scores;
    bool exceeds_detour_limit = false;
    bool geometry_is_approximate = false;
};

// Contains all selected routes and their recommendation.
struct RouteResponse {
    std::vector<Route> routes;

    // Empty only when no recommendation is available.
    std::string recommended_route_id;

    // Versions needed for consistency, debugging and cache invalidation.
    std::string graph_version;
    std::string traffic_version;
    std::string cost_model_version;
    std::string objective_version;
    double selected_preference = 0.5;
    Coordinate snapped_origin;
    Coordinate snapped_destination;
    double origin_snap_distance_meters = 0.0;
    double destination_snap_distance_meters = 0.0;
    std::vector<std::string> warnings;
};

// Paths to the prepared files required to initialize the engine.
struct EngineOptions {
    // graph_file / traffic_file are directories; names retained for compatibility.
    std::string graph_file;
    // Optional validated nested-dissection order cache, not a serialized hierarchy.
    std::string cch_topology_file;
    std::string vehicle_profiles_file;
    std::string traffic_file;
    double fallback_speed_kph = 30.0;
    double reference_time_seconds = 60.0;
    double reference_co2e_grams = 100.0;
    unsigned integer_scale = 1000000;
    double max_snap_distance_meters = 1000.0;
    double max_traffic_multiplier = 100.0;
    bool allow_missing_traffic = false;
    bool allow_free_flow = false;
    std::size_t max_cached_objectives = 6;
    std::size_t max_cached_routes = 64;
    std::string cost_model_version = "additive-v1-uncalibrated";
    std::string objective_version = "normalized-v1";
};

// Reports whether the engine is ready to serve requests.
struct EngineStatus {
    bool ready = false;
    std::string graph_version;
    std::string traffic_version;
    std::string status_message;
};

} // namespace ecoroute
