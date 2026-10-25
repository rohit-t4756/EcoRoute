/*
 * File: traffic_snapshot.cpp
 * Purpose: Validates traffic metadata and maps directed edge factors into CSR order.
 * Status: IMPLEMENTED
 * Remaining: External ML calibration and publisher integration.
 */

#include "traffic_snapshot.hpp"

#include <charconv>
#include <cmath>
#include <fstream>
#include <locale>
#include <new>
#include <sstream>
#include <unordered_map>
#include <utility>

namespace ecoroute {
namespace {

std::string trim(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}

void removeBom(std::string& text) {
    if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) text.erase(0, 3);
}

bool validVersion(const std::string& version) {
    if (version.empty() || version.size() > 256 || trim(version) != version) return false;
    for (unsigned char character : version) {
        if (character < 0x20 || character == 0x7f) return false;
    }
    return true;
}

template <typename Integer>
bool parseInteger(const std::string& text, Integer& value) {
    if (text.empty()) return false;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

bool parseDouble(const std::string& text, double& value) {
    std::istringstream stream(text);
    stream.imbue(std::locale::classic());
    stream >> std::noskipws >> value;
    return stream && stream.eof() && std::isfinite(value);
}

Result<TrafficSnapshot> invalid(const std::string& message) {
    return Result<TrafficSnapshot>::failure(ErrorCode::InvalidTrafficSnapshot, message);
}

} // namespace

Result<TrafficSnapshot> TrafficSnapshot::create(
    const RoutingGraph& graph,
    const std::string& graph_version,
    TrafficMetadata metadata,
    const std::vector<EdgeTrafficFactor>& factors,
    const TrafficPolicy& policy
) {
    if (!validVersion(graph_version) || !validVersion(metadata.graph_version) ||
        !validVersion(metadata.traffic_version)) {
        return invalid("Traffic and graph versions must be nonempty identifiers of at most 256 bytes.");
    }
    if (metadata.graph_version != graph_version) {
        return Result<TrafficSnapshot>::failure(
            ErrorCode::VersionMismatch, "Traffic belongs to a different graph version.");
    }
    if (metadata.valid_from_unix_seconds < 0 ||
        metadata.valid_until_unix_seconds <= metadata.valid_from_unix_seconds) {
        return invalid("Traffic validity must be a nonempty nonnegative UTC interval.");
    }
    if (!std::isfinite(policy.max_multiplier) || policy.max_multiplier < 1.0 ||
        (policy.missing != MissingTrafficPolicy::Reject &&
         policy.missing != MissingTrafficPolicy::FreeFlow)) {
        return Result<TrafficSnapshot>::failure(ErrorCode::InvalidArgument, "Invalid traffic policy.");
    }
    if (factors.size() > graph.arcCount()) {
        return invalid("Traffic contains more factors than graph arcs.");
    }

    try {
        TrafficSnapshot snapshot;
        snapshot.metadata_ = std::move(metadata);
        snapshot.segment_ids_ = graph.segmentIds();
        snapshot.multipliers_.assign(graph.arcCount(), 1.0);
        snapshot.missing_factor_count_ = graph.arcCount() - factors.size();

        std::unordered_map<RoadSegmentId, std::size_t> arcs;
        arcs.reserve(graph.arcCount());
        for (std::size_t arc = 0; arc < graph.arcCount(); ++arc) {
            arcs.emplace(graph.segmentIds()[arc], arc);
        }
        std::vector<bool> seen(graph.arcCount(), false);
        for (const auto& factor : factors) {
            const auto found = arcs.find(factor.segment_id);
            if (found == arcs.end()) return invalid("Traffic references an unknown directed edge ID.");
            const std::size_t arc = found->second;
            if (seen[arc]) return invalid("Traffic contains a duplicate directed edge ID.");
            if (!std::isfinite(factor.travel_time_multiplier) ||
                factor.travel_time_multiplier < 1.0 ||
                factor.travel_time_multiplier > policy.max_multiplier) {
                return invalid("Traffic multiplier is nonfinite or outside the configured [1, maximum] range.");
            }
            seen[arc] = true;
            snapshot.multipliers_[arc] = factor.travel_time_multiplier;
        }
        if (snapshot.missing_factor_count_ != 0 && policy.missing == MissingTrafficPolicy::Reject) {
            return invalid("Traffic is missing directed edge factors; free-flow fallback was not enabled.");
        }
        return Result<TrafficSnapshot>::success(std::move(snapshot));
    } catch (const std::bad_alloc&) {
        return Result<TrafficSnapshot>::failure(ErrorCode::ResourceExhausted, "Cannot allocate traffic snapshot.");
    }
}

const TrafficMetadata& TrafficSnapshot::metadata() const { return metadata_; }
const std::vector<double>& TrafficSnapshot::multipliers() const { return multipliers_; }
std::size_t TrafficSnapshot::missingFactorCount() const { return missing_factor_count_; }

bool TrafficSnapshot::isAlignedWith(const RoutingGraph& graph, const std::string& version) const {
    return metadata_.graph_version == version && segment_ids_ == graph.segmentIds() &&
           multipliers_.size() == graph.arcCount();
}

Result<void> TrafficSnapshot::validateTime(std::int64_t departure_time_unix_seconds) const {
    if (departure_time_unix_seconds < 0) {
        return Result<void>::failure(ErrorCode::InvalidArgument, "Departure time cannot be negative.");
    }
    if (departure_time_unix_seconds < metadata_.valid_from_unix_seconds ||
        departure_time_unix_seconds >= metadata_.valid_until_unix_seconds) {
        return Result<void>::failure(ErrorCode::InvalidTrafficSnapshot,
                                    "Departure time is outside the traffic validity interval.");
    }
    return Result<void>::success();
}

Result<TrafficSnapshot> loadTrafficSnapshot(
    const TrafficSnapshotFiles& files,
    const RoutingGraph& graph,
    const std::string& graph_version,
    const TrafficPolicy& policy
) {
    try {
        std::ifstream metadata_file(files.metadata_file);
        std::ifstream factors_file(files.factors_file);
        if (!metadata_file || !factors_file) {
            return Result<TrafficSnapshot>::failure(ErrorCode::FileError, "Cannot open traffic snapshot files.");
        }

        const std::vector<std::string> keys = {
            "schema_version", "graph_version", "traffic_version",
            "valid_from_unix_seconds", "valid_until_unix_seconds"
        };
        std::unordered_map<std::string, std::string> values;
        std::string line;
        bool first_line = true;
        while (std::getline(metadata_file, line)) {
            if (first_line) removeBom(line);
            first_line = false;
            line = trim(line);
            if (line.empty() || line.front() == '#') continue;
            if (line.size() > 4096) return invalid("Traffic metadata line is too long.");
            const auto separator = line.find('=');
            if (separator == std::string::npos) return invalid("Traffic metadata requires key=value lines.");
            const std::string key = trim(line.substr(0, separator));
            bool known = false;
            for (const auto& expected : keys) known = known || key == expected;
            if (!known || !values.emplace(key, trim(line.substr(separator + 1))).second) {
                return invalid("Traffic metadata contains an unknown or duplicate key.");
            }
        }
        if (metadata_file.bad()) {
            return Result<TrafficSnapshot>::failure(ErrorCode::FileError, "Cannot read traffic metadata.");
        }
        if (values.size() != keys.size() || values["schema_version"] != "1") {
            return invalid("Traffic metadata is incomplete or has an unsupported schema version.");
        }
        TrafficMetadata metadata;
        metadata.graph_version = values["graph_version"];
        metadata.traffic_version = values["traffic_version"];
        if (!parseInteger(values["valid_from_unix_seconds"], metadata.valid_from_unix_seconds) ||
            !parseInteger(values["valid_until_unix_seconds"], metadata.valid_until_unix_seconds)) {
            return invalid("Traffic validity timestamps must be integer UTC Unix seconds.");
        }

        if (!std::getline(factors_file, line)) return invalid("Traffic factor CSV is empty.");
        removeBom(line);
        if (trim(line) != "edge_id,travel_time_multiplier") {
            return invalid("Traffic CSV header must be edge_id,travel_time_multiplier.");
        }
        std::vector<EdgeTrafficFactor> factors;
        while (std::getline(factors_file, line)) {
            line = trim(line);
            if (line.empty()) continue;
            if (line.size() > 4096 || factors.size() >= graph.arcCount()) {
                return invalid("Traffic CSV exceeds supported row size or graph arc count.");
            }
            const auto separator = line.find(',');
            EdgeTrafficFactor factor;
            if (separator == std::string::npos || line.find(',', separator + 1) != std::string::npos ||
                !parseInteger(trim(line.substr(0, separator)), factor.segment_id) ||
                !parseDouble(trim(line.substr(separator + 1)), factor.travel_time_multiplier)) {
                return invalid("Traffic CSV contains an invalid ID or multiplier.");
            }
            factors.push_back(factor);
        }
        if (factors_file.bad()) {
            return Result<TrafficSnapshot>::failure(ErrorCode::FileError, "Cannot read traffic factors.");
        }
        return TrafficSnapshot::create(graph, graph_version, std::move(metadata), factors, policy);
    } catch (const std::bad_alloc&) {
        return Result<TrafficSnapshot>::failure(ErrorCode::ResourceExhausted, "Cannot allocate traffic input.");
    }
}

} // namespace ecoroute
