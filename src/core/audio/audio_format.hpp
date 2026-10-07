#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>

namespace arssyut::core::audio {

enum class AudioSampleType : std::uint8_t {
    Unknown = 0,
    Float32,
    Pcm16,
    Pcm24In32,
    Pcm32,
};

enum class AudioSourceId : std::uint8_t {
    Microphone = 0,
    SystemAudio = 1,
};

inline constexpr std::uint32_t kSpeakerFrontLeft = 0x00000001u;
inline constexpr std::uint32_t kSpeakerFrontRight = 0x00000002u;
inline constexpr std::uint32_t kSpeakerFrontCenter = 0x00000004u;
inline constexpr std::uint32_t kSpeakerLowFrequency = 0x00000008u;
inline constexpr std::uint32_t kSpeakerBackLeft = 0x00000010u;
inline constexpr std::uint32_t kSpeakerBackRight = 0x00000020u;
inline constexpr std::uint32_t kSpeakerSideLeft = 0x00000200u;
inline constexpr std::uint32_t kSpeakerSideRight = 0x00000400u;

inline constexpr std::uint32_t kStereoChannelMask =
    kSpeakerFrontLeft | kSpeakerFrontRight;
inline constexpr std::uint32_t kSurround51ChannelMask =
    kSpeakerFrontLeft |
    kSpeakerFrontRight |
    kSpeakerFrontCenter |
    kSpeakerLowFrequency |
    kSpeakerBackLeft |
    kSpeakerBackRight;
inline constexpr std::uint32_t kSurround71ChannelMask =
    kSurround51ChannelMask |
    kSpeakerSideLeft |
    kSpeakerSideRight;

[[nodiscard]] inline constexpr bool channel_mask_matches_count(
    std::uint16_t channels,
    std::uint32_t channel_mask) noexcept
{
    return channel_mask == 0 ||
           std::popcount(channel_mask) == static_cast<int>(channels);
}

struct AudioFormat {
    std::uint32_t sample_rate = 0;
    AudioSampleType sample_type = AudioSampleType::Unknown;
    std::uint16_t channels = 0;
    std::uint16_t container_bits_per_sample = 0;
    std::uint16_t valid_bits_per_sample = 0;
    std::uint32_t channel_mask = 0;
    std::uint16_t block_align = 0;

    [[nodiscard]] constexpr std::size_t bytes_per_container_sample() const noexcept
    {
        switch (sample_type) {
        case AudioSampleType::Float32:
        case AudioSampleType::Pcm24In32:
        case AudioSampleType::Pcm32:
            return 4;
        case AudioSampleType::Pcm16:
            return 2;
        case AudioSampleType::Unknown:
        default:
            return 0;
        }
    }

    [[nodiscard]] constexpr std::uint16_t expected_valid_bits() const noexcept
    {
        switch (sample_type) {
        case AudioSampleType::Float32:
        case AudioSampleType::Pcm32:
            return 32;
        case AudioSampleType::Pcm24In32:
            return 24;
        case AudioSampleType::Pcm16:
            return 16;
        case AudioSampleType::Unknown:
        default:
            return 0;
        }
    }

    [[nodiscard]] constexpr bool valid() const noexcept
    {
        const auto bytes = bytes_per_container_sample();
        const auto expected_bits = expected_valid_bits();

        if (sample_rate == 0 ||
            channels == 0 ||
            bytes == 0 ||
            expected_bits == 0)
            return false;

        if (container_bits_per_sample != bytes * 8 ||
            valid_bits_per_sample != expected_bits)
            return false;

        if (!channel_mask_matches_count(channels, channel_mask))
            return false;

        return block_align == channels * bytes;
    }

    [[nodiscard]] constexpr std::size_t bytes_for_frames(
        std::uint32_t frame_count) const noexcept
    {
        return static_cast<std::size_t>(frame_count) * block_align;
    }
};

struct AudioProfile {
    AudioFormat program_format{};
    std::uint32_t canonical_block_frames = 0;
    std::uint32_t aac_bitrate_bps = 0;

    [[nodiscard]] constexpr bool valid() const noexcept
    {
        return program_format.valid() &&
               program_format.sample_rate == 48'000 &&
               program_format.sample_type == AudioSampleType::Float32 &&
               program_format.channels == 2 &&
               program_format.channel_mask == kStereoChannelMask &&
               canonical_block_frames == 1'024 &&
               aac_bitrate_bps != 0;
    }
};

[[nodiscard]] inline constexpr AudioProfile canonical_audio_profile() noexcept
{
    return {
        .program_format = {
            .sample_rate = 48'000,
            .sample_type = AudioSampleType::Float32,
            .channels = 2,
            .container_bits_per_sample = 32,
            .valid_bits_per_sample = 32,
            .channel_mask = kStereoChannelMask,
            .block_align = 8,
        },
        .canonical_block_frames = 1'024,
        .aac_bitrate_bps = 192'000,
    };
}

[[nodiscard]] inline constexpr std::uint32_t source_presence_bit(
    AudioSourceId source) noexcept
{
    return 1u << static_cast<std::uint32_t>(source);
}

} // namespace arssyut::core::audio
