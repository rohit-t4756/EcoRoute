#pragma once

/*
 * File: result.hpp
 * Purpose: Provides common error codes and a reusable way to return
 *          either a successful value or an error from engine operations.
 * Status: VERIFIED
 * Tests: tests/test_result.cpp
 * Remaining: None for the currently supported interface
 */

#include <string>
#include <variant>
#include <optional>
#include <stdexcept>
#include <utility>

namespace ecoroute {

// Common errors that can occur in the routing engine.
enum class ErrorCode {
    InvalidArgument,
    OutOfCoverage,
    SnapFailed,
    NoRoute,
    UnsupportedProfile,
    InvalidGraph,
    InvalidMetric,
    InvalidTrafficSnapshot,
    VersionMismatch,
    FileError,
    Cancelled,
    DeadlineExceeded,
    ResourceExhausted,
    Unavailable,
    InternalError
};

// Contains the type of error and an explanation.
struct Error {
    ErrorCode code;
    std::string message;
};

// T represents the type of successful result (int, Route, Graph, etc.).
template <typename T>
class [[nodiscard]] Result {
private:
    // Stores either a successful value or an Error, never both.
    std::variant<T, Error> data_;

    // Private constructors ensure results are created through success() or failure().
    explicit Result(T value) : data_(std::move(value)) {}
    explicit Result(Error error) : data_(std::move(error)) {}

public:
    // Create a successful result containing a value.
    static Result success(T value) {
        return Result(std::move(value));
    }

    // Create a failed result using an error code and message.
    static Result failure(ErrorCode code, std::string message) {
        return Result(Error{code, std::move(message)});
    }

    // Check whether the operation succeeded.
    bool ok() const {
        return std::holds_alternative<T>(data_);
    }

    // Return the stored value; throw if the result contains an error.
    T& value() {
        if (!ok()) throw std::logic_error("Cannot access value of a failed Result.");
        return std::get<T>(data_);
    }

    // Read-only version for const Result objects.
    const T& value() const {
        if (!ok()) throw std::logic_error("Cannot access value of a failed Result.");
        return std::get<T>(data_);
    }

    // Return the error; throw if the operation succeeded.
    const Error& error() const {
        if (ok()) throw std::logic_error("Cannot access error of a successful Result.");
        return std::get<Error>(data_);
    }
};

// Special version for operations that return no value on success.
template <>
class [[nodiscard]] Result<void> {
private:
    // No error means success; an existing error means failure.
    std::optional<Error> error_;

    Result() = default;
    explicit Result(Error error) : error_(std::move(error)) {}

public:
    static Result success() {
        return Result();
    }

    static Result failure(ErrorCode code, std::string message) {
        return Result(Error{code, std::move(message)});
    }

    bool ok() const {
        return !error_.has_value();
    }

    const Error& error() const {
        if (ok()) throw std::logic_error("Cannot access error of a successful Result.");
        return *error_;
    }
};

} // namespace ecoroute
