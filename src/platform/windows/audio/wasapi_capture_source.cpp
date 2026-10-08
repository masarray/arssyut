#include "platform/windows/audio/wasapi_capture_source.hpp"

#ifdef _WIN32

#include "core/time/monotonic_clock.hpp"

#include <Windows.h>
#include <audioclient.h>
#include <avrt.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <utility>

namespace arssyut::windows {

namespace {

using Microsoft::WRL::ComPtr;
using core::Status;
using core::StatusCode;

[[nodiscard]] std::uint32_t hresult_detail(
    HRESULT hr) noexcept
{
    return static_cast<std::uint32_t>(hr);
}

[[nodiscard]] Status status_from_hresult(
    StatusCode code,
    HRESULT hr) noexcept
{
    return Status::failure(
        code,
        hresult_detail(hr));
}

[[nodiscard]] bool is_device_invalidation(
    HRESULT hr) noexcept
{
    return
        hr == AUDCLNT_E_DEVICE_INVALIDATED ||
        hr == AUDCLNT_E_RESOURCES_INVALIDATED;
}

class ScopedComMta final {
public:
    ScopedComMta() noexcept
        : hr_(CoInitializeEx(
              nullptr,
              COINIT_MULTITHREADED))
    {
        owns_ = hr_ == S_OK || hr_ == S_FALSE;
    }

    ~ScopedComMta()
    {
        if (owns_)
            CoUninitialize();
    }

    [[nodiscard]] HRESULT status() const noexcept
    {
        return hr_;
    }

private:
    HRESULT hr_ = E_FAIL;
    bool owns_ = false;
};

class ScopedMmcssAudio final {
public:
    ScopedMmcssAudio() noexcept
    {
        handle_ = AvSetMmThreadCharacteristicsW(
            L"Audio",
            &task_index_);
    }

    ~ScopedMmcssAudio()
    {
        if (handle_)
            AvRevertMmThreadCharacteristics(handle_);
    }

    [[nodiscard]] bool active() const noexcept
    {
        return handle_ != nullptr;
    }

private:
    DWORD task_index_ = 0;
    HANDLE handle_ = nullptr;
};

class ScopedHandle final {
public:
    explicit ScopedHandle(
        HANDLE handle = nullptr) noexcept
        : handle_(handle)
    {
    }

    ~ScopedHandle()
    {
        if (handle_)
            CloseHandle(handle_);
    }

    ScopedHandle(const ScopedHandle &) = delete;
    ScopedHandle &operator=(const ScopedHandle &) = delete;

    [[nodiscard]] HANDLE get() const noexcept
    {
        return handle_;
    }

    [[nodiscard]] bool valid() const noexcept
    {
        return handle_ != nullptr;
    }

private:
    HANDLE handle_ = nullptr;
};

struct CoTaskMemWaveDeleter {
    void operator()(
        WAVEFORMATEX *value) const noexcept
    {
        if (value)
            CoTaskMemFree(value);
    }
};

[[nodiscard]] std::span<const std::byte>
packet_bytes(
    const std::byte *data,
    const core::audio::AudioFormat &format,
    std::uint32_t frames,
    core::audio::AudioPacketFlag flags) noexcept
{
    if (core::audio::has_flag(
            flags,
            core::audio::AudioPacketFlag::Silent))
        return {};

    if (data == nullptr)
        return {};

    const auto bytes =
        format.bytes_for_frames(frames);
    return {
        data,
        bytes};
}

class NativeWasapiCaptureClient final
    : public IWasapiCaptureClient {
public:
    ~NativeWasapiCaptureClient() override
    {
        stop();

        if (audio_event_ != nullptr) {
            CloseHandle(audio_event_);
            audio_event_ = nullptr;
        }
    }

    [[nodiscard]] Status open(
        std::wstring_view endpoint_id,
        WasapiCaptureMode mode) noexcept override
    {
        try {
            std::wstring endpoint_key(endpoint_id);

            std::shared_ptr<IWasapiCaptureClient> client =
        client_override_;

    if (!client) {
        try {
            client =
                std::make_shared<
                    NativeWasapiCaptureClient>();
        } catch (const std::bad_alloc &) {
            fail(
                StatusCode::CapacityExceeded,
                E_OUTOFMEMORY);
            return;
        } catch (...) {
            fail(
                StatusCode::InternalError,
                E_FAIL);
            return;
        }
    }

    const Status open_status =
        client->open(
            request.endpoint_id,
            request.mode);
    if (!open_status.ok()) {
        const HRESULT open_hr =
            open_status.detail != 0
                ? static_cast<HRESULT>(
                      open_status.detail)
                : E_FAIL;
        fail(
            open_status.code,
            open_hr);
        return;
    }

    const auto parsed_format =
        client->native_format();
    const std::uint32_t buffer_frames =
        client->endpoint_buffer_frames();

    if (!parsed_format.valid() ||
        buffer_frames == 0) {
        fail(
            StatusCode::Unsupported,
            AUDCLNT_E_UNSUPPORTED_FORMAT);
        return;
    }

    const std::size_t bytes_per_slot =
        parsed_format.bytes_for_frames(
            buffer_frames);
    if (bytes_per_slot == 0 ||
        bytes_per_slot >
            static_cast<std::size_t>(
                std::numeric_limits<
                    std::uint32_t>::max())) {
        fail(
            StatusCode::CapacityExceeded,
            E_INVALIDARG);
        return;
    }

    std::shared_ptr<WasapiPacketHandoff> handoff;
    try {
        handoff =
            std::make_shared<
                WasapiPacketHandoff>(
                    request.options.queue_capacity,
                    request.options.packet_pool_capacity,
                    bytes_per_slot);
    } catch (const std::bad_alloc &) {
        fail(
            StatusCode::CapacityExceeded,
            E_OUTOFMEMORY);
        return;
    } catch (...) {
        fail(
            StatusCode::InternalError,
            E_FAIL);
        return;
    }

    if (!handoff->valid()) {
        fail(
            StatusCode::CapacityExceeded,
            E_OUTOFMEMORY);
        return;
    }

    {
        std::lock_guard metadata_lock(
            metadata_mutex_);
        native_format_ =
            parsed_format;
        endpoint_buffer_frames_ =
            buffer_frames;
    }

    handoff_ =
        std::move(handoff);
    handoff_view_.store(
        handoff_,
        std::memory_order_release);

    WasapiTimestampClassifier timing(
        parsed_format.sample_rate);

    if (stop_requested()) {
        mmcss_active_.store(
            false,
            std::memory_order_release);
        state_.store(
            WasapiCaptureState::Stopped,
            std::memory_order_release);
        (void)signal_startup(
            Status::failure(
                StatusCode::InvalidStateTransition));
        return;
    }

    const Status client_start =
        client->start();
    if (!client_start.ok()) {
        const HRESULT start_hr =
            client_start.detail != 0
                ? static_cast<HRESULT>(
                      client_start.detail)
                : E_FAIL;
        fail(
            client_start.code,
            start_hr);
        return;
    }

    // signal_startup() checks the cancellation flag under the startup
    // publication mutex. If stop raced IAudioClient::Start, startup is reported
    // as canceled rather than briefly claiming a dead microphone is Running.
    if (!signal_startup(
            Status::success())) {
        client->stop();
        mmcss_active_.store(
            false,
            std::memory_order_release);
        return;
    }
    startup_signaled = true;

    bool terminal = false;

    while (!terminal) {
        HRESULT wait_hr = S_OK;
        const auto wait_result =
            client->wait(
                stop_event_,
                wait_hr);

        if (wait_result ==
            WasapiCaptureWaitResult::StopRequested)
            break;

        if (wait_result ==
            WasapiCaptureWaitResult::Failed) {
            fail(
                StatusCode::PlatformFailure,
                FAILED(wait_hr)
                    ? wait_hr
                    : E_FAIL);
            break;
        }

        while (!terminal) {
            // Stop is authoritative even while one WASAPI event wake is
            // draining multiple packets. Do not keep consuming an arbitrarily
            // replenished endpoint after teardown has begun.
            if (stop_requested()) {
                terminal = true;
                break;
            }

            std::uint32_t next_frames = 0;
            const HRESULT next_hr =
                client->next_packet_size(
                    next_frames);
            if (FAILED(next_hr)) {
                fail(
                    StatusCode::PlatformFailure,
                    next_hr);
                terminal = true;
                break;
            }

            if (next_frames == 0)
                break;

            WasapiCapturePacketView captured;
            const HRESULT get_hr =
                client->get_packet(
                    captured);

            if (get_hr == AUDCLNT_S_BUFFER_EMPTY)
                break;

            if (FAILED(get_hr)) {
                fail(
                    StatusCode::PlatformFailure,
                    get_hr);
                terminal = true;
                break;
            }

            const std::uint32_t frame_count =
                captured.frame_count;

            // A stop request may race GetBuffer after the loop-level check.
            // Return the acquired endpoint buffer immediately and publish no
            // post-stop media.
            if (stop_requested()) {
                const HRESULT release_hr =
                    client->release_packet(
                        frame_count);
                if (FAILED(release_hr)) {
                    fail(
                        StatusCode::PlatformFailure,
                        release_hr);
                }
                terminal = true;
                break;
            }

            auto packet_flags =
                audio_packet_flags_from_wasapi(
                    captured.flags);

            captured_packets_.fetch_add(
                1,
                std::memory_order_relaxed);

            if (core::audio::has_flag(
                    packet_flags,
                    core::audio::AudioPacketFlag::Silent)) {
                silent_packets_.fetch_add(
                    1,
                    std::memory_order_relaxed);
            }
            if (core::audio::has_flag(
                    packet_flags,
                    core::audio::AudioPacketFlag::TimestampError)) {
                timestamp_error_packets_.fetch_add(
                    1,
                    std::memory_order_relaxed);
            }

            const auto host_now =
                core::MonotonicClock::now();

            const auto timing_evidence =
                timing.observe(
                    frame_count,
                    captured.device_position,
                    captured.qpc_position_100ns,
                    host_now.ticks_100ns,
                    packet_flags);

            // The classifier may discover an implicit clock-epoch reset even
            // when WASAPI omitted DATA_DISCONTINUITY. Promote that evidence to
            // the packet flag contract so the mixer/drift owner can reset its
            // retained estimator before accepting anchors from the new epoch.
            if (timing_evidence.quality ==
                    core::audio::AudioTimestampQuality::Discontinuous &&
                !core::audio::has_flag(
                    packet_flags,
                    core::audio::AudioPacketFlag::Discontinuity)) {
                packet_flags |=
                    core::audio::AudioPacketFlag::Discontinuity;
            }

            if (core::audio::has_flag(
                    packet_flags,
                    core::audio::AudioPacketFlag::Discontinuity)) {
                discontinuity_packets_.fetch_add(
                    1,
                    std::memory_order_relaxed);
            }

            core::audio::AudioSourcePacket packet{
                .source =
                    request.source_id,
                .native_format =
                    parsed_format,
                .frame_count =
                    frame_count,
                .timing =
                    timing_evidence,
                .flags =
                    packet_flags,
            };

            const auto bytes =
                packet_bytes(
                    captured.data,
                    parsed_format,
                    frame_count,
                    packet_flags);

            WasapiPacketPublishResult publish_result =
                WasapiPacketPublishResult::InvalidPacket;

            if (frame_count != 0 &&
                (core::audio::has_flag(
                     packet_flags,
                     core::audio::AudioPacketFlag::Silent) ||
                 !bytes.empty())) {
                publish_result =
                    handoff_->publish(
                        packet,
                        bytes);
            }

            if (publish_result ==
                WasapiPacketPublishResult::Published) {
                published_packets_.fetch_add(
                    1,
                    std::memory_order_relaxed);
            } else if (
                publish_result ==
                    WasapiPacketPublishResult::PoolExhausted ||
                publish_result ==
                    WasapiPacketPublishResult::QueueFull) {
                dropped_packets_.fetch_add(
                    1,
                    std::memory_order_relaxed);
            } else {
                set_terminal_error(
                    E_INVALIDARG,
                    false);
                terminal = true;
            }

            const HRESULT release_hr =
                client->release_packet(
                    frame_count);
            if (FAILED(release_hr)) {
                fail(
                    StatusCode::PlatformFailure,
                    release_hr);
                terminal = true;
            }
        }
    }

    client->stop();

    mmcss_active_.store(
        false,
        std::memory_order_release);

    const auto final_state =
        state_.load(
            std::memory_order_acquire);
    if (final_state !=
            WasapiCaptureState::DeviceInvalidated &&
        final_state !=
            WasapiCaptureState::Failed) {
        state_.store(
            WasapiCaptureState::Stopped,
            std::memory_order_release);
    }
}

} // namespace arssyut::windows

#endif
