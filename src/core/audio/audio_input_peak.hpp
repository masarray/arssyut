#pragma once

// Read-only per-channel live peak. No downmix, effect, gain or A/V clock.
#include "core/audio/audio_sample_normalizer.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <span>

namespace arssyut::core::audio {
struct AudioInputPeak {
    float left = 0.0f;
    float right = 0.0f;
    std::uint16_t channels = 0;
};

[[nodiscard]] inline bool accumulate_input_peak(
    const AudioSourcePacket &packet,
    std::span<const std::byte> bytes,
    AudioInputPeak &out) noexcept
{
    constexpr std::uint32_t chunk = 256;
    constexpr std::size_t max_channels = 8;
    if (!packet.metadata_valid() ||
        packet.native_format.channels > max_channels ||
        bytes.size() != packet.payload_bytes)
        return false;

    const auto channels = packet.native_format.channels;
    out.channels = channels;
    if (has_flag(packet.flags, AudioPacketFlag::Silent))
        return true;

    std::array<float, chunk * max_channels> scratch{};
    for (std::uint32_t offset = 0; offset < packet.frame_count; offset += chunk) {
        const auto frames = std::min(chunk, packet.frame_count - offset);
        const auto byte_stride = packet.native_format.block_align;
        AudioSourcePacket slice = packet;
        slice.frame_count = frames;
        slice.payload_bytes = frames * byte_stride;
        if (normalize_audio_packet_to_float32(
            slice,
            bytes.subspan(static_cast<std::size_t>(offset) * byte_stride,
                          slice.payload_bytes),
            std::span<float>{scratch.data(),
                static_cast<std::size_t>(frames) * channels}) !=
            AudioNormalizeStatus::Ok)
            return false;
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            const std::size_t index = static_cast<std::size_t>(frame) * channels;
            out.left = std::max(out.left,
                std::min(1.0f, std::abs(scratch[index])));
            if (channels > 1)
                out.right = std::max(out.right,
                    std::min(1.0f, std::abs(scratch[index+1])));
        }
    }
    return true;
}
} // namespace arssyut::core::audio
