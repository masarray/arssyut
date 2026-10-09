#pragma once

// P7A6 product ownership boundary, available only in the *explicitly*
// FFmpeg-enabled Windows build. RecorderSession's existing worker owns every
// operation. WASAPI capture threads ONLY publish native packet leases; only
// this worker invokes ingress, mixer and the sole Media Foundation AV writer.
#if defined(_WIN32) && defined(ARSSYUT_ENABLE_PRODUCT_AUDIO)

#include "app/audio_endpoint_preflight.hpp"
#include "app/audio_source_preparation.hpp"
#include "app/audio_pcm16_submission_adapter.hpp"
#include "core/audio/audio_packet_ingress.hpp"
#include "platform/windows/audio/libswresample_audio_resampler.hpp"
#include "platform/windows/audio/wasapi_microphone_source.hpp"
#include "platform/windows/audio/wasapi_loopback_source.hpp"
#include "platform/windows/media/mf_h264_mp4_writer.hpp"

#include <cstdint>

namespace arssyut::app::audio {

class ProductAudioRuntime final {
public:
    ProductAudioRuntime() = default;
    ProductAudioRuntime(const ProductAudioRuntime &) = delete;
    ProductAudioRuntime &operator=(const ProductAudioRuntime &) = delete;
    ~ProductAudioRuntime() { stop_sources(); }

    [[nodiscard]] core::Status prepare(const AudioEndpointRequest &request)
    {
        if (!request.requested())
            return core::Status::failure(core::StatusCode::InvalidArgument);

        WindowsAudioEndpointLookup lookup;
        auto status = resolve_audio_endpoints(request, lookup, pinned_);
        if (!status.ok())
            return status;

        ticket_ = gate_.begin({
            .microphone = pinned_.microphone,
            .system_audio = pinned_.system_audio
        });
        if (ticket_ == 0)
            return core::Status::failure(
                core::StatusCode::InvalidStateTransition);

        status = start_pinned_audio_sources(
            pinned_, gate_, ticket_, microphone_, loopback_,
            [] { return false; });
        if (!status.ok())
            return status;

        sources_started_ = true;
        return core::Status::success();
    }

    [[nodiscard]] core::Status writer_opened() noexcept
    {
        if (!sources_started_ || !gate_.writer_ready(ticket_) ||
            gate_.state() != AudioStartupState::Ready)
            return core::Status::failure(
                core::StatusCode::InvalidStateTransition);
        return core::Status::success();
    }

    // Armed capture pre-roll must not exhaust the fixed native packet pools.
    // No pre-roll audio is allowed to define media zero or AAC PTS.
    void discard_preroll() noexcept
    {
        if (!sources_started_ || recording_)
            return;
        auto discard = [](auto &source) noexcept {
            for (int i = 0; i < 4; ++i) {
                windows::WasapiPacketLease lease;
                if (!source.try_pop(lease))
                    return;
                // RAII returns this lease to the source's fixed buffer pool.
            }
        };
        if (pinned_.microphone)
            discard(microphone_);
        if (pinned_.system_audio)
            discard(loopback_);
    }

    [[nodiscard]] core::Status begin(std::int64_t media_zero_100ns) noexcept
    {
        if (!sources_started_ ||
            gate_.state() != AudioStartupState::Ready ||
            media_zero_100ns <= 0)
            return core::Status::failure(
                core::StatusCode::InvalidStateTransition);

        // Every recording uses exactly the RecorderSession QPC media zero.
        program_.reset(media_zero_100ns,
                       pinned_.microphone, pinned_.system_audio);
        adapter_.reset(media_zero_100ns);
        if (pinned_.microphone &&
            !mic_ingress_.configure(
                core::audio::AudioSourceId::Microphone,
                microphone_.snapshot().native_format,
                media_zero_100ns, mic_src_))
            return core::Status::failure(core::StatusCode::Unsupported);
        if (pinned_.system_audio &&
            !sys_ingress_.configure(
                core::audio::AudioSourceId::SystemAudio,
                loopback_.snapshot().native_format,
                media_zero_100ns, sys_src_))
            return core::Status::failure(core::StatusCode::Unsupported);

        recording_ = true;
        return core::Status::success();
    }

    // Invoke after each video process_due/write_frame, never on a capture
    // callback. Hard upper bound: 2 lease pops/source, 1 newly due program
    // interval and 2 AAC blocks per pass, with no blocking audio waits.
    [[nodiscard]] core::Status service(
        core::TimePoint now, windows::MfH264Mp4Writer &writer) noexcept
    {
        if (!recording_)
            return core::Status::failure(
                core::StatusCode::InvalidStateTransition);

        if (pinned_.microphone) {
            const auto r = mic_ingress_.pump_available(
                microphone_, program_, 2);
            if (!acceptable(r.status))
                return ingress_failure(r.status);
            mic_ingested_ = mic_ingested_ || r.packets_popped != 0;
            const auto state = microphone_.snapshot();
            if (state.state == windows::WasapiCaptureState::Failed ||
                state.state == windows::WasapiCaptureState::DeviceInvalidated)
                return core::Status::failure(
                    core::StatusCode::PlatformFailure, state.last_hresult);
        }
        if (pinned_.system_audio) {
            const auto r = sys_ingress_.pump_available(
                loopback_, program_, 2);
            if (!acceptable(r.status))
                return ingress_failure(r.status);
            sys_ingested_ = sys_ingested_ || r.packets_popped != 0;
            const auto state = loopback_.snapshot();
            if (state.state == windows::WasapiCaptureState::Failed ||
                state.state == windows::WasapiCaptureState::DeviceInvalidated)
                return core::Status::failure(
                    core::StatusCode::PlatformFailure, state.last_hresult);
        }

        (void)program_.close_one_due(now.ticks_100ns);
        return submit(writer, -1);
    }

    // Finish while MF writer is OPEN; no source or compressor runs beyond
    // finalize. A strict cap prevents a failed device or stalled writer from
    // causing an unbounded Stop. The final AAC block is whole-frame trimmed
    // to the same physical Stop instant as the video timeline.
    [[nodiscard]] core::Status finish(
        core::TimePoint stopped, windows::MfH264Mp4Writer &writer) noexcept
    {
        if (!recording_) {
            stop_sources();
            return core::Status::success();
        }

        // Captured leases were published before Stop, so consume the bounded
        // pending handoff before retiring the capture pools.
        for (int i = 0; i < 32; ++i) {
            bool got_any = false;
            if (pinned_.microphone) {
                const auto r = mic_ingress_.pump_available(
                    microphone_, program_, 2);
                if (!acceptable(r.status)) {
                    stop_sources();
                    return ingress_failure(r.status);
                }
                got_any = got_any || r.packets_popped != 0;
                mic_ingested_ = mic_ingested_ || r.packets_popped != 0;
            }
            if (pinned_.system_audio) {
                const auto r = sys_ingress_.pump_available(
                    loopback_, program_, 2);
                if (!acceptable(r.status)) {
                    stop_sources();
                    return ingress_failure(r.status);
                }
                got_any = got_any || r.packets_popped != 0;
                sys_ingested_ = sys_ingested_ || r.packets_popped != 0;
            }
            if (!got_any)
                break;
        }
        stop_sources();

        // Flush the already selected and linked libswresample instances;
        // this does not invent a new SRC or a second timestamp origin.
        for (int i = 0; i < 8; ++i) {
            bool emitted = false;
            if (pinned_.microphone && mic_ingested_) {
                const auto r = mic_ingress_.drain_one(program_);
                if (!acceptable(r.status))
                    return ingress_failure(r.status);
                emitted = emitted || r.canonical_frames_emitted != 0;
            }
            if (pinned_.system_audio && sys_ingested_) {
                const auto r = sys_ingress_.drain_one(program_);
                if (!acceptable(r.status))
                    return ingress_failure(r.status);
                emitted = emitted || r.canonical_frames_emitted != 0;
            }
            if (!emitted)
                break;
        }

        // Close only the intervals containing the physical recording span,
        // then let the existing AAC adapter trim the last block at Stop.
        for (int i = 0; i < 512; ++i) {
            if (program_.next_due_100ns() > stopped.ticks_100ns)
                break;
            if (!program_.close_one_due(program_.next_due_100ns()))
                return core::Status::failure(
                    core::StatusCode::InvalidStateTransition);
            const auto status = submit(writer, stopped.ticks_100ns);
            if (!status.ok())
                return status;
        }
        // The final partial 1024-frame block is explicitly trimmed by
        // submit_to(stop), never dropped merely because its end is after Stop.
        if (program_.next_due_100ns() > stopped.ticks_100ns &&
            program_.pending_blocks() == 0) {
            (void)program_.close_one_due(program_.next_due_100ns());
        }
        for (int i = 0; i < 8 && program_.pending_blocks() != 0; ++i) {
            const auto status = submit(writer, stopped.ticks_100ns);
            if (!status.ok())
                return status;
        }
        recording_ = false;
        return program_.pending_blocks() == 0
            ? core::Status::success()
            : core::Status::failure(core::StatusCode::InvalidStateTransition);
    }

    void stop_sources() noexcept
    {
        if (!sources_started_)
            return;
        // Reverse source-start order. The capture pool can now retire;
        // no new leases are accessed after this call.
        if (pinned_.system_audio)
            loopback_.stop();
        if (pinned_.microphone)
            microphone_.stop();
        sources_started_ = false;
    }

private:
    [[nodiscard]] static bool acceptable(
        core::audio::PacketIngressStatus status) noexcept
    {
        using core::audio::PacketIngressStatus;
        return status == PacketIngressStatus::Applied ||
               status == PacketIngressStatus::NoOutput ||
               status == PacketIngressStatus::BeforeMediaZero;
    }

    [[nodiscard]] static core::Status ingress_failure(
        core::audio::PacketIngressStatus status) noexcept
    {
        return core::Status::failure(
            core::StatusCode::PlatformFailure,
            static_cast<std::uint32_t>(status));
    }

    [[nodiscard]] core::Status submit(
        windows::MfH264Mp4Writer &writer,
        std::int64_t stop_100ns) noexcept
    {
        const auto r = adapter_.service_ready_blocks(
            writer,
            [this](core::audio::AudioProgramBlock &out,
                   std::uint64_t &first_frame) noexcept {
                return program_.try_take(out, first_frame);
            },
            scratch_, 2, stop_100ns);
        if (r.status.code == core::StatusCode::EncoderBackpressure)
            return core::Status::success();
        return r.status;
    }

    PinnedAudioEndpoints pinned_{};
    AudioStartupGate gate_{};
    std::uint64_t ticket_ = 0;
    windows::WasapiMicrophoneSource microphone_{};
    windows::WasapiLoopbackSource loopback_{};
    platform::windows::audio::LibSwResampleAudioResampler mic_src_{};
    platform::windows::audio::LibSwResampleAudioResampler sys_src_{};
    core::audio::CanonicalPacketIngress mic_ingress_{};
    core::audio::CanonicalPacketIngress sys_ingress_{};
    core::audio::CanonicalProgramAssembler program_{};
    AacPcm16SubmissionAdapter adapter_{};
    core::audio::AudioProgramBlock scratch_{};
    bool sources_started_ = false;
    bool recording_ = false;
    bool mic_ingested_ = false;
    bool sys_ingested_ = false;
};

} // namespace arssyut::app::audio

#endif // _WIN32 && ARSSYUT_ENABLE_PRODUCT_AUDIO
