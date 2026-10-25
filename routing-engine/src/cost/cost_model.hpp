
#pragma once

/*
 * File: cost_model.hpp
 * Purpose: Defines vehicle profiles and estimates segment travel time,
 *          energy consumption and operational CO2e.
 *
 * Status: IMPLEMENTED; mathematical tests do not establish calibration.
 *
 * Model assumptions:
 * - Reference consumption includes normal driving energy.
 * - Extra hourly consumption applies only to congestion delay.
 * - Elevation is optional.
 * - Downhill assistance and regeneration cannot double-count gravity.
 * - Road class is NOT used in v1 pending DB/ML mapping and calibration.
 */

#include "ecoroute/result.hpp"
#include "ecoroute/types.hpp"
#include "graph/road_network.hpp"

#include <optional>
#include <string>

namespace ecoroute {

enum class VehicleType {
    Petrol,
    Diesel,
    Hybrid,
    Electric
};

struct VehicleProfile {
    std::string id;

    VehicleType type = VehicleType::Petrol;

    // All-in, level-road reference consumption.
    // Includes ordinary driving energy at reference conditions.
    //
    // Petrol, Diesel, Hybrid: litres per 100 km
    // Electric:              kWh per 100 km
    double consumption_per_100km = 0.0;

    // Incremental consumption per hour of EXTRA congestion delay.
    // Not charged for baseline journey time.
    //
    // Combustion: litres/hour
    // Electric:   kWh/hour
    double auxiliary_consumption_per_hour = 0.0;

    // Combustion: grams CO2e per litre
    // Electric:   grams CO2e per kWh
    //
    // Accounting boundary must be consistently configured.
    double co2e_grams_per_unit = 0.0;

    // Optional elevation-model parameters.
    std::optional<double> mass_kg;

    // Effective mechanical wheel energy per energy unit:
    //
    // Combustion: kWh at wheels per litre
    // Electric:   kWh at wheels per kWh from battery, in (0, 1]
    std::optional<double> wheel_energy_kwh_per_unit;

    // Fraction of downhill gravitational energy used directly
    // to reduce propulsion demand.
    //
    // Range [0, 1].
    // Defaults to full available assistance, limited by
    // baseline propulsion consumption.
    double downhill_propulsion_fraction = 1.0;

    // Fraction of downhill gravitational energy returned as
    // usable stored battery electricity.
    //
    // Range [0, 1].
    // Initially supported only for Electric vehicles.
    //
    // For v1, Hybrid consumption is assumed to incorporate
    // its typical regenerative benefit in the reference rate.
    //
    // This is an effective recovery fraction, not a
    // detailed braking or battery-state simulation.
    double regenerative_recovery_fraction = 0.0;

    // The sum of the two downhill fractions cannot exceed 1.
};

struct CostModelOptions {
    // Used when the road speed limit is unavailable.
    double fallback_speed_kph = 0.0;
};

// Validate profiles independently of graph size or a route query.
Result<void> validateVehicleProfile(const VehicleProfile& vehicle);

// Estimates the physical metrics of one directed road segment.
//
// The congestion multiplier describes the ratio between
// predicted travel time and reference travel time.
//
// v1 requires congestion_multiplier >= 1.
//
// Road class is deliberately NOT applied in this function.
// TODO(INTEGRATION):
// Add calibrated speed/road-class consumption adjustments
// after the DB and ML teams agree on classification semantics.
Result<RouteMetrics> estimateSegmentMetrics(
    const RoadSegment& segment,
    const RoadNode& from_node,
    const RoadNode& to_node,
    const VehicleProfile& vehicle,
    double congestion_multiplier,
    const CostModelOptions& options
);

} // namespace ecoroute
