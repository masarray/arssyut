#pragma once

#include "app/audio_endpoint_preflight.hpp"
#include "app/audio_startup_gate.hpp"
#include "core/result/status.hpp"

#include <cstdint>
#include <string>

namespace arssyut::app::audio {

// RecorderSession's *existing* worker calls this ONCE during Preparing,
// after resolve_audio_endpoints(). This utility does not own threads, a
// source queue, a writer, a media clock or a second recording state machine.
//
// Both sources are the accepted WASAPI wrappers (or test fakes) exposing
// start(std::wstring) -> Status and stop() noexcept. Their lifetime remains
// owned by RecorderSession. The caller must stop any successfully started
// sources on the ordinary Stop path after this helper returns success.
//
// Writer open() is deliberately outside this function: the same RecorderSession
// worker marks gate.writer_ready(ticket) only after the single MF writer opens.
// A Ready gate may then authorize Armed, never merely two successful sources.
//
// Cancellation is observed before the first start, between source starts and
// immediately after each start. Source start() may block briefly while the
// endpoint initializes; cancellation is cooperative and always rolls back any
// sources already started. No other thread may mutate AudioStartupGate.
template<class MicrophoneSource, class SystemSource, class CancelRequested>
[[nodiscard]] core::Status start_pinned_audio_sources(
    const PinnedAudioEndpoints &endpoints,
    AudioStartupGate &gate,
    std::uint64_t ticket,
    MicrophoneSource &microphone,
    SystemSource &system_audio,
    CancelRequested &&cancel_requested)
{
    using core::Status;
    using core::StatusCode;
    using core::audio::AudioSourceId;

    const auto snapshot = gate.snapshot();
    const AudioStartRequirements requested{
        .microphone = endpoints.microphone,
        .system_audio = endpoints.system_audio,
    };

    // Prevent stale generations, mismatched role masks, accidental retry
    // against an already-started source, and a failed/cancelled writer.
    if (ticket == 0 ||
        snapshot.generation != ticket ||
        snapshot.requested_sources != requested.source_mask() ||
        snapshot.ready_sources != 0 ||
        gate.state() != (requested.audio_requested()
            ? AudioStartupState::Pending
            : AudioStartupState::Bypassed)) {
        return Status::failure(StatusCode::InvalidStateTransition);
    }

    if ((endpoints.microphone && endpoints.microphone_id.empty()) ||
        (endpoints.system_audio && endpoints.system_audio_id.empty()) ||
        (!endpoints.microphone && !endpoints.microphone_id.empty()) ||
        (!endpoints.system_audio && !endpoints.system_audio_id.empty())) {
        return Status::failure(StatusCode::InvalidArgument);
    }

    // Preserve the existing video-only path without probing any device or
    // querying any cancellation callback from this audio-only helper.
    if (!requested.audio_requested())
        return Status::success();

    bool microphone_attempted = false;
    bool system_attempted = false;

    const auto rollback = [&]() noexcept {
        // Reverse start order. stop() is safe after a failed/incomplete start,
        // as specified by the existing WASAPI capture source lifecycle.
        if (system_attempted)
            system_audio.stop();
        if (microphone_attempted)
            microphone.stop();
    };

    const auto cancelled = [&]() -> bool {
        if (!cancel_requested())
            return false;
        (void)gate.cancel(ticket);
        rollback();
        return true;
    };

    AudioSourceId active_source = AudioSourceId::Microphone;
    try {
        if (cancelled())
            return Status::failure(StatusCode::InvalidStateTransition);

        if (endpoints.microphone) {
            active_source = AudioSourceId::Microphone;
            microphone_attempted = true;
            const auto status = microphone.start(endpoints.microphone_id);
            if (!status.ok()) {
                (void)gate.source_failed(ticket, active_source);
                rollback();
                return status;
            }
            if (cancelled())
                return Status::failure(StatusCode::InvalidStateTransition);
            if (!gate.source_ready(ticket, active_source)) {
                rollback();
                return Status::failure(StatusCode::InvalidStateTransition);
            }
        }

        if (cancelled())
            return Status::failure(StatusCode::InvalidStateTransition);

        if (endpoints.system_audio) {
            active_source = AudioSourceId::SystemAudio;
            system_attempted = true;
            const auto status = system_audio.start(endpoints.system_audio_id);
            if (!status.ok()) {
                (void)gate.source_failed(ticket, active_source);
                rollback();
                return status;
            }
            if (cancelled())
                return Status::failure(StatusCode::InvalidStateTransition);
            if (!gate.source_ready(ticket, active_source)) {
                rollback();
                return Status::failure(StatusCode::InvalidStateTransition);
            }
        }
    } catch (...) {
        (void)gate.source_failed(ticket, active_source);
        rollback();
        return Status::failure(StatusCode::InternalError);
    }

    return Status::success();
}

} // namespace arssyut::app::audio
