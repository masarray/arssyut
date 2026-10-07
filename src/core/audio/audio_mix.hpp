#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

namespace arssyut::core::audio {

struct MixStats {
    std::uint64_t frames_mixed = 0;
    std::uint64_t over_range_samples = 0;
    float peak_absolute = 0.0f;
};

[[nodiscard]] inline bool accumulate_mono_to_stereo(
    std::span<const float> mono,
    std::span<float> stereo,
    float gain,
    MixStats *stats = nullptr) noexcept
{
    if (stereo.size() < mono.size() * 2)
        return false;

    MixStats local{};
    for (std::size_t frame = 0; frame < mono.size(); ++frame) {
        const float value = mono[frame] * gain;
        const std::size_t index = frame * 2;
        stereo[index] += value;
        stereo[index + 1] += value;

        const float left_abs = std::fabs(stereo[index]);
        const float right_abs = std::fabs(stereo[index + 1]);
        local.peak_absolute =
            std::max(local.peak_absolute, std::max(left_abs, right_abs));
        local.over_range_samples += left_abs > 1.0f ? 1u : 0u;
        local.over_range_samples += right_abs > 1.0f ? 1u : 0u;
    }

    local.frames_mixed = mono.size();
    if (stats != nullptr)
        *stats = local;
    return true;
}

[[nodiscard]] inline bool accumulate_stereo(
    std::span<const float> source_interleaved,
    std::span<float> destination_interleaved,
    float gain,
    MixStats *stats = nullptr) noexcept
{
    if ((source_interleaved.size() % 2) != 0 ||
        destination_interleaved.size() < source_interleaved.size())
        return false;

    MixStats local{};
    for (std::size_t index = 0;
         index < source_interleaved.size();
         ++index) {
        destination_interleaved[index] +=
            source_interleaved[index] * gain;
        const float absolute =
            std::fabs(destination_interleaved[index]);
        local.peak_absolute =
            std::max(local.peak_absolute, absolute);
        local.over_range_samples += absolute > 1.0f ? 1u : 0u;
    }

    local.frames_mixed = source_interleaved.size() / 2;
    if (stats != nullptr)
        *stats = local;
    return true;
}

[[nodiscard]] inline bool copy_or_map_to_stereo(
    std::span<const float> source_interleaved,
    std::uint16_t source_channels,
    std::span<float> destination_interleaved,
    float gain = 1.0f) noexcept
{
    if (source_channels == 1) {
        if (destination_interleaved.size() < source_interleaved.size() * 2)
            return false;

        for (std::size_t frame = 0;
             frame < source_interleaved.size();
             ++frame) {
            const float value = source_interleaved[frame] * gain;
            destination_interleaved[frame * 2] = value;
            destination_interleaved[frame * 2 + 1] = value;
        }
        return true;
    }

    if (source_channels == 2) {
        if ((source_interleaved.size() % 2) != 0 ||
            destination_interleaved.size() < source_interleaved.size())
            return false;

        for (std::size_t index = 0;
             index < source_interleaved.size();
             ++index)
            destination_interleaved[index] =
                source_interleaved[index] * gain;
        return true;
    }

    return false;
}

} // namespace arssyut::core::audio
