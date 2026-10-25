#include "ecoroute/engine.hpp"
#include "graph/graph_loader.hpp"
#include "runtime/order_cache.hpp"
#include "search/cch_order.hpp"
#include <filesystem>
#include <iostream>

int main(int argc, char** argv) {
    using namespace ecoroute;
    if (argc != 3) { std::cerr << "Usage: ecoroute_preprocess SNAPSHOT_DIRECTORY NEW_ORDER_FILE\n"; return 2; }
    try {
        const std::filesystem::path path(argv[1]);
        auto graph = loadGraphSnapshot({path / "metadata.txt", path / "nodes.csv", path / "edges.csv"});
        if (!graph.ok()) { std::cout << errorJson(graph.error()) << '\n'; return 2; }
        auto order = computeCchNodeOrder(graph.value().network, graph.value().routing_graph);
        if (!order.ok()) { std::cout << errorJson(order.error()) << '\n'; return 2; }
        auto saved = saveCchOrder(argv[2], graph.value(), order.value());
        if (!saved.ok()) { std::cout << errorJson(saved.error()) << '\n'; return 2; }
        std::cout << "{\"schema_version\":1,\"order_nodes\":" << order.value().size() << "}\n";
        return 0;
    } catch (const std::bad_alloc&) {
        std::cout << errorJson({ErrorCode::ResourceExhausted, "Preprocessing exhausted memory."}) << '\n'; return 2;
    } catch (const std::exception& e) {
        std::cout << errorJson({ErrorCode::InternalError, e.what()}) << '\n'; return 2;
    }
}