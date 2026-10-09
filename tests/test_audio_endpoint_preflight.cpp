// Include the canonical audio contracts before Windows MMDevice headers.
// This is the order used by the future single RecorderSession AV owner.
#include "app/audio_pcm16_submission_adapter.hpp"
#include "app/audio_endpoint_preflight.hpp"
#include "app/audio_source_preparation.hpp"

#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {
using arssyut::app::audio::AudioEndpointKind;
using arssyut::app::audio::AudioEndpointRequest;
using arssyut::app::audio::IAudioEndpointLookup;
using arssyut::app::audio::PinnedAudioEndpoints;
using arssyut::app::audio::resolve_audio_endpoints;
using arssyut::core::Status;
using arssyut::core::StatusCode;

struct FakeLookup final : IAudioEndpointLookup {
    std::vector<AudioEndpointKind> order;
    std::vector<std::wstring> supplied_ids;
    bool fail_microphone = false;
    bool fail_system = false;
    bool return_empty = false;

    [[nodiscard]] Status pin(
        AudioEndpointKind kind, std::wstring_view explicit_id,
        std::wstring &concrete_id) override
    {
        order.push_back(kind);
        supplied_ids.emplace_back(explicit_id);
        if ((kind == AudioEndpointKind::Microphone && fail_microphone) ||
            (kind == AudioEndpointKind::SystemAudio && fail_system))
            return Status::failure(StatusCode::PlatformFailure, 0x1234);
        if (return_empty)
            return Status::success(); // defective provider must be rejected
        concrete_id = explicit_id.empty()
            ? (kind == AudioEndpointKind::Microphone
                ? L"{pinned-default-capture}"
                : L"{pinned-default-render}")
            : std::wstring(explicit_id);
        return Status::success();
    }
};

int checks = 0;
int failures = 0;
void check(bool condition, const char *name)
{
    ++checks;
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << name << '\n';
    }
}

void check_video_only()
{
    FakeLookup fake;
    PinnedAudioEndpoints pinned;
    pinned.microphone = true;
    pinned.microphone_id = L"{old-recording}";
    const auto status = resolve_audio_endpoints({}, fake, pinned);
    check(status.ok() && fake.order.empty(),
          "video-only never queries COM/MMDevice");
    check(!pinned.microphone && !pinned.system_audio &&
              pinned.microphone_id.empty() && pinned.system_audio_id.empty(),
          "video-only cannot retain the previous recording ID");
}

void check_mic_system_and_dual()
{
    FakeLookup fake;
    PinnedAudioEndpoints pinned;

    AudioEndpointRequest request;
    request.microphone = true;
    check(resolve_audio_endpoints(request, fake, pinned).ok(),
          "default microphone resolves");
    check(pinned.microphone && !pinned.system_audio &&
              pinned.microphone_id == L"{pinned-default-capture}",
          "default capture is a real pinned ID");
    check(fake.order.size() == 1 &&
              fake.order[0] == AudioEndpointKind::Microphone &&
              fake.supplied_ids[0].empty(),
          "mic-only never touches render endpoint");

    fake = {};
    request = {};
    request.system_audio = true;
    check(resolve_audio_endpoints(request, fake, pinned).ok() &&
              pinned.system_audio && !pinned.microphone &&
              pinned.system_audio_id == L"{pinned-default-render}",
          "default loopback pins a render endpoint, never capture");

    fake = {};
    request = {};
    request.microphone = true;
    request.system_audio = true;
    request.microphone_id = L"{selected-mic-token-resolved-id}";
    check(resolve_audio_endpoints(request, fake, pinned).ok(),
          "dual source resolves both requested endpoints");
    check(fake.order.size() == 2 &&
              fake.order[0] == AudioEndpointKind::Microphone &&
              fake.order[1] == AudioEndpointKind::SystemAudio &&
              fake.supplied_ids[0] == request.microphone_id &&
              fake.supplied_ids[1].empty(),
          "explicit selected Mic ID is never silently swapped to default");
    check(pinned.microphone && pinned.system_audio &&
              pinned.microphone_id == request.microphone_id &&
              pinned.system_audio_id == L"{pinned-default-render}",
          "dual-source output carries two independently pinned IDs");
}

void check_fail_closed()
{
    PinnedAudioEndpoints output;
    FakeLookup fake;
    AudioEndpointRequest request{.microphone = true, .system_audio = true};
    fake.fail_system = true;
    auto status = resolve_audio_endpoints(request, fake, output);
    check(status.code == StatusCode::PlatformFailure &&
              status.detail == 0x1234 &&
              !output.microphone && !output.system_audio &&
              output.microphone_id.empty() && output.system_audio_id.empty(),
          "second-source failure cannot expose a partially armed profile");

    fake = {};
    fake.fail_microphone = true;
    check(!resolve_audio_endpoints(request, fake, output).ok() &&
              fake.order.size() == 1 && output.microphone_id.empty(),
          "first-source failure never initializes the second endpoint");

    fake = {};
    fake.return_empty = true;
    check(!resolve_audio_endpoints(request, fake, output).ok() &&
              output.microphone_id.empty(),
          "empty success from endpoint provider still fails closed");
}

void check_untrusted_selection()
{
    FakeLookup fake;
    PinnedAudioEndpoints result;
    AudioEndpointRequest request;
    request.microphone = true;
    request.microphone_id = std::wstring(L"valid");
    request.microphone_id.push_back(L'\0');
    request.microphone_id += L"malicious";
    auto status = resolve_audio_endpoints(request, fake, result);
    check(status.code == StatusCode::InvalidArgument && fake.order.empty(),
          "NUL-containing requested endpoint ID cannot truncate to another");

    request.microphone_id.assign(4'097, L'x');
    status = resolve_audio_endpoints(request, fake, result);
    check(status.code == StatusCode::InvalidArgument && fake.order.empty(),
          "oversized device identity cannot reach Windows lookup");

    request.microphone_id = L"{explicit-not-default}";
    fake.fail_microphone = true;
    status = resolve_audio_endpoints(request, fake, result);
    check(status.code == StatusCode::PlatformFailure &&
              fake.supplied_ids.size() == 1 &&
              fake.supplied_ids.front() == L"{explicit-not-default}",
          "unavailable selected ID is an error, never default fallback");
}

void check_session_isolation()
{
    FakeLookup fake;
    PinnedAudioEndpoints output;
    for (int i = 0; i < 1'000; ++i) {
        AudioEndpointRequest req;
        req.microphone = (i & 1) == 0;
        req.system_audio = (i % 3) == 0;
        const auto status = resolve_audio_endpoints(req, fake, output);
        if (!status.ok() ||
            output.microphone != req.microphone ||
            output.system_audio != req.system_audio ||
            (!req.microphone && !output.microphone_id.empty()) ||
            (!req.system_audio && !output.system_audio_id.empty())) {
            check(false, "preflight never retains a stale previous session");
            return;
        }
    }
    check(true, "1000 recording generations resolve without stale IDs");
}
} // namespace

int main()
{
    check_video_only();
    check_mic_system_and_dual();
    check_fail_closed();
    check_untrusted_selection();
    check_session_isolation();

    // Single-owner P7A6 cross-tranche acceptance: selected endpoint profile
    // -> source starts -> one AAC writer readiness -> canonical MF submission.
    // Real WASAPI/Media Foundation codecs are tested separately, while this
    // test verifies that both accepted contracts link and compose without
    // accidentally Armed-before-writer or independent media-zero origins.
    {
        using namespace arssyut::app::audio;
        FakeLookup lookup;
        AudioEndpointRequest profile;
        profile.microphone = true;
        profile.system_audio = true;
        profile.microphone_id = L"{pinned-capture}";
        PinnedAudioEndpoints pinned;
        check(resolve_audio_endpoints(profile, lookup, pinned).ok(),
              "integration resolves both Windows audio roles");
        struct IntegratedSource {
            int starts = 0;
            int stops = 0;
            Status result = Status::success();
            std::wstring device;
            [[nodiscard]] Status start(std::wstring id)
            {
                ++starts;
                device = std::move(id);
                return result;
            }
            void stop() noexcept { ++stops; }
        } mic, system;
        AudioStartupGate startup;
        const auto generation = startup.begin({
            .microphone = true, .system_audio = true
        });
        check(start_pinned_audio_sources(
                  pinned, startup, generation, mic, system,
                  [] { return false; }).ok() &&
                  mic.device == L"{pinned-capture}" &&
                  system.device == L"{pinned-default-render}",
              "integration starts two concrete pinned device identities");
        check(startup.state() == AudioStartupState::Pending,
              "two sources alone cannot falsely Arm without AAC writer");
        check(startup.writer_ready(generation) &&
                  startup.state() == AudioStartupState::Ready,
              "same-generation sole AAC writer permits Armed");
        struct IntegratedWriter {
            int calls = 0;
            bool discontinuity = false;
            std::int64_t pts = -1;
            std::size_t samples = 0;
            [[nodiscard]] Status write_audio_pcm16(
                std::span<const std::int16_t> pcm,
                arssyut::core::TimePoint relative,
                std::int64_t duration,
                bool boundary)
            {
                ++calls;
                samples = pcm.size();
                pts = relative.ticks_100ns;
                discontinuity = boundary;
                return duration > 0 ? Status::success() :
                    Status::failure(StatusCode::InvalidArgument);
            }
        } writer;
        AacPcm16SubmissionAdapter adapter;
        constexpr std::int64_t media_zero = 41'000'000;
        adapter.reset(media_zero);
        arssyut::core::audio::AudioProgramBlock block{};
        block.audio.clear();
        block.audio.media_start_100ns = media_zero;
        block.audio.samples[0] = 0.5F;
        const auto first = adapter.submit_to(writer, block, 0);
        check(first.submitted && writer.calls == 1 &&
                  writer.samples == 2048 && writer.pts == 0 &&
                  !writer.discontinuity,
              "integration writes first 1024-frame program block at media zero");
        block.audio.media_start_100ns = media_zero +
            static_cast<std::int64_t>(
                arssyut::core::audio::frames_to_ticks_floor(2048, 48'000));
        block.follows_output_discontinuity = true;
        const auto later = adapter.submit_to(writer, block, 2048);
        check(later.submitted && writer.calls == 2 &&
                  writer.discontinuity &&
                  writer.pts == static_cast<std::int64_t>(
                      arssyut::core::audio::frames_to_ticks_floor(2048, 48'000)),
              "integration preserves absolute gap and discontinuity to AAC");
        system.stop();
        mic.stop();
        check(system.stops == 1 && mic.stops == 1,
              "integration returns all source lifetimes to the one owner");

        // Negative cross-tranche regression: the second endpoint failing
        // must prevent a writer from being marked ready or consuming media.
        IntegratedSource failing_mic, failing_system;
        failing_system.result = Status::failure(
            StatusCode::PlatformFailure, 0x2480);
        AudioStartupGate failed_startup;
        const auto failed_ticket = failed_startup.begin({
            .microphone = true, .system_audio = true
        });
        const auto failure = start_pinned_audio_sources(
            pinned, failed_startup, failed_ticket,
            failing_mic, failing_system, [] { return false; });
        check(failure.code == StatusCode::PlatformFailure &&
                  failure.detail == 0x2480 &&
                  failing_mic.stops == 1 &&
                  failing_system.stops == 1 &&
                  failed_startup.state() == AudioStartupState::Failed &&
                  !failed_startup.writer_ready(failed_ticket),
              "failed dual-source startup rolls back before writer can Arm");
        check(writer.calls == 2,
              "failed source startup cannot emit a phantom AAC sample");
    }


    // Windows MSVC instantiates the same generic preparation function
    // against the real _WIN32 header set, without opening CI runner devices.
    struct FakeSource {
        int starts = 0;
        [[nodiscard]] Status start(std::wstring) {
            ++starts;
            return Status::success();
        }
        void stop() noexcept {}
    };
    FakeSource microphone;
    FakeSource loopback;
    arssyut::app::audio::AudioStartupGate gate;
    PinnedAudioEndpoints input{
        .microphone = true,
        .microphone_id = L"{already-pinned-capture}",
    };
    const auto ticket = gate.begin({.microphone = true});
    check(arssyut::app::audio::start_pinned_audio_sources(
              input, gate, ticket, microphone, loopback,
              [] { return false; }).ok() &&
              microphone.starts == 1 && loopback.starts == 0 &&
              gate.state() ==
                  arssyut::app::audio::AudioStartupState::Pending,
          "MSVC Preparing bridge starts only requested pinned WASAPI source");

#ifdef _WIN32
    // Compiles/links the real Windows COM resolver without relying on a
    // microphone or render endpoint on ephemeral CI runners.
    arssyut::app::audio::WindowsAudioEndpointLookup windows;
    PinnedAudioEndpoints video_only;
    check(resolve_audio_endpoints({}, windows, video_only).ok(),
          "video-only never starts an MMDevice COM lookup on Windows");
#endif
    if (failures != 0) {
        std::cerr << "FAIL: " << failures << '/' << checks
                  << " endpoint preflight checks\n";
        return 1;
    }
    std::cout << "PASS: " << checks
              << " endpoint preflight checks\n";
    return 0;
}
