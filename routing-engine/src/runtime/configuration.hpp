#pragma once

#include "cost/cost_model.hpp"
#include "ecoroute/engine.hpp"
#include <vector>

namespace ecoroute {
Result<void> validateEngineOptions(const EngineOptions& options);
Result<std::vector<VehicleProfile>> loadVehicleProfiles(const std::string& filename);
} // namespace ecoroute
