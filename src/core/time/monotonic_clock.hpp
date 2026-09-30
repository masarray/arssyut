#pragma once

#include <cstdint>

namespace arssyut::core {

struct TimePoint {
    std::int64_t ticks_100ns = 0;

    [[nodiscard]] friend constexpr bool operator==(
        TimePoint lhs,
        TimePoint rhs) noexcept
    {
        return lhs.ticks_100ns == rhs.ticks_100ns;
    }

    [[nodiscard]] friend constexpr bool operator<(
        TimePoint lhs,
        TimePoint rhs) noexcept
    {
        return lhs.ticks_100ns < rhs.ticks_100ns;
    }
};

class MonotonicClock {
public:
    static constexpr std::int64_t ticks_per_second = 10'000'000;

    [[nodiscard]] static TimePoint now() noexcept;

    [[nodiscard]] static constexpr std::int64_t duration_ticks(
        TimePoint start,
        TimePoint end) noexcept
    {
        return end.ticks_100ns - start.ticks_100ns;
    }

    [[nodiscard]] static double duration_seconds(
        TimePoint start,
        TimePoint end) noexcept
    {
        return static_cast<double>(duration_ticks(start, end)) /
               static_cast<double>(ticks_per_second);
    }
};

} // namespace arssyut::core
