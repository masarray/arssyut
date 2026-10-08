#pragma once

#include "core/audio/audio_format.hpp"
#include "core/audio/audio_time.hpp"

#include <cstdint>
#include <limits>

namespace arssyut::core::audio {

/*
 * Canonical P7A5 program timeline.
 *
 * Source arrival never advances this clock. Block N is derived only from
 * RecorderSession media zero plus exact rational 48 kHz frame time.
 */
class AudioProgramClock final {
public:
    static constexpr std::uint32_t kSampleRate = 48'000;
    static constexpr std::uint32_t kBlockFrames = 1'024;

    void reset(std::int64_t media_zero_100ns = 0) noexcept
    {
        media_zero_100ns_ = media_zero_100ns;
        next_block_index_ = 0;
    }

    [[nodiscard]] std::uint64_t next_block_index() const noexcept
    {
        return next_block_index_;
    }

    [[nodiscard]] std::int64_t block_start_100ns(
        std::uint64_t block_index) const noexcept
    {
        const auto frame_index =
            saturating_multiply(
                block_index,
                static_cast<std::uint64_t>(
                    kBlockFrames));

        const auto offset =
            frames_to_ticks_floor(
                frame_index,
                kSampleRate);

        if (offset >
            static_cast<std::uint64_t>(
                std::numeric_limits<
                    std::int64_t>::max()))
            return std::numeric_limits<
                std::int64_t>::max();

        const auto signed_offset =
            static_cast<std::int64_t>(
                offset);

        if (media_zero_100ns_ >
            std::numeric_limits<std::int64_t>::max() -
                signed_offset)
            return std::numeric_limits<
                std::int64_t>::max();

        return media_zero_100ns_ +
               signed_offset;
    }

    [[nodiscard]] std::int64_t next_start_100ns() const noexcept
    {
        return block_start_100ns(
            next_block_index_);
    }

    [[nodiscard]] std::int64_t next_end_100ns() const noexcept
    {
        return block_start_100ns(
            next_block_index_ + 1);
    }

    void advance() noexcept
    {
        if (next_block_index_ !=
            std::numeric_limits<
                std::uint64_t>::max())
            ++next_block_index_;
    }

private:
    [[nodiscard]] static constexpr std::uint64_t
    saturating_multiply(
        std::uint64_t left,
        std::uint64_t right) noexcept
    {
        if (right != 0 &&
            left >
                std::numeric_limits<
                    std::uint64_t>::max() /
                    right)
            return std::numeric_limits<
                std::uint64_t>::max();

        return left * right;
    }

    std::int64_t media_zero_100ns_ = 0;
    std::uint64_t next_block_index_ = 0;
};

} // namespace arssyut::core::audio
