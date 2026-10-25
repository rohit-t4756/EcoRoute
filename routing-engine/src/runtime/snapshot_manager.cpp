/*
 * File: snapshot_manager.cpp
 * Purpose: Precomputes profile metrics and atomically publishes complete bundles.
 * Status: IMPLEMENTED
 * Remaining: External publisher integration and large-network memory measurement.
 */

#include "snapshot_manager.hpp"

#include <atomic>
#include <cmath>
#include <new>
#include <unordered_set>
#include <utility>

namespace ecoroute {

MetricSnapshot::MetricSnapshot(TrafficSnapshot traffic, std::string cost_model_version)
    : traffic_(std::move(traffic)), cost_model_version_(std::move(cost_model_version)) {}

const TrafficSnapshot& MetricSnapshot::traffic() const { return traffic_; }
const std::string& MetricSnapshot::costModelVersion() const { return cost_model_version_; }

const std::vector<RouteMetrics>* MetricSnapshot::profileMetrics(const std::string& id) const {
    const auto found = metrics_.find(id);
    return found == metrics_.end() ? nullptr : &found->second;
}

SnapshotManager::SnapshotManager(std::shared_ptr<const LoadedGraphSnapshot> graph,
                                 std::vector<VehicleProfile> profiles,
                                 CostModelOptions cost_options, std::string cost_model_version)
    : graph_(std::move(graph)), profiles_(std::move(profiles)), cost_options_(cost_options),
      cost_model_version_(std::move(cost_model_version)) {}

Result<std::unique_ptr<SnapshotManager>> SnapshotManager::create(
    std::shared_ptr<const LoadedGraphSnapshot> graph,
    std::vector<VehicleProfile> profiles,
    CostModelOptions cost_options,
    std::string cost_model_version,
    TrafficSnapshot initial_traffic
) {
    using CreationResult = Result<std::unique_ptr<SnapshotManager>>;
    if (!graph || graph->graph_version.empty() || profiles.empty() || cost_model_version.empty() ||
        !std::isfinite(cost_options.fallback_speed_kph) || cost_options.fallback_speed_kph <= 0.0) {
        return CreationResult::failure(ErrorCode::InvalidArgument, "Invalid runtime graph, profiles or cost configuration.");
    }
    try {
        std::unordered_set<std::string> ids;
        for (const auto& profile : profiles) {
            auto validation = validateVehicleProfile(profile);
            if (!validation.ok()) {
                return CreationResult::failure(validation.error().code, validation.error().message);
            }
            if (!ids.insert(profile.id).second) {
                return CreationResult::failure(ErrorCode::InvalidArgument, "Duplicate vehicle profile ID.");
            }
        }
        std::unique_ptr<SnapshotManager> manager(new SnapshotManager(
            std::move(graph), std::move(profiles), cost_options, std::move(cost_model_version)));
        auto initial = manager->prepare(std::move(initial_traffic));
        if (!initial.ok()) return CreationResult::failure(initial.error().code, initial.error().message);
        manager->active_ = std::move(initial.value());
        return CreationResult::success(std::move(manager));
    } catch (const std::bad_alloc&) {
        return CreationResult::failure(ErrorCode::ResourceExhausted, "Cannot allocate runtime snapshot manager.");
    }
}

Result<std::shared_ptr<const MetricSnapshot>> SnapshotManager::prepare(TrafficSnapshot traffic) const {
    using PreparationResult = Result<std::shared_ptr<const MetricSnapshot>>;
    const auto& graph = graph_->routing_graph;
    const auto& network = graph_->network;
    if (!traffic.isAlignedWith(graph, graph_->graph_version)) {
        return PreparationResult::failure(ErrorCode::VersionMismatch, "Traffic graph version or CSR alignment differs.");
    }
    if (network.segmentCount() != graph.arcCount() || network.nodeCount() != graph.nodeCount()) {
        return PreparationResult::failure(ErrorCode::InvalidGraph, "Runtime physical graph and CSR dimensions differ.");
    }
    try {
        std::shared_ptr<MetricSnapshot> snapshot(new MetricSnapshot(std::move(traffic), cost_model_version_));
        for (const auto& profile : profiles_) {
            std::vector<RouteMetrics> metrics;
            metrics.reserve(graph.arcCount());
            for (std::size_t arc = 0; arc < graph.arcCount(); ++arc) {
                const auto* segment = network.findSegment(graph.segmentIds()[arc]);
                const auto* from = network.findNode(graph.nodeIds()[graph.tail()[arc]]);
                const auto* to = network.findNode(graph.nodeIds()[graph.head()[arc]]);
                if (!segment || !from || !to || segment->from != from->id || segment->to != to->id) {
                    return PreparationResult::failure(ErrorCode::InvalidGraph, "Runtime cannot resolve a directed CSR arc.");
                }
                auto estimated = estimateSegmentMetrics(*segment, *from, *to, profile,
                    snapshot->traffic_.multipliers()[arc], cost_options_);
                if (!estimated.ok()) {
                    return PreparationResult::failure(estimated.error().code, estimated.error().message);
                }
                metrics.push_back(estimated.value());
            }
            snapshot->metrics_.emplace(profile.id, std::move(metrics));
        }
        return PreparationResult::success(std::move(snapshot));
    } catch (const std::bad_alloc&) {
        return PreparationResult::failure(ErrorCode::ResourceExhausted, "Cannot allocate profile metric snapshot.");
    }
}

std::shared_ptr<const MetricSnapshot> SnapshotManager::current() const {
    return std::atomic_load_explicit(&active_, std::memory_order_acquire);
}

Result<void> SnapshotManager::publish(TrafficSnapshot traffic) {
    auto prepared = prepare(std::move(traffic));
    if (!prepared.ok()) return Result<void>::failure(prepared.error().code, prepared.error().message);

    // Preparation can be expensive; it does not hold the publication mutex.
    // Compare versions under the mutex so an older concurrent update cannot win.
    std::lock_guard<std::mutex> lock(publication_mutex_);
    const auto previous = current();
    const auto& next = prepared.value()->traffic().metadata();
    const auto& old = previous->traffic().metadata();
    if (next.traffic_version == old.traffic_version) {
        if (next.valid_from_unix_seconds == old.valid_from_unix_seconds &&
            next.valid_until_unix_seconds == old.valid_until_unix_seconds &&
            prepared.value()->traffic().multipliers() == previous->traffic().multipliers() &&
            prepared.value()->traffic().missingFactorCount() == previous->traffic().missingFactorCount()) {
            return Result<void>::success();
        }
        return Result<void>::failure(ErrorCode::VersionMismatch, "A traffic version cannot identify different data.");
    }
    if (next.valid_from_unix_seconds <= old.valid_from_unix_seconds) {
        return Result<void>::failure(ErrorCode::InvalidTrafficSnapshot,
            "A new traffic version must have a strictly newer validity start.");
    }
    std::atomic_store_explicit(&active_, std::move(prepared.value()), std::memory_order_release);
    return Result<void>::success();
}

} // namespace ecoroute
