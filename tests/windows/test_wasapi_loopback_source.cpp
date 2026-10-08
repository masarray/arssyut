#ifdef _WIN32

#include "core/audio/audio_format.hpp"
#include "core/result/status.hpp"
#include "platform/windows/audio/wasapi_capture_source.hpp"
#include "platform/windows/audio/wasapi_loopback_source.hpp"

#include <iostream>

namespace {

struct TestContext {
    int checks = 0;
    int failures = 0;

    void expect(bool condition, const char *message)
    {
        ++checks;
        if (!condition) {
            ++failures;
            std::cerr << "FAIL: " << message << '\n';
        }
    }
};

void test_mode_source_contract(TestContext &test)
{
    using namespace arssyut;

    windows::WasapiCaptureSource source;

    const auto wrong_pair =
        source.start(
            L"__not_used__",
            core::audio::AudioSourceId::Microphone,
            windows::WasapiCaptureMode::Loopback);

    test.expect(
        !wrong_pair.ok() &&
        wrong_pair.code == core::StatusCode::InvalidArgument,
        "Loopback mode cannot publish microphone source identity");

    const auto inverse_pair =
        source.start(
            L"__not_used__",
            core::audio::AudioSourceId::SystemAudio,
            windows::WasapiCaptureMode::Microphone);

    test.expect(
        !inverse_pair.ok() &&
        inverse_pair.code == core::StatusCode::InvalidArgument,
        "Microphone mode cannot publish system-audio identity");
}

void test_loopback_lifetime_without_hardware(TestContext &test)
{
    using namespace arssyut;

    windows::WasapiLoopbackSource source;

    test.expect(
        source.snapshot().state ==
            windows::WasapiLoopbackState::Idle,
        "Loopback source starts idle");

    const auto empty = source.start(L"");
    test.expect(
        !empty.ok() &&
        empty.code == core::StatusCode::InvalidArgument,
        "Empty render endpoint is rejected before worker creation");

    windows::WasapiPacketLease lease;
    test.expect(
        !source.try_pop(lease),
        "Loopback never synthesizes fake media when no endpoint is running");

    for (int cycle = 0; cycle < 8; ++cycle) {
        windows::WasapiLoopbackSource attempt;
        const auto status =
            attempt.start(
                L"__arssyut_missing_render_endpoint__");

        test.expect(
            !status.ok(),
            "Missing render endpoint fails as controlled startup error");

        attempt.stop();

        const auto snapshot = attempt.snapshot();
        test.expect(
            snapshot.state != windows::WasapiLoopbackState::Running &&
            snapshot.state != windows::WasapiLoopbackState::Starting &&
            snapshot.state != windows::WasapiLoopbackState::Stopping,
            "Failed loopback startup leaves no live worker state");

        windows::WasapiPacketLease stale;
        test.expect(
            !attempt.try_pop(stale),
            "Stopped loopback exposes no queued or synthetic silence packets");
    }
}

} // namespace

int main()
{
    TestContext test;

    test_mode_source_contract(test);
    test_loopback_lifetime_without_hardware(test);

    if (test.failures != 0) {
        std::cerr
            << test.failures << " of "
            << test.checks
            << " checks failed\n";
        return 1;
    }

    std::cout
        << "PASS: " << test.checks
        << " P7A3 WASAPI loopback checks\n";
    return 0;
}

#endif
