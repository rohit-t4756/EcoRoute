#pragma once

/*
 * File: snapshot_manager.hpp
 * Purpose: Prepares and safely publishes immutable traffic/profile metrics.
 * Status: IMPLEMENTED
 * Remaining: External publisher integration and large-network memory measurement.
 */

#include "cost/cost_model.hpp"
#include "cost/traffic_snapshot.hpp"
#include "graph/graph_loader.hpp"

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace ecoroute {

class MetricSnapshot {
public:
    const TrafficSnapshot& traffic() const;
    const std::string& costModelVersion() const;
    const std::vector<RouteMetrics>* profileMetrics(const std::string& id) const;

private:
    friend class SnapshotManager;
    MetricSnapshot(TrafficSnapshot traffic, std::string cost_model_version);
    TrafficSnapshot traffic_;
    std::string cost_model_version_;
    std::unordered_map<std::string, std::vector<RouteMetrics>> metrics_;
};

class SnapshotManager {
public:
    static Result<std::unique_ptr<SnapshotManager>> create(
        std::shared_ptr<const LoadedGraphSnapshot> graph,
        std::vector<VehicleProfile> profiles,
        CostModelOptions cost_options,
        std::string cost_model_version,
        TrafficSnapshot initial_traffic
    );

    // Readers retain a complete immutable bundle while updates prepare a new one.
    std::shared_ptr<const MetricSnapshot> current() const;

    // Preparation happens before publication. Any failure retains the old bundle.
    // Reusing the current version with different data and older validity windows
    // is rejected, including when publishers complete out of order.
    Result<void> publish(TrafficSnapshot traffic);

private:
    SnapshotManager(std::shared_ptr<const LoadedGraphSnapshot> graph,
                    std::vector<VehicleProfile> profiles,
                    CostModelOptions cost_options, std::string cost_model_version);
    Result<std::shared_ptr<const MetricSnapshot>> prepare(TrafficSnapshot traffic) const;

    std::shared_ptr<const LoadedGraphSnapshot> graph_;
    std::vector<VehicleProfile> profiles_;
    CostModelOptions cost_options_;
    std::string cost_model_version_;
    std::shared_ptr<const MetricSnapshot> active_;
    std::mutex publication_mutex_;
};

} // namespace ecoroute
