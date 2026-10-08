#pragma once

#include "core/audio/audio_mix.hpp"
#include "core/audio/audio_packet.hpp"
#include "core/audio/audio_program_clock.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

namespace arssyut::core::audio {

struct AudioSourceMixConfig {
    float gain = 1.0F;
    bool muted = false;
};

struct AudioLevelSnapshot {
    float peak = 0.0F;
    float rms = 0.0F;
    std::uint64_t clipped_samples = 0;
    std::uint64_t frames_observed = 0;
};

struct AudioProgramMediaSpan {
    std::int64_t media_start_100ns = 0;
    std::int64_t media_end_100ns = 0;

    [[nodiscard]] bool valid() const noexcept
    {
        return media_end_100ns > media_start_100ns;
    }
};

struct AudioProgramBlock {
    CanonicalAudioBlock audio{};
    bool follows_output_discontinuity = false;
};

struct AudioProgramMixStats {
    std::uint64_t blocks_closed = 0;
    std::uint64_t blocks_emitted = 0;
    std::uint64_t silent_blocks = 0;
    std::uint64_t stale_packets_discarded = 0;
    std::uint64_t missing_source_intervals = 0;
    std::array<std::uint64_t, 2> missing_source_intervals_by_source{};
    std::uint64_t output_overflow_blocks = 0;
    std::uint64_t output_discontinuities = 0;
    std::uint64_t clipped_samples = 0;
};

[[nodiscard]] inline constexpr std::size_t
source_index(AudioSourceId source) noexcept
{
    return static_cast<std::size_t>(source);
}

/*
 * Portable P7A5 program-block compositor.
 *
 * This object intentionally does not own packet queues or a resampler. It is
 * the deterministic block authority used after each source has been normalized
 * into canonical 48 kHz stereo frames aligned to the current program interval.
 *
 * Missing input remains silence. Mute never changes time. Source arrival never
 * changes the block PTS. A packet is fully stale only when its media interval
 * ends at or before the current open block; overlapping packets must be trimmed
 * by the assembler instead of discarded wholesale.
 *
 * Closing a block always advances the program clock. If the bounded writer
 * handoff rejects a block, the lost interval is counted and the next successful
 * block carries follows_output_discontinuity=true. Backpressure therefore never
 * shifts later media earlier.
 */
class AudioProgramMixer final {
public:
    static constexpr std::size_t kSourceCount = 2;

    void reset(std::int64_t media_zero_100ns = 0) noexcept
    {
        clock_.reset(media_zero_100ns);
        stats_ = {};
        levels_ = {};
        pending_output_discontinuity_ = false;
    }

    void set_source_config(
        AudioSourceId source,
        AudioSourceMixConfig config) noexcept
    {
        configs_[source_index(source)] =
            sanitize(config);
    }

    [[nodiscard]] AudioSourceMixConfig source_config(
        AudioSourceId source) const noexcept
    {
        return configs_[source_index(source)];
    }

    [[nodiscard]] std::int64_t current_start_100ns() const noexcept
    {
        return clock_.next_start_100ns();
    }

    [[nodiscard]] std::int64_t current_end_100ns() const noexcept
    {
        return clock_.next_end_100ns();
    }

    [[nodiscard]] bool interval_is_fully_stale(
        AudioProgramMediaSpan span) const noexcept
    {
        return span.valid() &&
               span.media_end_100ns <=
                   current_start_100ns();
    }

    void count_stale_packet() noexcept
    {
        ++stats_.stale_packets_discarded;
    }

    [[nodiscard]] bool mix_source(
        AudioProgramBlock &block,
        AudioSourceId source,
        std::span<const float> canonical_stereo) noexcept
    {
        if (block.audio.frame_count >
                CanonicalAudioBlock::kFrameCapacity ||
            canonical_stereo.size() !=
                static_cast<std::size_t>(
                    block.audio.frame_count) *
                    CanonicalAudioBlock::kChannels)
            return false;

        const auto config =
            configs_[source_index(source)];

        if (config.muted) {
            levels_[source_index(source)] = {};
            return true;
        }

        if (!accumulate_stereo(
                canonical_stereo,
                block.audio.interleaved(),
                config.gain,
                nullptr))
            return false;

        block.audio.source_presence_mask |=
            source_presence_bit(source);

        levels_[source_index(source)] =
            level_snapshot(
                canonical_stereo,
                config.gain);

        return true;
    }

    void note_missing_source_interval(
        AudioSourceId source) noexcept
    {
        ++stats_.missing_source_intervals;
        ++stats_.missing_source_intervals_by_source[
            source_index(source)];
        levels_[source_index(source)] = {};
    }

    [[nodiscard]] AudioProgramBlock begin_block() const noexcept
    {
        AudioProgramBlock block;
        block.audio.media_start_100ns =
            clock_.next_start_100ns();
        block.audio.clear(
            CanonicalAudioBlock::kFrameCapacity);
        block.follows_output_discontinuity =
            pending_output_discontinuity_;
        return block;
    }

    void close_block(
        const AudioProgramBlock &block,
        bool output_accepted) noexcept
    {
        ++stats_.blocks_closed;

        if (output_accepted) {
            ++stats_.blocks_emitted;

            if (block.audio.source_presence_mask == 0)
                ++stats_.silent_blocks;

            stats_.clipped_samples +=
                count_over_range(
                    block.audio.interleaved());

            if (block.follows_output_discontinuity) {
                ++stats_.output_discontinuities;
                pending_output_discontinuity_ = false;
            }
        } else {
            ++stats_.output_overflow_blocks;
            pending_output_discontinuity_ = true;
        }

        clock_.advance();
    }

    [[nodiscard]] const AudioProgramMixStats &stats() const noexcept
    {
        return stats_;
    }

    [[nodiscard]] AudioLevelSnapshot level(
        AudioSourceId source) const noexcept
    {
        return levels_[source_index(source)];
    }

private:
    [[nodiscard]] static AudioSourceMixConfig sanitize(
        AudioSourceMixConfig config) noexcept
    {
        if (!std::isfinite(config.gain))
            config.gain = 0.0F;

        config.gain =
            std::clamp(
                config.gain,
                0.0F,
                4.0F);
        return config;
    }

    [[nodiscard]] static std::uint64_t count_over_range(
        std::span<const float> stereo) noexcept
    {
        std::uint64_t count = 0;
        for (const float sample : stereo)
            count += std::fabs(sample) > 1.0F ? 1U : 0U;
        return count;
    }

    [[nodiscard]] static AudioLevelSnapshot level_snapshot(
        std::span<const float> stereo,
        float gain) noexcept
    {
        AudioLevelSnapshot result;
        if ((stereo.size() % 2) != 0)
            return result;

        double square_sum = 0.0;
        for (float sample : stereo) {
            const float scaled =
                sample * gain;
            const float magnitude =
                std::fabs(scaled);

            result.peak =
                std::max(
                    result.peak,
                    magnitude);
            result.clipped_samples +=
                magnitude > 1.0F ? 1U : 0U;
            square_sum +=
                static_cast<double>(scaled) *
                static_cast<double>(scaled);
        }

        if (!stereo.empty()) {
            result.rms =
                static_cast<float>(
                    std::sqrt(
                        square_sum /
                        static_cast<double>(
                            stereo.size())));
        }

        result.frames_observed =
            stereo.size() / 2;
        return result;
    }

    AudioProgramClock clock_{};
    std::array<AudioSourceMixConfig, kSourceCount> configs_{};
    std::array<AudioLevelSnapshot, kSourceCount> levels_{};
    AudioProgramMixStats stats_{};
    bool pending_output_discontinuity_ = false;
};

} // namespace arssyut::core::audio
