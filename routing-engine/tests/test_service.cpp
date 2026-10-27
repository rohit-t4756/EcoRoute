#include "ecoroute/engine.hpp"
#include <iostream>
int main() {
    using namespace ecoroute;
    RouteResponse response;
    response.graph_version = "graph\"\\\n";
    Route route;
    route.id = "route-1";
    route.road_segment_ids = {18446744073709551615ull};
    route.geometry = {{40, -74}, {41, -73}};
    response.routes.push_back(route);
    const auto json = routeResponseJson(response);
    const bool valid = json.find("\"18446744073709551615\"") != std::string::npos &&
        json.find("[-74,40]") != std::string::npos &&
        json.find("graph\\\"\\\\\\u000a") != std::string::npos &&
        errorJson({ErrorCode::NoRoute, "no route"}).find("NoRoute") != std::string::npos;
    std::cout << (valid ? "PASS" : "FAILED") << ": JSON contract\n";
    return valid ? 0 : 1;
}