#pragma once

#include "core/audio/audio_packet.hpp"

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace arssyut::core::audio {

enum class AudioCanonicalStagePushStatus : std::uint8_t {
    Applied = 0,
    Invalid,
    NonContiguous,
    Full,
};

struct AudioCanonicalStageChunk {
    std::int64_t start_frame = 0;
    std::span<const float> interleaved{};
    std::uint32_t frame_count = 0;
    bool discontinuity = false;

    [[nodiscard]] bool valid() const noexcept
    {
        return frame_count != 0 &&
               interleaved.size() ==
                   static_cast<std::size_t>(
                       frame_count) *
                       CanonicalAudioBlock::kChannels;
    }
};

/*
 * Fixed-capacity canonical 48 kHz stereo staging for one source.
 *
 * This is not a clock and not a jitter buffer. It retains already-produced SRC
 * output that belongs to future program blocks so callers never discard or
 * time-shift a suffix merely because the current 1024-frame window ended.
 *
 * Appends must be exactly contiguous in canonical frame coordinates. A gap or
 * overlap fails closed and must be handled by the caller as discontinuity /
 * missing-media policy. Capacity never grows.
 *
 * The ring publishes front chunks split at wrap boundaries and discontinuity
 * markers. This lets the mixer-thread offer each chunk to
 * AudioSourceBlockWindow without losing a discontinuity that begins in a
 * future suffix.
 */
template <std::size_t CapacityFrames>
class FixedCanonicalAudioStaging final {
public:
    static_assert(
        CapacityFrames >=
            CanonicalAudioBlock::kFrameCapacity,
        "Canonical staging must hold at least one program block");

    static constexpr std::uint16_t kChannels =
        CanonicalAudioBlock::kChannels;
    static constexpr std::size_t kSampleCapacity =
        CapacityFrames * kChannels;

    void reset() noexcept
    {
        read_frame_ = 0;
        size_frames_ = 0;
        start_frame_ = 0;
        have_start_ = false;
        discontinuities_.reset();
        high_water_frames_ = 0;
        overflow_events_ = 0;
        non_contiguous_events_ = 0;
    }

    [[nodiscard]] AudioCanonicalStagePushStatus push(
        std::int64_t start_frame,
        std::span<const float> canonical_stereo,
        std::uint32_t frame_count,
        bool discontinuity = false) noexcept
    {
        if (frame_count == 0 ||
            frame_count > CapacityFrames ||
            canonical_stereo.size() !=
                static_cast<std::size_t>(
                    frame_count) *
                    kChannels)
            return AudioCanonicalStagePushStatus::Invalid;

        const auto free_frames =
            CapacityFrames - size_frames_;

        if (frame_count > free_frames) {
            ++overflow_events_;
            return AudioCanonicalStagePushStatus::Full;
        }

        if (have_start_) {
            const auto expected =
                saturating_add(
                    start_frame_,
                    size_frames_);

            if (start_frame != expected) {
                ++non_contiguous_events_;
                return AudioCanonicalStagePushStatus::NonContiguous;
            }
        }
        else {
            start_frame_ = start_frame;
            have_start_ = true;
        }

        const std::size_t write_frame =
            (read_frame_ + size_frames_) %
            CapacityFrames;

        for (std::uint32_t frame = 0;
             frame < frame_count;
             ++frame) {
            const std::size_t physical_frame =
                (write_frame + frame) %
                CapacityFrames;

            const std::size_t source_index =
                static_cast<std::size_t>(frame) *
                kChannels;
            const std::size_t destination_index =
                physical_frame *
                kChannels;

            samples_[destination_index] =
                canonical_stereo[source_index];
            samples_[destination_index + 1] =
                canonical_stereo[
                    source_index + 1];

            discontinuities_.reset(
                physical_frame);
        }

        if (discontinuity)
            discontinuities_.set(
                write_frame);

        size_frames_ += frame_count;
        if (size_frames_ > high_water_frames_)
            high_water_frames_ =
                size_frames_;

        return AudioCanonicalStagePushStatus::Applied;
    }

    [[nodiscard]] AudioCanonicalStageChunk front_chunk() const noexcept
    {
        AudioCanonicalStageChunk chunk;

        if (!have_start_ ||
            size_frames_ == 0)
            return chunk;

        std::size_t contiguous =
            std::min(
                size_frames_,
                CapacityFrames - read_frame_);

        /*
         * Keep a future discontinuity at a chunk boundary. If the front frame
         * itself is discontinuous, the returned chunk carries that flag and
         * extends until the next marker or physical wrap.
         */
        for (std::size_t offset = 1;
             offset < contiguous;
             ++offset) {
            if (discontinuities_.test(
                    read_frame_ + offset)) {
                contiguous = offset;
                break;
            }
        }

        chunk.start_frame =
            start_frame_;
        chunk.interleaved = {
            samples_.data() +
                read_frame_ * kChannels,
            contiguous * kChannels};
        chunk.frame_count =
            static_cast<std::uint32_t>(
                contiguous);
        chunk.discontinuity =
            discontinuities_.test(
                read_frame_);
        return chunk;
    }

    [[nodiscard]] bool consume(
        std::uint32_t frame_count) noexcept
    {
        if (frame_count == 0 ||
            frame_count > size_frames_)
            return false;

        for (std::uint32_t frame = 0;
             frame < frame_count;
             ++frame) {
            discontinuities_.reset(
                (read_frame_ + frame) %
                CapacityFrames);
        }

        read_frame_ =
            (read_frame_ + frame_count) %
            CapacityFrames;
        size_frames_ -= frame_count;

        start_frame_ =
            saturating_add(
                start_frame_,
                frame_count);

        if (size_frames_ == 0) {
            have_start_ = false;
            read_frame_ = 0;
        }

        return true;
    }

    [[nodiscard]] std::size_t size_frames() const noexcept
    {
        return size_frames_;
    }

    [[nodiscard]] constexpr std::size_t capacity_frames() const noexcept
    {
        return CapacityFrames;
    }

    [[nodiscard]] std::size_t free_frames() const noexcept
    {
        return CapacityFrames -
               size_frames_;
    }

    [[nodiscard]] std::size_t high_water_frames() const noexcept
    {
        return high_water_frames_;
    }

    [[nodiscard]] std::uint64_t overflow_events() const noexcept
    {
        return overflow_events_;
    }

    [[nodiscard]] std::uint64_t non_contiguous_events() const noexcept
    {
        return non_contiguous_events_;
    }

private:
    [[nodiscard]] static constexpr std::int64_t
    saturating_add(
        std::int64_t start,
        std::size_t frames) noexcept
    {
        if (frames >
            static_cast<std::size_t>(
                std::numeric_limits<
                    std::int64_t>::max()))
            return std::numeric_limits<
                std::int64_t>::max();

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

    std::array<float, kSampleCapacity>
        samples_{};
    std::bitset<CapacityFrames>
        discontinuities_{};

    std::size_t read_frame_ = 0;
    std::size_t size_frames_ = 0;
    std::int64_t start_frame_ = 0;
    bool have_start_ = false;

    std::size_t high_water_frames_ = 0;
    std::uint64_t overflow_events_ = 0;
    std::uint64_t non_contiguous_events_ = 0;
};

} // namespace arssyut::core::audio
