/*
 * Coordinates graph loading, immutable traffic metrics, exact objective caching,
 * CCH search, physical evaluation, endpoint snapping and public route selection.
 */
#include "ecoroute/engine.hpp"
#include "graph/graph_loader.hpp"
#include "graph/snapper.hpp"
#include "runtime/configuration.hpp"
#include "runtime/order_cache.hpp"
#include "runtime/snapshot_manager.hpp"
#include "search/alternatives.hpp"
#include "search/cch_adapter.hpp"
#include "search/cch_order.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <list>
#include <locale>
#include <mutex>
#include <new>
#include <sstream>

namespace ecoroute {
namespace {
template<class T> Result<T> failure(const Error& error) {
    return Result<T>::failure(error.code, error.message);
}
Result<void> validateRequest(const RouteRequest& r) {
    if (!std::isfinite(r.urgency_preference) || r.urgency_preference < 0.0 ||
        r.urgency_preference > 1.0 || r.vehicle_profile_id.empty() ||
        r.departure_time_unix_seconds < 0 ||
        !std::isfinite(r.max_extra_time_ratio) || r.max_extra_time_ratio < 0.0 ||
        !std::isfinite(r.max_extra_time_seconds) || r.max_extra_time_seconds < 0.0 ||
        r.max_routes < 1 || r.max_routes > 3) {
        return Result<void>::failure(ErrorCode::InvalidArgument,
            "Invalid preference, vehicle, departure, detour threshold or route count (1..3).");
    }
    return Result<void>::success();
}
TrafficPolicy trafficPolicy(const EngineOptions& o) {
    return {o.allow_missing_traffic ? MissingTrafficPolicy::FreeFlow : MissingTrafficPolicy::Reject,
            o.max_traffic_multiplier};
}
bool sameCoordinate(const Coordinate& a, const Coordinate& b) {
    return a.latitude == b.latitude && a.longitude == b.longitude;
}
std::string requestKey(unsigned source, unsigned target, const RouteRequest& r,
                       const SnapResult& from, const SnapResult& to) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    // Hexadecimal floating-point retains exact double preferences and coordinates.
    out << source << ' ' << target << ' ' << std::quoted(r.vehicle_profile_id) << ' '
        << r.departure_time_unix_seconds << ' ' << r.max_routes << ' ' << std::hexfloat
        << r.urgency_preference << ' ' << r.max_extra_time_ratio << ' '
        << r.max_extra_time_seconds << ' ' << from.distance_meters << ' '
        << to.distance_meters;
    return out.str();
}
} // namespace

struct Engine::Impl {
    EngineOptions options;
    std::shared_ptr<const LoadedGraphSnapshot> graph;
    NodeSnapper snapper;
    std::unique_ptr<SnapshotManager> snapshots;
    mutable CchAdapter topology;

    struct PreparedObjective {
        std::shared_ptr<const MetricSnapshot> snapshot;
        std::string profile;
        double preference;
        std::shared_ptr<CchAdapter> cch;
        double scale;
        double error_bound;
    };
    struct CachedRoute {
        std::shared_ptr<const MetricSnapshot> snapshot;
        std::string key;
        RouteResponse response;
    };
    mutable std::mutex cache_mutex;
    mutable std::list<PreparedObjective> objectives;
    mutable std::list<CachedRoute> routes;

    Impl(EngineOptions opts, std::shared_ptr<const LoadedGraphSnapshot> loaded,
         NodeSnapper index, std::unique_ptr<SnapshotManager> manager, CchAdapter hierarchy)
        : options(std::move(opts)), graph(std::move(loaded)), snapper(std::move(index)),
          snapshots(std::move(manager)), topology(std::move(hierarchy)) {}

    Result<PreparedObjective> objective(const std::shared_ptr<const MetricSnapshot>& snapshot,
                                        const std::string& profile, double preference) const {
        {
            std::lock_guard<std::mutex> lock(cache_mutex);
            for (auto it = objectives.begin(); it != objectives.end(); ++it) {
                if (it->snapshot == snapshot && it->profile == profile && it->preference == preference) {
                    objectives.splice(objectives.begin(), objectives, it);
                    return Result<PreparedObjective>::success(objectives.front());
                }
            }
        }
        const auto* metrics = snapshot->profileMetrics(profile);
        if (!metrics) return Result<PreparedObjective>::failure(ErrorCode::UnsupportedProfile,
                                                               "Unknown vehicle profile.");
        WeightBuilderOptions weight_options{options.reference_time_seconds,
                                             options.reference_co2e_grams, options.integer_scale};
        auto weights = buildGraphSafeCchArcWeights(*metrics, preference, weight_options,
                                                   graph->routing_graph.nodeCount());
        if (!weights.ok()) return failure<PreparedObjective>(weights.error());
        const double scale = weights.value().effective_integer_scale;
        const double bound = weights.value().maximum_path_objective_error;
        auto customized = topology.withWeights(std::move(weights.value().arc_weights));
        if (!customized.ok()) return failure<PreparedObjective>(customized.error());
        PreparedObjective result{snapshot, profile, preference,
            std::make_shared<CchAdapter>(std::move(customized.value())), scale, bound};
        if (options.max_cached_objectives) {
            std::lock_guard<std::mutex> lock(cache_mutex);
            // A concurrent request may have prepared the same exact objective.
            for (const auto& cached : objectives)
                if (cached.snapshot == snapshot && cached.profile == profile && cached.preference == preference)
                    return Result<PreparedObjective>::success(cached);
            objectives.push_front(result);
            while (objectives.size() > options.max_cached_objectives) objectives.pop_back();
        }
        return Result<PreparedObjective>::success(std::move(result));
    }

    Result<RouteResponse> route(const RouteRequest& request, const SnapResult& from,
                                const SnapResult& to) const {
        auto valid = validateRequest(request);
        if (!valid.ok()) return failure<RouteResponse>(valid.error());
        // This shared ownership is the query's snapshot boundary. An update cannot
        // change either traffic factors or physical metrics used below.
        auto snapshot = snapshots->current();
        auto time_valid = snapshot->traffic().validateTime(request.departure_time_unix_seconds);
        if (!time_valid.ok()) return failure<RouteResponse>(time_valid.error());
        const auto* metrics = snapshot->profileMetrics(request.vehicle_profile_id);
        if (!metrics) return Result<RouteResponse>::failure(ErrorCode::UnsupportedProfile, "Unknown vehicle profile.");
        const auto key = requestKey(from.node_index, to.node_index, request, from, to);
        {
            std::lock_guard<std::mutex> lock(cache_mutex);
            for (auto it = routes.begin(); it != routes.end(); ++it) {
                if (it->snapshot == snapshot && it->key == key) {
                    routes.splice(routes.begin(), routes, it);
                    return Result<RouteResponse>::success(routes.front().response);
                }
            }
        }
        AlternativeRouteOptions route_options;
        route_options.verify_graph_identity = false; // bound to this immutable graph at Engine::create
        route_options.selected_time_preference = request.urgency_preference;
        route_options.neighboring_steps_each_side = 0; // Prefetch is opt-in in the lower-level API.
        route_options.max_pool_routes = 3;
        WeightBuilderOptions weight_options{options.reference_time_seconds,
                                             options.reference_co2e_grams, options.integer_scale};
        auto provider = [&](double preference) -> Result<ObjectivePath> {
            auto prepared = objective(snapshot, request.vehicle_profile_id, preference);
            if (!prepared.ok()) return failure<ObjectivePath>(prepared.error());
            auto path = prepared.value().cch->shortestPath(from.node_index, to.node_index);
            if (!path.ok()) return failure<ObjectivePath>(path.error());
            return Result<ObjectivePath>::success({std::move(path.value()),
                                                   prepared.value().scale, prepared.value().error_bound});
        };
        // The supplied callback uses immutable objective adapters. This topology
        // handle is only read by the generator's graph compatibility check.
        auto candidates = generateAlternativeRoutes(topology,
            graph->routing_graph, *metrics, from.node_index, to.node_index,
            weight_options, route_options, provider);
        if (!candidates.ok()) return failure<RouteResponse>(candidates.error());

        RouteResponse response;
        response.graph_version = graph->graph_version;
        response.traffic_version = snapshot->traffic().metadata().traffic_version;
        response.cost_model_version = snapshot->costModelVersion();
        response.objective_version = options.objective_version;
        response.selected_preference = request.urgency_preference;
        response.snapped_origin = from.location;
        response.snapped_destination = to.location;
        response.origin_snap_distance_meters = from.distance_meters;
        response.destination_snap_distance_meters = to.distance_meters;
        response.warnings.push_back("Energy and CO2e are uncalibrated model estimates.");
        response.warnings.push_back("Nearest-node routing does not model turn restrictions or road access permissions.");
        if (snapshot->traffic().missingFactorCount())
            response.warnings.push_back("Missing traffic factors explicitly use free-flow values.");
        const auto& fastest = candidates.value().front().totals;
        for (const auto& candidate : candidates.value()) {
            Route route;
            route.id = "route-" + std::to_string(response.routes.size() + 1);
            route.metrics = candidate.totals;
            route.matching_preferences = candidate.matching_preferences;
            for (const auto& score : candidate.objective_scores)
                route.objective_scores.push_back({score.time_preference, score.total_cost,
                    score.integer_scale, score.maximum_path_objective_error});
            for (double preference : route.matching_preferences) {
                if (preference == 1.0) route.roles.push_back("fastest");
                if (preference == request.urgency_preference) {
                    route.roles.push_back("selected");
                    response.recommended_route_id = route.id;
                }
                if (preference == 0.0) route.roles.push_back("lowest_emissions");
            }
            route.extra_time_seconds = route.metrics.duration_seconds - fastest.duration_seconds;
            route.co2e_saved_grams = fastest.co2e_grams - route.metrics.co2e_grams;
            const double permitted_ratio_delay = fastest.duration_seconds * request.max_extra_time_ratio;
            route.exceeds_detour_limit = route.extra_time_seconds > permitted_ratio_delay ||
                                          route.extra_time_seconds > request.max_extra_time_seconds;
            route.geometry.push_back(from.location);
            for (auto arc : candidate.path.arc_indices) {
                const auto segment_id = graph->routing_graph.segmentIds()[arc];
                route.road_segment_ids.push_back(segment_id);
                const auto& segment = *graph->network.findSegment(segment_id);
                auto append = [&](const Coordinate& point) {
                    if (!sameCoordinate(route.geometry.back(), point)) route.geometry.push_back(point);
                };
                if (segment.geometry.empty()) {
                    route.geometry_is_approximate = true;
                    append(graph->network.findNode(segment.to)->location);
                } else {
                    for (const auto& point : segment.geometry) append(point);
                    // Geometry validation permits small endpoint tolerance.
                    append(graph->network.findNode(segment.to)->location);
                }
            }
            response.routes.push_back(std::move(route));
        }
        // When a caller requests fewer cards, preserve its exact selected route.
        if (response.routes.size() > request.max_routes) {
            auto selected = std::find_if(response.routes.begin(), response.routes.end(),
                [&](const Route& route) { return route.id == response.recommended_route_id; });
            std::rotate(response.routes.begin(), selected, selected + 1);
            response.routes.resize(request.max_routes);
        }
        if (options.max_cached_routes) {
            std::lock_guard<std::mutex> lock(cache_mutex);
            routes.push_front({snapshot, key, response});
            while (routes.size() > options.max_cached_routes) routes.pop_back();
        }
        return Result<RouteResponse>::success(std::move(response));
    }
};

Engine::Engine(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Engine::~Engine() = default;
Engine::Engine(Engine&&) noexcept = default;
Engine& Engine::operator=(Engine&&) noexcept = default;

Result<Engine> Engine::create(const EngineOptions& options) {
    try {
        auto valid = validateEngineOptions(options);
        if (!valid.ok()) return failure<Engine>(valid.error());
        const std::filesystem::path directory(options.graph_file);
        auto loaded = loadGraphSnapshot({directory / "metadata.txt", directory / "nodes.csv", directory / "edges.csv"});
        if (!loaded.ok()) return failure<Engine>(loaded.error());
        auto graph = std::make_shared<const LoadedGraphSnapshot>(std::move(loaded.value()));
        auto profiles = loadVehicleProfiles(options.vehicle_profiles_file);
        if (!profiles.ok()) return failure<Engine>(profiles.error());
        auto traffic = [&]() -> Result<TrafficSnapshot> {
            if (!options.traffic_file.empty()) {
                const std::filesystem::path path(options.traffic_file);
                return loadTrafficSnapshot({path / "metadata.txt", path / "factors.csv"},
                    graph->routing_graph, graph->graph_version, trafficPolicy(options));
            }
            return TrafficSnapshot::create(graph->routing_graph, graph->graph_version,
                {graph->graph_version, "explicit-free-flow", 0, std::numeric_limits<std::int64_t>::max()},
                {}, {MissingTrafficPolicy::FreeFlow, options.max_traffic_multiplier});
        }();
        if (!traffic.ok()) return failure<Engine>(traffic.error());
        auto manager = SnapshotManager::create(graph, std::move(profiles.value()),
            {options.fallback_speed_kph}, options.cost_model_version, std::move(traffic.value()));
        if (!manager.ok()) return failure<Engine>(manager.error());
        auto snapper = NodeSnapper::create(graph->network, graph->routing_graph);
        if (!snapper.ok()) return failure<Engine>(snapper.error());
        auto order = options.cch_topology_file.empty()
            ? computeCchNodeOrder(graph->network, graph->routing_graph)
            : loadCchOrder(options.cch_topology_file, *graph);
        if (!order.ok()) return failure<Engine>(order.error());
        auto cch = CchAdapter::create(graph->routing_graph, std::move(order.value()),
                                    std::vector<unsigned>(graph->routing_graph.arcCount(), 0));
        if (!cch.ok()) return failure<Engine>(cch.error());
        return Result<Engine>::success(Engine(std::make_unique<Impl>(options, std::move(graph),
            std::move(snapper.value()), std::move(manager.value()), std::move(cch.value()))));
    } catch (const std::bad_alloc&) {
        return Result<Engine>::failure(ErrorCode::ResourceExhausted, "Engine initialization exhausted memory.");
    } catch (const std::exception& e) {
        return Result<Engine>::failure(ErrorCode::InternalError, e.what());
    }
}

Result<RouteResponse> Engine::route(const RouteRequest& request) const {
    if (!impl_) return Result<RouteResponse>::failure(ErrorCode::Unavailable, "Engine is not initialized.");
    try {
        auto valid = validateRequest(request);
        if (!valid.ok()) return failure<RouteResponse>(valid.error());
        const bool self = sameCoordinate(request.origin, request.destination);
        auto from = impl_->snapper.snap(request.origin, impl_->options.max_snap_distance_meters,
                                       self ? SnapRole::Any : SnapRole::Source);
        if (!from.ok()) return failure<RouteResponse>(from.error());
        auto to = self ? from : impl_->snapper.snap(request.destination,
                               impl_->options.max_snap_distance_meters, SnapRole::Target);
        if (!to.ok()) return failure<RouteResponse>(to.error());
        return impl_->route(request, from.value(), to.value());
    } catch (const std::bad_alloc&) {
        return Result<RouteResponse>::failure(ErrorCode::ResourceExhausted, "Routing exhausted memory.");
    } catch (const std::exception& e) {
        return Result<RouteResponse>::failure(ErrorCode::InternalError, e.what());
    }
}

Result<RouteResponse> Engine::routeNodes(std::uint64_t source, std::uint64_t target,
                                         const RouteRequest& request) const {
    if (!impl_) return Result<RouteResponse>::failure(ErrorCode::Unavailable, "Engine is not initialized.");
    try {
        auto from = impl_->graph->routing_graph.findNodeIndex(source);
        auto to = impl_->graph->routing_graph.findNodeIndex(target);
        if (!from || !to) return Result<RouteResponse>::failure(ErrorCode::InvalidArgument, "Unknown external node ID.");
        return impl_->route(request,
            {*from, source, impl_->graph->network.findNode(source)->location, 0.0},
            {*to, target, impl_->graph->network.findNode(target)->location, 0.0});
    } catch (const std::bad_alloc&) {
        return Result<RouteResponse>::failure(ErrorCode::ResourceExhausted, "Routing exhausted memory.");
    } catch (const std::exception& e) {
        return Result<RouteResponse>::failure(ErrorCode::InternalError, e.what());
    }
}

Result<void> Engine::updateTraffic(const std::string& snapshot_directory) {
    if (!impl_) return Result<void>::failure(ErrorCode::Unavailable, "Engine is not initialized.");
    try {
        const std::filesystem::path path(snapshot_directory);
        auto traffic = loadTrafficSnapshot({path / "metadata.txt", path / "factors.csv"},
            impl_->graph->routing_graph, impl_->graph->graph_version, trafficPolicy(impl_->options));
        if (!traffic.ok()) return failure<void>(traffic.error());
        auto published = impl_->snapshots->publish(std::move(traffic.value()));
        if (published.ok()) {
            std::lock_guard<std::mutex> lock(impl_->cache_mutex);
            impl_->objectives.clear();
            impl_->routes.clear();
        }
        return published;
    } catch (const std::bad_alloc&) {
        return Result<void>::failure(ErrorCode::ResourceExhausted, "Traffic update exhausted memory.");
    } catch (const std::exception& e) {
        return Result<void>::failure(ErrorCode::InternalError, e.what());
    }
}

EngineStatus Engine::status() const {
    if (!impl_) return {false, {}, {}, "Engine is not initialized."};
    auto snapshot = impl_->snapshots->current();
    return {true, impl_->graph->graph_version, snapshot->traffic().metadata().traffic_version,
            "Ready; traffic validity is checked against each request departure time."};
}
} // namespace ecoroute
