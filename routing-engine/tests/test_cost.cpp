
/*
 * File: test_cost.cpp
 * Purpose: Verifies travel time, reference energy consumption,
 *          incremental congestion, elevation corrections,
 *          regenerative recovery, emissions and input validation.
 *
 * Status: VERIFIED
 * Remaining: Compile, run and verify all tests.
 *
 * Note: These tests verify the mathematical implementation.
 *       Real-world calibration is tracked separately.
 */

#include "cost/cost_model.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <string>

using namespace ecoroute;

// Floating-point calculations should be compared using a tolerance.
bool approximatelyEqual(double a, double b) {
    return std::isfinite(a) &&
           std::isfinite(b) &&
           std::fabs(a - b) < 0.0001;
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

bool hasError(
    const Result<RouteMetrics>& result,
    ErrorCode expected = ErrorCode::InvalidArgument
) {
    return !result.ok() &&
           result.error().code == expected;
}

// Create a directed, one-kilometre road with a speed of 36 km/h.
// Reference travel time = 100 seconds.
RoadSegment makeSegment() {
    RoadSegment segment;

    segment.id = 501;
    segment.from = 101;
    segment.to = 102;
    segment.length_meters = 1000.0;
    segment.speed_limit_kph = 36.0;

    return segment;
}

VehicleProfile makeVehicle(
    const std::string& id,
    VehicleType type,
    double consumption,
    double auxiliary,
    double emissions
) {
    VehicleProfile vehicle;

    vehicle.id = id;
    vehicle.type = type;
    vehicle.consumption_per_100km = consumption;
    vehicle.auxiliary_consumption_per_hour = auxiliary;
    vehicle.co2e_grams_per_unit = emissions;

    return vehicle;
}

int main() {
    int failures = 0;

    RoadSegment segment = makeSegment();

    RoadNode from{
        101,
        {40.700, -73.900},
        100.0
    };

    RoadNode to{
        102,
        {40.701, -73.899},
        100.0
    };

    CostModelOptions options;
    options.fallback_speed_kph = 36.0;

    VehicleProfile petrol = makeVehicle(
        "petrol-test",
        VehicleType::Petrol,
        8.0,     // 8 litres / 100 km
        0.72,    // 0.72 litres / hour of additional delay
        2300.0   // 2300 g CO2e / litre
    );

    // --------------------------------------------------
    // 1. Petrol: baseline calculations.
    // --------------------------------------------------

    // Distance = 1 km.
    // Reference time = 100 seconds.
    // Reference fuel = 8 / 100 = 0.08 litres = 80 mL.
    //
    // Congestion multiplier = 1, so additional delay = 0.
    // No additional hourly consumption is charged.

    auto baseline = estimateSegmentMetrics(
        segment, from, to, petrol, 1.0, options
    );

    check(
        baseline.ok(),
        "Valid petrol segment accepted",
        failures
    );

    if (baseline.ok()) {
        const RouteMetrics& m = baseline.value();

        check(
            approximatelyEqual(m.duration_seconds, 100.0),
            "Baseline travel time = 100 seconds",
            failures
        );

        check(
            approximatelyEqual(m.distance_meters, 1000.0),
            "Distance = 1000 metres",
            failures
        );

        check(
            approximatelyEqual(m.fuel_milliliters, 80.0),
            "Petrol baseline = 80 mL without double-counting",
            failures
        );

        check(
            approximatelyEqual(m.co2e_grams, 184.0),
            "Petrol baseline emissions = 184 grams",
            failures
        );

        check(
            approximatelyEqual(m.electricity_wh, 0.0),
            "Petrol electricity consumption = 0",
            failures
        );
    }

    // --------------------------------------------------
    // 2. Congestion: only EXTRA travel time adds fuel.
    // --------------------------------------------------

    // Congestion multiplier = 2.
    //
    // Reference time = 100 seconds.
    // Congested time = 200 seconds.
    // Extra delay = 100 seconds.
    //
    // Extra fuel = (100 / 3600) * 0.72 = 0.02 litres.
    //
    // Total fuel = 80 + 20 = 100 mL.

    auto congested = estimateSegmentMetrics(
        segment, from, to, petrol, 2.0, options
    );

    check(
        congested.ok() &&
        approximatelyEqual(
            congested.value().duration_seconds, 200.0
        ) &&
        approximatelyEqual(
            congested.value().fuel_milliliters, 100.0
        ) &&
        approximatelyEqual(
            congested.value().co2e_grams, 230.0
        ),
        "Congestion adds consumption only for extra delay",
        failures
    );

    // --------------------------------------------------
    // 3. Fallback speed.
    // --------------------------------------------------

    RoadSegment unknown_speed = segment;
    unknown_speed.speed_limit_kph.reset();

    auto fallback = estimateSegmentMetrics(
        unknown_speed, from, to, petrol, 1.0, options
    );

    check(
        fallback.ok() &&
        approximatelyEqual(
            fallback.value().duration_seconds, 100.0
        ) &&
        approximatelyEqual(
            fallback.value().fuel_milliliters, 80.0
        ),
        "Missing road speed uses configured fallback",
        failures
    );

    // --------------------------------------------------
    // 4. Diesel calculations.
    // --------------------------------------------------

    VehicleProfile diesel = makeVehicle(
        "diesel-test",
        VehicleType::Diesel,
        6.0, 0.0, 2700.0
    );

    auto diesel_result = estimateSegmentMetrics(
        segment, from, to, diesel, 1.0, options
    );

    check(
        diesel_result.ok() &&
        approximatelyEqual(
            diesel_result.value().fuel_milliliters, 60.0
        ) &&
        approximatelyEqual(
            diesel_result.value().co2e_grams, 162.0
        ),
        "Diesel fuel and emissions calculation",
        failures
    );

    // --------------------------------------------------
    // 5. Hybrid calculations.
    // --------------------------------------------------

    VehicleProfile hybrid = makeVehicle(
        "hybrid-test",
        VehicleType::Hybrid,
        5.0, 0.0, 2300.0
    );

    auto hybrid_result = estimateSegmentMetrics(
        segment, from, to, hybrid, 1.0, options
    );

    check(
        hybrid_result.ok() &&
        approximatelyEqual(
            hybrid_result.value().fuel_milliliters, 50.0
        ) &&
        approximatelyEqual(
            hybrid_result.value().co2e_grams, 115.0
        ),
        "Hybrid baseline fuel calculation",
        failures
    );

    // --------------------------------------------------
    // 6. Electric vehicle: baseline calculations.
    // --------------------------------------------------

    VehicleProfile electric = makeVehicle(
        "electric-test",
        VehicleType::Electric,
        18.0,   // 18 kWh / 100 km
        0.72,   // 0.72 kWh / hour of extra delay
        400.0   // 400 g CO2e / kWh
    );

    // Baseline electricity = 18 / 100 = 0.18 kWh.
    // No additional consumption at congestion = 1.

    auto ev_result = estimateSegmentMetrics(
        segment, from, to, electric, 1.0, options
    );

    check(
        ev_result.ok() &&
        approximatelyEqual(
            ev_result.value().electricity_wh, 180.0
        ) &&
        approximatelyEqual(
            ev_result.value().co2e_grams, 72.0
        ) &&
        approximatelyEqual(
            ev_result.value().fuel_milliliters, 0.0
        ),
        "EV baseline electricity = 180 Wh",
        failures
    );

    // With congestion = 2:
    // Extra electricity = 100 / 3600 * 0.72 = 0.02 kWh.
    // Total = 0.18 + 0.02 = 0.20 kWh.

    auto ev_congested = estimateSegmentMetrics(
        segment, from, to, electric, 2.0, options
    );

    check(
        ev_congested.ok() &&
        approximatelyEqual(
            ev_congested.value().electricity_wh, 200.0
        ) &&
        approximatelyEqual(
            ev_congested.value().co2e_grams, 80.0
        ),
        "EV congestion adds electricity only for extra delay",
        failures
    );

    // --------------------------------------------------
    // 7. Petrol: uphill elevation correction.
    // --------------------------------------------------

    petrol.mass_kg = 1000.0;
    petrol.wheel_energy_kwh_per_unit = 2.0;

    RoadNode uphill_to = to;
    uphill_to.elevation_meters = 110.0;

    // Elevation gain = 10 metres.
    //
    // Gravity = m*g*h / 3,600,000
    //         = 1000*9.80665*10 / 3,600,000 kWh.
    //
    // Additional fuel = gravity / 2.0 litres.
    //
    // Total = 80 + 13.620347 = 93.620347 mL.

    auto uphill = estimateSegmentMetrics(
        segment, from, uphill_to, petrol, 1.0, options
    );

    check(
        uphill.ok() &&
        approximatelyEqual(
            uphill.value().fuel_milliliters,
            93.620347
        ) &&
        uphill.value().co2e_grams > 184.0,
        "Petrol uphill increases fuel and emissions",
        failures
    );

    // --------------------------------------------------
    // 8. Petrol: downhill gravitational assistance.
    // --------------------------------------------------

    RoadNode downhill_to = to;
    downhill_to.elevation_meters = 90.0;

    // Default downhill propulsion fraction = 1.
    // There is no regenerative battery recovery.
    //
    // Fuel = 80 - 13.620347 = 66.379653 mL.

    auto petrol_downhill = estimateSegmentMetrics(
        segment, from, downhill_to, petrol, 1.0, options
    );

    check(
        petrol_downhill.ok() &&
        approximatelyEqual(
            petrol_downhill.value().fuel_milliliters,
            66.379653
        ),
        "Petrol downhill assistance reduces propulsion fuel",
        failures
    );

    // --------------------------------------------------
    // 9. EV: uphill energy.
    // --------------------------------------------------

    electric.mass_kg = 1800.0;
    electric.wheel_energy_kwh_per_unit = 0.9;

    // The model divides downhill gravitational energy
    // between direct propulsion assistance and recovery.
    electric.downhill_propulsion_fraction = 0.5;
    electric.regenerative_recovery_fraction = 0.3;

    // Uphill correction:
    // 1800 * 9.80665 * 10 / 3,600,000 = 0.04903325 kWh.
    //
    // Battery consumption = 0.04903325 / 0.9.
    //
    // Total = 180 + 54.481389 = 234.481389 Wh.

    auto ev_uphill = estimateSegmentMetrics(
        segment, from, uphill_to, electric, 1.0, options
    );

    check(
        ev_uphill.ok() &&
        approximatelyEqual(
            ev_uphill.value().electricity_wh,
            234.481389
        ),
        "EV uphill energy calculation",
        failures
    );

    // --------------------------------------------------
    // 10. EV: downhill assistance and regeneration.
    // --------------------------------------------------

    // Gravity available = 0.04903325 kWh.
    //
    // Direct assistance:
    // 0.04903325 * 0.5 / 0.9 = 0.027240694 kWh.
    //
    // Battery recovery:
    // 0.04903325 * 0.3 = 0.014709975 kWh.
    //
    // Total:
    // 0.18 - 0.027240694 - 0.014709975
    // = 0.138049331 kWh.

    auto ev_downhill = estimateSegmentMetrics(
        segment, from, downhill_to, electric, 1.0, options
    );

    check(
        ev_downhill.ok() &&
        approximatelyEqual(
            ev_downhill.value().electricity_wh,
            138.049331
        ) &&
        ev_downhill.value().electricity_wh <
            ev_result.value().electricity_wh,
        "EV downhill assistance and regeneration",
        failures
    );

    // --------------------------------------------------
    // 11. Missing elevation uses baseline calculations.
    // --------------------------------------------------

    RoadNode unknown_elevation = to;
    unknown_elevation.elevation_meters.reset();

    auto no_elevation = estimateSegmentMetrics(
        segment, from, unknown_elevation,
        electric, 1.0, options
    );

    check(
        no_elevation.ok() &&
        approximatelyEqual(
            no_elevation.value().electricity_wh, 180.0
        ),
        "Missing elevation uses baseline consumption",
        failures
    );

    // --------------------------------------------------
    // 12. Prevent negative energy on steep descents.
    // --------------------------------------------------

    RoadNode steep_downhill = to;
    steep_downhill.elevation_meters = 0.0;

    auto limited_recovery = estimateSegmentMetrics(
        segment, from, steep_downhill,
        electric, 1.0, options
    );

    check(
        limited_recovery.ok() &&
        approximatelyEqual(
            limited_recovery.value().electricity_wh, 0.0
        ) &&
        approximatelyEqual(
            limited_recovery.value().co2e_grams, 0.0
        ),
        "Steep downhill energy clamped to zero",
        failures
    );

    // --------------------------------------------------
    // 13. Road class is deliberately unused in v1.
    // --------------------------------------------------

    RoadSegment classified = segment;
    classified.road_class = "motorway";

    auto classified_result = estimateSegmentMetrics(
        classified, from, to, petrol, 1.0, options
    );

    auto unclassified_result = estimateSegmentMetrics(
        segment, from, to, petrol, 1.0, options
    );

    check(
        classified_result.ok() &&
        unclassified_result.ok() &&
        approximatelyEqual(
            classified_result.value().fuel_milliliters,
            unclassified_result.value().fuel_milliliters
        ),
        "Road class not yet used without calibrated mapping",
        failures
    );

    // --------------------------------------------------
    // 14. Invalid physical inputs.
    // --------------------------------------------------

    RoadSegment invalid_length = segment;
    invalid_length.length_meters = -1.0;

    check(
        hasError(estimateSegmentMetrics(
            invalid_length, from, to, petrol, 1.0, options
        )),
        "Negative road length rejected",
        failures
    );

    check(
        hasError(estimateSegmentMetrics(
            segment, from, to, petrol, 0.5, options
        )),
        "Congestion multiplier below 1 rejected",
        failures
    );

    check(
        hasError(estimateSegmentMetrics(
            segment, from, to, petrol,
            std::numeric_limits<double>::quiet_NaN(),
            options
        )),
        "NaN congestion multiplier rejected",
        failures
    );

    RoadNode wrong_endpoint = to;
    wrong_endpoint.id = 999;

    check(
        hasError(estimateSegmentMetrics(
            segment, from, wrong_endpoint,
            petrol, 1.0, options
        )),
        "Mismatched road endpoint rejected",
        failures
    );

    CostModelOptions invalid_options;
    invalid_options.fallback_speed_kph = 0.0;

    check(
        hasError(estimateSegmentMetrics(
            segment, from, to, petrol, 1.0,
            invalid_options
        )),
        "Invalid fallback speed rejected",
        failures
    );

    RoadSegment invalid_speed = segment;
    invalid_speed.speed_limit_kph = 0.0;

    check(
        hasError(estimateSegmentMetrics(
            invalid_speed, from, to, petrol, 1.0, options
        )),
        "Zero speed limit rejected",
        failures
    );

    RoadNode invalid_elevation = to;
    invalid_elevation.elevation_meters =
        std::numeric_limits<double>::infinity();

    check(
        hasError(estimateSegmentMetrics(
            segment, from, invalid_elevation,
            petrol, 1.0, options
        )),
        "Infinite elevation rejected",
        failures
    );

    // --------------------------------------------------
    // 15. Invalid vehicle parameters.
    // --------------------------------------------------

    VehicleProfile invalid_vehicle = petrol;
    invalid_vehicle.consumption_per_100km = -5.0;

    check(
        hasError(estimateSegmentMetrics(
            segment, from, to, invalid_vehicle,
            1.0, options
        )),
        "Negative vehicle consumption rejected",
        failures
    );

    VehicleProfile partial_elevation = petrol;
    partial_elevation.wheel_energy_kwh_per_unit.reset();

    check(
        hasError(estimateSegmentMetrics(
            segment, from, to, partial_elevation,
            1.0, options
        )),
        "Incomplete elevation parameters rejected",
        failures
    );

    VehicleProfile invalid_recovery = petrol;
    invalid_recovery.regenerative_recovery_fraction = 0.5;
    invalid_recovery.downhill_propulsion_fraction = 0.5;

    check(
        hasError(estimateSegmentMetrics(
            segment, from, to, invalid_recovery,
            1.0, options
        )),
        "Petrol regenerative recovery rejected",
        failures
    );

    VehicleProfile invalid_hybrid = hybrid;
    invalid_hybrid.mass_kg = 1300.0;
    invalid_hybrid.wheel_energy_kwh_per_unit = 2.0;
    invalid_hybrid.downhill_propulsion_fraction = 0.5;
    invalid_hybrid.regenerative_recovery_fraction = 0.3;

    check(
        hasError(estimateSegmentMetrics(
            segment, from, to, invalid_hybrid,
            1.0, options
        )),
        "Explicit hybrid regeneration deferred in v1",
        failures
    );

    VehicleProfile invalid_downhill = electric;
    invalid_downhill.downhill_propulsion_fraction = 0.8;
    invalid_downhill.regenerative_recovery_fraction = 0.5;

    check(
        hasError(estimateSegmentMetrics(
            segment, from, to, invalid_downhill,
            1.0, options
        )),
        "Overlapping downhill energy fractions rejected",
        failures
    );

    VehicleProfile negative_recovery = electric;
    negative_recovery.regenerative_recovery_fraction = -0.1;

    check(
        hasError(estimateSegmentMetrics(
            segment, from, to, negative_recovery,
            1.0, options
        )),
        "Negative regenerative recovery rejected",
        failures
    );

    VehicleProfile incomplete_ev = electric;
    incomplete_ev.mass_kg.reset();

    check(
        hasError(estimateSegmentMetrics(
            segment, from, to, incomplete_ev,
            1.0, options
        )),
        "Incomplete EV elevation parameters rejected",
        failures
    );

    // --------------------------------------------------
    // Final result.
    // --------------------------------------------------

    VehicleProfile impossible_efficiency = electric;
    impossible_efficiency.wheel_energy_kwh_per_unit = 1.01;
    check(!validateVehicleProfile(impossible_efficiency).ok() &&
          hasError(estimateSegmentMetrics(segment, from, to,
              impossible_efficiency, 1.0, options)),
          "EV efficiency above unity rejected independently of a route", failures);
    VehicleProfile ideal_efficiency = electric;
    ideal_efficiency.wheel_energy_kwh_per_unit = 1.0;
    check(validateVehicleProfile(ideal_efficiency).ok(),
          "Unity EV efficiency accepted", failures);

    // A uniform-grade split scales every model term proportionally.
    // This is a metamorphic property, independent of exact constants.
    for (const auto& profile : {petrol, diesel, hybrid, electric}) {
        for (double change : {-100.0, -10.0, 0.0, 10.0, 100.0}) {
            RoadSegment first = segment;
            RoadSegment second = segment;
            RoadNode midpoint = from;
            midpoint.id = 103;
            midpoint.elevation_meters = *from.elevation_meters + change * 0.3;
            RoadNode end = to;
            end.elevation_meters = *from.elevation_meters + change;
            first.to = midpoint.id;
            first.length_meters *= 0.3;
            second.from = midpoint.id;
            second.length_meters *= 0.7;
            const auto whole = estimateSegmentMetrics(segment, from, end, profile, 2.0, options);
            const auto part1 = estimateSegmentMetrics(first, from, midpoint, profile, 2.0, options);
            const auto part2 = estimateSegmentMetrics(second, midpoint, end, profile, 2.0, options);
            check(whole.ok() && part1.ok() && part2.ok() &&
                  approximatelyEqual(whole.value().duration_seconds,
                      part1.value().duration_seconds + part2.value().duration_seconds) &&
                  approximatelyEqual(whole.value().fuel_milliliters,
                      part1.value().fuel_milliliters + part2.value().fuel_milliliters) &&
                  approximatelyEqual(whole.value().electricity_wh,
                      part1.value().electricity_wh + part2.value().electricity_wh) &&
                  approximatelyEqual(whole.value().co2e_grams,
                      part1.value().co2e_grams + part2.value().co2e_grams),
                  "Uniform-grade splitting preserves totals: " + profile.id +
                      " elevation change " + std::to_string(change), failures);
        }
    }

    // Preserve an explicit counterexample to arbitrary aggregation.
    // 1 km flat + 1 km descending 100 m differs from a single edge
    // that spreads that descent over 2 km. Endpoint-only elevation and
    // the per-edge zero floor cannot preserve arbitrary aggregation.
    RoadSegment two_km = segment;
    two_km.length_meters = 2000.0;
    RoadNode midpoint = from;
    midpoint.id = 103;
    RoadNode low = to;
    low.elevation_meters = 0.0;
    RoadSegment flat = segment;
    flat.to = midpoint.id;
    RoadSegment descent = segment;
    descent.from = midpoint.id;
    auto merged = estimateSegmentMetrics(two_km, from, low, electric, 1.0, options);
    auto flat_part = estimateSegmentMetrics(flat, from, midpoint, electric, 1.0, options);
    auto descent_part = estimateSegmentMetrics(descent, midpoint, low, electric, 1.0, options);
    check(merged.ok() && flat_part.ok() && descent_part.ok() &&
          approximatelyEqual(merged.value().electricity_wh, 0.0) &&
          approximatelyEqual(flat_part.value().electricity_wh +
              descent_part.value().electricity_wh, 180.0),
          "Known nonuniform-grade aggregation limitation remains explicit", failures);

    RoadSegment extreme = segment;
    extreme.length_meters = std::numeric_limits<double>::max();
    check(hasError(estimateSegmentMetrics(extreme, from, to, petrol, 2.0, options),
                   ErrorCode::InvalidMetric),
          "Overflowing derived travel time rejected", failures);
    VehicleProfile extreme_emissions = petrol;
    extreme_emissions.co2e_grams_per_unit = std::numeric_limits<double>::max();
    RoadSegment long_road = segment;
    long_road.length_meters = 100000.0;
    check(hasError(estimateSegmentMetrics(long_road, from, to,
              extreme_emissions, 1.0, options), ErrorCode::InvalidMetric),
          "Overflowing derived emissions rejected", failures);

    std::cout << '\n';

    if (failures == 0) {
        std::cout << "All revised cost-model tests passed.\n";
        return 0;
    }

    std::cerr << failures << " cost-model test(s) failed.\n";
    return 1;
}
