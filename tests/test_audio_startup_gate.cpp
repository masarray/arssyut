#include "app/audio_startup_gate.hpp"

#include <cstdint>
#include <iostream>
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

} // namespace

int main()
{
    video_only_never_changes_legacy_gate();
    microphone_requires_both_capture_and_aac();
    system_only_and_dual_source_ordering();
    failure_and_cancel_fail_closed();
    reject_stale_generations_and_invalid_ids();
    repeated_lifecycle_has_no_retained_readiness();

    if (failures != 0) {
        std::cerr << "FAIL: " << failures << "/" << checks
                  << " P7A6 startup checks\n";
        return 1;
    }
    std::cout << "PASS: " << checks
              << " P7A6 audio startup readiness checks\n";
    return 0;
}
