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

struct AudioProgramMixStats {
    std::uint64_t blocks_emitted = 0;
    std::uint64_t silent_blocks = 0;
    std::uint64_t stale_packets_discarded = 0;
    std::uint64_t missing_source_intervals = 0;
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
 * changes the block PTS. Late/stale policy is represented explicitly rather
 * than shifting media into a future block.
 */
class AudioProgramMixer final {
public:
    static constexpr std::size_t kSourceCount = 2;

    void reset(std::int64_t media_zero_100ns = 0) noexcept
    {
        clock_.reset(media_zero_100ns);
        stats_ = {};
        levels_ = {};
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

    [[nodiscard]] bool packet_is_stale(
        const AudioSourcePacket &packet) const noexcept
    {
        if (packet.timing.packet_start_qpc_100ns <= 0)
            return false;

        return packet.timing.packet_start_qpc_100ns <
               current_start_100ns();
    }

    void count_stale_packet() noexcept
    {
        ++stats_.stale_packets_discarded;
    }

    [[nodiscard]] bool mix_source(
        CanonicalAudioBlock &block,
        AudioSourceId source,
        std::span<const float> canonical_stereo) noexcept
    {
        if (block.frame_count >
                CanonicalAudioBlock::kFrameCapacity ||
            canonical_stereo.size() !=
                static_cast<std::size_t>(
                    block.frame_count) *
                    CanonicalAudioBlock::kChannels)
            return false;

        const auto config =
            configs_[source_index(source)];

        if (config.muted) {
            levels_[source_index(source)] = {};
            return true;
        }

        MixStats mix_stats{};
        if (!accumulate_stereo(
                canonical_stereo,
                block.interleaved(),
                config.gain,
                &mix_stats))
            return false;

        block.source_presence_mask |=
            source_presence_bit(source);

        stats_.clipped_samples +=
            mix_stats.over_range_samples;

        levels_[source_index(source)] =
            level_snapshot(
                canonical_stereo,
                config.gain);

        return true;
    }

    void note_missing_source_interval() noexcept
    {
        ++stats_.missing_source_intervals;
    }

    [[nodiscard]] CanonicalAudioBlock begin_block() const noexcept
    {
        CanonicalAudioBlock block;
        block.media_start_100ns =
            clock_.next_start_100ns();
        block.clear(
            CanonicalAudioBlock::kFrameCapacity);
        return block;
    }

    void commit_block(
        const CanonicalAudioBlock &block) noexcept
    {
        ++stats_.blocks_emitted;

        if (block.source_presence_mask == 0)
            ++stats_.silent_blocks;

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
};

} // namespace arssyut::core::audio
