/*
 * File: bench_routes.cpp
 * Purpose: Reproducible phase timings for synthetic or supplied road snapshots.
 * Synthetic values are engineering fixtures, not vehicle or city calibration.
 */
#include "cost/cost_model.hpp"
#include "cost/weight_builder.hpp"
#include "graph/graph_loader.hpp"
#include "search/alternatives.hpp"
#include "search/cch_adapter.hpp"
#include "search/cch_order.hpp"
#include "search/dijkstra.hpp"
#include <routingkit/customizable_contraction_hierarchy.h>

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <random>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#endif

using namespace ecoroute;
namespace {
using Clock = std::chrono::steady_clock;
double milliseconds(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
template <class T>
T take(Result<T> result) {
    if (!result.ok()) throw std::runtime_error(result.error().message);
    return std::move(result.value());
}
void require(Result<void> result) {
    if (!result.ok()) throw std::runtime_error(result.error().message);
}
unsigned argument(const char* text, unsigned low, unsigned high) {
    unsigned value = 0;
    const std::string input(text);
    const auto parsed = std::from_chars(input.data(), input.data() + input.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != input.data() + input.size() ||
        value < low || value > high) throw std::runtime_error("Invalid benchmark size argument.");
    return value;
}
LoadedGraphSnapshot synthetic(unsigned side) {
    std::vector<RoadNode> nodes;
    std::vector<RoadSegment> edges;
    nodes.reserve(static_cast<std::size_t>(side) * side);
    auto id = [side](unsigned row, unsigned column) {
        return static_cast<RoadNodeId>(row) * side + column + 1;
    };
    auto add = [&](RoadNodeId from, RoadNodeId to, double length) {
        RoadSegment edge;
        edge.id = edges.size() + 1;
        edge.from = from;
        edge.to = to;
        edge.length_meters = length;
        edge.speed_limit_kph = 30.0 + 10.0 * (edge.id % 4);
        edges.push_back(std::move(edge));
    };
    for (unsigned row = 0; row < side; ++row) {
        for (unsigned column = 0; column < side; ++column) {
            nodes.push_back({id(row, column), {40.0 + row * 0.0005, -74.0 + column * 0.0005},
                             std::nullopt});
            // Alternating one-way streets and two-way cross streets. Adjacent
            // rows provide return paths; occasional parallel arcs exercise mapping.
            if (column + 1 < side) {
                if (row % 2 == 0) add(id(row, column), id(row, column + 1), 45.0);
                else add(id(row, column + 1), id(row, column), 45.0);
            }
            if (row + 1 < side) {
                add(id(row, column), id(row + 1, column), 55.0);
                add(id(row + 1, column), id(row, column), 55.0);
                if ((row + column) % 17 == 0) add(id(row, column), id(row + 1, column), 62.0);
            }
        }
    }
    auto network = take(RoadNetwork::create(std::move(nodes), std::move(edges)));
    auto graph = take(RoutingGraph::create(network));
    return {"synthetic-grid-v1", std::move(network), std::move(graph)};
}
std::uint64_t peakResidentBytes() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS info{};
    if (!K32GetProcessMemoryInfo(GetCurrentProcess(), &info, sizeof(info))) return 0;
    return static_cast<std::uint64_t>(info.PeakWorkingSetSize);
#else
    rusage info{};
    if (getrusage(RUSAGE_SELF, &info) != 0) return 0;
#ifdef __APPLE__
    return static_cast<std::uint64_t>(info.ru_maxrss);
#else
    return static_cast<std::uint64_t>(info.ru_maxrss) * 1024;
#endif
#endif
}
std::size_t hierarchyVectorBytes(const RoutingKit::CustomizableContractionHierarchy& h) {
    // This is a lower bound: excludes bit vectors, rank/select indexes and
    // allocator overhead. Peak process RSS is reported separately.
    std::size_t count = 0;
    for (const auto* vector : {&h.order, &h.rank, &h.elimination_tree_parent,
        &h.up_first_out, &h.up_head, &h.up_tail, &h.down_first_out, &h.down_head,
        &h.down_to_up, &h.input_arc_to_cch_arc, &h.forward_input_arc_of_cch,
        &h.backward_input_arc_of_cch, &h.first_extra_forward_input_arc_of_cch,
        &h.first_extra_backward_input_arc_of_cch, &h.extra_forward_input_arc_of_cch,
        &h.extra_backward_input_arc_of_cch}) count += vector->capacity();
    return count * sizeof(unsigned);
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc > 5) throw std::runtime_error(
            "Usage: ecoroute_bench [grid_side=50] [queries=2000] [threads=4] [snapshot_directory]");
        const unsigned side = argc > 1 ? argument(argv[1], 2, 1000) : 50;
        const unsigned count = argc > 2 ? argument(argv[2], 10, 1000000) : 2000;
        const unsigned worker_count = argc > 3 ? argument(argv[3], 1, 64) : 4;
        const auto load_start = Clock::now();
        auto loaded = [&]() {
            if (argc == 5) {
                const std::filesystem::path directory(argv[4]);
                return take(loadGraphSnapshot({directory / "metadata.txt", directory / "nodes.csv",
                                               directory / "edges.csv"}));
            }
            return synthetic(side);
        }();
        const double input_ms = milliseconds(load_start);
        const auto csr_start = Clock::now();
        auto graph = take(RoutingGraph::create(loaded.network));
        const double csr_ms = milliseconds(csr_start);

        const auto physical_start = Clock::now();
        VehicleProfile vehicle;
        vehicle.id = "synthetic-petrol";
        vehicle.consumption_per_100km = 7.0;
        vehicle.auxiliary_consumption_per_hour = 1.0;
        vehicle.co2e_grams_per_unit = 2300.0;
        std::vector<RouteMetrics> metrics;
        metrics.reserve(graph.arcCount());
        for (RoadSegmentId id : graph.segmentIds()) {
            const auto& segment = *loaded.network.findSegment(id);
            metrics.push_back(take(estimateSegmentMetrics(segment,
                *loaded.network.findNode(segment.from), *loaded.network.findNode(segment.to),
                vehicle, 1.0 + 0.1 * (id % 7), {30.0})));
        }
        const double physical_ms = milliseconds(physical_start);
        const auto weight_start = Clock::now();
        WeightBuilderOptions weight_options{60.0, 100.0, 1000000};
        auto weights = take(buildGraphSafeCchArcWeights(metrics, 0.5, weight_options, graph.nodeCount()));
        const double weight_ms = milliseconds(weight_start);
        const auto order_start = Clock::now();
        auto order = take(computeCchNodeOrder(loaded.network, graph));
        const double order_ms = milliseconds(order_start);

        const auto hierarchy_start = Clock::now();
        RoutingKit::CustomizableContractionHierarchy hierarchy(order, graph.tail(), graph.head());
        const double hierarchy_ms = milliseconds(hierarchy_start);
        const auto customization_start = Clock::now();
        unsigned sentinel = 0;
        RoutingKit::CustomizableContractionHierarchyMetric raw_metric(
            hierarchy, weights.arc_weights.empty() ? &sentinel : weights.arc_weights.data());
        raw_metric.customize();
        const double customization_ms = milliseconds(customization_start);
        const auto adapter_start = Clock::now();
        auto adapter = take(CchAdapter::create(graph, order, weights.arc_weights));
        const double adapter_build_ms = milliseconds(adapter_start);

        std::mt19937 random(20261025u);
        std::vector<std::pair<unsigned, unsigned>> requests;
        requests.reserve(count);
        for (unsigned i = 0; i < count; ++i) {
            requests.emplace_back(random() % graph.nodeCount(), random() % graph.nodeCount());
        }
        // Both raw-query measurements include distance and original-arc extraction.
        std::uint64_t fresh_checksum = 0, reused_checksum = 0, adapter_checksum = 0;
        const auto fresh_start = Clock::now();
        for (const auto& [source, target] : requests) {
            RoutingKit::CustomizableContractionHierarchyQuery query(raw_metric);
            query.add_source(source).add_target(target).run();
            fresh_checksum += query.get_distance();
            fresh_checksum += query.get_arc_path().size();
        }
        const double fresh_ms = milliseconds(fresh_start);
        RoutingKit::CustomizableContractionHierarchyQuery query(raw_metric);
        const auto reused_start = Clock::now();
        for (const auto& [source, target] : requests) {
            query.reset().add_source(source).add_target(target).run();
            reused_checksum += query.get_distance();
            reused_checksum += query.get_arc_path().size();
        }
        const double reused_ms = milliseconds(reused_start);
        const auto adapter_query_start = Clock::now();
        unsigned successful_queries = 0;
        for (const auto& [source, target] : requests) {
            auto path = adapter.shortestPath(source, target);
            if (path.ok()) {
                adapter_checksum += path.value().total_cost + path.value().arc_indices.size();
                ++successful_queries;
            } else if (path.error().code != ErrorCode::NoRoute) {
                throw std::runtime_error(path.error().message);
            }
        }
        const double adapter_query_ms = milliseconds(adapter_query_start);
        if (fresh_checksum != reused_checksum) throw std::runtime_error("Fresh/reused query mismatch.");

        // Reference validation is bounded and separate from measured query phases.
        for (unsigned i = 0; i < std::min(count, 20u); ++i) {
            auto reference = dijkstraShortestPath(graph, weights.arc_weights, requests[i].first, requests[i].second);
            auto optimized = adapter.shortestPath(requests[i].first, requests[i].second);
            if (reference.ok() != optimized.ok() ||
                (reference.ok() && reference.value().total_cost != optimized.value().total_cost) ||
                (!reference.ok() && reference.error().code != optimized.error().code)) {
                throw std::runtime_error("Dijkstra cross-check failed.");
            }
        }

        const auto identical_start = Clock::now();
        for (unsigned i = 0; i < 20; ++i) require(adapter.updateWeights(weights.arc_weights));
        const double identical_ms = milliseconds(identical_start) / 20;
        auto changed_weights = weights.arc_weights;
        // Scaling by <= 0.5 cannot exceed the finite bound.
        for (auto& weight : changed_weights) weight /= 2;
        const auto update_start = Clock::now();
        require(adapter.updateWeights(changed_weights));
        const double changed_update_ms = milliseconds(update_start);
        require(adapter.updateWeights(weights.arc_weights));

        const auto clone_start = Clock::now();
        auto snapshot = std::make_shared<CchAdapter>(take(adapter.withWeights(changed_weights)));
        const double snapshot_prepare_ms = milliseconds(clone_start);
        std::shared_ptr<CchAdapter> active;
        const auto publish_start = Clock::now();
        std::atomic_store(&active, snapshot);
        const double snapshot_publish_us = milliseconds(publish_start) * 1000.0;

        const unsigned alternative_count = std::min(count, 20u);
        auto alternative_timing = [&](unsigned neighbors) {
            const auto start = Clock::now();
            for (unsigned i = 0; i < alternative_count; ++i) {
                AlternativeRouteOptions options;
                options.selected_time_preference = 0.5;
                options.neighboring_steps_each_side = neighbors;
                options.max_pool_routes = neighbors ? 5 : 3;
                auto result = generateAlternativeRoutes(adapter, graph, metrics,
                    requests[i].first, requests[i].second, weight_options, options);
                if (!result.ok() && result.error().code != ErrorCode::NoRoute) {
                    throw std::runtime_error(result.error().message);
                }
            }
            return milliseconds(start) / alternative_count;
        };
        const double essential_ms = alternative_timing(0);
        const double prefetch_ms = alternative_timing(1);
        require(adapter.updateWeights(weights.arc_weights));

        std::vector<CchAdapter> workers;
        workers.reserve(worker_count);
        const auto workers_prepare_start = Clock::now();
        for (unsigned i = 0; i < worker_count; ++i) {
            workers.push_back(take(adapter.withWeights(weights.arc_weights)));
        }
        const double workers_prepare_ms = milliseconds(workers_prepare_start);
        auto concurrent_timing = [&](bool shared) {
            std::vector<std::thread> threads;
            std::atomic<bool> failed{false};
            std::atomic<std::uint64_t> checksum{0};
            const auto start = Clock::now();
            for (unsigned worker = 0; worker < worker_count; ++worker) {
                threads.emplace_back([&, worker] {
                    std::uint64_t local = 0;
                    for (unsigned i = worker; i < count; i += worker_count) {
                        auto path = (shared ? adapter : workers[worker]).shortestPath(
                            requests[i].first, requests[i].second);
                        if (path.ok()) local += path.value().total_cost + path.value().arc_indices.size();
                        else if (path.error().code != ErrorCode::NoRoute) failed = true;
                    }
                    checksum += local;
                });
            }
            for (auto& thread : threads) thread.join();
            const double elapsed = milliseconds(start);
            if (failed || checksum != adapter_checksum) throw std::runtime_error("Concurrent query mismatch.");
            return elapsed;
        };
        const double shared_ms = concurrent_timing(true);
        const double independent_ms = concurrent_timing(false);
        const std::size_t metric_query_estimate = graph.arcCount() * sizeof(unsigned) +
            (raw_metric.forward.capacity() + raw_metric.backward.capacity()) * sizeof(unsigned) +
            graph.nodeCount() * (4 * sizeof(unsigned)) + (2 * graph.nodeCount() + 7) / 8;

        std::cout << std::fixed << std::setprecision(6)
            << "{\n  \"dataset\": \"" << (argc == 5 ? "supplied-snapshot" : "synthetic-directed-grid")
            << "\",\n  \"seed\": 20261025,\n  \"nodes\": " << graph.nodeCount()
            << ",\n  \"arcs\": " << graph.arcCount() << ",\n  \"queries\": " << count
            << ",\n  \"successful_queries\": " << successful_queries << ",\n  \"threads\": " << worker_count
            << ",\n  \"input_load_or_generation_including_csr_ms\": " << input_ms
            << ",\n  \"csr_rebuild_ms\": " << csr_ms << ",\n  \"physical_metrics_ms\": " << physical_ms
            << ",\n  \"weight_conversion_ms\": " << weight_ms << ",\n  \"nested_dissection_ms\": " << order_ms
            << ",\n  \"hierarchy_ms\": " << hierarchy_ms << ",\n  \"metric_customization_ms\": " << customization_ms
            << ",\n  \"adapter_topology_metric_build_ms\": " << adapter_build_ms
            << ",\n  \"fresh_raw_query_mean_us\": " << fresh_ms * 1000 / count
            << ",\n  \"reused_raw_query_mean_us\": " << reused_ms * 1000 / count
            << ",\n  \"validated_adapter_query_mean_us\": " << adapter_query_ms * 1000 / count
            << ",\n  \"identical_update_mean_ms\": " << identical_ms
            << ",\n  \"changed_update_ms\": " << changed_update_ms
            << ",\n  \"metric_snapshot_prepare_ms\": " << snapshot_prepare_ms
            << ",\n  \"metric_snapshot_publication_us\": " << snapshot_publish_us
            << ",\n  \"essential_three_preferences_mean_ms\": " << essential_ms
            << ",\n  \"five_preferences_mean_ms\": " << prefetch_ms
            << ",\n  \"worker_metrics_prepare_ms\": " << workers_prepare_ms
            << ",\n  \"shared_adapter_concurrent_queries_per_second\": " << 1000 * count / shared_ms
            << ",\n  \"independent_adapters_queries_per_second\": " << 1000 * count / independent_ms
            << ",\n  \"hierarchy_vector_bytes_lower_bound\": " << hierarchyVectorBytes(hierarchy)
            << ",\n  \"per_metric_query_bytes_estimate\": " << metric_query_estimate
            << ",\n  \"whole_benchmark_peak_resident_bytes\": " << peakResidentBytes()
            << ",\n  \"effective_integer_scale\": " << weights.effective_integer_scale
            << ",\n  \"maximum_path_objective_error\": " << weights.maximum_path_objective_error
            << ",\n  \"dijkstra_cross_checks\": " << std::min(count, 20u)
            << ",\n  \"checksum\": " << adapter_checksum << "\n}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Benchmark failed: " << error.what() << '\n';
        return 1;
    }
}