#include "graph/snapper.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>

using namespace ecoroute;
namespace {
int failures = 0;
void check(bool condition, const char* name) {
    if (!condition) { std::cerr << "[FAIL] " << name << '\n'; ++failures; }
}
}

int main() {
    auto network = RoadNetwork::create(
        {{20, {0, 0}, {}}, {10, {0, 0}, {}}, {30, {0, 0.01}, {}},
         {40, {0, 0.02}, {}}, {50, {0, 180}, {}}, {60, {90, 0}, {}},
         {70, {0, 179.99}, {}}, {80, {-90, 0}, {}}},
        {{1, 10, 30, 100, {}, {}, {}}, {2, 30, 40, 100, {}, {}, {}},
         {3, 20, 20, 10, {}, {}, {}}, {4, 50, 70, 100, {}, {}, {}}});
    if (!network.ok()) return 1;
    auto graph = RoutingGraph::create(network.value());
    if (!graph.ok()) return 1;
    auto snapper = NodeSnapper::create(network.value(), graph.value());
    if (!snapper.ok()) return 1;
    const auto tie = snapper.value().snap({0, 0}, 0);
    check(tie.ok() && tie.value().node_id == 10 && tie.value().distance_meters == 0,
          "Coincident nodes break ties by external ID, including zero radius");
    const auto source = snapper.value().snap({0, 0.02}, 1200, SnapRole::Source);
    check(source.ok() && source.value().node_id == 30, "Source excludes terminal node");
    const auto target = snapper.value().snap({0, 0}, 1200, SnapRole::Target);
    check(target.ok() && target.value().node_id == 30, "Target excludes source-only and self-loop-only nodes");
    check(!snapper.value().snap({0, 0.02}, 100, SnapRole::Source).ok(), "Eligible node still must satisfy radius");
    const auto exact_end = snapper.value().snap({0, 0.02}, 0, SnapRole::Target);
    check(exact_end.ok() && exact_end.value().node_id == 40, "Directed destination accepted");
    const auto across = snapper.value().snap({0, -179.999}, 200, SnapRole::Source);
    check(across.ok() && across.value().node_id == 50 &&
          std::fabs(across.value().distance_meters - 111.19508) < 0.01,
          "Great-circle snapping crosses antimeridian");
    const auto antimeridian = snapper.value().snap({0, -180}, 0);
    check(antimeridian.ok() && antimeridian.value().node_id == 50,
          "Equivalent antimeridian coordinates snap at zero radius");
    const auto pole = snapper.value().snap({90, 170}, 0);
    check(pole.ok() && pole.value().node_id == 60, "North pole ignores longitude");
    const auto south = snapper.value().snap({-90, -90}, 0);
    check(south.ok() && south.value().node_id == 80, "South pole ignores longitude");
    check(!snapper.value().snap({90, 0}, 0, SnapRole::Source).ok(), "Isolated node excluded as traveling source");
    check(!snapper.value().snap({91, 0}, 100).ok(), "Latitude validation");
    check(!snapper.value().snap({0, 181}, 100).ok(), "Longitude validation");
    check(!snapper.value().snap({0, 0}, -1).ok(), "Negative radius validation");
    check(!snapper.value().snap({0, std::numeric_limits<double>::quiet_NaN()}, 100).ok(), "NaN coordinate validation");
    check(!snapper.value().snap({0, 0}, std::numeric_limits<double>::infinity()).ok(), "Infinite radius validation");
    // Independent exhaustive haversine reference for index pruning and distances.
    constexpr double radians = 3.14159265358979323846 / 180;
    for (int query = 1; query <= 40; ++query) {
        const Coordinate point{0.0003 * query, -0.002 + 0.0008 * query};
        double best = std::numeric_limits<double>::infinity();
        RoadNodeId best_id = 0;
        for (const auto& node : network.value().nodes()) {
            const double half_lat = (node.location.latitude - point.latitude) * radians / 2;
            const double half_lon = (node.location.longitude - point.longitude) * radians / 2;
            const double a = std::sin(half_lat) * std::sin(half_lat) +
                std::cos(point.latitude * radians) * std::cos(node.location.latitude * radians) *
                std::sin(half_lon) * std::sin(half_lon);
            const double distance = 2 * 6371008.8 * std::asin(std::sqrt(std::min(1.0, a)));
            if (distance < best || (distance == best && node.id < best_id)) {
                best = distance; best_id = node.id;
            }
        }
        const auto result = snapper.value().snap(point, 30000000);
        check(result.ok() && result.value().node_id == best_id &&
              std::fabs(result.value().distance_meters - best) < 1e-6,
              "Indexed result equals exhaustive haversine reference");
    }
    std::cout << "SnapperTests failures: " << failures << '\n';
    return failures == 0 ? 0 : 1;
}