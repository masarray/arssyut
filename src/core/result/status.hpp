#pragma once

#include <cstdint>

namespace arssyut::core {

enum class StatusCode : std::uint16_t {
    Ok = 0,
    InvalidArgument,
    InvalidStateTransition,
    CapacityExceeded,
    PlatformFailure,
    GraphicsDeviceUnavailable,
    StorageFailure,
    Unsupported,
    InternalError,
};

struct Status {
    StatusCode code = StatusCode::Ok;
    std::uint32_t detail = 0;

    [[nodiscard]] constexpr bool ok() const noexcept
    {
        return code == StatusCode::Ok;
    }

    [[nodiscard]] static constexpr Status success() noexcept
    {
        return {};
    }

    [[nodiscard]] static constexpr Status failure(
        StatusCode code_value,
        std::uint32_t detail_value = 0) noexcept
    {
        return {code_value, detail_value};
    }
};

} // namespace arssyut::core
