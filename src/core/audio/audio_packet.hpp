#pragma once

#include "core/audio/audio_format.hpp"

#include <array>
#include <cstdint>
#include <limits>
#include <span>

namespace arssyut::core::audio {

enum class AudioPacketFlag : std::uint8_t {
    None = 0,
    Silent = 1u << 0,
    Discontinuity = 1u << 1,
    TimestampError = 1u << 2,
};

[[nodiscard]] inline constexpr AudioPacketFlag operator|(
    AudioPacketFlag left,
    AudioPacketFlag right) noexcept
{
    return static_cast<AudioPacketFlag>(
        static_cast<std::uint8_t>(left) |
        static_cast<std::uint8_t>(right));
}

inline constexpr AudioPacketFlag &operator|=(
    AudioPacketFlag &left,
    AudioPacketFlag right) noexcept
{
    left = left | right;
    return left;
}

[[nodiscard]] inline constexpr bool has_flag(
    AudioPacketFlag flags,
    AudioPacketFlag flag) noexcept
{
    return (static_cast<std::uint8_t>(flags) &
            static_cast<std::uint8_t>(flag)) != 0;
}

enum class AudioTimestampQuality : std::uint8_t {
    DeviceQpcTrusted = 0,
    ContinuityReconstructed,
    HostQpcFallback,
    Discontinuous,
};

struct AudioTimestampEvidence {
    AudioTimestampQuality quality =
        AudioTimestampQuality::HostQpcFallback;
    std::int64_t packet_start_qpc_100ns = 0;
    std::int64_t host_observed_qpc_100ns = 0;
    std::uint64_t device_frame_position = 0;
    bool eligible_for_drift = false;
};

struct AudioTimingState {
    AudioTimestampQuality quality =
        AudioTimestampQuality::HostQpcFallback;
    std::int64_t last_trusted_qpc_100ns = 0;
    std::uint64_t last_trusted_device_frame = 0;
    std::uint32_t consecutive_bad_timestamps = 0;
    std::uint32_t consecutive_good_timestamps = 0;
    bool has_trusted_anchor = false;
};

inline constexpr std::uint32_t kInvalidAudioPoolSlot =
    std::numeric_limits<std::uint32_t>::max();

struct AudioSourcePacket {
    AudioSourceId source = AudioSourceId::Microphone;
    AudioFormat native_format{};
    std::uint32_t frame_count = 0;
    AudioTimestampEvidence timing{};
    AudioPacketFlag flags = AudioPacketFlag::None;
    std::uint32_t pool_slot = kInvalidAudioPoolSlot;
    std::uint32_t payload_bytes = 0;

    [[nodiscard]] bool metadata_valid() const noexcept
    {
        if (!native_format.valid() || frame_count == 0)
            return false;

        if (has_flag(flags, AudioPacketFlag::Silent))
            return payload_bytes == 0 ||
                   pool_slot != kInvalidAudioPoolSlot;

        return pool_slot != kInvalidAudioPoolSlot &&
               payload_bytes ==
                   native_format.bytes_for_frames(frame_count);
    }
};

struct CanonicalAudioBlock {
    static constexpr std::uint32_t kSampleRate = 48'000;
    static constexpr std::uint32_t kFrameCapacity = 1'024;
    static constexpr std::uint16_t kChannels = 2;
    static constexpr std::size_t kSampleCapacity =
        static_cast<std::size_t>(kFrameCapacity) * kChannels;

    std::int64_t media_start_100ns = 0;
    std::uint32_t frame_count = 0;
    std::uint32_t source_presence_mask = 0;
    std::uint32_t discontinuity_mask = 0;
    std::array<float, kSampleCapacity> samples{};

    [[nodiscard]] bool valid() const noexcept
    {
        return frame_count <= kFrameCapacity;
    }

    [[nodiscard]] std::span<float> interleaved() noexcept
    {
        return {samples.data(),
                static_cast<std::size_t>(frame_count) * kChannels};
    }

    [[nodiscard]] std::span<const float> interleaved() const noexcept
    {
        return {samples.data(),
                static_cast<std::size_t>(frame_count) * kChannels};
    }

    void clear(std::uint32_t frames = kFrameCapacity) noexcept
    {
        frame_count = frames <= kFrameCapacity ? frames : kFrameCapacity;
        source_presence_mask = 0;
        discontinuity_mask = 0;
        for (auto &sample : samples)
            sample = 0.0f;
    }
};

} // namespace arssyut::core::audio
