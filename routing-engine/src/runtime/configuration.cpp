/* Configuration is deliberately a small, documented key=value format and a
 * strict numeric CSV profile table. It does not pretend to parse general YAML. */
#include "runtime/configuration.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <locale>
#include <set>
#include <sstream>
#include <stdexcept>

namespace ecoroute {
namespace {
std::string trim(std::string value) {
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return {};
    return value.substr(begin, value.find_last_not_of(" \t\r\n") - begin + 1);
}
double number(const std::string& value) {
    std::istringstream input(value);
    input.imbue(std::locale::classic());
    double result;
    if (!(input >> result) || !(input >> std::ws).eof() || !std::isfinite(result))
        throw std::invalid_argument("Expected a finite decimal number: " + value);
    return result;
}
std::size_t count(const std::string& value) {
    std::size_t result = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (value.empty() || parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
        throw std::invalid_argument("Expected an unsigned integer: " + value);
    return result;
}
bool boolean(const std::string& value) {
    if (value == "true") return true;
    if (value == "false") return false;
    throw std::invalid_argument("Expected true or false: " + value);
}
bool token(const std::string& value) {
    return !value.empty() && std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
               (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
    });
}
std::vector<std::string> fields(const std::string& line) {
    std::vector<std::string> result;
    std::size_t begin = 0;
    do {
        auto end = line.find(',', begin);
        result.push_back(trim(line.substr(begin, end - begin)));
        if (end == std::string::npos) break;
        begin = end + 1;
    } while (true);
    return result;
}
} // namespace

Result<void> validateEngineOptions(const EngineOptions& o) {
    auto positive = [](double x) { return std::isfinite(x) && x > 0.0; };
    if (o.graph_file.empty() || o.vehicle_profiles_file.empty() ||
        (!o.allow_free_flow && o.traffic_file.empty()) ||
        !positive(o.fallback_speed_kph) || !positive(o.reference_time_seconds) ||
        !positive(o.reference_co2e_grams) || !positive(o.max_snap_distance_meters) ||
        !std::isfinite(o.max_traffic_multiplier) || o.max_traffic_multiplier < 1.0 ||
        o.integer_scale == 0 || o.max_cached_objectives > 64 || o.max_cached_routes > 4096 ||
        !token(o.cost_model_version) || !token(o.objective_version)) {
        return Result<void>::failure(ErrorCode::InvalidArgument,
            "Invalid engine paths, numeric limits, cache limits or version identifiers.");
    }
    return Result<void>::success();
}

Result<EngineOptions> loadEngineOptions(const std::string& filename) {
    using R = Result<EngineOptions>;
    std::ifstream input(filename);
    if (!input) return R::failure(ErrorCode::FileError, "Cannot open engine configuration.");
    EngineOptions o;
    std::set<std::string> seen;
    std::string line;
    std::size_t line_number = 0;
    try {
        const auto base = std::filesystem::absolute(filename).parent_path();
        auto path = [&](const std::string& value) {
            if (value.empty()) return std::string{};
            return (base / std::filesystem::path(value)).lexically_normal().string();
        };
        while (std::getline(input, line)) {
            ++line_number;
            if (line_number == 1 && line.compare(0, 3, "\xef\xbb\xbf") == 0) line.erase(0, 3);
            line = trim(line);
            if (line.empty() || line.front() == '#') continue;
            const auto separator = line.find('=');
            if (separator == std::string::npos) throw std::invalid_argument("Expected key=value.");
            const auto key = trim(line.substr(0, separator));
            const auto value = trim(line.substr(separator + 1));
            if (!seen.insert(key).second) throw std::invalid_argument("Duplicate key: " + key);
            if (key == "graph_file") o.graph_file = path(value);
            else if (key == "traffic_file") o.traffic_file = path(value);
            else if (key == "vehicle_profiles_file") o.vehicle_profiles_file = path(value);
            else if (key == "cch_topology_file") o.cch_topology_file = path(value);
            else if (key == "fallback_speed_kph") o.fallback_speed_kph = number(value);
            else if (key == "reference_time_seconds") o.reference_time_seconds = number(value);
            else if (key == "reference_co2e_grams") o.reference_co2e_grams = number(value);
            else if (key == "integer_scale") {
                const auto n = count(value);
                if (n > std::numeric_limits<unsigned>::max()) throw std::invalid_argument("Scale overflow.");
                o.integer_scale = static_cast<unsigned>(n);
            }
            else if (key == "max_snap_distance_meters") o.max_snap_distance_meters = number(value);
            else if (key == "max_traffic_multiplier") o.max_traffic_multiplier = number(value);
            else if (key == "allow_missing_traffic") o.allow_missing_traffic = boolean(value);
            else if (key == "allow_free_flow") o.allow_free_flow = boolean(value);
            else if (key == "max_cached_objectives") o.max_cached_objectives = count(value);
            else if (key == "max_cached_routes") o.max_cached_routes = count(value);
            else if (key == "cost_model_version") o.cost_model_version = value;
            else if (key == "objective_version") o.objective_version = value;
            else throw std::invalid_argument("Unknown configuration key: " + key);
        }
        if (input.bad()) return R::failure(ErrorCode::FileError, "Cannot read engine configuration.");
        auto valid = validateEngineOptions(o);
        if (!valid.ok()) return R::failure(valid.error().code, valid.error().message);
        return R::success(std::move(o));
    } catch (const std::exception& e) {
        return R::failure(ErrorCode::InvalidArgument,
            "Configuration line " + std::to_string(line_number) + ": " + e.what());
    }
}

Result<std::vector<VehicleProfile>> loadVehicleProfiles(const std::string& filename) {
    using R = Result<std::vector<VehicleProfile>>;
    std::ifstream input(filename);
    if (!input) return R::failure(ErrorCode::FileError, "Cannot open vehicle profiles.");
    std::string line;
    std::getline(input, line);
    if (line.compare(0, 3, "\xef\xbb\xbf") == 0) line.erase(0, 3);
    const std::string header = "id,type,consumption_per_100km,auxiliary_consumption_per_hour,"
        "co2e_grams_per_unit,mass_kg,wheel_energy_kwh_per_unit,downhill_propulsion_fraction,"
        "regenerative_recovery_fraction";
    if (trim(line) != header) return R::failure(ErrorCode::InvalidArgument, "Invalid vehicle CSV header.");
    std::vector<VehicleProfile> profiles;
    std::set<std::string> ids;
    std::size_t row = 1;
    try {
        while (std::getline(input, line)) {
            ++row;
            if (trim(line).empty()) continue;
            const auto f = fields(line);
            if (f.size() != 9 || !token(f[0]) || !ids.insert(f[0]).second)
                throw std::invalid_argument("Invalid columns, profile ID or duplicate ID.");
            VehicleProfile p;
            p.id = f[0];
            if (f[1] == "petrol") p.type = VehicleType::Petrol;
            else if (f[1] == "diesel") p.type = VehicleType::Diesel;
            else if (f[1] == "hybrid") p.type = VehicleType::Hybrid;
            else if (f[1] == "electric") p.type = VehicleType::Electric;
            else throw std::invalid_argument("Unknown vehicle type.");
            p.consumption_per_100km = number(f[2]);
            p.auxiliary_consumption_per_hour = number(f[3]);
            p.co2e_grams_per_unit = number(f[4]);
            if (!f[5].empty()) p.mass_kg = number(f[5]);
            if (!f[6].empty()) p.wheel_energy_kwh_per_unit = number(f[6]);
            p.downhill_propulsion_fraction = number(f[7]);
            p.regenerative_recovery_fraction = number(f[8]);
            auto valid = validateVehicleProfile(p);
            if (!valid.ok()) throw std::invalid_argument(valid.error().message);
            profiles.push_back(std::move(p));
            if (profiles.size() > 64) throw std::invalid_argument("At most 64 profiles are supported.");
        }
        if (input.bad()) return R::failure(ErrorCode::FileError, "Cannot read vehicle profiles.");
        if (profiles.empty()) throw std::invalid_argument("At least one profile is required.");
        return R::success(std::move(profiles));
    } catch (const std::exception& e) {
        return R::failure(ErrorCode::InvalidArgument,
            "Vehicle row " + std::to_string(row) + ": " + e.what());
    }
}
} // namespace ecoroute
