#include "app/audio_startup_gate.hpp"
#include "app/audio_source_preparation.hpp"

#include <cstdint>
#include <iostream>
#include <string>
#include <stdexcept>
#include <type_traits>

namespace {

using arssyut::app::audio::AudioStartRequirements;
using arssyut::app::audio::AudioStartupGate;
using arssyut::app::audio::AudioStartupState;
using arssyut::core::audio::AudioSourceId;

static_assert(sizeof(AudioStartupGate) <= 32,
    "Startup gate must use fixed-size state only");
static_assert(!std::is_polymorphic_v<AudioStartupGate>,
    "Startup gate is single-owner, not a callback interface");

int failures = 0;
int checks = 0;

void expect(bool condition, const char *name)
{
    ++checks;
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << name << '\n';
    }
}

void video_only_never_changes_legacy_gate()
{
    AudioStartupGate gate;
    auto ticket = gate.begin({});
    expect(ticket != 0, "video-only generation created");
    expect(gate.state() == AudioStartupState::Bypassed,
        "video-only bypasses audio barrier");
    expect(!gate.writer_ready(ticket),
        "video-only never adds a writer readiness dependency");
    expect(!gate.source_ready(ticket, AudioSourceId::Microphone),
        "video-only does not start an implicit microphone");
    expect(gate.state() == AudioStartupState::Bypassed,
        "legacy video-only semantics remain unchanged");
}

void microphone_requires_both_capture_and_aac()
{
    AudioStartupGate gate;
    auto ticket = gate.begin({.microphone = true});
    expect(gate.state() == AudioStartupState::Pending,
        "requested microphone initially pending");
    expect(gate.source_ready(ticket, AudioSourceId::Microphone),
        "started microphone is accepted");
    expect(gate.state() == AudioStartupState::Pending,
        "microphone alone cannot publish Armed without AAC");
    expect(!gate.source_ready(ticket, AudioSourceId::SystemAudio),
        "unrequested system audio cannot satisfy microphone");
    expect(gate.writer_ready(ticket), "AAC writer readiness accepted");
    expect(gate.state() == AudioStartupState::Ready,
        "microphone plus writer permits Armed");
    expect(gate.source_ready(ticket, AudioSourceId::Microphone),
        "duplicate readiness is idempotent");
}

void system_only_and_dual_source_ordering()
{
    AudioStartupGate gate;
    auto ticket = gate.begin({.system_audio = true});
    expect(gate.writer_ready(ticket), "writer may finish before source");
    expect(gate.state() == AudioStartupState::Pending,
        "writer alone does not arm system-only recording");
    expect(gate.source_ready(ticket, AudioSourceId::SystemAudio),
        "system loopback accepted");
    expect(gate.state() == AudioStartupState::Ready,
        "system audio and writer ready");

    ticket = gate.begin({.microphone = true, .system_audio = true});
    expect(gate.source_ready(ticket, AudioSourceId::Microphone),
        "dual source microphone ready");
    expect(gate.writer_ready(ticket), "dual source writer ready");
    expect(gate.state() == AudioStartupState::Pending,
        "dual source blocks Armed until loopback ready");
    expect(gate.source_ready(ticket, AudioSourceId::SystemAudio),
        "dual source loopback ready");
    expect(gate.snapshot().requested_sources == 3 &&
           gate.snapshot().ready_sources == 3 &&
           gate.state() == AudioStartupState::Ready,
        "dual source readiness mask complete");
}

void failure_and_cancel_fail_closed()
{
    AudioStartupGate gate;
    auto ticket = gate.begin({.microphone = true, .system_audio = true});
    expect(gate.source_ready(ticket, AudioSourceId::Microphone),
        "first source ready");
    expect(gate.source_failed(ticket, AudioSourceId::SystemAudio),
        "unavailable requested source reported");
    expect(gate.state() == AudioStartupState::Failed,
        "failed source blocks Armed");
    expect(!gate.writer_ready(ticket) &&
           !gate.source_ready(ticket, AudioSourceId::SystemAudio),
        "terminal failure rejects late success");
    expect(gate.snapshot().failed_sources == 2,
        "source-specific failure evidence preserved");

    ticket = gate.begin({.microphone = true});
    expect(gate.writer_failed(ticket), "AAC writer failure reported");
    expect(gate.state() == AudioStartupState::Failed,
        "writer failure blocks Armed");
    expect(!gate.source_ready(ticket, AudioSourceId::Microphone),
        "late source cannot hide AAC failure");

    ticket = gate.begin({.microphone = true});
    expect(gate.cancel(ticket), "cancel-before-frame-zero accepted");
    expect(gate.state() == AudioStartupState::Cancelled,
        "user cancel is distinguishable from start failure");
    expect(!gate.source_ready(ticket, AudioSourceId::Microphone),
        "cancelled startup never becomes ready");
}

void reject_stale_generations_and_invalid_ids()
{
    AudioStartupGate gate;
    const auto old_ticket = gate.begin({.microphone = true});
    const auto fresh_ticket = gate.begin({.system_audio = true});
    expect(old_ticket != fresh_ticket, "reset advances generation");
    expect(!gate.source_ready(old_ticket, AudioSourceId::Microphone),
        "old microphone completion cannot mutate new recording");
    expect(!gate.writer_failed(old_ticket),
        "old writer failure cannot poison new recording");
    expect(!gate.cancel(old_ticket), "stale cancel ignored");
    expect(!gate.source_ready(fresh_ticket,
        static_cast<AudioSourceId>(255)),
        "invalid source enum never shifts an unsafe bit");
    expect(!gate.source_ready(fresh_ticket, AudioSourceId::Microphone),
        "unrequested source ignored");
    expect(gate.writer_ready(fresh_ticket), "new writer ready");
    expect(gate.source_ready(fresh_ticket, AudioSourceId::SystemAudio),
        "new requested source ready");
    expect(gate.state() == AudioStartupState::Ready,
        "new generation remains healthy");
}

void repeated_lifecycle_has_no_retained_readiness()
{
    AudioStartupGate gate;
    for (int i = 0; i < 10'000; ++i) {
        const auto ticket = gate.begin({.microphone = true});
        expect(gate.state() == AudioStartupState::Pending,
            "each start begins pending");
        expect(gate.source_ready(ticket, AudioSourceId::Microphone),
            "source ready each generation");
        expect(gate.writer_ready(ticket), "writer ready each generation");
        expect(gate.state() == AudioStartupState::Ready,
            "ready each generation");
    }
}


using arssyut::app::audio::PinnedAudioEndpoints;
using arssyut::app::audio::start_pinned_audio_sources;
using arssyut::core::Status;
using arssyut::core::StatusCode;

struct FakeCapture final {
    Status result = Status::success();
    std::wstring seen_id;
    int starts = 0;
    int stops = 0;
    bool *signal_cancel_on_start = nullptr;
    bool throw_on_start = false;

    [[nodiscard]] Status start(std::wstring id)
    {
        ++starts;
        seen_id = std::move(id);
        if (signal_cancel_on_start != nullptr)
            *signal_cancel_on_start = true;
        if (throw_on_start)
            throw std::runtime_error("simulated capture initialization failure");
        return result;
    }

    void stop() noexcept { ++stops; }
};

void source_startup_video_only_and_single_source()
{
    AudioStartupGate gate;
    PinnedAudioEndpoints selection;
    FakeCapture mic;
    FakeCapture system;
    int cancellation_reads = 0;
    const auto no_cancel = [&] {
        ++cancellation_reads;
        return false;
    };

    const auto bypass = gate.begin({});
    expect(start_pinned_audio_sources(
        selection, gate, bypass, mic, system, no_cancel).ok(),
        "video-only startup bypasses all WASAPI");
    expect(cancellation_reads == 0 && mic.starts == 0 &&
           system.starts == 0 && gate.state() == AudioStartupState::Bypassed,
        "video-only does not even read audio cancellation callback");

    selection.microphone = true;
    selection.microphone_id = L"{concrete-capture-endpoint}";
    const auto mic_ticket = gate.begin({.microphone = true});
    auto result = start_pinned_audio_sources(
        selection, gate, mic_ticket, mic, system, no_cancel);
    expect(result.ok() && mic.starts == 1 && system.starts == 0 &&
           mic.seen_id == selection.microphone_id,
        "microphone uses pinned ID exactly once");
    expect(gate.state() == AudioStartupState::Pending,
        "microphone source alone does not Arm until writer exists");
    expect(gate.writer_ready(mic_ticket) &&
           gate.state() == AudioStartupState::Ready,
        "single AAC writer readiness completes microphone startup");
    expect(start_pinned_audio_sources(
        selection, gate, mic_ticket, mic, system, no_cancel).code ==
            StatusCode::InvalidStateTransition &&
           mic.starts == 1,
        "same-generation repeated source start cannot create duplicate worker");

    selection = {};
    selection.system_audio = true;
    selection.system_audio_id = L"{concrete-render-endpoint}";
    const auto system_ticket = gate.begin({.system_audio = true});
    expect(gate.writer_ready(system_ticket), "writer may be ready before loopback");
    expect(start_pinned_audio_sources(
        selection, gate, system_ticket, mic, system, no_cancel).ok() &&
           system.starts == 1 &&
           system.seen_id == selection.system_audio_id &&
           gate.state() == AudioStartupState::Ready,
        "system-only uses render ID with writer-before-source ordering");
}

void source_startup_dual_rollback_and_identity_checks()
{
    AudioStartupGate gate;
    PinnedAudioEndpoints dual{
        .microphone = true,
        .system_audio = true,
        .microphone_id = L"{capture}",
        .system_audio_id = L"{render}",
    };
    FakeCapture mic;
    FakeCapture system;
    const auto no_cancel = [] { return false; };
    const auto ticket = gate.begin({.microphone = true, .system_audio = true});
    system.result = Status::failure(StatusCode::PlatformFailure, 0xBEEF);
    const auto status = start_pinned_audio_sources(
        dual, gate, ticket, mic, system, no_cancel);
    expect(status.code == StatusCode::PlatformFailure &&
           status.detail == 0xBEEF,
        "dual-source failure retains original Windows error evidence");
    expect(mic.starts == 1 && mic.stops == 1 &&
           system.starts == 1 && system.stops == 1,
        "second source failure stops both attempted sources in reverse order");
    expect(gate.state() == AudioStartupState::Failed &&
           gate.snapshot().failed_sources == 0x02,
        "second-source failure never publishes partial Armed state");
    expect(!gate.writer_ready(ticket),
        "AAC writer cannot resurrect a failed two-source startup");

    const auto next = gate.begin({.microphone = true, .system_audio = true});
    mic = {};
    system = {};
    const auto success = start_pinned_audio_sources(
        dual, gate, next, mic, system, no_cancel);
    expect(success.ok() && gate.snapshot().ready_sources == 0x03 &&
           gate.state() == AudioStartupState::Pending,
        "dual-source success still awaits sole AAC writer readiness");
    expect(gate.writer_ready(next) &&
           gate.state() == AudioStartupState::Ready,
        "dual-source and writer all ready permit Armed");
    expect(!gate.cancel(ticket) && gate.state() == AudioStartupState::Ready,
        "stale previous-generation cancellation cannot affect new worker");

    const auto mismatch_ticket = gate.begin({.system_audio = true});
    mic = {};
    system = {};
    expect(start_pinned_audio_sources(
        dual, gate, mismatch_ticket, mic, system, no_cancel).code ==
            StatusCode::InvalidStateTransition &&
           mic.starts == 0 && system.starts == 0,
        "mismatched role mask fails before initializing WASAPI");
    dual.microphone = false;
    dual.microphone_id.clear();
    dual.system_audio_id.clear();
    expect(start_pinned_audio_sources(
        dual, gate, mismatch_ticket, mic, system, no_cancel).code ==
            StatusCode::InvalidArgument &&
           system.starts == 0,
        "requested source missing concrete endpoint ID fails closed");

    dual.system_audio_id = L"{render}";
    dual.microphone_id = L"{unexpected-stale-id}";
    expect(start_pinned_audio_sources(
        dual, gate, mismatch_ticket, mic, system, no_cancel).code ==
            StatusCode::InvalidArgument && system.starts == 0,
        "unrequested source carrying stale ID is rejected");

    dual.microphone_id.clear();
    expect(start_pinned_audio_sources(
        dual, gate, ticket, mic, system, no_cancel).code ==
            StatusCode::InvalidStateTransition &&
           system.starts == 0,
        "stale generation cannot start sources from the new recording");
}

void source_startup_cancel_exception_and_first_source_failure()
{
    AudioStartupGate gate;
    PinnedAudioEndpoints dual{
        .microphone = true,
        .system_audio = true,
        .microphone_id = L"{capture}",
        .system_audio_id = L"{render}",
    };
    FakeCapture mic;
    FakeCapture system;
    bool cancel = true;
    const auto is_cancelled = [&] { return cancel; };

    auto ticket = gate.begin({.microphone = true, .system_audio = true});
    expect(start_pinned_audio_sources(
        dual, gate, ticket, mic, system, is_cancelled).code ==
            StatusCode::InvalidStateTransition &&
           mic.starts == 0 && system.starts == 0 &&
           gate.state() == AudioStartupState::Cancelled,
        "pre-start cancel never opens audio endpoints");

    cancel = false;
    ticket = gate.begin({.microphone = true, .system_audio = true});
    mic.signal_cancel_on_start = &cancel;
    expect(start_pinned_audio_sources(
        dual, gate, ticket, mic, system, is_cancelled).code ==
            StatusCode::InvalidStateTransition &&
           mic.starts == 1 && mic.stops == 1 &&
           system.starts == 0 && gate.state() == AudioStartupState::Cancelled,
        "cancel during first WASAPI initialization rolls back immediately");

    cancel = false;
    mic = {};
    system = {};
    system.signal_cancel_on_start = &cancel;
    ticket = gate.begin({.microphone = true, .system_audio = true});
    expect(start_pinned_audio_sources(
        dual, gate, ticket, mic, system, is_cancelled).code ==
            StatusCode::InvalidStateTransition &&
           mic.stops == 1 && system.stops == 1 &&
           gate.state() == AudioStartupState::Cancelled,
        "cancel during second WASAPI initialization stops both workers");

    cancel = false;
    mic = {};
    system = {};
    system.throw_on_start = true;
    ticket = gate.begin({.microphone = true, .system_audio = true});
    expect(start_pinned_audio_sources(
        dual, gate, ticket, mic, system, is_cancelled).code ==
            StatusCode::InternalError &&
           mic.stops == 1 && system.stops == 1 &&
           gate.state() == AudioStartupState::Failed &&
           gate.snapshot().failed_sources == 0x02,
        "second-source C++ exception preserves single-worker rollback");

    mic = {};
    system = {};
    mic.result = Status::failure(StatusCode::PlatformFailure, 0x5678);
    ticket = gate.begin({.microphone = true, .system_audio = true});
    const auto error = start_pinned_audio_sources(
        dual, gate, ticket, mic, system, is_cancelled);
    expect(error.code == StatusCode::PlatformFailure &&
           error.detail == 0x5678 &&
           mic.stops == 1 && system.starts == 0 &&
           gate.snapshot().failed_sources == 0x01,
        "first-source failure never attempts other capture device");
}

void source_startup_repeat_1000_generations()
{
    AudioStartupGate gate;
    PinnedAudioEndpoints pinned{
        .microphone = true,
        .system_audio = true,
        .microphone_id = L"{capture}",
        .system_audio_id = L"{render}",
    };
    for (int i = 0; i < 1'000; ++i) {
        FakeCapture mic;
        FakeCapture system;
        const auto ticket = gate.begin({.microphone = true, .system_audio = true});
        if (!start_pinned_audio_sources(
                pinned, gate, ticket, mic, system, [] { return false; }).ok() ||
            !gate.writer_ready(ticket) ||
            gate.state() != AudioStartupState::Ready ||
            mic.starts != 1 || system.starts != 1) {
            expect(false, "bounded dual-source preparation per generation");
            return;
        }
        // RecorderSession owns the ordinary Stop cleanup after successful
        // start; the startup helper never creates a hidden detached worker.
        system.stop();
        mic.stop();
        if (mic.stops != 1 || system.stops != 1) {
            expect(false, "successful source workers stopped exactly once");
            return;
        }
    }
    expect(true, "1000 generations start/stop with no retained gate readiness");
}

} // namespace

int main()
{
    video_only_never_changes_legacy_gate();
    microphone_requires_both_capture_and_aac();
    system_only_and_dual_source_ordering();
    failure_and_cancel_fail_closed();
    reject_stale_generations_and_invalid_ids();
    repeated_lifecycle_has_no_retained_readiness();
    source_startup_video_only_and_single_source();
    source_startup_dual_rollback_and_identity_checks();
    source_startup_cancel_exception_and_first_source_failure();
    source_startup_repeat_1000_generations();

    if (failures != 0) {
        std::cerr << "FAIL: " << failures << "/" << checks
                  << " P7A6 startup checks\n";
        return 1;
    }
    std::cout << "PASS: " << checks
              << " P7A6 audio startup readiness checks\n";
    return 0;
}
