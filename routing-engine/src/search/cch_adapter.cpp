/*
 * File: cch_adapter.cpp
 * Purpose: Wraps RoutingKit CCH with shared immutable topology, independent
 *          metrics, reusable synchronized queries and checked reconstruction.
 * Status: IMPLEMENTED
 * Remaining: Large-network throughput and hierarchy persistence.
 */

#include "cch_adapter.hpp"

#include <routingkit/constants.h>
#include <routingkit/customizable_contraction_hierarchy.h>

#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <utility>
#include <vector>

namespace ecoroute {
namespace {

Result<void> validateWeights(const std::vector<unsigned>& weights,
                             std::size_t expected_count) {
    if (weights.size() != expected_count) {
        return Result<void>::failure(ErrorCode::InvalidMetric,
            "Weight count does not match CSR arc count.");
    }
    for (std::size_t i = 0; i < weights.size(); ++i) {
        if (weights[i] >= RoutingKit::inf_weight) {
            return Result<void>::failure(ErrorCode::InvalidMetric,
                "Invalid weight at arc " + std::to_string(i));
        }
    }
    return Result<void>::success();
}

// The hierarchy and original arc order are never modified after construction.
struct Topology {
    std::vector<unsigned> first_out;
    std::vector<unsigned> tail;
    std::vector<unsigned> head;
    RoutingKit::CustomizableContractionHierarchy hierarchy;

    Topology(const RoutingGraph& graph, std::vector<unsigned> order)
        : first_out(graph.firstOut()), tail(graph.tail()), head(graph.head()),
          hierarchy(std::move(order), tail, head) {}

    // Only used when CCH reports infinity: distinguish a disconnected pair
    // from a connected route whose objective exceeds the finite CCH range.
    bool reachable(unsigned source, unsigned target) const {
        std::vector<unsigned char> visited(hierarchy.node_count(), 0);
        std::vector<unsigned> pending{source};
        visited[source] = 1;
        while (!pending.empty()) {
            const unsigned node = pending.back();
            pending.pop_back();
            if (node == target) return true;
            for (unsigned arc = first_out[node]; arc < first_out[node + 1]; ++arc) {
                const unsigned next = head[arc];
                if (!visited[next]) {
                    visited[next] = 1;
                    pending.push_back(next);
                }
            }
        }
        return false;
    }
};

struct MetricState {
    std::vector<unsigned> weights;
    // RoutingKit's vector overload addresses element zero even when empty.
    // Its pointer overload allows a stable sentinel for graphs without arcs.
    unsigned empty_weight = 0;
    RoutingKit::CustomizableContractionHierarchyMetric metric;

    MetricState(const Topology& topology, std::vector<unsigned> input)
        : weights(std::move(input)),
          metric(topology.hierarchy, weights.empty() ? &empty_weight : weights.data()) {
        metric.customize();
    }
};

} // namespace

struct CchAdapter::Impl {
    // Declaration order keeps topology and metric alive for the query.
    std::shared_ptr<const Topology> topology;
    std::unique_ptr<MetricState> state;
    mutable RoutingKit::CustomizableContractionHierarchyQuery query;
    mutable std::mutex mutex;

    Impl(std::shared_ptr<const Topology> input_topology,
         std::vector<unsigned> weights)
        : topology(std::move(input_topology)),
          state(std::make_unique<MetricState>(*topology, std::move(weights))),
          query(state->metric) {}

    Result<void> replaceWeights(std::vector<unsigned> weights) {
        auto valid = validateWeights(weights, topology->head.size());
        if (!valid.ok()) return valid;
        if (weights == state->weights) return Result<void>::success();

        // Customize before publication: a failure leaves the previous metric
        // usable. reset on the same hierarchy reuses all query buffers.
        auto replacement = std::make_unique<MetricState>(*topology, std::move(weights));
        query.reset(replacement->metric);
        state = std::move(replacement);
        return Result<void>::success();
    }

    Result<ShortestPath> run(unsigned source, unsigned target) const {
        if (source >= topology->hierarchy.node_count() ||
            target >= topology->hierarchy.node_count()) {
            return Result<ShortestPath>::failure(ErrorCode::InvalidArgument,
                "Source or target node index is out of range.");
        }
        query.reset().add_source(source).add_target(target).run();
        const unsigned distance = query.get_distance();
        if (distance == RoutingKit::inf_weight) {
            if (topology->reachable(source, target)) {
                return Result<ShortestPath>::failure(ErrorCode::ResourceExhausted,
                    "Shortest-path cost exceeds the supported CCH range.");
            }
            return Result<ShortestPath>::failure(ErrorCode::NoRoute,
                "No directed path exists between source and target.");
        }

        ShortestPath path;
        path.total_cost = distance;
        path.arc_indices = query.get_arc_path();
        unsigned current = source;
        std::uint64_t calculated_cost = 0;
        for (unsigned arc : path.arc_indices) {
            if (arc >= state->weights.size() || topology->tail[arc] != current) {
                return Result<ShortestPath>::failure(ErrorCode::InternalError,
                    "CCH returned an invalid directed arc sequence.");
            }
            calculated_cost += state->weights[arc];
            current = topology->head[arc];
        }
        if (current != target || calculated_cost != path.total_cost) {
            return Result<ShortestPath>::failure(ErrorCode::InternalError,
                "CCH path reconstruction does not match its distance.");
        }
        return Result<ShortestPath>::success(std::move(path));
    }
};

CchAdapter::CchAdapter(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
CchAdapter::~CchAdapter() = default;
CchAdapter::CchAdapter(CchAdapter&&) noexcept = default;
CchAdapter& CchAdapter::operator=(CchAdapter&&) noexcept = default;

Result<CchAdapter> CchAdapter::create(const RoutingGraph& graph,
                                    std::vector<unsigned> node_order,
                                    std::vector<unsigned> arc_weights) {
    const std::size_t node_count = graph.nodeCount();
    if (node_count == 0 || node_order.size() != node_count) {
        return Result<CchAdapter>::failure(ErrorCode::InvalidArgument,
            "CCH node order has an incorrect size or graph is empty.");
    }
    std::vector<unsigned char> seen(node_count, 0);
    for (unsigned node : node_order) {
        if (node >= node_count || seen[node]) {
            return Result<CchAdapter>::failure(ErrorCode::InvalidArgument,
                "CCH node order is not a valid permutation.");
        }
        seen[node] = 1;
    }
    auto valid = validateWeights(arc_weights, graph.arcCount());
    if (!valid.ok()) return Result<CchAdapter>::failure(valid.error().code, valid.error().message);
    try {
        auto topology = std::make_shared<Topology>(graph, std::move(node_order));
        return Result<CchAdapter>::success(CchAdapter(
            std::make_unique<Impl>(std::move(topology), std::move(arc_weights))));
    } catch (const std::bad_alloc&) {
        return Result<CchAdapter>::failure(ErrorCode::ResourceExhausted,
            "Insufficient memory for CCH topology or metric.");
    } catch (const std::exception& error) {
        return Result<CchAdapter>::failure(ErrorCode::InternalError, error.what());
    }
}

Result<CchAdapter> CchAdapter::withWeights(std::vector<unsigned> arc_weights) const {
    if (!impl_) return Result<CchAdapter>::failure(ErrorCode::InternalError, "CCH adapter is not initialized.");
    auto valid = validateWeights(arc_weights, impl_->topology->head.size());
    if (!valid.ok()) return Result<CchAdapter>::failure(valid.error().code, valid.error().message);
    try {
        return Result<CchAdapter>::success(CchAdapter(
            std::make_unique<Impl>(impl_->topology, std::move(arc_weights))));
    } catch (const std::bad_alloc&) {
        return Result<CchAdapter>::failure(ErrorCode::ResourceExhausted, "Insufficient memory for CCH metric.");
    } catch (const std::exception& error) {
        return Result<CchAdapter>::failure(ErrorCode::InternalError, error.what());
    }
}

bool CchAdapter::matchesGraph(const RoutingGraph& graph) const {
    return impl_ && impl_->topology->first_out == graph.firstOut() &&
        impl_->topology->tail == graph.tail() && impl_->topology->head == graph.head();
}

Result<void> CchAdapter::updateWeights(std::vector<unsigned> arc_weights) {
    if (!impl_) return Result<void>::failure(ErrorCode::InternalError, "CCH adapter is not initialized.");
    std::lock_guard<std::mutex> lock(impl_->mutex);
    try {
        return impl_->replaceWeights(std::move(arc_weights));
    } catch (const std::bad_alloc&) {
        return Result<void>::failure(ErrorCode::ResourceExhausted, "Insufficient memory for CCH metric update.");
    } catch (const std::exception& error) {
        return Result<void>::failure(ErrorCode::InternalError, error.what());
    }
}

Result<ShortestPath> CchAdapter::shortestPath(unsigned source, unsigned target) const {
    if (!impl_) return Result<ShortestPath>::failure(ErrorCode::InternalError, "CCH adapter is not initialized.");
    std::lock_guard<std::mutex> lock(impl_->mutex);
    try {
        return impl_->run(source, target);
    } catch (const std::bad_alloc&) {
        return Result<ShortestPath>::failure(ErrorCode::ResourceExhausted, "Insufficient memory for CCH query.");
    } catch (const std::exception& error) {
        return Result<ShortestPath>::failure(ErrorCode::InternalError, error.what());
    }
}

Result<ShortestPath> CchAdapter::shortestPathWithWeights(unsigned source, unsigned target,
                                                       std::vector<unsigned> arc_weights) {
    if (!impl_) return Result<ShortestPath>::failure(ErrorCode::InternalError, "CCH adapter is not initialized.");
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (source >= impl_->topology->hierarchy.node_count() || target >= impl_->topology->hierarchy.node_count()) {
        return Result<ShortestPath>::failure(ErrorCode::InvalidArgument, "Source or target node index is out of range.");
    }
    try {
        auto updated = impl_->replaceWeights(std::move(arc_weights));
        if (!updated.ok()) return Result<ShortestPath>::failure(updated.error().code, updated.error().message);
        return impl_->run(source, target);
    } catch (const std::bad_alloc&) {
        return Result<ShortestPath>::failure(ErrorCode::ResourceExhausted, "Insufficient memory for CCH metric/query.");
    } catch (const std::exception& error) {
        return Result<ShortestPath>::failure(ErrorCode::InternalError, error.what());
    }
}

} // namespace ecoroute
