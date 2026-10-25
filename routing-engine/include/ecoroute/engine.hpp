#pragma once

/* Public C++17 interface. Graphs are immutable for an Engine lifetime; traffic
 * publication is atomic. Concurrent route/status/update calls are supported.
 * Destroying or moving an engine while calls are running is not supported. */
#include "ecoroute/result.hpp"
#include "ecoroute/types.hpp"
#include <memory>
#include <string>

namespace ecoroute {

// Strict key=value configuration. Relative paths resolve against the config file.
Result<EngineOptions> loadEngineOptions(const std::string& filename);

class Engine {
public:
    static Result<Engine> create(const EngineOptions& options);
    Result<RouteResponse> route(const RouteRequest& request) const;
    // External 64-bit node IDs, never CSR indices. Coordinates in request are ignored.
    Result<RouteResponse> routeNodes(std::uint64_t source_node_id,
                                    std::uint64_t target_node_id,
                                    const RouteRequest& request) const;
    Result<void> updateTraffic(const std::string& snapshot_directory);
    EngineStatus status() const;

    ~Engine();
    Engine(Engine&&) noexcept;
    Engine& operator=(Engine&&) noexcept;
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    explicit Engine(std::unique_ptr<Impl> impl);
};

// JSON helpers shared by CLI and integrations. IDs are strings;
// geometry uses GeoJSON [longitude, latitude].
std::string routeResponseJson(const RouteResponse& response);
std::string errorJson(const Error& error);
std::string engineStatusJson(const EngineStatus& status);
const char* errorCodeName(ErrorCode code);

} // namespace ecoroute