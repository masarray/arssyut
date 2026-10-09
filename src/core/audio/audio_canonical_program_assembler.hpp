#pragma once

// P7A5 assembled canonical stage: no second clock, queue, resampler or mixer.
// Runs only on the RecorderSession audio/program owner, AFTER native packet
// timestamp mapping, format normalization and libswresample have produced
// 48 kHz interleaved float32 stereo with absolute canonical frame coordinates.
//
// Bounded output is a four-slot *handoff* of closed 1024-frame program blocks
// to P7A6's nonblocking service_ready_blocks(). This is not another worker
// or a producer clock. A full handoff drops a whole original interval, with
// the existing mixer marking the next output discontinuity.

#include "core/audio/audio_canonical_staging.hpp"
#include "core/audio/audio_program_mixer.hpp"
#include "core/audio/audio_source_block_window.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace arssyut::core::audio {

enum class CanonicalProgramOfferStatus : std::uint8_t {
    Applied = 0,
    Invalid,
    Overflow,
    NonContiguous,
    Inactive,
};

struct CanonicalProgramAssemblerStats {
    std::uint64_t queued_program_blocks = 0;
    std::uint64_t dequeued_program_blocks = 0;
    std::uint64_t dropped_program_blocks = 0;
    std::uint64_t stale_canonical_frames = 0;
    std::uint64_t duplicate_canonical_frames = 0;
    std::uint64_t staging_overflows = 0;
    std::uint64_t staging_gaps = 0;
};

class CanonicalProgramAssembler final {
public:
    static constexpr std::uint32_t kFramesPerBlock = 1'024;
    static constexpr std::uint32_t kOutputSlots = 4;
    static constexpr std::uint32_t kStageCapacityFrames = 16'384;

    // The MEDIA ZERO is RecorderSession's QPC start, NEVER packet arrival.
    // Do not call reset again while output is open or capture is active.
    void reset(std::int64_t media_zero_100ns,
               bool microphone, bool system_audio) noexcept
    {
        mixer_.reset(media_zero_100ns);
        for (auto &stage : source_staging_)
            stage.reset();
        for (auto &window : source_windows_)
            window.begin(0);
        active_[0] = microphone;
        active_[1] = system_audio;
        read_slot_ = 0;
        pending_count_ = 0;
        stats_ = {};
        initialized_ = media_zero_100ns >= 0 &&
                       (microphone || system_audio);
    }

    [[nodiscard]] CanonicalProgramOfferStatus offer(
        AudioSourceId source,
        std::int64_t absolute_first_frame,
        std::span<const float> canonical_stereo,
        std::uint32_t frames,
        bool discontinuity = false) noexcept
    {
        const auto index = source_index(source);
        if (!initialized_ || index >= active_.size() || !active_[index])
            return CanonicalProgramOfferStatus::Inactive;
        if (absolute_first_frame < 0 || frames == 0 ||
            canonical_stereo.size() !=
                static_cast<std::size_t>(frames) * 2U)
            return CanonicalProgramOfferStatus::Invalid;

        const auto stage_status = source_staging_[index].push(
            absolute_first_frame, canonical_stereo, frames, discontinuity);
        switch (stage_status) {
        case AudioCanonicalStagePushStatus::Applied:
            return CanonicalProgramOfferStatus::Applied;
        case AudioCanonicalStagePushStatus::Full:
            ++stats_.staging_overflows;
            return CanonicalProgramOfferStatus::Overflow;
        case AudioCanonicalStagePushStatus::NonContiguous:
            ++stats_.staging_gaps;
            return CanonicalProgramOfferStatus::NonContiguous;
        case AudioCanonicalStagePushStatus::Invalid:
        default:
            return CanonicalProgramOfferStatus::Invalid;
        }
    }

    // Close ONLY one already-due interval per call. RecorderSession decides
    // the deadline and keeps video due-work ahead of this service point.
    // A late/delayed packet is never allowed to move the canonical clock.
    [[nodiscard]] bool close_one_due(
        std::int64_t absolute_now_100ns) noexcept
    {
        if (!initialized_ ||
            absolute_now_100ns < mixer_.current_end_100ns())
            return false;

        const std::uint64_t block_index =
            mixer_.stats().blocks_closed;
        if (block_index >
            static_cast<std::uint64_t>(
                (std::numeric_limits<std::int64_t>::max)() -
                kFramesPerBlock) / kFramesPerBlock)
            return false;

        const std::int64_t first_frame =
            static_cast<std::int64_t>(
                block_index * kFramesPerBlock);
        const auto block_end = first_frame + kFramesPerBlock;

        auto block = mixer_.begin_block();
        for (std::size_t index = 0; index < active_.size(); ++index) {
            if (!active_[index])
                continue;
            auto &window = source_windows_[index];
            auto &stage = source_staging_[index];
            window.begin(first_frame);

            // The ring has a strictly bounded capacity. The loop always
            // consumes a positive prefix or stops at a future timestamp.
            while (true) {
                const auto chunk = stage.front_chunk();
                if (!chunk.valid() ||
                    chunk.start_frame >= block_end)
                    break;
                const auto offered = window.offer(
                    chunk.start_frame, chunk.interleaved,
                    chunk.frame_count, chunk.discontinuity);
                if (offered.status ==
                        AudioWindowOfferStatus::Invalid ||
                    offered.status == AudioWindowOfferStatus::Future ||
                    offered.input_frames_consumed == 0)
                    break;
                stats_.stale_canonical_frames +=
                    offered.stale_prefix_frames;
                stats_.duplicate_canonical_frames +=
                    offered.duplicate_frames;
                (void)stage.consume(offered.input_frames_consumed);
            }

            const auto source = static_cast<AudioSourceId>(index);
            if (window.covered_frames() == 0) {
                mixer_.note_missing_source_interval(source);
                continue;
            }
            if (!mixer_.mix_source(
                    block, source, window.interleaved())) {
                // Invalid existing mixer result must not pass a half-mixed
                // program block downstream. The fixed queue is unchanged.
                mixer_.close_block(block, false);
                ++stats_.dropped_program_blocks;
                return true;
            }
            if (window.discontinuity())
                block.audio.discontinuity_mask |=
                    source_presence_bit(source);
        }

        if (pending_count_ == kOutputSlots) {
            mixer_.close_block(block, false);
            ++stats_.dropped_program_blocks;
            return true;
        }

        const auto write_slot =
            (read_slot_ + pending_count_) % kOutputSlots;
        output_[write_slot].block = block;
        output_[write_slot].first_frame =
            static_cast<std::uint64_t>(first_frame);
        ++pending_count_;
        ++stats_.queued_program_blocks;
        mixer_.close_block(block, true);
        return true;
    }

    // P7A6 service_ready_blocks() may pass this function as a nonblocking
    // callback on the SAME writer-owner worker. No waiting, locks or heap.
    [[nodiscard]] bool try_take(
        AudioProgramBlock &out,
        std::uint64_t &absolute_first_frame) noexcept
    {
        if (!initialized_ || pending_count_ == 0)
            return false;
        const auto &entry = output_[read_slot_];
        out = entry.block;
        absolute_first_frame = entry.first_frame;
        read_slot_ = (read_slot_ + 1) % kOutputSlots;
        --pending_count_;
        ++stats_.dequeued_program_blocks;
        return true;
    }

    [[nodiscard]] std::uint32_t pending_blocks() const noexcept
    {
        return pending_count_;
    }

    [[nodiscard]] std::int64_t next_due_100ns() const noexcept
    {
        return mixer_.current_end_100ns();
    }

    [[nodiscard]] CanonicalProgramAssemblerStats
    stats() const noexcept
    {
        return stats_;
    }

    [[nodiscard]] AudioProgramMixStats mix_stats() const noexcept
    {
        return mixer_.stats();
    }

    [[nodiscard]] AudioProgramMixer &mixer() noexcept
    {
        return mixer_;
    }

private:
    struct ClosedOutput {
        AudioProgramBlock block{};
        std::uint64_t first_frame = 0;
    };

    std::array<FixedCanonicalAudioStaging<kStageCapacityFrames>, 2>
        source_staging_{};
    std::array<AudioSourceBlockWindow, 2> source_windows_{};
    AudioProgramMixer mixer_{};
    std::array<ClosedOutput, kOutputSlots> output_{};
    CanonicalProgramAssemblerStats stats_{};
    std::array<bool, 2> active_{};
    std::uint32_t read_slot_ = 0;
    std::uint32_t pending_count_ = 0;
    bool initialized_ = false;
};

} // namespace arssyut::core::audio
