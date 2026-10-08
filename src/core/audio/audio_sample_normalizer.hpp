#pragma once

#include "core/audio/audio_packet.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

namespace arssyut::core::audio {

struct AudioNormalizationStats {
    std::uint64_t frames_written = 0;
    std::uint64_t samples_written = 0;
    std::uint64_t non_finite_float_samples = 0;
};

enum class AudioNormalizeStatus : std::uint8_t {
    Ok = 0,
    InvalidPacket,
    OutputTooSmall,
    UnsupportedFormat,
};

/*
 * Converts one retained WASAPI/native packet into normalized interleaved
 * float32 without allocation and without changing sample rate/channel count.
 *
 * Source-format normalization is deliberately separate from resampling:
 * - this function owns only native container -> float32 conversion;
 * - IAudioResampler owns channel/rate conversion into the canonical 48 kHz
 *   stereo program bus;
 * - timeline placement remains owned by AudioSourceTimelineMapper.
 *
 * WAVEFORMATEXTENSIBLE PCM valid bits are left-aligned when valid precision is
 * smaller than the container. Pcm24In32 therefore uses the signed 32-bit
 * container value directly with 2^31 scaling; no right-aligned 24-bit guess is
 * permitted.
 *
 * Float32 non-finite values are converted to exact silence and counted so NaN
 * or Inf cannot poison mixer/RMS/clipping state.
 */
[[nodiscard]] inline AudioNormalizeStatus
normalize_audio_packet_to_float32(
    const AudioSourcePacket &packet,
    std::span<const std::byte> payload,
    std::span<float> output_interleaved,
    AudioNormalizationStats *stats = nullptr) noexcept
{
    AudioNormalizationStats local{};

    if (!packet.metadata_valid() ||
        payload.size() != packet.payload_bytes)
        return AudioNormalizeStatus::InvalidPacket;

    const std::size_t sample_count =
        static_cast<std::size_t>(packet.frame_count) *
        packet.native_format.channels;

    if (output_interleaved.size() < sample_count)
        return AudioNormalizeStatus::OutputTooSmall;

    if (has_flag(
            packet.flags,
            AudioPacketFlag::Silent)) {
        std::fill_n(
            output_interleaved.begin(),
            sample_count,
            0.0F);

        local.frames_written =
            packet.frame_count;
        local.samples_written =
            sample_count;

        if (stats != nullptr)
            *stats = local;
        return AudioNormalizeStatus::Ok;
    }

    const auto read_u16 =
        [](const std::byte *p) noexcept {
            return static_cast<std::uint16_t>(
                static_cast<std::uint16_t>(
                    std::to_integer<std::uint8_t>(p[0])) |
                (static_cast<std::uint16_t>(
                     std::to_integer<std::uint8_t>(p[1]))
                 << 8U));
        };

    const auto read_u32 =
        [](const std::byte *p) noexcept {
            return
                static_cast<std::uint32_t>(
                    std::to_integer<std::uint8_t>(p[0])) |
                (static_cast<std::uint32_t>(
                     std::to_integer<std::uint8_t>(p[1]))
                 << 8U) |
                (static_cast<std::uint32_t>(
                     std::to_integer<std::uint8_t>(p[2]))
                 << 16U) |
                (static_cast<std::uint32_t>(
                     std::to_integer<std::uint8_t>(p[3]))
                 << 24U);
        };

    const auto sample_type =
        packet.native_format.sample_type;

    switch (sample_type) {
    case AudioSampleType::Float32:
        if (payload.size() !=
            sample_count * sizeof(float))
            return AudioNormalizeStatus::InvalidPacket;

        for (std::size_t sample = 0;
             sample < sample_count;
             ++sample) {
            const std::uint32_t bits =
                read_u32(
                    payload.data() +
                    sample * sizeof(float));
            const float value =
                std::bit_cast<float>(bits);

            if (std::isfinite(value)) {
                output_interleaved[sample] =
                    value;
            } else {
                output_interleaved[sample] =
                    0.0F;
                ++local.non_finite_float_samples;
            }
        }
        break;

    case AudioSampleType::Pcm16:
        if (payload.size() !=
            sample_count * sizeof(std::int16_t))
            return AudioNormalizeStatus::InvalidPacket;

        for (std::size_t sample = 0;
             sample < sample_count;
             ++sample) {
            const auto raw =
                static_cast<std::int16_t>(
                    read_u16(
                        payload.data() +
                        sample * sizeof(std::int16_t)));

            output_interleaved[sample] =
                static_cast<float>(
                    static_cast<double>(raw) /
                    32'768.0);
        }
        break;

    case AudioSampleType::Pcm24In32:
    case AudioSampleType::Pcm32:
        if (payload.size() !=
            sample_count * sizeof(std::int32_t))
            return AudioNormalizeStatus::InvalidPacket;

        for (std::size_t sample = 0;
             sample < sample_count;
             ++sample) {
            const auto raw =
                static_cast<std::int32_t>(
                    read_u32(
                        payload.data() +
                        sample * sizeof(std::int32_t)));

            output_interleaved[sample] =
                static_cast<float>(
                    static_cast<double>(raw) /
                    2'147'483'648.0);
        }
        break;

    case AudioSampleType::Unknown:
    default:
        return AudioNormalizeStatus::UnsupportedFormat;
    }

    local.frames_written =
        packet.frame_count;
    local.samples_written =
        sample_count;

    if (stats != nullptr)
        *stats = local;

    return AudioNormalizeStatus::Ok;
}

} // namespace arssyut::core::audio
