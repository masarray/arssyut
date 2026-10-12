#pragma once

// P7A6 product ownership boundary, available only in the *explicitly*
// FFmpeg-enabled Windows build. RecorderSession's existing worker owns every
// operation. WASAPI capture threads ONLY publish native packet leases; only
// this worker invokes ingress, mixer and the sole Media Foundation AV writer.
#if defined(_WIN32) && defined(ARSSYUT_ENABLE_PRODUCT_AUDIO)

#include "app/audio_endpoint_preflight.hpp"
#include "app/recording_audio_health.hpp"
#include "app/audio_source_preparation.hpp"
#include "app/audio_pcm16_submission_adapter.hpp"
#include "app/recorded_input_sounds.hpp"
#include "presentation/presentation_state.hpp"
#include "core/audio/audio_packet_ingress.hpp"
#include "core/audio/audio_stop_drain_schedule.hpp"
#include "platform/windows/audio/libswresample_audio_resampler.hpp"
#include "platform/windows/audio/wasapi_microphone_source.hpp"
#include "platform/windows/audio/wasapi_loopback_source.hpp"
#include "platform/windows/media/mf_h264_mp4_writer.hpp"

#include <cstdint>
#include <limits>

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

    [[nodiscard]] core::Status begin(
        std::int64_t media_zero_100ns,
        float mic_gain = 1.0f, bool mic_muted = false,
        float system_gain = 1.0f, bool system_muted = false) noexcept
    {
        if (!sources_started_ ||
            gate_.state() != AudioStartupState::Ready ||
            media_zero_100ns <= 0)
            return core::Status::failure(
                core::StatusCode::InvalidStateTransition);

        // Every recording uses exactly the RecorderSession QPC media zero.
        program_.reset(media_zero_100ns,
                       pinned_.microphone, pinned_.system_audio);
        media_zero_100ns_ = media_zero_100ns;
        // Existing canonical per-source mixer; unity defaults are unchanged.
        program_.mixer().set_source_config(
            core::audio::AudioSourceId::Microphone, {mic_gain, mic_muted});
        program_.mixer().set_source_config(
            core::audio::AudioSourceId::SystemAudio, {system_gain, system_muted});
        adapter_.reset(media_zero_100ns);
        recorded_cues_.reset(media_zero_100ns);
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

        mic_ingested_ = false;
        sys_ingested_ = false;
        first_error_phase_ = 0;
        first_error_source_ = 0;
        first_packet_status_ = 0;
        first_native_hresult_ = 0;
        recording_ = true;
        return core::Status::success();
    }

    // The one native recorder worker owns Pause/Resume. Capture callbacks
    // continue to publish into their original bounded WASAPI packet pools;
    // discard their contents without creating AAC media during exclusion.
    void discard_while_paused() noexcept
    {
        if (!recording_) return;
        const auto drop = [](auto &source) noexcept {
            windows::WasapiPacketLease packet;
            for (int i = 0; i < 64 && source.try_pop(packet); ++i)
                (void)packet.release();
        };
        if (pinned_.microphone) drop(microphone_);
        if (pinned_.system_audio) drop(loopback_);
    }

    void pause() noexcept
    {
        if (!recording_ || paused_) return;
        program_.discard_sources_after_pause();
        discard_while_paused();
        paused_ = true;
    }

    void resume(std::int64_t completed_pause_ticks,
                std::int64_t physical_resume_qpc) noexcept
    {
        if (!recording_ || !paused_) return;
        discard_while_paused();
        program_.discard_sources_after_pause();
        if (pinned_.microphone) mic_ingress_.reanchor_after_pause(
            completed_pause_ticks, physical_resume_qpc);
        if (pinned_.system_audio) sys_ingress_.reanchor_after_pause(
            completed_pause_ticks, physical_resume_qpc);
        paused_ = false;
    }

    // Worker-only transport mutation: leave WASAPI running and preserve the
    // canonical 48kHz program, AAC history and source gain. The mixer performs
    // a bounded 5ms transition only when the microphone mute bit changes.
    void set_microphone_muted(bool muted, float gain) noexcept
    {
        if (!recording_ || !pinned_.microphone) return;
        program_.mixer().set_source_config(
            core::audio::AudioSourceId::Microphone, {gain, muted});
    }

    void record_click(presentation::ClickKind kind, std::int64_t timestamp) noexcept
    {
        if (!recording_) return;
        if (kind == presentation::ClickKind::Left)
            recorded_cues_.push(RecordedCue::LeftClick, timestamp);
        else if (kind == presentation::ClickKind::Right)
            recorded_cues_.push(RecordedCue::RightClick, timestamp);
    }
    void record_keycap(std::int64_t timestamp) noexcept
    {
        if (recording_)
            recorded_cues_.push(RecordedCue::Keycap, timestamp);
    }

    // Run before video submission on RecorderSession's sole writer worker:
    // feed the muxer's AAC stream before another video surface is retained.
    // Four bounded rounds can recover from video/compositor latency without
    // dropping overdue program intervals or growing an unbounded queue.
    // WASAPI callbacks only hand off leases; no second writer/clock/thread.
    [[nodiscard]] core::Status service(
        core::TimePoint now, windows::MfH264Mp4Writer &writer) noexcept
    {
        if (!recording_ || paused_)
            return core::Status::failure(
                core::StatusCode::InvalidStateTransition);

        // Real 25.4s capture discarded 230993 stale source frames at 1080p60.
        // 10ms settle is below ordinary event+AV writer scheduling jitter.
        // Keep output closure 35ms behind host QPC (around 2 AAC blocks).
        // The original RecorderSession QPC/PTS remains authoritative:
        // no audio retiming, additional clock or unbounded buffering.
        constexpr std::int64_t kPacketSettle100ns = 350'000;
        constexpr std::uint32_t kMaxRounds = 4;
        constexpr std::uint32_t kPacketsPerRound = 2;
        constexpr std::uint32_t kBlocksPerRound = 2;
        const auto due_through = now.ticks_100ns - kPacketSettle100ns;

        for (std::uint32_t round = 0; round < kMaxRounds; ++round) {
            if (pinned_.microphone) {
                const auto r = mic_ingress_.pump_available(
                    microphone_, program_, kPacketsPerRound);
                if (!acceptable(r.status))
                    return ingress_failure_recorded(1, 1, r.status);
                mic_ingested_ = mic_ingested_ || r.packets_popped != 0;
                const auto state = microphone_.snapshot();
                if (state.state == windows::WasapiCaptureState::Failed ||
                    state.state == windows::WasapiCaptureState::DeviceInvalidated)
                    return native_failure_recorded(1, 1, state.last_hresult);
            }
            if (pinned_.system_audio) {
                const auto r = sys_ingress_.pump_available(
                    loopback_, program_, kPacketsPerRound);
                if (!acceptable(r.status))
                    return ingress_failure_recorded(1, 2, r.status);
                sys_ingested_ = sys_ingested_ || r.packets_popped != 0;
                const auto state = loopback_.snapshot();
                if (state.state == windows::WasapiCaptureState::Failed ||
                    state.state == windows::WasapiCaptureState::DeviceInvalidated)
                    return native_failure_recorded(1, 2, state.last_hresult);
            }

            // Drain EACH completed interval before closing the next one.
            // The canonical handoff has only four slots; batching closes
            // without matching writer consumption silently drops PCM.
            for (std::uint32_t n = 0; n < kBlocksPerRound; ++n) {
                if (program_.next_due_100ns() > due_through)
                    break;
                if (!program_.close_one_due(program_.next_due_100ns()))
                    return program_failure_recorded(4,
                        core::StatusCode::InvalidStateTransition);
                const auto status = submit(writer, -1);
                if (!status.ok())
                    return submitted_failure_recorded(status);
            }
            if (program_.next_due_100ns() > due_through)
                break;
        }
        return core::Status::success();
    }

    // Finish while MF writer is OPEN; no source or compressor runs beyond
    // finalize. A strict cap prevents a failed device or stalled writer from
    // causing an unbounded Stop. The final AAC block is whole-frame trimmed
    // to the same physical Stop instant as the video timeline.
    [[nodiscard]] core::Status finish(
        core::TimePoint stopped, windows::MfH264Mp4Writer &writer,
        bool stopped_during_pause = false) noexcept
    {
        if (!recording_) {
            stop_sources();
            return core::Status::success();
        }

        if (stopped_during_pause) {
            // All captured PCM in the excluded interval is abandoned, not
            // replayed as post-resume media. Audio timeline stays frozen.
            discard_while_paused();
            program_.discard_sources_after_pause();
        }
        // First quiesce the producers, but retain their bounded WASAPI
        // handoffs and pool generations. WasapiCaptureSource::stop() retires
        // queued packets, so using stop() here would lose audible tail data.
        // This is a Stop control-plane operation, never a capture callback.
        if (pinned_.microphone)
            microphone_.quiesce();
        if (pinned_.system_audio)
            loopback_.quiesce();
        if (stopped_during_pause) {
            // Producer is now quiesced: drain any remaining paused packets
            // (no concurrent writer), preserving the physical Stop cap.
            discard_while_paused();
        }

        // Physical Stop has already been compressed by RecordingPauseClock.
        // Compute one exclusive 48k media boundary for both WASAPI sources.
        // Rounding upward keeps the final in-range partial PCM frame; the
        // existing AAC submit adapter performs final exact-time truncation.
        const auto stop_elapsed = static_cast<std::uint64_t>(
            (std::max)(std::int64_t{0},
                       stopped.ticks_100ns - media_zero_100ns_));
        const auto full_frames = core::audio::ticks_to_frames_floor(
            stop_elapsed, 48'000);
        const auto stop_frame_exclusive = static_cast<std::int64_t>(
            (std::min)(
                full_frames +
                    (core::audio::frames_to_ticks_floor(
                         full_frames, 48'000) < stop_elapsed ? 1ULL : 0ULL),
                static_cast<std::uint64_t>(
                    (std::numeric_limits<std::int64_t>::max)())));
        
        const auto fail_stop = [this](core::Status error) noexcept {
            stop_sources();  // retire pool only after last lease released
            recording_ = false;
            return error;
        };

        // Exactly one owner drives all pending source leases, SRC state and
        // the existing MF writer. Each iteration services at most one
        // captured packet per source, then closes/submits only program blocks
        // BOTH sources have already reached. No batch-first overflow and no
        // false synthetic silence ahead of delayed microphone packets.
        bool mic_empty = !pinned_.microphone || stopped_during_pause;
        bool sys_empty = !pinned_.system_audio || stopped_during_pause;
        bool mic_done = !pinned_.microphone || stopped_during_pause;
        bool sys_done = !pinned_.system_audio || stopped_during_pause;
        constexpr int kMaxStopRounds = 512;
        constexpr int kMaxClosesPerRound = 4;
        bool completed = false;
        for (int round = 0; round < kMaxStopRounds; ++round) {
            if (!mic_empty) {
                const auto r = mic_ingress_.pump_available(
                    microphone_, program_, 1, stop_frame_exclusive);
                if (!acceptable(r.status))
                    return fail_stop(
                        ingress_failure_recorded(2, 1, r.status));
                mic_ingested_ = mic_ingested_ || r.packets_popped != 0;
                mic_empty = r.source_empty;
            }
            if (!sys_empty) {
                const auto r = sys_ingress_.pump_available(
                    loopback_, program_, 1, stop_frame_exclusive);
                if (!acceptable(r.status))
                    return fail_stop(
                        ingress_failure_recorded(2, 2, r.status));
                sys_ingested_ = sys_ingested_ || r.packets_popped != 0;
                sys_empty = r.source_empty;
            }

            // SRC tails belong to the same already-configured resamplers.
            // A source is "complete" only after its last lease AND its
            // resampler delay have both been consumed. This also handles
            // 44.1->48kHz without moving the canonical audio media zero.
            if (mic_empty && !mic_done) {
                if (!mic_ingress_.has_canonical_cursor()) {
                    mic_done = true;
                } else {
                    const auto r = mic_ingress_.drain_one(
                        program_, stop_frame_exclusive);
                    if (!acceptable(r.status))
                        return fail_stop(
                            ingress_failure_recorded(3, 1, r.status));
                    mic_done = r.canonical_frames_emitted == 0;
                }
            }
            if (sys_empty && !sys_done) {
                if (!sys_ingress_.has_canonical_cursor()) {
                    sys_done = true;
                } else {
                    const auto r = sys_ingress_.drain_one(
                        program_, stop_frame_exclusive);
                    if (!acceptable(r.status))
                        return fail_stop(
                            ingress_failure_recorded(3, 2, r.status));
                    sys_done = r.canonical_frames_emitted == 0;
                }
            }

            const core::audio::AudioStopDrainFrontier frontier{
                .microphone_enabled = pinned_.microphone,
                .system_enabled = pinned_.system_audio,
                .microphone_complete = mic_done,
                .system_complete = sys_done,
                .microphone_next_frame =
                    mic_ingress_.canonical_next_frame(),
                .system_next_frame =
                    sys_ingress_.canonical_next_frame(),
                .stop_ticks_100ns = stopped.ticks_100ns
            };
            for (int n = 0; n < kMaxClosesPerRound &&
                 core::audio::audio_stop_block_safe(
                     program_, frontier); ++n) {
                if (!program_.close_one_due(program_.next_due_100ns()))
                    return fail_stop(program_failure_recorded(
                        4, core::StatusCode::InvalidStateTransition));
                const auto status = submit(writer, stopped.ticks_100ns);
                if (!status.ok())
                    return fail_stop(submitted_failure_recorded(status));
            }
            if (mic_done && sys_done) {
                completed = true;
                break;
            }
        }
        if (!completed)
            return fail_stop(program_failure_recorded(
                2, core::StatusCode::InvalidStateTransition));

        // Both packet handoffs and SRC tails are exhausted. It is NOW safe
        // to close remaining physical-time intervals as silence if a WASAPI
        // endpoint was quiet; no delayed audio from that endpoint can arrive.
        constexpr int kMaxFinalBlocks = 512;
        for (int i = 0; i < kMaxFinalBlocks &&
             program_.next_due_100ns() <= stopped.ticks_100ns; ++i) {
            if (!program_.close_one_due(program_.next_due_100ns()))
                return fail_stop(program_failure_recorded(
                    4, core::StatusCode::InvalidStateTransition));
            const auto status = submit(writer, stopped.ticks_100ns);
            if (!status.ok())
                return fail_stop(submitted_failure_recorded(status));
        }
        if (program_.next_due_100ns() <= stopped.ticks_100ns)
            return fail_stop(program_failure_recorded(
                4, core::StatusCode::InvalidStateTransition));

        // Explicitly close ONE final partial AAC interval and submit with
        // the original captured Stop timestamp; adapter truncates it.
        if (program_.pending_blocks() == 0)
            if (!program_.close_one_due(program_.next_due_100ns()))
                return fail_stop(program_failure_recorded(
                    4, core::StatusCode::InvalidStateTransition));
        for (int i = 0; i < 8 &&
             program_.pending_blocks() != 0; ++i) {
            const auto status = submit(writer, stopped.ticks_100ns);
            if (!status.ok())
                return fail_stop(submitted_failure_recorded(status));
        }
        const bool emptied = program_.pending_blocks() == 0 &&
            program_.stats().dropped_program_blocks == 0;
        stop_sources();
        recording_ = false;
        paused_ = false;
        return emptied ? core::Status::success()
            : program_failure_recorded(
                4, core::StatusCode::InvalidStateTransition);
    }

    // Snapshot the actual capture evidence on the existing recorder worker.
    // No packet processing, reconfiguration or extra synchronization.
    [[nodiscard]] app::RecordingAudioHealth health_snapshot() const noexcept
    {
        app::RecordingAudioHealth v{};
        const auto copy = [](auto &dst, const auto &s) noexcept {
            dst.captured=s.captured_packets; dst.published=s.published_packets;
            dst.silent=s.silent_packets; dst.discontinuities=s.discontinuity_packets;
            dst.timestamp_errors=s.timestamp_error_packets; dst.dropped=s.dropped_packets;
            dst.pool_exhaustions=s.pool_exhaustions; dst.ring_overflows=s.ring_overflows;
        };
        if (pinned_.microphone) copy(v.microphone, microphone_.snapshot());
        if (pinned_.system_audio) copy(v.system, loopback_.snapshot());
        const auto stats=program_.stats();
        const auto mix=program_.mix_stats();
        v.missing_source_intervals=mix.missing_source_intervals;
        v.silent_program_blocks=mix.silent_blocks;
        v.stale_canonical_frames=stats.stale_canonical_frames;
        v.duplicate_canonical_frames=stats.duplicate_canonical_frames;
        v.staging_gaps=stats.staging_gaps;
        v.staging_overflows=stats.staging_overflows;
        v.dropped_program_blocks=stats.dropped_program_blocks;
        v.failure_phase=first_error_phase_;
        v.failure_source=first_error_source_;
        v.failure_packet_ingress_status=first_packet_status_;
        v.failure_native_hresult=first_native_hresult_;
        v.mic_stage_frames=static_cast<std::uint32_t>(
            program_.source_stage_frames(core::audio::AudioSourceId::Microphone));
        v.system_stage_frames=static_cast<std::uint32_t>(
            program_.source_stage_frames(core::audio::AudioSourceId::SystemAudio));
        v.mic_stage_peak=static_cast<std::uint32_t>(
            program_.source_stage_peak(core::audio::AudioSourceId::Microphone));
        v.system_stage_peak=static_cast<std::uint32_t>(
            program_.source_stage_peak(core::audio::AudioSourceId::SystemAudio));
        v.pending_audio_program_blocks=program_.pending_blocks();
        return v;
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

    void note_error(std::uint32_t phase, std::uint32_t source,
                    std::uint32_t packet_status = 0,
                    std::uint32_t hresult = 0) noexcept
    {
        if (first_error_phase_ != 0)
            return; // Preserve first origin; finalizer must not overwrite.
        first_error_phase_ = phase;
        first_error_source_ = source;
        first_packet_status_ = packet_status;
        first_native_hresult_ = hresult;
    }

    [[nodiscard]] core::Status ingress_failure_recorded(
        std::uint32_t phase, std::uint32_t source,
        core::audio::PacketIngressStatus status) noexcept
    {
        note_error(phase, source, static_cast<std::uint32_t>(status));
        return core::Status::failure(
            core::StatusCode::PlatformFailure,
            static_cast<std::uint32_t>(status));
    }

    [[nodiscard]] core::Status native_failure_recorded(
        std::uint32_t phase, std::uint32_t source,
        std::uint32_t hresult) noexcept
    {
        note_error(phase, source, 0, hresult);
        return core::Status::failure(
            core::StatusCode::PlatformFailure, hresult);
    }

    [[nodiscard]] core::Status program_failure_recorded(
        std::uint32_t phase, core::StatusCode code) noexcept
    {
        note_error(phase, 3);
        return core::Status::failure(code);
    }

    [[nodiscard]] core::Status submitted_failure_recorded(
        core::Status status) noexcept
    {
        note_error(5, 3);
        return status;
    }

    [[nodiscard]] core::Status submit(
        windows::MfH264Mp4Writer &writer,
        std::int64_t stop_100ns) noexcept
    {
        const auto r = adapter_.service_ready_blocks(
            writer,
            [this](core::audio::AudioProgramBlock &out,
                   std::uint64_t &first_frame) noexcept {
                if (!program_.try_take(out, first_frame))
                    return false;
                recorded_cues_.apply(out, first_frame);
                return true;
            },
            scratch_, 2, stop_100ns);
        // Never discard all subsequent audio while still reporting Saved.
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
    RecordedInputSounds recorded_cues_{};
    core::audio::AudioProgramBlock scratch_{};
    std::int64_t media_zero_100ns_ = 0;
    bool sources_started_ = false;
    bool recording_ = false;
    bool paused_ = false;
    bool mic_ingested_ = false;
    bool sys_ingested_ = false;
    std::uint32_t first_error_phase_ = 0, first_error_source_ = 0;
    std::uint32_t first_packet_status_ = 0, first_native_hresult_ = 0;
};

} // namespace arssyut::app::audio

#endif // _WIN32 && ARSSYUT_ENABLE_PRODUCT_AUDIO
