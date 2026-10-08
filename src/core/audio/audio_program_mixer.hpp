#pragma once

#include "core/audio/audio_mix.hpp"
#include "core/audio/audio_packet.hpp"
#include "core/audio/audio_program_clock.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
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

namespace detail {

/*
 * Versioned atomic publications are single-writer/multi-reader.
 *
 * Payload members are themselves atomic, so readers never data-race with the
 * realtime writer. The even sequence value gives a coherent immutable snapshot
 * across multiple fields without locking the mixer thread.
 */
class AtomicAudioLevelPublication final {
public:
    void publish(
        const AudioLevelSnapshot &value) noexcept
    {
        sequence_.fetch_add(
            1,
            std::memory_order_acq_rel);

        peak_bits_.store(
            std::bit_cast<std::uint32_t>(value.peak),
            std::memory_order_relaxed);
        rms_bits_.store(
            std::bit_cast<std::uint32_t>(value.rms),
            std::memory_order_relaxed);
        clipped_samples_.store(
            value.clipped_samples,
            std::memory_order_relaxed);
        frames_observed_.store(
            value.frames_observed,
            std::memory_order_relaxed);

        sequence_.fetch_add(
            1,
            std::memory_order_release);
    }

    [[nodiscard]] AudioLevelSnapshot load() const noexcept
    {
        for (;;) {
            const auto before =
                sequence_.load(
                    std::memory_order_acquire);
            if ((before & 1U) != 0)
                continue;

            AudioLevelSnapshot result;
            result.peak =
                std::bit_cast<float>(
                    peak_bits_.load(
                        std::memory_order_relaxed));
            result.rms =
                std::bit_cast<float>(
                    rms_bits_.load(
                        std::memory_order_relaxed));
            result.clipped_samples =
                clipped_samples_.load(
                    std::memory_order_relaxed);
            result.frames_observed =
                frames_observed_.load(
                    std::memory_order_relaxed);

            const auto after =
                sequence_.load(
                    std::memory_order_acquire);
            if (before == after &&
                (after & 1U) == 0)
                return result;
        }
    }

private:
    std::atomic<std::uint64_t> sequence_{0};
    std::atomic<std::uint32_t> peak_bits_{0};
    std::atomic<std::uint32_t> rms_bits_{0};
    std::atomic<std::uint64_t> clipped_samples_{0};
    std::atomic<std::uint64_t> frames_observed_{0};
};

class AtomicAudioConfigPublication final {
public:
    void publish(
        AudioSourceMixConfig value) noexcept
    {
        sequence_.fetch_add(
            1,
            std::memory_order_acq_rel);
        gain_bits_.store(
            std::bit_cast<std::uint32_t>(value.gain),
            std::memory_order_relaxed);
        muted_.store(
            value.muted,
            std::memory_order_relaxed);
        sequence_.fetch_add(
            1,
            std::memory_order_release);
    }

    [[nodiscard]] AudioSourceMixConfig load() const noexcept
    {
        for (;;) {
            const auto before =
                sequence_.load(
                    std::memory_order_acquire);
            if ((before & 1U) != 0)
                continue;

            AudioSourceMixConfig result;
            result.gain =
                std::bit_cast<float>(
                    gain_bits_.load(
                        std::memory_order_relaxed));
            result.muted =
                muted_.load(
                    std::memory_order_relaxed);

            const auto after =
                sequence_.load(
                    std::memory_order_acquire);
            if (before == after &&
                (after & 1U) == 0)
                return result;
        }
    }

private:
    std::atomic<std::uint64_t> sequence_{0};
    std::atomic<std::uint32_t> gain_bits_{
        std::bit_cast<std::uint32_t>(1.0F)};
    std::atomic<bool> muted_{false};
};

class AtomicAudioStatsPublication final {
public:
    void publish(
        const AudioProgramMixStats &value) noexcept
    {
        sequence_.fetch_add(
            1,
            std::memory_order_acq_rel);

        blocks_closed_.store(
            value.blocks_closed,
            std::memory_order_relaxed);
        blocks_emitted_.store(
            value.blocks_emitted,
            std::memory_order_relaxed);
        silent_blocks_.store(
            value.silent_blocks,
            std::memory_order_relaxed);
        stale_packets_discarded_.store(
            value.stale_packets_discarded,
            std::memory_order_relaxed);
        missing_source_intervals_.store(
            value.missing_source_intervals,
            std::memory_order_relaxed);
        for (std::size_t index = 0;
             index < value.missing_source_intervals_by_source.size();
             ++index) {
            missing_by_source_[index].store(
                value.missing_source_intervals_by_source[index],
                std::memory_order_relaxed);
        }
        output_overflow_blocks_.store(
            value.output_overflow_blocks,
            std::memory_order_relaxed);
        output_discontinuities_.store(
            value.output_discontinuities,
            std::memory_order_relaxed);
        clipped_samples_.store(
            value.clipped_samples,
            std::memory_order_relaxed);

        sequence_.fetch_add(
            1,
            std::memory_order_release);
    }

    [[nodiscard]] AudioProgramMixStats load() const noexcept
    {
        for (;;) {
            const auto before =
                sequence_.load(
                    std::memory_order_acquire);
            if ((before & 1U) != 0)
                continue;

            AudioProgramMixStats result;
            result.blocks_closed =
                blocks_closed_.load(
                    std::memory_order_relaxed);
            result.blocks_emitted =
                blocks_emitted_.load(
                    std::memory_order_relaxed);
            result.silent_blocks =
                silent_blocks_.load(
                    std::memory_order_relaxed);
            result.stale_packets_discarded =
                stale_packets_discarded_.load(
                    std::memory_order_relaxed);
            result.missing_source_intervals =
                missing_source_intervals_.load(
                    std::memory_order_relaxed);
            for (std::size_t index = 0;
                 index < result.missing_source_intervals_by_source.size();
                 ++index) {
                result.missing_source_intervals_by_source[index] =
                    missing_by_source_[index].load(
                        std::memory_order_relaxed);
            }
            result.output_overflow_blocks =
                output_overflow_blocks_.load(
                    std::memory_order_relaxed);
            result.output_discontinuities =
                output_discontinuities_.load(
                    std::memory_order_relaxed);
            result.clipped_samples =
                clipped_samples_.load(
                    std::memory_order_relaxed);

            const auto after =
                sequence_.load(
                    std::memory_order_acquire);
            if (before == after &&
                (after & 1U) == 0)
                return result;
        }
    }

private:
    std::atomic<std::uint64_t> sequence_{0};
    std::atomic<std::uint64_t> blocks_closed_{0};
    std::atomic<std::uint64_t> blocks_emitted_{0};
    std::atomic<std::uint64_t> silent_blocks_{0};
    std::atomic<std::uint64_t> stale_packets_discarded_{0};
    std::atomic<std::uint64_t> missing_source_intervals_{0};
    std::array<std::atomic<std::uint64_t>, 2> missing_by_source_{};
    std::atomic<std::uint64_t> output_overflow_blocks_{0};
    std::atomic<std::uint64_t> output_discontinuities_{0};
    std::atomic<std::uint64_t> clipped_samples_{0};
};

} // namespace detail

/*
 * Portable P7A5 program-block compositor.
 *
 * Mixer/media operations are single-owner and will run on the P7A5 mixer
 * worker. Control-plane gain/mute publication and telemetry reads are safe from
 * other threads through versioned atomic snapshots; no UI/diagnostic reader
 * locks the realtime mixer.
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
        worker_stats_ = {};
        stats_publication_.publish(worker_stats_);

        for (auto &level : level_publications_)
            level.publish({});

        pending_output_discontinuity_ = false;
    }

    void set_source_config(
        AudioSourceId source,
        AudioSourceMixConfig config) noexcept
    {
        config_publications_[source_index(source)].publish(
            sanitize(config));
    }

    [[nodiscard]] AudioSourceMixConfig source_config(
        AudioSourceId source) const noexcept
    {
        return config_publications_[source_index(source)].load();
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
        ++worker_stats_.stale_packets_discarded;
        publish_stats();
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
            source_config(source);

        if (config.muted) {
            level_publications_[source_index(source)].publish({});
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

        level_publications_[source_index(source)].publish(
            level_snapshot(
                canonical_stereo,
                config.gain));

        return true;
    }

    void note_missing_source_interval(
        AudioSourceId source) noexcept
    {
        ++worker_stats_.missing_source_intervals;
        ++worker_stats_.missing_source_intervals_by_source[
            source_index(source)];
        level_publications_[source_index(source)].publish({});
        publish_stats();
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
        ++worker_stats_.blocks_closed;

        if (output_accepted) {
            ++worker_stats_.blocks_emitted;

            if (block.audio.source_presence_mask == 0)
                ++worker_stats_.silent_blocks;

            worker_stats_.clipped_samples +=
                count_over_range(
                    block.audio.interleaved());

            if (block.follows_output_discontinuity) {
                ++worker_stats_.output_discontinuities;
                pending_output_discontinuity_ = false;
            }
        } else {
            ++worker_stats_.output_overflow_blocks;
            pending_output_discontinuity_ = true;
        }

        clock_.advance();
        publish_stats();
    }

    [[nodiscard]] AudioProgramMixStats stats() const noexcept
    {
        return stats_publication_.load();
    }

    [[nodiscard]] AudioLevelSnapshot level(
        AudioSourceId source) const noexcept
    {
        return level_publications_[source_index(source)].load();
    }

private:
    void publish_stats() noexcept
    {
        stats_publication_.publish(
            worker_stats_);
    }

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
    std::array<detail::AtomicAudioConfigPublication, kSourceCount>
        config_publications_{};
    std::array<detail::AtomicAudioLevelPublication, kSourceCount>
        level_publications_{};
    AudioProgramMixStats worker_stats_{};
    detail::AtomicAudioStatsPublication stats_publication_{};
    bool pending_output_discontinuity_ = false;
};

} // namespace arssyut::core::audio
