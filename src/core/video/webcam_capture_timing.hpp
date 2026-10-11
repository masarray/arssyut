#pragma once

// P8W1C capture-cadence algebra. No camera/OS/COM dependencies.
// Canonical monotonic ticks are 100 ns. A 30 Hz capture of 151 samples
// spanning five seconds must report 30.00 fps, not 151/5 = 30.2.
#include <algorithm>
#include <cstdint>
#include <limits>

namespace arssyut::core {

[[nodiscard]] constexpr std::uint32_t webcam_observed_fps_x100(
    std::uint64_t samples,
    std::int64_t first_ticks_100ns,
    std::int64_t last_ticks_100ns) noexcept
{
    if (samples < 2 || first_ticks_100ns <= 0 ||
        last_ticks_100ns <= first_ticks_100ns)
        return 0;
    constexpr std::uint64_t kScale = 1000'000'000ULL;
    const auto intervals = samples - 1ULL;
    const auto elapsed = static_cast<std::uint64_t>(
        last_ticks_100ns - first_ticks_100ns);
    if (intervals > std::numeric_limits<std::uint64_t>::max() / kScale)
        return std::numeric_limits<std::uint32_t>::max();
    return static_cast<std::uint32_t>(
        std::min<std::uint64_t>(intervals * kScale / elapsed,
                                 std::numeric_limits<std::uint32_t>::max()));
}

[[nodiscard]] constexpr bool webcam_excessive_frame_interval(
    std::int64_t previous_ticks_100ns,
    std::int64_t current_ticks_100ns) noexcept
{
    // Telemetry-only watchdog for a 30 Hz preference: >3 frame periods.
    return previous_ticks_100ns > 0 &&
        current_ticks_100ns > previous_ticks_100ns &&
        current_ticks_100ns - previous_ticks_100ns > 1'000'000LL;
}

} // namespace arssyut::core
