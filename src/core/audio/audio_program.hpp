#pragma once

#include "core/audio/audio_format.hpp"
#include "core/audio/audio_packet.hpp"
#include "core/audio/audio_time.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace arssyut::core::audio {

class AudioProgramClock final {
public:
    static constexpr std::uint32_t kSampleRate =
        CanonicalAudioBlock::kSampleRate;
    static constexpr std::uint32_t kBlockFrames =
        CanonicalAudioBlock::kFrameCapacity;

    explicit AudioProgramClock(
        std::int64_t media_zero_100ns = 0) noexcept
        : media_zero_100ns_(media_zero_100ns)
    {
    }

    void reset(
        std::int64_t media_zero_100ns) noexcept
    {
        media_zero_100ns_ =
            media_zero_100ns;
    }

    [[nodiscard]] std::int64_t media_zero_100ns() const noexcept
    {
        return media_zero_100ns_;
    }

    [[nodiscard]] bool block_start_100ns(
        std::uint64_t block_index,
        std::int64_t &result) const noexcept
    {
        if (block_index >
            std::numeric_limits<std::uint64_t>::max() /
                kBlockFrames)
            return false;

        const std::uint64_t frames =
            block_index *
            static_cast<std::uint64_t>(
                kBlockFrames);
        const std::uint64_t offset =
            frames_to_ticks_floor(
                frames,
                kSampleRate);

        if (offset >
            static_cast<std::uint64_t>(
                std::numeric_limits<std::int64_t>::max()))
            return false;

        const auto signed_offset =
            static_cast<std::int64_t>(
                offset);

        if (media_zero_100ns_ >
            std::numeric_limits<std::int64_t>::max() -
                signed_offset)
            return false;

        result =
            media_zero_100ns_ +
            signed_offset;
        return true;
    }

    [[nodiscard]] bool begin_block(
        std::uint64_t block_index,
        CanonicalAudioBlock &block) const noexcept
    {
        std::int64_t start = 0;
        if (!block_start_100ns(
                block_index,
                start))
            return false;

        block.clear(kBlockFrames);
        block.media_start_100ns =
            start;
        return true;
    }

private:
    std::int64_t media_zero_100ns_ = 0;
};

struct CanonicalSourceSpan {
    AudioSourceId source =
        AudioSourceId::Microphone;
    std::int64_t media_start_100ns = 0;
    std::uint32_t frame_count = 0;
    bool discontinuity = false;
    std::span<const float> samples{};

    [[nodiscard]] bool valid() const noexcept
    {
        return
            frame_count > 0 &&
            samples.size() >=
                static_cast<std::size_t>(
                    frame_count) *
                    CanonicalAudioBlock::kChannels;
    }
};

struct AudioSourceMixSettings {
    float gain = 1.0F;
    bool muted = false;

    [[nodiscard]] bool valid() const noexcept
    {
        return
            std::isfinite(gain) &&
            gain >= 0.0F;
    }
};

struct AudioSourceMixTelemetry {
    std::uint64_t source_frames = 0;
    std::uint64_t mixed_frames = 0;
    std::uint64_t stale_frames = 0;
    std::uint64_t deferred_frames = 0;
    std::uint64_t muted_frames = 0;
    std::uint64_t over_range_samples = 0;
    float peak_absolute = 0.0F;
    double rms = 0.0;
};

struct AudioProgramFinalizeTelemetry {
    std::uint64_t clipped_samples = 0;
    float peak_before_clip = 0.0F;
};

[[nodiscard]] inline bool mix_canonical_source_span(
    CanonicalAudioBlock &block,
    const CanonicalSourceSpan &source,
    AudioSourceMixSettings settings,
    AudioSourceMixTelemetry *telemetry = nullptr) noexcept
{
    if (!block.valid() ||
        block.frame_count == 0 ||
        !source.valid() ||
        !settings.valid())
        return false;

    AudioSourceMixTelemetry local{};
    local.source_frames =
        source.frame_count;

    std::uint32_t source_offset = 0;
    std::uint32_t destination_offset = 0;

    if (source.media_start_100ns <
        block.media_start_100ns) {
        const std::uint64_t delta =
            static_cast<std::uint64_t>(
                block.media_start_100ns -
                source.media_start_100ns);

        source_offset =
            static_cast<std::uint32_t>(
                std::min<std::uint64_t>(
                    source.frame_count,
                    ticks_to_frames_floor(
                        delta,
                        CanonicalAudioBlock::kSampleRate)));
    } else if (source.media_start_100ns >
               block.media_start_100ns) {
        const std::uint64_t delta =
            static_cast<std::uint64_t>(
                source.media_start_100ns -
                block.media_start_100ns);

        destination_offset =
            static_cast<std::uint32_t>(
                std::min<std::uint64_t>(
                    block.frame_count,
                    ticks_to_frames_floor(
                        delta,
                        CanonicalAudioBlock::kSampleRate)));
    }

    local.stale_frames =
        source_offset;

    if (source_offset >=
            source.frame_count ||
        destination_offset >=
            block.frame_count) {
        local.deferred_frames =
            source.frame_count -
            source_offset;

        if (telemetry != nullptr)
            *telemetry = local;
        return true;
    }

    const std::uint32_t source_remaining =
        source.frame_count -
        source_offset;
    const std::uint32_t destination_remaining =
        block.frame_count -
        destination_offset;
    const std::uint32_t frames =
        std::min(
            source_remaining,
            destination_remaining);

    local.mixed_frames =
        frames;
    local.deferred_frames =
        source_remaining -
        frames;

    if (frames == 0) {
        if (telemetry != nullptr)
            *telemetry = local;
        return true;
    }

    block.source_presence_mask |=
        source_presence_bit(
            source.source);

    if (source.discontinuity) {
        block.discontinuity_mask |=
            source_presence_bit(
                source.source);
    }

    const float effective_gain =
        settings.muted
            ? 0.0F
            : settings.gain;

    if (settings.muted)
        local.muted_frames =
            frames;

    long double sum_squares = 0.0L;

    const std::size_t source_sample_offset =
        static_cast<std::size_t>(
            source_offset) *
        CanonicalAudioBlock::kChannels;
    const std::size_t destination_sample_offset =
        static_cast<std::size_t>(
            destination_offset) *
        CanonicalAudioBlock::kChannels;
    const std::size_t sample_count =
        static_cast<std::size_t>(
            frames) *
        CanonicalAudioBlock::kChannels;

    for (std::size_t index = 0;
         index < sample_count;
         ++index) {
        const float value =
            source.samples[
                source_sample_offset +
                index] *
            effective_gain;

        block.samples[
            destination_sample_offset +
            index] +=
            value;

        const float absolute =
            std::fabs(value);
        local.peak_absolute =
            std::max(
                local.peak_absolute,
                absolute);
        local.over_range_samples +=
            absolute > 1.0F
                ? 1u
                : 0u;

        sum_squares +=
            static_cast<long double>(value) *
            static_cast<long double>(value);
    }

    if (sample_count != 0) {
        local.rms =
            std::sqrt(
                static_cast<double>(
                    sum_squares /
                    static_cast<long double>(
                        sample_count)));
    }

    if (telemetry != nullptr)
        *telemetry = local;

    return true;
}

[[nodiscard]] inline AudioProgramFinalizeTelemetry
finalize_program_block(
    CanonicalAudioBlock &block) noexcept
{
    AudioProgramFinalizeTelemetry result{};

    if (!block.valid())
        return result;

    const std::size_t sample_count =
        static_cast<std::size_t>(
            block.frame_count) *
        CanonicalAudioBlock::kChannels;

    for (std::size_t index = 0;
         index < sample_count;
         ++index) {
        const float value =
            block.samples[index];

        result.peak_before_clip =
            std::max(
                result.peak_before_clip,
                std::fabs(value));

        if (value > 1.0F) {
            block.samples[index] = 1.0F;
            ++result.clipped_samples;
        } else if (value < -1.0F) {
            block.samples[index] = -1.0F;
            ++result.clipped_samples;
        }
    }

    return result;
}

} // namespace arssyut::core::audio
