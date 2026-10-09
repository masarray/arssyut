#pragma once

#include "core/audio/audio_format.hpp"

#include <cstdint>
#include <limits>

namespace arssyut::app::audio {

/*
 * P7A6 startup-only readiness barrier. It is not a RecorderSession state
 * machine and never owns a media clock, capture device, or writer.
 *
 * RecorderSession's single worker owns this object. The caller obtains
 * readiness facts from the accepted WASAPI start() results and the single
 * Media Foundation writer open() result. No callback or UI thread may
 * mutate this object. A generation ticket rejects delayed startup events
 * from a previous recording.
 */
struct AudioStartRequirements {
    bool microphone = false;
    bool system_audio = false;

    [[nodiscard]] constexpr std::uint8_t source_mask() const noexcept
    {
        return static_cast<std::uint8_t>(
            (microphone ? 0x01U : 0U) |
            (system_audio ? 0x02U : 0U));
    }

    [[nodiscard]] constexpr bool audio_requested() const noexcept
    {
        return source_mask() != 0;
    }
};

enum class AudioStartupState : std::uint8_t {
    Bypassed = 0,  // Audio OFF: preserve existing video-only start.
    Pending,
    Ready,
    Failed,
    Cancelled,
};

struct AudioStartupSnapshot {
    AudioStartupState state = AudioStartupState::Bypassed;
    std::uint64_t generation = 0;
    std::uint8_t requested_sources = 0;
    std::uint8_t ready_sources = 0;
    std::uint8_t failed_sources = 0;
    bool writer_ready = false;
    bool writer_failed = false;
};

class AudioStartupGate final {
public:
    /*
     * Begin the next recording generation. A zero ticket is invalid and
     * fails closed if generation space is ever exhausted.
     */
    [[nodiscard]] std::uint64_t begin(
        AudioStartRequirements requested) noexcept
    {
        if (generation_ ==
            std::numeric_limits<std::uint64_t>::max()) {
            generation_exhausted_ = true;
            return 0;
        }

        ++generation_;
        requested_sources_ = requested.source_mask();
        ready_sources_ = 0;
        failed_sources_ = 0;
        writer_ready_ = false;
        writer_failed_ = false;
        cancelled_ = false;
        return generation_;
    }

    [[nodiscard]] bool source_ready(
        std::uint64_t ticket,
        core::audio::AudioSourceId source) noexcept
    {
        const auto bit = source_bit(source);
        if (!event_allowed(ticket) ||
            (requested_sources_ & bit) == 0)
            return false;

        ready_sources_ = static_cast<std::uint8_t>(
            ready_sources_ | bit);
        return true;
    }

    [[nodiscard]] bool source_failed(
        std::uint64_t ticket,
        core::audio::AudioSourceId source) noexcept
    {
        const auto bit = source_bit(source);
        if (!event_allowed(ticket) ||
            (requested_sources_ & bit) == 0)
            return false;

        failed_sources_ = static_cast<std::uint8_t>(
            failed_sources_ | bit);
        return true;
    }

    [[nodiscard]] bool writer_ready(
        std::uint64_t ticket) noexcept
    {
        if (!event_allowed(ticket))
            return false;
        writer_ready_ = true;
        return true;
    }

    [[nodiscard]] bool writer_failed(
        std::uint64_t ticket) noexcept
    {
        if (!event_allowed(ticket))
            return false;
        writer_failed_ = true;
        return true;
    }

    [[nodiscard]] bool cancel(
        std::uint64_t ticket) noexcept
    {
        if (!event_allowed(ticket))
            return false;
        cancelled_ = true;
        return true;
    }

    [[nodiscard]] AudioStartupState state() const noexcept
    {
        if (generation_exhausted_)
            return AudioStartupState::Failed;
        if (requested_sources_ == 0)
            return AudioStartupState::Bypassed;
        if (cancelled_)
            return AudioStartupState::Cancelled;
        if (writer_failed_ || failed_sources_ != 0)
            return AudioStartupState::Failed;
        if (writer_ready_ &&
            ready_sources_ == requested_sources_)
            return AudioStartupState::Ready;
        return AudioStartupState::Pending;
    }

    [[nodiscard]] AudioStartupSnapshot snapshot() const noexcept
    {
        return {
            .state = state(),
            .generation = generation_,
            .requested_sources = requested_sources_,
            .ready_sources = ready_sources_,
            .failed_sources = failed_sources_,
            .writer_ready = writer_ready_,
            .writer_failed = writer_failed_,
        };
    }

private:
    [[nodiscard]] static constexpr std::uint8_t source_bit(
        core::audio::AudioSourceId source) noexcept
    {
        switch (source) {
        case core::audio::AudioSourceId::Microphone:
            return 0x01U;
        case core::audio::AudioSourceId::SystemAudio:
            return 0x02U;
        default:
            return 0U;
        }
    }

    [[nodiscard]] bool event_allowed(
        std::uint64_t ticket) const noexcept
    {
        return ticket != 0 &&
               ticket == generation_ &&
               requested_sources_ != 0 &&
               !generation_exhausted_ &&
               !cancelled_ &&
               !writer_failed_ &&
               failed_sources_ == 0;
    }

    std::uint64_t generation_ = 0;
    std::uint8_t requested_sources_ = 0;
    std::uint8_t ready_sources_ = 0;
    std::uint8_t failed_sources_ = 0;
    bool writer_ready_ = false;
    bool writer_failed_ = false;
    bool cancelled_ = false;
    bool generation_exhausted_ = false;
};

} // namespace arssyut::app::audio
