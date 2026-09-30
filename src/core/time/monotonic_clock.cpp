#include "core/time/monotonic_clock.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#else
#include <chrono>
#endif

namespace arssyut::core {

TimePoint MonotonicClock::now() noexcept
{
#ifdef _WIN32
    static const std::int64_t frequency = []() noexcept {
        LARGE_INTEGER value{};
        if (!QueryPerformanceFrequency(&value) || value.QuadPart <= 0)
            return std::int64_t{1};
        return static_cast<std::int64_t>(value.QuadPart);
    }();

    LARGE_INTEGER counter{};
    if (!QueryPerformanceCounter(&counter))
        return {};

    const std::int64_t whole_seconds =
        static_cast<std::int64_t>(counter.QuadPart) / frequency;
    const std::int64_t remainder =
        static_cast<std::int64_t>(counter.QuadPart) % frequency;

    return {
        whole_seconds * ticks_per_second +
        (remainder * ticks_per_second) / frequency
    };
#else
    const auto value = std::chrono::steady_clock::now().time_since_epoch();
    const auto ticks = std::chrono::duration_cast<
        std::chrono::duration<std::int64_t, std::ratio<1, 10'000'000>>>(
            value);
    return {ticks.count()};
#endif
}

} // namespace arssyut::core
