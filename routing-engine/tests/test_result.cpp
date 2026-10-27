
/*
 * File: test_result.cpp
 * Purpose: Tests the Result<T> error-handling class.
 * Status: VERIFIED
 * Remaining: None for the current test cases
 */

#include "ecoroute/result.hpp"
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

using ecoroute::Result;
using ecoroute::ErrorCode;

// Checks a condition and records a failure if it is false.
void check(bool condition, const std::string& name, int& failures) {
    if (condition) {
        std::cout << "[PASS] " << name << '\n';
    } else {
        std::cerr << "[FAIL] " << name << '\n';
        failures++;
    }
}

int main() {
    int failures = 0;

    // Test 1: Successful result containing an integer.
    Result<int> success = Result<int>::success(42);
    check(success.ok(), "Integer success status", failures);
    check(success.value() == 42, "Integer stored value", failures);

    // Test 2: Failed result containing an error code and message.
    Result<int> failure = Result<int>::failure(
        ErrorCode::InvalidArgument, "Invalid source node"
    );
    check(!failure.ok(), "Failure status", failures);
    check(failure.error().code == ErrorCode::InvalidArgument,
          "Correct error code", failures);
    check(failure.error().message == "Invalid source node",
          "Correct error message", failures);

    // Test 3: Successful result containing a string, including const access.
    const Result<std::string> text = Result<std::string>::success("EcoRoute");
    check(text.ok(), "String success status", failures);
    check(text.value() == "EcoRoute", "String stored value", failures);

    // Test 4: Result<void> can succeed without returning a value.
    Result<void> emptySuccess = Result<void>::success();
    check(emptySuccess.ok(), "Void success status", failures);

    // Test 5: Result<void> can also report an error.
    Result<void> emptyFailure = Result<void>::failure(
        ErrorCode::FileError, "Unable to open graph file"
    );
    check(!emptyFailure.ok(), "Void failure status", failures);
    check(emptyFailure.error().code == ErrorCode::FileError,
          "Void error code", failures);

    // Test 6: Accessing a value from a failed result must throw.
    bool caughtValueError = false;
    try {
        failure.value();
    } catch (const std::logic_error&) {
        caughtValueError = true;
    }
    check(caughtValueError, "Invalid value access throws", failures);

    // Test 7: Accessing an error from a successful result must throw.
    bool caughtErrorAccess = false;
    try {
        success.error();
    } catch (const std::logic_error&) {
        caughtErrorAccess = true;
    }
    check(caughtErrorAccess, "Invalid error access throws", failures);

    // Test 8: Result must support move-only values such as unique_ptr.
    Result<std::unique_ptr<int>> pointerResult =
        Result<std::unique_ptr<int>>::success(std::make_unique<int>(100));

    check(pointerResult.ok(), "Move-only result success", failures);
    check(pointerResult.value() != nullptr && *pointerResult.value() == 100,
          "Move-only stored value", failures);

    // A nonzero return code tells CTest that the test executable failed.
    std::cout << "\nTotal failures: " << failures << '\n';
    return failures == 0 ? 0 : 1;
}
