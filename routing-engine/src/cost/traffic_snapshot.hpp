#pragma once

/*
 * File: traffic_snapshot.hpp
 * Purpose: Defines validated, versioned traffic factors in CSR arc order.
 * Status: IMPLEMENTED
 * Remaining: Calibrate traffic bounds and integrate the external ML publisher.
 */

#include "ecoroute/result.hpp"
#include "graph/routing_graph.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ecoroute {

struct TrafficMetadata {
    std::string graph_version;
    std::string traffic_version;
    // UTC Unix seconds. A snapshot applies on [valid_from, valid_until).
    std::int64_t valid_from_unix_seconds = 0;
    std::int64_t valid_until_unix_seconds = 0;
};

struct EdgeTrafficFactor {
    RoadSegmentId segment_id = 0;
    // Predicted travel time divided by the cost model's reference time.
    double travel_time_multiplier = 1.0;
};

enum class MissingTrafficPolicy { Reject, FreeFlow };

struct TrafficPolicy {
    MissingTrafficPolicy missing = MissingTrafficPolicy::Reject;
    // An operational rejection bound, not an ML calibration or clipping rule.
    double max_multiplier = 100.0;
};

class TrafficSnapshot {
public:
    static Result<TrafficSnapshot> create(
        const RoutingGraph& graph,
        const std::string& graph_version,
        TrafficMetadata metadata,
        const std::vector<EdgeTrafficFactor>& factors,
        const TrafficPolicy& policy = {}
    );

    const TrafficMetadata& metadata() const;
    const std::vector<double>& multipliers() const;
    std::size_t missingFactorCount() const;
    bool isAlignedWith(const RoutingGraph& graph, const std::string& version) const;
    Result<void> validateTime(std::int64_t departure_time_unix_seconds) const;

private:
    TrafficSnapshot() = default;
    TrafficMetadata metadata_;
    std::vector<RoadSegmentId> segment_ids_;
    std::vector<double> multipliers_;
    std::size_t missing_factor_count_ = 0;
};

struct TrafficSnapshotFiles {
    std::filesystem::path metadata_file;
    std::filesystem::path factors_file;
};

// Metadata keys: schema_version=1, graph_version, traffic_version,
// valid_from_unix_seconds, valid_until_unix_seconds. CSV columns:
// edge_id,travel_time_multiplier. Numeric CSV fields are deliberately unquoted.
Result<TrafficSnapshot> loadTrafficSnapshot(
    const TrafficSnapshotFiles& files,
    const RoutingGraph& graph,
    const std::string& graph_version,
    const TrafficPolicy& policy = {}
);

} // namespace ecoroute
