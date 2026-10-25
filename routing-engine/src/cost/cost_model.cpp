
/*
 * File: cost_model.cpp
 * Purpose: Implements reference consumption, incremental congestion,
 *          elevation adjustments and non-double-counted downhill energy.
 * Status: IMPLEMENTED; real-world calibration remains required.
 */

#include "cost_model.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace ecoroute {

namespace {

constexpr double GRAVITY = 9.80665;
constexpr double JOULES_PER_KWH = 3600000.0;

bool nonnegativeFinite(double value) {
    return std::isfinite(value) && value >= 0.0;
}

bool positiveFinite(double value) {
    return std::isfinite(value) && value > 0.0;
}

bool validFraction(double value) {
    return std::isfinite(value) &&
           value >= 0.0 && value <= 1.0;
}

} // namespace

Result<void> validateVehicleProfile(const VehicleProfile& vehicle) {
    if (vehicle.id.empty()) {
        return Result<void>::failure(
            ErrorCode::InvalidArgument,
            "Vehicle profile ID cannot be empty."
        );
    }

    switch (vehicle.type) {
        case VehicleType::Petrol:
        case VehicleType::Diesel:
        case VehicleType::Hybrid:
        case VehicleType::Electric:
            break;
        default:
            return Result<void>::failure(
                ErrorCode::InvalidArgument,
                "Unsupported vehicle type."
            );
    }

    if (!positiveFinite(vehicle.consumption_per_100km) ||
        !nonnegativeFinite(vehicle.auxiliary_consumption_per_hour) ||
        !nonnegativeFinite(vehicle.co2e_grams_per_unit)) {

        return Result<void>::failure(
            ErrorCode::InvalidArgument,
            "Invalid vehicle consumption or emissions parameters."
        );
    }

    bool has_mass = vehicle.mass_kg.has_value();
    bool has_efficiency =
        vehicle.wheel_energy_kwh_per_unit.has_value();

    if (has_mass != has_efficiency) {
        return Result<void>::failure(
            ErrorCode::InvalidArgument,
            "Vehicle mass and wheel-energy conversion must be configured together."
        );
    }

    if (has_mass &&
        (!positiveFinite(*vehicle.mass_kg) ||
         !positiveFinite(*vehicle.wheel_energy_kwh_per_unit))) {

        return Result<void>::failure(
            ErrorCode::InvalidArgument,
            "Invalid elevation-related vehicle parameters."
        );
    }

    // Battery-to-wheel conversion is an efficiency, unlike the
    // combustion conversion expressed in kWh per litre.
    if (has_efficiency && vehicle.type == VehicleType::Electric &&
        *vehicle.wheel_energy_kwh_per_unit > 1.0) {
        return Result<void>::failure(
            ErrorCode::InvalidArgument,
            "Electric wheel-energy efficiency cannot exceed 1."
        );
    }

    double assist = vehicle.downhill_propulsion_fraction;
    double regen = vehicle.regenerative_recovery_fraction;

    if (!validFraction(assist) || !validFraction(regen) ||
        assist + regen > 1.0) {

        return Result<void>::failure(
            ErrorCode::InvalidArgument,
            "Downhill assistance and recovery fractions must sum to at most 1."
        );
    }

    // Additional battery-energy recovery is only modelled
    // for EVs in this version.
    if (regen > 0.0 && vehicle.type != VehicleType::Electric) {
        return Result<void>::failure(
            ErrorCode::InvalidArgument,
            "Explicit regeneration currently supports electric vehicles only."
        );
    }

    if (regen > 0.0 && !has_mass) {
        return Result<void>::failure(
            ErrorCode::InvalidArgument,
            "Regeneration requires elevation-model parameters."
        );
    }

    return Result<void>::success();
}

Result<RouteMetrics> estimateSegmentMetrics(
    const RoadSegment& segment,
    const RoadNode& from_node,
    const RoadNode& to_node,
    const VehicleProfile& vehicle,
    double congestion_multiplier,
    const CostModelOptions& options
) {
    // --------------------------------------------------
    // 1. Validate input.
    // --------------------------------------------------

    if (segment.from != from_node.id ||
        segment.to != to_node.id) {

        return Result<RouteMetrics>::failure(
            ErrorCode::InvalidArgument,
            "Segment endpoints do not match supplied road nodes."
        );
    }

    if (!positiveFinite(segment.length_meters) ||
        !positiveFinite(options.fallback_speed_kph) ||
        !std::isfinite(congestion_multiplier) ||
        congestion_multiplier < 1.0) {

        return Result<RouteMetrics>::failure(
            ErrorCode::InvalidArgument,
            "Invalid segment length, fallback speed or congestion."
        );
    }

    if (segment.speed_limit_kph.has_value() &&
        !positiveFinite(*segment.speed_limit_kph)) {

        return Result<RouteMetrics>::failure(
            ErrorCode::InvalidArgument,
            "Invalid road speed."
        );
    }

    if ((from_node.elevation_meters.has_value() &&
         !std::isfinite(*from_node.elevation_meters)) ||
        (to_node.elevation_meters.has_value() &&
         !std::isfinite(*to_node.elevation_meters))) {

        return Result<RouteMetrics>::failure(
            ErrorCode::InvalidArgument,
            "Invalid endpoint elevation."
        );
    }

    auto validation = validateVehicleProfile(vehicle);

    if (!validation.ok()) {
        return Result<RouteMetrics>::failure(
            validation.error().code,
            validation.error().message
        );
    }

    // --------------------------------------------------
    // 2. Travel time.
    // --------------------------------------------------

    double speed_kph = segment.speed_limit_kph.value_or(
        options.fallback_speed_kph
    );

    double reference_time_seconds =
        3.6 * segment.length_meters / speed_kph;

    double travel_time_seconds =
        reference_time_seconds * congestion_multiplier;

    if (!positiveFinite(travel_time_seconds) ||
        !positiveFinite(reference_time_seconds)) {

        return Result<RouteMetrics>::failure(
            ErrorCode::InvalidMetric,
            "Calculated travel time is invalid."
        );
    }

    // --------------------------------------------------
    // 3. Reference energy consumption.
    // --------------------------------------------------

    // This rate already includes ordinary reference driving.
    double reference_units =
        segment.length_meters / 100000.0 *
        vehicle.consumption_per_100km;

    // Only EXTRA time is charged, avoiding baseline double count.
    double extra_time_seconds = std::max(
        0.0,
        travel_time_seconds - reference_time_seconds
    );

    double delay_units =
        extra_time_seconds / 3600.0 *
        vehicle.auxiliary_consumption_per_hour;

    double total_units = reference_units + delay_units;

    if (!nonnegativeFinite(total_units)) {
        return Result<RouteMetrics>::failure(
            ErrorCode::InvalidMetric,
            "Calculated reference consumption is invalid."
        );
    }

    // --------------------------------------------------
    // 4. Optional elevation correction.
    // --------------------------------------------------

    bool has_elevation =
        from_node.elevation_meters.has_value() &&
        to_node.elevation_meters.has_value();

    bool has_vehicle_data =
        vehicle.mass_kg.has_value() &&
        vehicle.wheel_energy_kwh_per_unit.has_value();

    if (has_elevation && has_vehicle_data) {
        double height_change =
            *to_node.elevation_meters -
            *from_node.elevation_meters;

        double gravity_kwh =
            (*vehicle.mass_kg * GRAVITY * height_change) /
            JOULES_PER_KWH;

        if (!std::isfinite(gravity_kwh)) {
            return Result<RouteMetrics>::failure(
                ErrorCode::InvalidMetric,
                "Calculated gravitational energy is invalid."
            );
        }

        double wheel_kwh_per_unit =
            *vehicle.wheel_energy_kwh_per_unit;

        if (gravity_kwh > 0.0) {
            // Uphill mechanical energy must be supplied
            // by the drivetrain.
            total_units += gravity_kwh / wheel_kwh_per_unit;

        } else if (gravity_kwh < 0.0) {
            double downhill_kwh = -gravity_kwh;

            // A fraction of gravity directly substitutes
            // for propulsion energy.
            double direct_assistance_kwh =
                downhill_kwh *
                vehicle.downhill_propulsion_fraction;

            // Do not subtract more than the reference
            // driving consumption itself.
            double propulsion_credit_units = std::min(
                reference_units,
                direct_assistance_kwh / wheel_kwh_per_unit
            );

            total_units -= propulsion_credit_units;

            if (vehicle.type == VehicleType::Electric) {
                // A disjoint share of gravity is recovered
                // as battery electricity.
                // This fraction is already net of recovery losses.
                double recovered_kwh =
                    downhill_kwh *
                    vehicle.regenerative_recovery_fraction;

                total_units -= recovered_kwh;
            }
        }
    }

    if (!std::isfinite(total_units)) {
        return Result<RouteMetrics>::failure(
            ErrorCode::InvalidMetric,
            "Elevation calculation produced invalid consumption."
        );
    }

    // No negative per-segment edge costs in the v1 model.
    // Route-level energy storage remains future work.
    total_units = std::max(0.0, total_units);

    // --------------------------------------------------
    // 5. Convert physical units into RouteMetrics.
    // --------------------------------------------------

    RouteMetrics metrics;

    metrics.duration_seconds = travel_time_seconds;
    metrics.distance_meters = segment.length_meters;

    if (vehicle.type == VehicleType::Electric) {
        metrics.electricity_wh = total_units * 1000.0;
    } else {
        metrics.fuel_milliliters = total_units * 1000.0;
    }

    metrics.co2e_grams =
        total_units * vehicle.co2e_grams_per_unit;

    if (!nonnegativeFinite(metrics.electricity_wh) ||
        !nonnegativeFinite(metrics.fuel_milliliters) ||
        !nonnegativeFinite(metrics.co2e_grams)) {

        return Result<RouteMetrics>::failure(
            ErrorCode::InvalidMetric,
            "Final energy or emissions value is invalid."
        );
    }

    return Result<RouteMetrics>::success(metrics);
}

} // namespace ecoroute
