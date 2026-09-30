#pragma once

#include "core/result/status.hpp"

#include <optional>
#include <utility>

namespace arssyut::core {

template <typename T>
class [[nodiscard]] Result {
public:
    [[nodiscard]] static Result success(T value)
    {
        return Result(std::move(value));
    }

    [[nodiscard]] static Result failure(Status status) noexcept
    {
        return Result(status);
    }

    [[nodiscard]] bool has_value() const noexcept
    {
        return value_.has_value();
    }

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return has_value();
    }

    [[nodiscard]] T &value() & noexcept
    {
        return *value_;
    }

    [[nodiscard]] const T &value() const & noexcept
    {
        return *value_;
    }

    [[nodiscard]] T &&value() && noexcept
    {
        return std::move(*value_);
    }

    [[nodiscard]] Status status() const noexcept
    {
        return status_;
    }

private:
    explicit Result(T value)
        : value_(std::move(value)),
          status_(Status::success())
    {
    }

    explicit Result(Status status) noexcept
        : status_(status)
    {
    }

    std::optional<T> value_;
    Status status_ = Status::success();
};

template <>
class [[nodiscard]] Result<void> {
public:
    [[nodiscard]] static constexpr Result success() noexcept
    {
        return Result(Status::success());
    }

    [[nodiscard]] static constexpr Result failure(Status status) noexcept
    {
        return Result(status);
    }

    [[nodiscard]] constexpr bool has_value() const noexcept
    {
        return status_.ok();
    }

    [[nodiscard]] constexpr explicit operator bool() const noexcept
    {
        return has_value();
    }

    [[nodiscard]] constexpr Status status() const noexcept
    {
        return status_;
    }

private:
    explicit constexpr Result(Status status) noexcept
        : status_(status)
    {
    }

    Status status_;
};

} // namespace arssyut::core
