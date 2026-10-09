#include "app/audio_endpoint_preflight.hpp"

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
