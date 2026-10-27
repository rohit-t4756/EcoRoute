#include "runtime/snapshot_manager.hpp"

#include <atomic>
#include <iostream>
#include <thread>

using namespace ecoroute;
namespace {
int failures = 0;
void check(bool condition, const char* name) {
    if (!condition) { std::cerr << "[FAIL] " << name << '\n'; ++failures; }
}
TrafficSnapshot traffic(const RoutingGraph& graph, const std::string& version,
                        double multiplier, std::int64_t start = 100) {
    return std::move(TrafficSnapshot::create(graph, "graph-v1",
        {"graph-v1", version, start, 10000}, {{11, multiplier}, {12, multiplier}}).value());
}
}

int main() {
    auto network = RoadNetwork::create(
        {{1, {0, 0}, {}}, {2, {0, 0.01}, {}}, {3, {0, 0.02}, {}}},
        {{12, 2, 3, 100, 36, {}, {}}, {11, 1, 2, 100, 36, {}, {}}});
    if (!network.ok()) return 1;
    auto graph = RoutingGraph::create(network.value());
    if (!graph.ok()) return 1;
    auto loaded = std::make_shared<const LoadedGraphSnapshot>(LoadedGraphSnapshot{
        "graph-v1", std::move(network.value()), std::move(graph.value())});
    std::vector<VehicleProfile> profiles;
    for (const auto type : {VehicleType::Petrol, VehicleType::Diesel, VehicleType::Hybrid, VehicleType::Electric}) {
        VehicleProfile profile;
        profile.id = std::to_string(static_cast<int>(type));
        profile.type = type;
        profile.consumption_per_100km = 10;
        profile.auxiliary_consumption_per_hour = 1;
        profile.co2e_grams_per_unit = 1000;
        profiles.push_back(profile);
    }
    auto created = SnapshotManager::create(loaded, profiles, {30}, "model-v1",
                                          traffic(loaded->routing_graph, "traffic-1", 1));
    check(created.ok(), "Runtime initializes validated immutable bundle");
    if (!created.ok()) return 1;
    auto& manager = *created.value();
    const auto old = manager.current();
    check(old->costModelVersion() == "model-v1" && !old->profileMetrics("unknown"), "Version and unknown profile");
    for (const auto& profile : profiles) {
        const auto* metrics = old->profileMetrics(profile.id);
        check(metrics && metrics->size() == 2, "All vehicle profiles prepared in CSR order");
        if (metrics) {
            check((*metrics)[0].duration_seconds == 10 && (*metrics)[0].co2e_grams == 10,
                  "Independent 100m/36kph and 10 units/100km arithmetic");
            check(profile.type == VehicleType::Electric ? (*metrics)[0].electricity_wh == 10 :
                  (*metrics)[0].fuel_milliliters == 10, "Correct energy units");
        }
    }
    check(manager.publish(traffic(loaded->routing_graph, "traffic-2", 2, 101)).ok(), "Traffic update publishes");
    const auto updated = manager.current();
    check(updated != old && updated->profileMetrics("0")->at(0).duration_seconds == 20, "Updated physical metrics");
    check(old->profileMetrics("0")->at(0).duration_seconds == 10 &&
          old->traffic().metadata().traffic_version == "traffic-1", "In-flight readers retain original state");
    check(!manager.publish(traffic(loaded->routing_graph, "traffic-2", 3, 101)).ok() &&
          manager.current() == updated, "Conflicting current version retains active bundle");
    check(!manager.publish(traffic(loaded->routing_graph, "older", 1, 100)).ok() &&
          manager.current() == updated, "Older publication rejected atomically");
    check(!manager.publish(traffic(loaded->routing_graph, "same-time", 3, 101)).ok() &&
          manager.current() == updated, "Ambiguous same-time publication rejected");
    check(manager.publish(traffic(loaded->routing_graph, "traffic-2", 2, 101)).ok() &&
          manager.current() == updated, "Idempotent update preserves snapshot identity");
    auto other = TrafficSnapshot::create(loaded->routing_graph, "other-graph",
        {"other-graph", "bad", 102, 10000}, {{11, 3}, {12, 3}});
    check(other.ok() && !manager.publish(std::move(other.value())).ok() && manager.current() == updated,
          "Foreign graph cannot replace active state");
    // Concurrent readers must see matching traffic and per-profile physical data.
    std::atomic<bool> stop{false};
    std::atomic<unsigned> observed{0}, inconsistencies{0};
    std::vector<std::thread> readers;
    for (int thread = 0; thread < 4; ++thread) readers.emplace_back([&] {
        do {
            const auto snapshot = manager.current();
            for (const auto& profile : profiles) {
                const auto* metrics = snapshot->profileMetrics(profile.id);
                for (std::size_t arc = 0; arc < metrics->size(); ++arc)
                    if (metrics->at(arc).duration_seconds != 10 * snapshot->traffic().multipliers()[arc])
                        ++inconsistencies;
            }
            ++observed;
        } while (!stop.load(std::memory_order_relaxed));
    });
    for (int update = 3; update <= 40; ++update)
        check(manager.publish(traffic(loaded->routing_graph, "traffic-" + std::to_string(update),
                                     1 + update % 5, 100 + update)).ok(), "Concurrent publication accepted");
    stop.store(true, std::memory_order_relaxed);
    for (auto& reader : readers) reader.join();
    check(observed > 0 && inconsistencies == 0, "Concurrent readers observe consistent bundles");
    check(old->profileMetrics("0")->at(0).duration_seconds == 10, "Retired snapshot remains alive while referenced");
    auto duplicate_profiles = profiles; duplicate_profiles.push_back(profiles.front());
    check(!SnapshotManager::create(loaded, duplicate_profiles, {30}, "model",
        traffic(loaded->routing_graph, "initial", 1)).ok(), "Duplicate profiles rejected at startup");
    profiles.front().consumption_per_100km = -1;
    check(!SnapshotManager::create(loaded, profiles, {30}, "model",
        traffic(loaded->routing_graph, "initial", 1)).ok(), "Invalid profiles rejected before publication");
    std::cout << "RuntimeTests failures: " << failures << '\n';
    return failures == 0 ? 0 : 1;
}
