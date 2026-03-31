#pragma once

#include <optional>
#include <string>
#include <utility>

namespace common {

template <typename T>
class Result {
public:
    static Result Success(T value) {
        return Result(std::move(value), std::wstring{});
    }

    static Result Failure(std::wstring error_message) {
        return Result(std::nullopt, std::move(error_message));
    }

    [[nodiscard]] bool HasValue() const {
        return value_.has_value();
    }

    [[nodiscard]] explicit operator bool() const {
        return HasValue();
    }

    [[nodiscard]] const T& Value() const {
        return value_.value();
    }

    [[nodiscard]] T& Value() {
        return value_.value();
    }

    [[nodiscard]] const std::wstring& Error() const {
        return error_message_;
    }

private:
    Result(std::optional<T> value, std::wstring error_message)
        : value_(std::move(value)), error_message_(std::move(error_message)) {}

    std::optional<T> value_;
    std::wstring error_message_;
};

template <>
class Result<void> {
public:
    static Result Success() {
        return Result(true, std::wstring{});
    }

    static Result Failure(std::wstring error_message) {
        return Result(false, std::move(error_message));
    }

    [[nodiscard]] bool HasValue() const {
        return success_;
    }

    [[nodiscard]] explicit operator bool() const {
        return HasValue();
    }

    [[nodiscard]] const std::wstring& Error() const {
        return error_message_;
    }

private:
    Result(bool success, std::wstring error_message)
        : success_(success), error_message_(std::move(error_message)) {}

    bool success_ = false;
    std::wstring error_message_;
};

}  // namespace common
