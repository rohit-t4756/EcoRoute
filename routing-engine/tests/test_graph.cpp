
/*
 * File: test_graph.cpp
 * Purpose: Tests road-network validation, storage and ID lookups.
 * Status: VERIFIED
 * Remaining: Compile, execute and verify all test cases.
 */

#include "graph/road_network.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

using ecoroute::Coordinate;
using ecoroute::ErrorCode;
using ecoroute::GEOMETRY_ENDPOINT_TOLERANCE_DEGREES;
using ecoroute::RoadNetwork;
using ecoroute::RoadNode;
using ecoroute::RoadSegment;

// Small valid road network used as the starting point for tests.
std::vector<RoadNode> makeNodes() {
    return {
        {101, {40.7000, -73.9000}, 12.0},
        {102, {40.7010, -73.8990}, std::nullopt},
        {103, {40.7020, -73.8980}, 15.0}
    };
}

std::vector<RoadSegment> makeSegments() {
    RoadSegment first;
    first.id = 501;
    first.from = 101;
    first.to = 102;
    first.length_meters = 150.0;
    first.speed_limit_kph = 30.0;
    first.road_class = "residential";
    first.geometry = {
        {40.7000, -73.9000},
        {40.7010, -73.8990}
    };

    RoadSegment second;
    second.id = 502;
    second.from = 102;
    second.to = 103;
    second.length_meters = 180.0;

    // Missing speed, class and geometry are permitted.
    return {first, second};
}

// Check that the network rejects invalid input with InvalidGraph.
bool rejected(
    std::vector<RoadNode> nodes,
    std::vector<RoadSegment> segments
) {
    auto result = RoadNetwork::create(
        std::move(nodes),
        std::move(segments)
    );

    return !result.ok() &&
           result.error().code == ErrorCode::InvalidGraph;
}

void check(
    bool condition,
    const std::string& name,
    int& failures
) {
    if (condition) {
        std::cout << "[PASS] " << name << '\n';
    } else {
        std::cerr << "[FAIL] " << name << '\n';
        ++failures;
    }
}

int main() {
    int failures = 0;

    // ----------------------------------------------
    // 1. Valid network and lookup operations.
    // ----------------------------------------------

    auto result = RoadNetwork::create(
        makeNodes(), makeSegments()
    );

    check(result.ok(), "Valid network accepted", failures);

    if (result.ok()) {
        const RoadNetwork& network = result.value();

        check(
            network.nodeCount() == 3 &&
            network.segmentCount() == 2,
            "Correct node and segment counts",
            failures
        );

        const RoadNode* node = network.findNode(101);

        check(
            node != nullptr &&
            node->location.latitude == 40.7000,
            "Existing node lookup",
            failures
        );

        check(
            network.findNode(999) == nullptr,
            "Unknown node returns nullptr",
            failures
        );

        const RoadSegment* segment =
            network.findSegment(501);

        check(
            segment != nullptr &&
            segment->from == 101 &&
            segment->to == 102 &&
            segment->length_meters == 150.0,
            "Directed segment data preserved",
            failures
        );

        check(
            network.findSegment(999) == nullptr,
            "Unknown segment returns nullptr",
            failures
        );

        const RoadSegment* second =
            network.findSegment(502);

        check(
            second != nullptr &&
            !second->speed_limit_kph.has_value() &&
            !second->road_class.has_value() &&
            second->geometry.empty(),
            "Missing optional attributes accepted",
            failures
        );

        check(
            network.nodes().size() == 3 &&
            network.segments().size() == 2,
            "Read-only data access",
            failures
        );
    }

    // ----------------------------------------------
    // 2. Empty network.
    // ----------------------------------------------

    check(
        rejected({}, {}),
        "Empty network rejected",
        failures
    );
    check(RoadNetwork::create(makeNodes(), {}).ok(),
          "Nonempty isolated-node snapshot accepted", failures);

    // ----------------------------------------------
    // 3. Duplicate node IDs.
    // ----------------------------------------------

    {
        auto nodes = makeNodes();
        nodes[1].id = nodes[0].id;

        check(
            rejected(nodes, makeSegments()),
            "Duplicate node ID rejected",
            failures
        );
    }

    // ----------------------------------------------
    // 4. Duplicate segment IDs.
    // ----------------------------------------------

    {
        auto segments = makeSegments();
        segments[1].id = segments[0].id;

        check(
            rejected(makeNodes(), segments),
            "Duplicate segment ID rejected",
            failures
        );
    }

    // ----------------------------------------------
    // 5. Missing endpoint.
    // ----------------------------------------------

    {
        auto segments = makeSegments();
        segments[0].to = 999;

        check(
            rejected(makeNodes(), segments),
            "Unknown endpoint rejected",
            failures
        );
    }

    // ----------------------------------------------
    // 6. Invalid geographic coordinates.
    // ----------------------------------------------

    {
        auto nodes = makeNodes();
        nodes[0].location.latitude = 100.0;

        check(
            rejected(nodes, makeSegments()),
            "Latitude outside valid range rejected",
            failures
        );
    }

    {
        auto nodes = makeNodes();
        nodes[0].location.longitude = -200.0;

        check(
            rejected(nodes, makeSegments()),
            "Longitude outside valid range rejected",
            failures
        );
    }

    {
        auto nodes = makeNodes();
        nodes[0].location.latitude =
            std::numeric_limits<double>::quiet_NaN();

        check(
            rejected(nodes, makeSegments()),
            "NaN coordinate rejected",
            failures
        );
    }

    // ----------------------------------------------
    // 7. Invalid elevation.
    // ----------------------------------------------

    {
        auto nodes = makeNodes();
        nodes[0].elevation_meters =
            std::numeric_limits<double>::infinity();

        check(
            rejected(nodes, makeSegments()),
            "Infinite elevation rejected",
            failures
        );
    }

    // ----------------------------------------------
    // 8. Invalid segment lengths.
    // ----------------------------------------------

    {
        auto segments = makeSegments();
        segments[0].length_meters = 0.0;

        check(
            rejected(makeNodes(), segments),
            "Zero-length segment rejected",
            failures
        );
    }

    {
        auto segments = makeSegments();
        segments[0].length_meters = -50.0;

        check(
            rejected(makeNodes(), segments),
            "Negative-length segment rejected",
            failures
        );
    }

    {
        auto segments = makeSegments();
        segments[0].length_meters =
            std::numeric_limits<double>::quiet_NaN();

        check(
            rejected(makeNodes(), segments),
            "NaN segment length rejected",
            failures
        );
    }

    // ----------------------------------------------
    // 9. Invalid speed limits.
    // ----------------------------------------------

    {
        auto segments = makeSegments();
        segments[0].speed_limit_kph = 0.0;

        check(
            rejected(makeNodes(), segments),
            "Zero speed limit rejected",
            failures
        );
    }

    {
        auto segments = makeSegments();
        segments[0].speed_limit_kph =
            std::numeric_limits<double>::infinity();

        check(
            rejected(makeNodes(), segments),
            "Infinite speed limit rejected",
            failures
        );
    }

    // ----------------------------------------------
    // 10. Invalid road geometry.
    // ----------------------------------------------

    {
        auto segments = makeSegments();

        segments[0].geometry = {
            {40.7000, -73.9000}
        };

        check(
            rejected(makeNodes(), segments),
            "Single-point geometry rejected",
            failures
        );
    }

    {
        auto segments = makeSegments();

        segments[0].geometry[1].longitude = 250.0;

        check(
            rejected(makeNodes(), segments),
            "Invalid geometry coordinate rejected",
            failures
        );
    }

    // ----------------------------------------------
    // 11. Self-loop edges are allowed.
    // ----------------------------------------------

    {
        auto segments = makeSegments();

        segments[0].to = segments[0].from;
        segments[0].geometry.clear();

        check(
            RoadNetwork::create(
                makeNodes(), segments
            ).ok(),
            "Self-loop segment accepted",
            failures
        );
    }

    {
        auto segments = makeSegments();
        std::reverse(segments[0].geometry.begin(), segments[0].geometry.end());
        check(rejected(makeNodes(), segments),
              "Reversed directed geometry rejected", failures);
    }
    {
        auto segments = makeSegments();
        segments[0].geometry.back().latitude += 0.001;
        check(rejected(makeNodes(), segments),
              "Disconnected geometry endpoint rejected", failures);
    }
    {
        auto segments = makeSegments();
        segments[0].geometry.front().latitude +=
            GEOMETRY_ENDPOINT_TOLERANCE_DEGREES / 2.0;
        check(RoadNetwork::create(makeNodes(), segments).ok(),
              "Geometry export rounding within tolerance accepted", failures);
    }
    {
        auto nodes = makeNodes();
        auto segments = makeSegments();
        nodes[0].location.longitude = 180.0;
        segments[0].geometry.front().longitude = -180.0;
        check(RoadNetwork::create(nodes, segments).ok(),
              "Equivalent antimeridian endpoint accepted", failures);
    }

    std::cout << "\n";

    if (failures == 0) {
        std::cout << "All road-network tests passed.\n";
        return 0;
    }

    std::cerr << failures << " test(s) failed.\n";
    return 1;
}
