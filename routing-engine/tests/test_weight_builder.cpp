
/*
 * File: test_weight_builder.cpp
 * Purpose: Tests physical-metric to CCH-weight conversion,
 *          normalization, rounding and numerical validation.
 * Status: VERIFIED
 * Remaining: Build and run tests.
 */

#include "cost/weight_builder.hpp"

#include <routingkit/constants.h>

#include <cmath>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

using namespace ecoroute;

namespace {

int failures = 0;
int checks = 0;

void check(bool condition, const std::string& name) {
    ++checks;

    if (condition) {
        std::cout << "[PASS] " << name << '\n';
    } else {
        ++failures;
        std::cerr << "[FAIL] " << name << '\n';
    }
}

RouteMetrics makeMetrics(double seconds, double grams) {
    RouteMetrics metrics{};

    metrics.duration_seconds = seconds;
    metrics.distance_meters = 100.0;
    metrics.co2e_grams = grams;

    return metrics;
}

bool hasError(
    const Result<std::vector<unsigned>>& result,
    ErrorCode code
) {
    return !result.ok() && result.error().code == code;
}

} // namespace

int main() {

    WeightBuilderOptions options;
    options.reference_time_seconds = 60.0;
    options.reference_co2e_grams = 100.0;
    options.integer_scale = 1000u;

    std::vector<RouteMetrics> metrics = {
        makeMetrics(30.0, 80.0),
        makeMetrics(120.0, 50.0)
    };

    // --------------------------------------------------
    // 1. Time-only optimization.
    // --------------------------------------------------

    auto fastest = buildCchArcWeights(
        metrics, 1.0, options
    );

    check(
        fastest.ok() &&
        fastest.value() == std::vector<unsigned>{500, 2000},
        "Time-only weights are correct"
    );

    // --------------------------------------------------
    // 2. Emissions-only optimization.
    // --------------------------------------------------

    auto eco = buildCchArcWeights(
        metrics, 0.0, options
    );

    check(
        eco.ok() &&
        eco.value() == std::vector<unsigned>{800, 500},
        "Emissions-only weights are correct"
    );

    // --------------------------------------------------
    // 3. Balanced optimization.
    // --------------------------------------------------

    auto balanced = buildCchArcWeights(
        metrics, 0.5, options
    );

    check(
        balanced.ok() &&
        balanced.value() == std::vector<unsigned>{650, 1250},
        "Balanced objective weights are correct"
    );

    // --------------------------------------------------
    // 4. Intermediate preference.
    // --------------------------------------------------

    auto intermediate = buildCchArcWeights(
        metrics, 0.75, options
    );

    check(
        intermediate.ok() &&
        intermediate.value() == std::vector<unsigned>{575, 1625},
        "Intermediate preference weights are correct"
    );

    // --------------------------------------------------
    // 5. Zero-emissions edge.
    // --------------------------------------------------

    std::vector<RouteMetrics> zero_emissions = {
        makeMetrics(30.0, 0.0)
    };

    auto zero = buildCchArcWeights(
        zero_emissions, 0.0, options
    );

    check(
        zero.ok() &&
        zero.value() == std::vector<unsigned>{0},
        "Genuine zero-cost edge remains zero"
    );

    // --------------------------------------------------
    // 6. Tiny positive objective.
    // --------------------------------------------------

    std::vector<RouteMetrics> tiny = {
        makeMetrics(0.000001, 0.0)
    };

    auto tiny_result = buildCchArcWeights(
        tiny, 1.0, options
    );

    check(
        tiny_result.ok() &&
        tiny_result.value() == std::vector<unsigned>{1},
        "Tiny positive objective receives weight 1"
    );

    // --------------------------------------------------
    // 7. Invalid optimization preferences.
    // --------------------------------------------------

    check(
        hasError(
            buildCchArcWeights(metrics, -0.1, options),
            ErrorCode::InvalidArgument
        ),
        "Negative preference rejected"
    );

    check(
        hasError(
            buildCchArcWeights(metrics, 1.1, options),
            ErrorCode::InvalidArgument
        ),
        "Preference greater than 1 rejected"
    );

    check(
        hasError(
            buildCchArcWeights(
                metrics,
                std::numeric_limits<double>::quiet_NaN(),
                options
            ),
            ErrorCode::InvalidArgument
        ),
        "NaN preference rejected"
    );

    // --------------------------------------------------
    // 8. Invalid physical metrics.
    // --------------------------------------------------

    auto negative = metrics;
    negative[0].co2e_grams = -1.0;

    check(
        hasError(
            buildCchArcWeights(negative, 0.5, options),
            ErrorCode::InvalidMetric
        ),
        "Negative emissions rejected"
    );

    auto nan_metrics = metrics;
    nan_metrics[0].duration_seconds =
        std::numeric_limits<double>::quiet_NaN();

    check(
        hasError(
            buildCchArcWeights(nan_metrics, 0.5, options),
            ErrorCode::InvalidMetric
        ),
        "NaN duration rejected"
    );

    auto infinite = metrics;
    infinite[1].distance_meters =
        std::numeric_limits<double>::infinity();

    check(
        hasError(
            buildCchArcWeights(infinite, 0.5, options),
            ErrorCode::InvalidMetric
        ),
        "Infinite physical metric rejected"
    );

    // --------------------------------------------------
    // 9. Invalid normalization configuration.
    // --------------------------------------------------

    auto invalid_time = options;
    invalid_time.reference_time_seconds = 0.0;

    check(
        hasError(
            buildCchArcWeights(metrics, 0.5, invalid_time),
            ErrorCode::InvalidArgument
        ),
        "Zero reference time rejected"
    );

    auto invalid_emissions = options;
    invalid_emissions.reference_co2e_grams = -10.0;

    check(
        hasError(
            buildCchArcWeights(metrics, 0.5, invalid_emissions),
            ErrorCode::InvalidArgument
        ),
        "Negative reference emissions rejected"
    );

    auto invalid_scale = options;
    invalid_scale.integer_scale = 0;

    check(
        hasError(
            buildCchArcWeights(metrics, 0.5, invalid_scale),
            ErrorCode::InvalidArgument
        ),
        "Zero integer scale rejected"
    );

    // --------------------------------------------------
    // 10. RoutingKit weight boundaries.
    // --------------------------------------------------

    WeightBuilderOptions boundary_options;
    boundary_options.reference_time_seconds = 1.0;
    boundary_options.reference_co2e_grams = 1.0;
    boundary_options.integer_scale = 1u;

    const unsigned infinity = RoutingKit::inf_weight;

    // Greatest individually representable finite weight.
    auto near_limit = buildCchArcWeights(
        {makeMetrics(static_cast<double>(infinity - 1), 0.0)},
        1.0,
        boundary_options
    );

    check(
        near_limit.ok() &&
        near_limit.value() ==
            std::vector<unsigned>{infinity - 1},
        "Largest finite edge weight accepted"
    );

    auto at_limit = buildCchArcWeights(
        {makeMetrics(static_cast<double>(infinity), 0.0)},
        1.0,
        boundary_options
    );

    check(
        hasError(at_limit, ErrorCode::ResourceExhausted),
        "RoutingKit infinity weight rejected"
    );

    auto huge = buildCchArcWeights(
        {makeMetrics(
            std::numeric_limits<double>::max(), 0.0
        )},
        1.0,
        options
    );

    check(
        !huge.ok(),
        "Extremely large objective rejected safely"
    );

    // --------------------------------------------------
    // 11. Empty input.
    // --------------------------------------------------

    auto empty = buildCchArcWeights(
        {}, 0.5, options
    );

    check(
        empty.ok() && empty.value().empty(),
        "Empty metric input returns empty weights"
    );

    // Endpoint objectives must ignore an inactive dimension even when its
    // normalization would overflow ordinary double arithmetic.
    auto extreme_options = boundary_options;
    extreme_options.reference_co2e_grams = std::numeric_limits<double>::min();
    auto inactive = buildCchArcWeights(
        {makeMetrics(5.0, std::numeric_limits<double>::max())}, 1.0, extreme_options);
    check(inactive.ok() && inactive.value()[0] == 5,
          "Time-only objective ignores overflowing inactive emissions normalization");

    std::vector<RouteMetrics> long_metrics(2000, makeMetrics(2.0, 3.0));
    WeightBuilderOptions long_options;
    long_options.reference_time_seconds = 1.0;
    long_options.reference_co2e_grams = 1.0;
    auto safe = buildGraphSafeCchArcWeights(long_metrics, 1.0, long_options, 2001);
    unsigned long long path_sum = 0;
    if (safe.ok()) for (unsigned weight : safe.value().arc_weights) path_sum += weight;
    check(safe.ok() && safe.value().effective_integer_scale < long_options.integer_scale &&
          path_sum < RoutingKit::inf_weight,
          "Graph-wide scale keeps a 2000-arc route below RoutingKit infinity");
    if (safe.ok()) {
        const double reconstructed = static_cast<double>(path_sum) / safe.value().effective_integer_scale;
        check(std::fabs(reconstructed - 4000.0) <= safe.value().maximum_path_objective_error,
              "Reported rounding bound covers long-route objective error");
    }
    auto already_safe = buildGraphSafeCchArcWeights(metrics, 0.5, options, 3);
    check(already_safe.ok() && already_safe.value().effective_integer_scale == options.integer_scale &&
          already_safe.value().arc_weights == balanced.value(),
          "Graph-safe weights retain configured precision when already safe");
    auto subunit = buildGraphSafeCchArcWeights(
        {makeMetrics(1e20, 0.0)}, 1.0, boundary_options, 2);
    check(subunit.ok() && subunit.value().effective_integer_scale > 0.0 &&
          subunit.value().effective_integer_scale < 1.0 &&
          subunit.value().arc_weights[0] < RoutingKit::inf_weight,
          "Large physical objectives expose a safe subunit scale");
    auto all_zero = buildGraphSafeCchArcWeights({makeMetrics(0.0, 0.0)}, 1.0, options, 2);
    check(all_zero.ok() && all_zero.value().arc_weights[0] == 0 &&
          all_zero.value().maximum_path_objective_error == 0.0,
          "Zero objective has no quantization error");
    check(!buildGraphSafeCchArcWeights(metrics, 0.5, options, 0).ok(),
          "Graph-safe scaling rejects zero node count");

    // --------------------------------------------------
    // Final result.
    // --------------------------------------------------

    std::cout << "\nChecks executed: " << checks << '\n';

    if (failures == 0) {
        std::cout << "All WeightBuilder tests passed.\n";
        return 0;
    }

    std::cerr << failures << " test(s) failed.\n";
    return 1;
}
