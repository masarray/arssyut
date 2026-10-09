#pragma once

#include "core/audio/audio_program_mixer.hpp"
#include "core/audio/audio_time.hpp"
#include "core/result/status.hpp"
#include "core/time/monotonic_clock.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace arssyut::app::audio {

/*
 * The sole RecorderSession writer caller consumes this adapter; no capture or
 * mixer worker writes Media Foundation samples. It owns one preallocated
 * 1024-frame PCM16 block only, never a backlog, an independent audio clock,
 * or a writer. It converts the accepted canonical 48 kHz float32 program
 * directly into the Media Foundation AAC input format.
 *
 * Caller sequence: prepare() -> writer.write_audio_pcm16(view) -> finish().
 * Failed/backpressured writer submissions are dropped without ever retiming
 * later program blocks. All time is derived from canonical absolute frame
 * coordinates relative to the RecorderSession media-zero timestamp.
 */
enum class AacPrepareStatus : std::uint8_t {
    Ready = 0,
    PastStop,
    Busy,
    Invalid,
    OutOfOrder,
};

struct AacPcm16WriteView {
    std::span<const std::int16_t> interleaved{};
    std::int64_t relative_pts_100ns = 0;
    std::int64_t duration_100ns = 0;
    std::uint32_t frames = 0;
    bool discontinuity = false;
};

struct AacPcm16AdapterStats {
    std::uint64_t submitted_blocks = 0;
    std::uint64_t submitted_frames = 0;
    std::uint64_t dropped_blocks = 0;
    std::uint64_t skipped_frames = 0;
    std::uint64_t trimmed_stop_frames = 0;
    std::uint64_t clipped_samples = 0;
    std::uint64_t non_finite_samples = 0;
    std::uint64_t invalid_blocks = 0;
    std::uint64_t out_of_order_blocks = 0;
};

struct AacPrepareResult {
    AacPrepareStatus status = AacPrepareStatus::Invalid;
    AacPcm16WriteView view{};
};

// One synchronous attempt: distinguishes skipped tail/invalid timeline,
// writer backpressure, and successful AAC handoff. No retry queue.
struct AacSubmissionResult {
    AacPrepareStatus prepare_status = AacPrepareStatus::Invalid;
    core::Status writer_status = core::Status::success();
    bool writer_attempted = false;
    bool submitted = false;
};

class AacPcm16SubmissionAdapter final {
public:
    static constexpr std::uint32_t kFrameCount =
        core::audio::CanonicalAudioBlock::kFrameCapacity;
    static constexpr std::uint32_t kSampleRate =
        core::audio::CanonicalAudioBlock::kSampleRate;
    static constexpr std::size_t kSampleCapacity =
        core::audio::CanonicalAudioBlock::kSampleCapacity;

    void reset(std::int64_t media_zero_100ns) noexcept
    {
        media_zero_100ns_ = media_zero_100ns;
        next_minimum_frame_ = 0;
        pending_ = false;
        pending_next_frame_ = 0;
        pending_frames_ = 0;
        pending_clipped_ = 0;
        pending_non_finite_ = 0;
        next_output_discontinuity_ = false;
        stats_ = {};
        active_ = media_zero_100ns >= 0;
    }

    /*
     * A negative stop indicates normal live recording. A nonnegative stop is
     * the RecorderSession's absolute monotonic stop instant. Stop trimming
     * uses whole canonical frames; a partial physical sample is not emitted.
     *
     * first_frame must be the absolute canonical frame coordinate from the
     * P7A5 program clock (0, 1024, 2048, ...), not an arrival timestamp.
     */
    [[nodiscard]] AacPrepareResult prepare(
        const core::audio::AudioProgramBlock &block,
        std::uint64_t first_frame,
        std::int64_t stop_100ns = -1) noexcept
    {
        AacPrepareResult result;
        if (pending_) {
            result.status = AacPrepareStatus::Busy;
            return result;
        }

        if (!active_ ||
            block.audio.frame_count != kFrameCount ||
            first_frame % kFrameCount != 0 ||
            first_frame >
                std::numeric_limits<std::uint64_t>::max() - kFrameCount ||
            (stop_100ns >= 0 && stop_100ns < media_zero_100ns_)) {
            ++stats_.invalid_blocks;
            return result;
        }

        if (first_frame < next_minimum_frame_) {
            ++stats_.out_of_order_blocks;
            result.status = AacPrepareStatus::OutOfOrder;
            return result;
        }

        const auto start_offset =
            core::audio::frames_to_ticks_floor(first_frame, kSampleRate);
        const auto end_offset =
            core::audio::frames_to_ticks_floor(
                first_frame + kFrameCount, kSampleRate);
        if (start_offset >
                static_cast<std::uint64_t>(
                    std::numeric_limits<std::int64_t>::max()) ||
            end_offset >
                static_cast<std::uint64_t>(
                    std::numeric_limits<std::int64_t>::max()) ||
            end_offset <= start_offset ||
            start_offset >
                static_cast<std::uint64_t>(
                    std::numeric_limits<std::int64_t>::max() -
                    media_zero_100ns_) ||
            block.audio.media_start_100ns !=
                media_zero_100ns_ +
                    static_cast<std::int64_t>(start_offset)) {
            ++stats_.invalid_blocks;
            return result;
        }

        std::uint32_t output_frames = kFrameCount;
        if (stop_100ns >= 0) {
            const auto stop_frames =
                core::audio::ticks_to_frames_floor(
                    static_cast<std::uint64_t>(
                        stop_100ns - media_zero_100ns_), kSampleRate);
            if (stop_frames <= first_frame) {
                result.status = AacPrepareStatus::PastStop;
                return result;
            }
            output_frames = static_cast<std::uint32_t>(
                std::min<std::uint64_t>(
                    kFrameCount, stop_frames - first_frame));
        }

        const auto end_output_offset =
            core::audio::frames_to_ticks_floor(
                first_frame + output_frames, kSampleRate);
        const auto duration = end_output_offset - start_offset;
        if (duration == 0 ||
            duration >
                static_cast<std::uint64_t>(
                    std::numeric_limits<std::int64_t>::max())) {
            ++stats_.invalid_blocks;
            return result;
        }

        std::uint64_t clipped = 0;
        std::uint64_t non_finite = 0;
        const auto count =
            static_cast<std::size_t>(output_frames) *
            core::audio::CanonicalAudioBlock::kChannels;
        for (std::size_t sample = 0; sample < count; ++sample) {
            const float value = block.audio.samples[sample];
            if (!std::isfinite(value)) {
                ++non_finite;
                pcm16_[sample] = 0;
                continue;
            }
            const double bounded =
                std::clamp(static_cast<double>(value), -1.0, 1.0);
            if (bounded != static_cast<double>(value))
                ++clipped;

            // Deliberately asymmetric signed PCM16 full-scale endpoints:
            // +1.0 -> +32767, -1.0 -> -32768. Explicit half-away-from-zero
            // integer rounding is independent of ambient FPU rounding mode.
            const double scaled =
                bounded >= 0.0 ? bounded * 32767.0 : bounded * 32768.0;
            const double rounded =
                scaled >= 0.0
                    ? std::floor(scaled + 0.5)
                    : std::ceil(scaled - 0.5);
            pcm16_[sample] = static_cast<std::int16_t>(rounded);
        }

        pending_ = true;
        pending_frames_ = output_frames;
        pending_next_frame_ = first_frame + kFrameCount;
        pending_clipped_ = clipped;
        pending_non_finite_ = non_finite;
        if (first_frame > next_minimum_frame_)
            stats_.skipped_frames += first_frame - next_minimum_frame_;

        result.status = AacPrepareStatus::Ready;
        result.view = {
            .interleaved = {
                pcm16_.data(), count,
            },
            .relative_pts_100ns = static_cast<std::int64_t>(start_offset),
            .duration_100ns = static_cast<std::int64_t>(duration),
            .frames = output_frames,
            .discontinuity =
                block.follows_output_discontinuity ||
                block.audio.discontinuity_mask != 0 ||
                first_frame > next_minimum_frame_ ||
                next_output_discontinuity_,
        };
        return result;
    }

    /*
     * Call exactly once after the sole writer caller has attempted this
     * pending sample. false covers backpressure/failure: never shift a later
     * sample into the missing interval or retain an unbounded pending block.
     */
    [[nodiscard]] bool finish(bool writer_accepted) noexcept
    {
        if (!pending_)
            return false;
        next_minimum_frame_ = pending_next_frame_;
        stats_.clipped_samples += pending_clipped_;
        stats_.non_finite_samples += pending_non_finite_;
        stats_.trimmed_stop_frames += kFrameCount - pending_frames_;
        if (writer_accepted) {
            next_output_discontinuity_ = false;
            ++stats_.submitted_blocks;
            stats_.submitted_frames += pending_frames_;
        } else {
            next_output_discontinuity_ = true;
            ++stats_.dropped_blocks;
        }
        pending_ = false;
        pending_frames_ = 0;
        return true;
    }

    /*
     * The RecorderSession's SINGLE AV-writer owner calls this for each
     * canonical P7A5 program block. It is the exact production API that
     * replaces error-prone manual prepare/write/finish choreography.
     *
     * If the writer rejects or throws, finish(false) still runs and the next
     * accepted block retains absolute PTS plus a discontinuity flag. Writer
     * failure is returned untouched, so RecorderSession can distinguish
     * backpressure (drop) from fatal Media Foundation failure (stop/fail).
     * Do not retry the same block, synthesize a second clock, or grow a queue.
     */
    template<class Writer>
    [[nodiscard]] AacSubmissionResult submit_to(
        Writer &writer,
        const core::audio::AudioProgramBlock &block,
        std::uint64_t first_frame,
        std::int64_t stop_100ns = -1) noexcept
    {
        const auto prepared = prepare(block, first_frame, stop_100ns);
        AacSubmissionResult result;
        result.prepare_status = prepared.status;
        if (prepared.status != AacPrepareStatus::Ready) {
            if (prepared.status == AacPrepareStatus::Invalid) {
                result.writer_status = core::Status::failure(
                    core::StatusCode::InvalidArgument);
            } else if (prepared.status != AacPrepareStatus::PastStop) {
                result.writer_status = core::Status::failure(
                    core::StatusCode::InvalidStateTransition);
            }
            return result;
        }

        result.writer_attempted = true;
        try {
            result.writer_status = writer.write_audio_pcm16(
                prepared.view.interleaved,
                core::TimePoint{prepared.view.relative_pts_100ns},
                prepared.view.duration_100ns,
                prepared.view.discontinuity);
        } catch (...) {
            result.writer_status = core::Status::failure(
                core::StatusCode::InternalError);
        }
        result.submitted = result.writer_status.ok();
        (void)finish(result.submitted);
        return result;
    }

    [[nodiscard]] bool has_pending() const noexcept { return pending_; }
    [[nodiscard]] std::uint64_t next_minimum_frame() const noexcept
    {
        return next_minimum_frame_;
    }
    [[nodiscard]] const AacPcm16AdapterStats &stats() const noexcept
    {
        return stats_;
    }

private:
    std::array<std::int16_t, kSampleCapacity> pcm16_{};
    AacPcm16AdapterStats stats_{};
    std::int64_t media_zero_100ns_ = 0;
    std::uint64_t next_minimum_frame_ = 0;
    std::uint64_t pending_next_frame_ = 0;
    std::uint64_t pending_clipped_ = 0;
    std::uint64_t pending_non_finite_ = 0;
    std::uint32_t pending_frames_ = 0;
    bool pending_ = false;
    bool active_ = false;
    bool next_output_discontinuity_ = false;
};

} // namespace arssyut::app::audio
