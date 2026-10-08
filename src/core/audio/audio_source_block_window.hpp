#pragma once

#include "core/audio/audio_packet.hpp"

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace arssyut::core::audio {

enum class AudioWindowOfferStatus : std::uint8_t {
    Applied = 0,
    FullyStale,
    Future,
    Invalid,
};

struct AudioWindowOfferResult {
    AudioWindowOfferStatus status =
        AudioWindowOfferStatus::Invalid;
    std::uint32_t input_frames_consumed = 0;
    std::uint32_t stale_prefix_frames = 0;
    std::uint32_t frames_applied = 0;
    std::uint32_t duplicate_frames = 0;
    std::uint32_t future_suffix_frames = 0;

    [[nodiscard]] bool accepted() const noexcept
    {
        return status == AudioWindowOfferStatus::Applied ||
               status == AudioWindowOfferStatus::FullyStale;
    }
};

/*
 * One source's exact contribution to one canonical 1024-frame program block.
 *
 * The window is mixer-thread owned and allocation-free. It does not queue
 * packets and does not create another clock. All coordinates are frame indexes
 * on the same canonical 48 kHz RecorderSession program timeline.
 *
 * Callers may offer arbitrary canonical packet/resampler slices. Prefix media
 * that belongs to already-closed blocks is consumed as stale, current overlap
 * is copied to the exact destination frame, and a future suffix is left
 * unconsumed for the caller's existing bounded packet/resampler storage.
 *
 * Missing frames are never shifted: untouched frames remain exact silence.
 */
class AudioSourceBlockWindow final {
public:
    static constexpr std::uint32_t kFrameCapacity =
        CanonicalAudioBlock::kFrameCapacity;
    static constexpr std::uint16_t kChannels =
        CanonicalAudioBlock::kChannels;
    static constexpr std::size_t kSampleCapacity =
        CanonicalAudioBlock::kSampleCapacity;

    void begin(
        std::int64_t block_start_frame) noexcept
    {
        block_start_frame_ =
            block_start_frame;
        samples_.fill(0.0F);
        covered_.reset();
        discontinuity_ = false;
        stale_frames_ = 0;
        duplicate_frames_ = 0;
        active_ = true;
    }

    [[nodiscard]] AudioWindowOfferResult offer(
        std::int64_t source_start_frame,
        std::span<const float> canonical_stereo,
        std::uint32_t frame_count,
        bool discontinuity = false) noexcept
    {
        AudioWindowOfferResult result;

        if (!active_ ||
            frame_count == 0 ||
            canonical_stereo.size() !=
                static_cast<std::size_t>(
                    frame_count) *
                    kChannels) {
            return result;
        }

        const std::int64_t source_end_frame =
            saturating_add_frames(
                source_start_frame,
                frame_count);
        const std::int64_t block_end_frame =
            saturating_add_frames(
                block_start_frame_,
                kFrameCapacity);

        if (source_end_frame <=
            block_start_frame_) {
            result.status =
                AudioWindowOfferStatus::FullyStale;
            result.input_frames_consumed =
                frame_count;
            result.stale_prefix_frames =
                frame_count;
            stale_frames_ +=
                frame_count;
            return result;
        }

        if (source_start_frame >=
            block_end_frame) {
            result.status =
                AudioWindowOfferStatus::Future;
            result.future_suffix_frames =
                frame_count;
            return result;
        }

        const std::int64_t overlap_start =
            source_start_frame >
                    block_start_frame_
                ? source_start_frame
                : block_start_frame_;
        const std::int64_t overlap_end =
            source_end_frame <
                    block_end_frame
                ? source_end_frame
                : block_end_frame;

        if (source_start_frame <
            block_start_frame_) {
            const auto stale =
                block_start_frame_ -
                source_start_frame;
            result.stale_prefix_frames =
                static_cast<std::uint32_t>(
                    stale >
                            static_cast<std::int64_t>(
                                frame_count)
                        ? frame_count
                        : stale);
            stale_frames_ +=
                result.stale_prefix_frames;
        }

        const auto source_offset =
            static_cast<std::uint32_t>(
                overlap_start -
                source_start_frame);
        const auto destination_offset =
            static_cast<std::uint32_t>(
                overlap_start -
                block_start_frame_);
        const auto overlap_frames =
            static_cast<std::uint32_t>(
                overlap_end -
                overlap_start);

        for (std::uint32_t frame = 0;
             frame < overlap_frames;
             ++frame) {
            const std::size_t destination_frame =
                static_cast<std::size_t>(
                    destination_offset +
                    frame);

            if (covered_.test(
                    destination_frame)) {
                ++result.duplicate_frames;
                ++duplicate_frames_;
                continue;
            }

            const std::size_t source_index =
                static_cast<std::size_t>(
                    source_offset +
                    frame) *
                kChannels;
            const std::size_t destination_index =
                destination_frame *
                kChannels;

            samples_[destination_index] =
                canonical_stereo[source_index];
            samples_[destination_index + 1] =
                canonical_stereo[
                    source_index + 1];

            covered_.set(
                destination_frame);
            ++result.frames_applied;
        }

        if (discontinuity &&
            overlap_frames != 0)
            discontinuity_ = true;

        const std::int64_t consumed_end =
            source_end_frame <
                    block_end_frame
                ? source_end_frame
                : block_end_frame;

        if (consumed_end >
            source_start_frame) {
            const auto consumed =
                consumed_end -
                source_start_frame;
            result.input_frames_consumed =
                static_cast<std::uint32_t>(
                    consumed >
                            static_cast<std::int64_t>(
                                frame_count)
                        ? frame_count
                        : consumed);
        }

        result.future_suffix_frames =
            frame_count -
            result.input_frames_consumed;
        result.status =
            AudioWindowOfferStatus::Applied;
        return result;
    }

    [[nodiscard]] std::span<const float>
    interleaved() const noexcept
    {
        return {
            samples_.data(),
            samples_.size(),
        };
    }

    [[nodiscard]] std::uint32_t
    covered_frames() const noexcept
    {
        return static_cast<std::uint32_t>(
            covered_.count());
    }

    [[nodiscard]] std::uint32_t
    missing_frames() const noexcept
    {
        return kFrameCapacity -
               covered_frames();
    }

    [[nodiscard]] std::uint64_t
    stale_frames() const noexcept
    {
        return stale_frames_;
    }

    [[nodiscard]] std::uint64_t
    duplicate_frames() const noexcept
    {
        return duplicate_frames_;
    }

    [[nodiscard]] bool
    discontinuity() const noexcept
    {
        return discontinuity_;
    }

    [[nodiscard]] std::int64_t
    block_start_frame() const noexcept
    {
        return block_start_frame_;
    }

private:
    [[nodiscard]] static constexpr std::int64_t
    saturating_add_frames(
        std::int64_t start,
        std::uint32_t frames) noexcept
    {
        const auto delta =
            static_cast<std::int64_t>(
                frames);

        if (start >
            std::numeric_limits<
                std::int64_t>::max() -
                delta)
            return std::numeric_limits<
                std::int64_t>::max();

        return start + delta;
    }

    std::int64_t block_start_frame_ = 0;
    std::array<float, kSampleCapacity>
        samples_{};
    std::bitset<kFrameCapacity>
        covered_{};
    std::uint64_t stale_frames_ = 0;
    std::uint64_t duplicate_frames_ = 0;
    bool discontinuity_ = false;
    bool active_ = false;
};

} // namespace arssyut::core::audio
