#ifdef _WIN32

#include "core/audio/audio_format.hpp"
#include "core/result/status.hpp"
#include "core/time/monotonic_clock.hpp"
#include "platform/windows/audio/wasapi_capture_source.hpp"
#include "platform/windows/audio/wasapi_loopback_source.hpp"

#include <Windows.h>
#include <audioclient.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string_view>

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

class FakeLoopbackCaptureClient final
    : public arssyut::windows::IWasapiCaptureClient {
public:
    FakeLoopbackCaptureClient()
    {
        released_event_ =
            CreateEventW(
                nullptr,
                TRUE,
                FALSE,
                nullptr);
    }

    ~FakeLoopbackCaptureClient() override
    {
        if (released_event_ != nullptr)
            CloseHandle(released_event_);
    }

    [[nodiscard]] arssyut::core::Status open(
        std::wstring_view endpoint_id,
        arssyut::windows::WasapiCaptureMode mode) noexcept override
    {
        open_called_.store(
            true,
            std::memory_order_release);
        loopback_mode_.store(
            mode ==
                arssyut::windows::WasapiCaptureMode::Loopback,
            std::memory_order_release);

        if (endpoint_id !=
                L"__arssyut_fake_render_endpoint__" ||
            mode !=
                arssyut::windows::WasapiCaptureMode::Loopback) {
            return arssyut::core::Status::failure(
                arssyut::core::StatusCode::InvalidArgument);
        }

        opened_.store(
            true,
            std::memory_order_release);
        return arssyut::core::Status::success();
    }

    [[nodiscard]] arssyut::core::audio::AudioFormat
    native_format() const noexcept override
    {
        return {
            .sample_rate = 48'000,
            .sample_type =
                arssyut::core::audio::AudioSampleType::Float32,
            .channels = 2,
            .container_bits_per_sample = 32,
            .valid_bits_per_sample = 32,
            .channel_mask =
                arssyut::core::audio::kStereoChannelMask,
            .block_align = 8,
        };
    }

    [[nodiscard]] std::uint32_t
    endpoint_buffer_frames() const noexcept override
    {
        return kFrames;
    }

    [[nodiscard]] arssyut::core::Status start() noexcept override
    {
        if (!opened_.load(
                std::memory_order_acquire)) {
            return arssyut::core::Status::failure(
                arssyut::core::StatusCode::InvalidStateTransition);
        }

        start_called_.store(
            true,
            std::memory_order_release);
        return arssyut::core::Status::success();
    }

    [[nodiscard]] arssyut::windows::WasapiCaptureWaitResult wait(
        HANDLE stop_event,
        HRESULT &failure_hr) noexcept override
    {
        if (!packet_wake_sent_) {
            packet_wake_sent_ = true;
            return arssyut::windows::
                WasapiCaptureWaitResult::PacketReady;
        }

        const DWORD result =
            WaitForSingleObject(
                stop_event,
                INFINITE);
        if (result == WAIT_OBJECT_0) {
            return arssyut::windows::
                WasapiCaptureWaitResult::StopRequested;
        }

        failure_hr =
            HRESULT_FROM_WIN32(
                GetLastError());
        return arssyut::windows::
            WasapiCaptureWaitResult::Failed;
    }

    [[nodiscard]] HRESULT next_packet_size(
        std::uint32_t &frames) noexcept override
    {
        frames = packet_available_
            ? kFrames
            : 0;
        return S_OK;
    }

    [[nodiscard]] HRESULT get_packet(
        arssyut::windows::WasapiCapturePacketView &packet) noexcept override
    {
        if (!packet_available_)
            return AUDCLNT_S_BUFFER_EMPTY;

        const auto now =
            arssyut::core::MonotonicClock::now();

        packet.data =
            reinterpret_cast<const std::byte *>(
                samples_.data());
        packet.frame_count =
            kFrames;
        packet.flags =
            AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY;
        packet.device_position = 0;
        packet.qpc_position_100ns =
            static_cast<std::uint64_t>(
                now.ticks_100ns);
        return S_OK;
    }

    [[nodiscard]] HRESULT release_packet(
        std::uint32_t frames) noexcept override
    {
        if (!packet_available_ ||
            frames != kFrames) {
            return E_INVALIDARG;
        }

        packet_available_ = false;

        if (released_event_ != nullptr)
            SetEvent(released_event_);

        return S_OK;
    }

    void stop() noexcept override
    {
        stop_called_.store(
            true,
            std::memory_order_release);
    }

    [[nodiscard]] bool wait_until_packet_released() const noexcept
    {
        return released_event_ != nullptr &&
               WaitForSingleObject(
                   released_event_,
                   5'000) == WAIT_OBJECT_0;
    }

    [[nodiscard]] bool open_called() const noexcept
    {
        return open_called_.load(
            std::memory_order_acquire);
    }

    [[nodiscard]] bool loopback_mode() const noexcept
    {
        return loopback_mode_.load(
            std::memory_order_acquire);
    }

    [[nodiscard]] bool start_called() const noexcept
    {
        return start_called_.load(
            std::memory_order_acquire);
    }

    [[nodiscard]] bool stop_called() const noexcept
    {
        return stop_called_.load(
            std::memory_order_acquire);
    }

private:
    static constexpr std::uint32_t kFrames = 480;

    std::array<float, kFrames * 2> samples_{};
    HANDLE released_event_ = nullptr;
    bool packet_wake_sent_ = false;
    bool packet_available_ = true;

    std::atomic<bool> opened_{false};
    std::atomic<bool> open_called_{false};
    std::atomic<bool> loopback_mode_{false};
    std::atomic<bool> start_called_{false};
    std::atomic<bool> stop_called_{false};
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

void test_running_loopback_worker(
    TestContext &test)
{
    using namespace arssyut;

    auto fake =
        std::make_shared<
            FakeLoopbackCaptureClient>();
    windows::WasapiLoopbackSource source{
        fake};

    const auto status =
        source.start(
            L"__arssyut_fake_render_endpoint__");

    test.expect(
        status.ok(),
        "Injected render endpoint reaches Running loopback path");
    test.expect(
        fake->open_called() &&
        fake->loopback_mode() &&
        fake->start_called(),
        "Loopback wrapper opens and starts the injected endpoint client in loopback mode");

    test.expect(
        fake->wait_until_packet_released(),
        "Running worker drains and releases the injected endpoint packet");

    windows::WasapiPacketLease lease;
    test.expect(
        source.try_pop(lease) &&
        lease.valid(),
        "Running loopback publishes one bounded packet lease");

    if (lease.valid()) {
        const auto &packet =
            lease.packet();

        test.expect(
            packet.source ==
                core::audio::AudioSourceId::SystemAudio,
            "Loopback packet is tagged as SystemAudio");
        test.expect(
            packet.frame_count == 480,
            "Loopback packet preserves endpoint frame count");
        test.expect(
            core::audio::has_flag(
                packet.flags,
                core::audio::AudioPacketFlag::Discontinuity),
            "Loopback packet preserves discontinuity evidence");
        test.expect(
            lease.payload().size() ==
                static_cast<std::size_t>(
                    480 * 2 * sizeof(float)),
            "Loopback lease retains exact copied endpoint payload");
    }

    const auto running_snapshot =
        source.snapshot();
    test.expect(
        running_snapshot.state ==
                windows::WasapiLoopbackState::Running &&
        running_snapshot.captured_packets == 1 &&
        running_snapshot.published_packets == 1 &&
        running_snapshot.queue_high_water == 1 &&
        running_snapshot.pool_high_water == 1,
        "Running loopback exposes bounded capture and handoff telemetry");

    // Retain the lease across Stop to prove source teardown retires its current
    // generation without invalidating consumer-owned packet storage.
    source.stop();

    test.expect(
        fake->stop_called(),
        "Loopback Stop reaches the injected endpoint client");

    const auto stopped_snapshot =
        source.snapshot();
    test.expect(
        stopped_snapshot.state ==
                windows::WasapiLoopbackState::Stopped &&
        stopped_snapshot.queue_depth == 0 &&
        stopped_snapshot.queue_high_water == 1 &&
        stopped_snapshot.pool_high_water == 1,
        "Loopback Stop joins the worker and preserves terminal pressure evidence");

    test.expect(
        lease.valid() &&
        lease.payload().size() ==
            static_cast<std::size_t>(
                480 * 2 * sizeof(float)),
        "Packet lease safely pins its bounded pool generation across Stop");

    test.expect(
        lease.release(),
        "Retained loopback lease releases into its original pool generation");
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
    test_running_loopback_worker(test);
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
