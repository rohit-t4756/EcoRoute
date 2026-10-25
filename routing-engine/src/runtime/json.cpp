#include "ecoroute/engine.hpp"

#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace ecoroute {
namespace {
std::string quote(const std::string& value) {
    std::ostringstream out;
    out << '"';
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') out << '\\' << c;
        else if (c < 32 || c >= 128) {
            // Version identifiers may contain UTF-8. Preserve bytes >=128;
            // input files are required to use UTF-8 by the interchange contract.
            if (c >= 128) out << c;
            else out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << unsigned(c) << std::dec;
        } else out << c;
    }
    return out.str() + '"';
}
void coordinate(std::ostream& out, const Coordinate& p) {
    if (!std::isfinite(p.longitude) || !std::isfinite(p.latitude))
        throw std::invalid_argument("Cannot serialize a nonfinite coordinate.");
    out << '[' << p.longitude << ',' << p.latitude << ']';
}
void strings(std::ostream& out, const std::vector<std::string>& values) {
    out << '[';
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i) out << ',';
        out << quote(values[i]);
    }
    out << ']';
}
} // namespace

const char* errorCodeName(ErrorCode code) {
    switch (code) {
    case ErrorCode::InvalidArgument: return "InvalidArgument";
    case ErrorCode::OutOfCoverage: return "OutOfCoverage";
    case ErrorCode::SnapFailed: return "SnapFailed";
    case ErrorCode::NoRoute: return "NoRoute";
    case ErrorCode::UnsupportedProfile: return "UnsupportedProfile";
    case ErrorCode::InvalidGraph: return "InvalidGraph";
    case ErrorCode::InvalidMetric: return "InvalidMetric";
    case ErrorCode::InvalidTrafficSnapshot: return "InvalidTrafficSnapshot";
    case ErrorCode::VersionMismatch: return "VersionMismatch";
    case ErrorCode::FileError: return "FileError";
    case ErrorCode::Cancelled: return "Cancelled";
    case ErrorCode::DeadlineExceeded: return "DeadlineExceeded";
    case ErrorCode::ResourceExhausted: return "ResourceExhausted";
    case ErrorCode::Unavailable: return "Unavailable";
    case ErrorCode::InternalError: return "InternalError";
    }
    return "InternalError";
}

std::string errorJson(const Error& error) {
    return "{\"schema_version\":1,\"error\":{\"code\":" + quote(errorCodeName(error.code)) +
           ",\"message\":" + quote(error.message) + "}}";
}

std::string engineStatusJson(const EngineStatus& status) {
    return "{\"schema_version\":1,\"ready\":" + std::string(status.ready ? "true" : "false") +
           ",\"graph_version\":" + quote(status.graph_version) +
           ",\"traffic_version\":" + quote(status.traffic_version) +
           ",\"message\":" + quote(status.status_message) + "}";
}

std::string routeResponseJson(const RouteResponse& response) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(17) << "{\"schema_version\":1,\"graph_version\":"
        << quote(response.graph_version) << ",\"traffic_version\":" << quote(response.traffic_version)
        << ",\"cost_model_version\":" << quote(response.cost_model_version)
        << ",\"objective_version\":" << quote(response.objective_version)
        << ",\"selected_preference\":" << response.selected_preference
        << ",\"recommended_route_id\":" << quote(response.recommended_route_id)
        << ",\"snapped_origin\":";
    coordinate(out, response.snapped_origin);
    out << ",\"snapped_destination\":";
    coordinate(out, response.snapped_destination);
    out << ",\"origin_snap_distance_meters\":" << response.origin_snap_distance_meters
        << ",\"destination_snap_distance_meters\":" << response.destination_snap_distance_meters
        << ",\"warnings\":";
    strings(out, response.warnings);
    out << ",\"routes\":[";
    for (std::size_t i = 0; i < response.routes.size(); ++i) {
        if (i) out << ',';
        const auto& route = response.routes[i];
        out << "{\"id\":" << quote(route.id) << ",\"roles\":";
        strings(out, route.roles);
        out << ",\"matching_preferences\":[";
        for (std::size_t j = 0; j < route.matching_preferences.size(); ++j) {
            if (j) out << ',';
            out << route.matching_preferences[j];
        }
        out << "],\"road_segment_ids\":[";
        for (std::size_t j = 0; j < route.road_segment_ids.size(); ++j) {
            if (j) out << ',';
            out << quote(std::to_string(route.road_segment_ids[j]));
        }
        out << "],\"geometry\":";
        if (route.geometry.empty()) out << "null";
        else if (route.geometry.size() == 1) {
            out << "{\"type\":\"Point\",\"coordinates\":";
            coordinate(out, route.geometry.front()); out << '}';
        } else {
            out << "{\"type\":\"LineString\",\"coordinates\":[";
            for (std::size_t j = 0; j < route.geometry.size(); ++j) {
                if (j) out << ',';
                coordinate(out, route.geometry[j]);
            }
            out << "]}";
        }
        const auto& m = route.metrics;
        out << ",\"geometry_is_approximate\":" << (route.geometry_is_approximate ? "true" : "false")
            << ",\"metrics\":{\"duration_seconds\":" << m.duration_seconds
            << ",\"distance_meters\":" << m.distance_meters << ",\"co2e_grams\":" << m.co2e_grams
            << ",\"fuel_milliliters\":" << m.fuel_milliliters << ",\"electricity_wh\":" << m.electricity_wh
            << "},\"extra_time_seconds\":" << route.extra_time_seconds
            << ",\"co2e_saved_grams\":" << route.co2e_saved_grams
            << ",\"exceeds_detour_limit\":" << (route.exceeds_detour_limit ? "true" : "false")
            << ",\"objective_scores\":[";
        for (std::size_t j = 0; j < route.objective_scores.size(); ++j) {
            if (j) out << ',';
            const auto& score = route.objective_scores[j];
            out << "{\"preference\":" << score.preference << ",\"integer_cost\":" << score.integer_cost
                << ",\"integer_scale\":" << score.integer_scale << ",\"maximum_path_objective_error\":"
                << score.maximum_path_objective_error << '}';
        }
        out << "]}";
    }
    out << "]}";
    return out.str();
}
} // namespace ecoroute
