#pragma once

#include <cstdint>
#include <limits>

namespace arssyut::core::audio {

inline constexpr std::uint64_t kMediaTicksPerSecond = 10'000'000ull;

[[nodiscard]] inline constexpr std::uint64_t frames_to_ticks_floor(
    std::uint64_t frame_count,
    std::uint32_t sample_rate) noexcept
{
    if (sample_rate == 0)
        return 0;

    const std::uint64_t whole_seconds = frame_count / sample_rate;
    const std::uint64_t remainder_frames = frame_count % sample_rate;

    if (whole_seconds >
        std::numeric_limits<std::uint64_t>::max() / kMediaTicksPerSecond)
        return std::numeric_limits<std::uint64_t>::max();

    const std::uint64_t whole_ticks =
        whole_seconds * kMediaTicksPerSecond;
    const std::uint64_t fractional_ticks =
        (remainder_frames * kMediaTicksPerSecond) / sample_rate;

    if (whole_ticks >
        std::numeric_limits<std::uint64_t>::max() - fractional_ticks)
        return std::numeric_limits<std::uint64_t>::max();

    return whole_ticks + fractional_ticks;
}

[[nodiscard]] inline constexpr std::uint64_t ticks_to_frames_floor(
    std::uint64_t ticks_100ns,
    std::uint32_t sample_rate) noexcept
{
    if (sample_rate == 0)
        return 0;

    const std::uint64_t whole_seconds =
        ticks_100ns / kMediaTicksPerSecond;
    const std::uint64_t remainder_ticks =
        ticks_100ns % kMediaTicksPerSecond;

    if (whole_seconds >
        std::numeric_limits<std::uint64_t>::max() / sample_rate)
        return std::numeric_limits<std::uint64_t>::max();

    const std::uint64_t whole_frames = whole_seconds * sample_rate;
    const std::uint64_t fractional_frames =
        (remainder_ticks * sample_rate) / kMediaTicksPerSecond;

    if (whole_frames >
        std::numeric_limits<std::uint64_t>::max() - fractional_frames)
        return std::numeric_limits<std::uint64_t>::max();

    return whole_frames + fractional_frames;
}

/*
 * Converts successive frame durations to 100 ns media ticks while carrying the
 * fractional remainder. This prevents cumulative rounding drift at rates such
 * as 44.1 kHz without requiring floating point.
 */
class FrameTimeAccumulator final {
public:
    FrameTimeAccumulator() = default;

    explicit FrameTimeAccumulator(std::uint32_t sample_rate) noexcept
    {
        reset(sample_rate);
    }

    void reset(std::uint32_t sample_rate) noexcept
    {
        sample_rate_ = sample_rate;
        remainder_ = 0;
    }

    [[nodiscard]] std::uint32_t sample_rate() const noexcept
    {
        return sample_rate_;
    }

    [[nodiscard]] std::uint64_t remainder() const noexcept
    {
        return remainder_;
    }

    [[nodiscard]] std::uint64_t advance(
        std::uint64_t frame_count) noexcept
    {
        if (sample_rate_ == 0)
            return 0;

        const std::uint64_t whole_seconds = frame_count / sample_rate_;
        const std::uint64_t remainder_frames = frame_count % sample_rate_;

        if (whole_seconds >
            std::numeric_limits<std::uint64_t>::max() /
                kMediaTicksPerSecond)
            return std::numeric_limits<std::uint64_t>::max();

        const std::uint64_t numerator =
            remainder_frames * kMediaTicksPerSecond + remainder_;
        const std::uint64_t fractional_ticks = numerator / sample_rate_;
        remainder_ = numerator % sample_rate_;

        const std::uint64_t whole_ticks =
            whole_seconds * kMediaTicksPerSecond;
        if (whole_ticks >
            std::numeric_limits<std::uint64_t>::max() - fractional_ticks)
            return std::numeric_limits<std::uint64_t>::max();

        return whole_ticks + fractional_ticks;
    }

private:
    std::uint32_t sample_rate_ = 0;
    std::uint64_t remainder_ = 0;
};

} // namespace arssyut::core::audio
