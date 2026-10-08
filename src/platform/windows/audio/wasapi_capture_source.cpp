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
    const BYTE *data,
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
        reinterpret_cast<const std::byte *>(data),
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

            ComPtr<IMMDeviceEnumerator> enumerator;
            HRESULT hr =
                CoCreateInstance(
                    __uuidof(MMDeviceEnumerator),
                    nullptr,
                    CLSCTX_ALL,
                    IID_PPV_ARGS(
                        enumerator.GetAddressOf()));
            if (FAILED(hr))
                return status_from_hresult(
                    StatusCode::PlatformFailure,
                    hr);

            ComPtr<IMMDevice> endpoint;
            hr = enumerator->GetDevice(
                endpoint_key.c_str(),
                endpoint.GetAddressOf());
            if (FAILED(hr))
                return status_from_hresult(
                    StatusCode::PlatformFailure,
                    hr);

            DWORD endpoint_state = 0;
            hr = endpoint->GetState(
                &endpoint_state);
            if (FAILED(hr) ||
                (endpoint_state &
                 DEVICE_STATE_ACTIVE) == 0) {
                return status_from_hresult(
                    StatusCode::PlatformFailure,
                    FAILED(hr)
                        ? hr
                        : AUDCLNT_E_DEVICE_INVALIDATED);
            }

            ComPtr<IMMEndpoint> endpoint_flow;
            hr = endpoint.As(
                &endpoint_flow);
            if (FAILED(hr))
                return status_from_hresult(
                    StatusCode::PlatformFailure,
                    hr);

            EDataFlow data_flow = eAll;
            hr = endpoint_flow->GetDataFlow(
                &data_flow);
            const EDataFlow expected_flow =
                mode == WasapiCaptureMode::Loopback
                    ? eRender
                    : eCapture;
            if (FAILED(hr) ||
                data_flow != expected_flow) {
                return status_from_hresult(
                    StatusCode::InvalidArgument,
                    FAILED(hr)
                        ? hr
                        : E_INVALIDARG);
            }

            hr = endpoint->Activate(
                __uuidof(IAudioClient),
                CLSCTX_ALL,
                nullptr,
                reinterpret_cast<void **>(
                    audio_client_.GetAddressOf()));
            if (FAILED(hr))
                return status_from_hresult(
                    StatusCode::PlatformFailure,
                    hr);

            WAVEFORMATEX *raw_mix = nullptr;
            hr = audio_client_->GetMixFormat(
                &raw_mix);
            if (FAILED(hr) ||
                raw_mix == nullptr) {
                return status_from_hresult(
                    StatusCode::Unsupported,
                    FAILED(hr)
                        ? hr
                        : E_FAIL);
            }

            std::unique_ptr<
                WAVEFORMATEX,
                CoTaskMemWaveDeleter>
                mix_format(raw_mix);

            const auto parsed =
                audio_format_from_wave_format(
                    mix_format.get());
            if (!parsed.has_value()) {
                return status_from_hresult(
                    StatusCode::Unsupported,
                    AUDCLNT_E_UNSUPPORTED_FORMAT);
            }

            audio_event_ =
                CreateEventW(
                    nullptr,
                    FALSE,
                    FALSE,
                    nullptr);
            if (audio_event_ == nullptr) {
                return status_from_hresult(
                    StatusCode::PlatformFailure,
                    HRESULT_FROM_WIN32(
                        GetLastError()));
            }

            const DWORD stream_flags =
                AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
                AUDCLNT_STREAMFLAGS_NOPERSIST |
                (mode == WasapiCaptureMode::Loopback
                     ? AUDCLNT_STREAMFLAGS_LOOPBACK
                     : 0U);

            hr = audio_client_->Initialize(
                AUDCLNT_SHAREMODE_SHARED,
                stream_flags,
                0,
                0,
                mix_format.get(),
                nullptr);
            if (FAILED(hr))
                return status_from_hresult(
                    StatusCode::PlatformFailure,
                    hr);

            hr = audio_client_->SetEventHandle(
                audio_event_);
            if (FAILED(hr))
                return status_from_hresult(
                    StatusCode::PlatformFailure,
                    hr);

            hr = audio_client_->GetBufferSize(
                &buffer_frames_);
            if (FAILED(hr) ||
                buffer_frames_ == 0) {
                return status_from_hresult(
                    StatusCode::PlatformFailure,
                    FAILED(hr)
                        ? hr
                        : E_FAIL);
            }

            hr = audio_client_->GetService(
                IID_PPV_ARGS(
                    capture_client_.GetAddressOf()));
            if (FAILED(hr))
                return status_from_hresult(
                    StatusCode::PlatformFailure,
                    hr);

            format_ = *parsed;
            return Status::success();
        } catch (const std::bad_alloc &) {
            return Status::failure(
                StatusCode::CapacityExceeded,
                static_cast<std::uint32_t>(
                    E_OUTOFMEMORY));
        } catch (...) {
            return Status::failure(
                StatusCode::InternalError,
                static_cast<std::uint32_t>(
                    E_FAIL));
        }
    }

    [[nodiscard]] core::audio::AudioFormat
    native_format() const noexcept override
    {
        return format_;
    }

    [[nodiscard]] std::uint32_t
    endpoint_buffer_frames() const noexcept override
    {
        return buffer_frames_;
    }

    [[nodiscard]] Status start() noexcept override
    {
        if (!audio_client_)
            return Status::failure(
                StatusCode::InvalidStateTransition);

        const HRESULT hr =
            audio_client_->Start();
        if (FAILED(hr))
            return status_from_hresult(
                StatusCode::PlatformFailure,
                hr);

        started_ = true;
        return Status::success();
    }

    [[nodiscard]] WasapiCaptureWaitResult wait(
        HANDLE stop_event,
        HRESULT &failure_hr) noexcept override
    {
        HANDLE waits[2]{
            stop_event,
            audio_event_};

        const DWORD wait_result =
            WaitForMultipleObjects(
                2,
                waits,
                FALSE,
                INFINITE);

        if (wait_result == WAIT_OBJECT_0)
            return WasapiCaptureWaitResult::StopRequested;

        if (wait_result == WAIT_OBJECT_0 + 1)
            return WasapiCaptureWaitResult::PacketReady;

        failure_hr =
            HRESULT_FROM_WIN32(
                GetLastError());
        return WasapiCaptureWaitResult::Failed;
    }

    [[nodiscard]] HRESULT next_packet_size(
        std::uint32_t &frames) noexcept override
    {
        if (!capture_client_)
            return E_POINTER;

        UINT32 next = 0;
        const HRESULT hr =
            capture_client_->GetNextPacketSize(
                &next);
        frames = next;
        return hr;
    }

    [[nodiscard]] HRESULT get_packet(
        WasapiCapturePacketView &packet) noexcept override
    {
        if (!capture_client_)
            return E_POINTER;

        BYTE *data = nullptr;
        UINT32 frame_count = 0;
        DWORD flags = 0;
        UINT64 device_position = 0;
        UINT64 qpc_position = 0;

        const HRESULT hr =
            capture_client_->GetBuffer(
                &data,
                &frame_count,
                &flags,
                &device_position,
                &qpc_position);
        if (FAILED(hr))
            return hr;

        packet.data =
            reinterpret_cast<const std::byte *>(
                data);
        packet.frame_count =
            frame_count;
        packet.flags =
            flags;
        packet.device_position =
            device_position;
        packet.qpc_position_100ns =
            qpc_position;
        return S_OK;
    }

    [[nodiscard]] HRESULT release_packet(
        std::uint32_t frames) noexcept override
    {
        return capture_client_
            ? capture_client_->ReleaseBuffer(
                  frames)
            : E_POINTER;
    }

    void stop() noexcept override
    {
        if (started_ &&
            audio_client_) {
            (void)audio_client_->Stop();
        }
        started_ = false;
    }

private:
    ComPtr<IAudioClient> audio_client_;
    ComPtr<IAudioCaptureClient> capture_client_;
    HANDLE audio_event_ = nullptr;
    core::audio::AudioFormat format_{};
    std::uint32_t buffer_frames_ = 0;
    bool started_ = false;
};

} // namespace

WasapiCaptureSource::~WasapiCaptureSource()
{
    stop();
}

void WasapiCaptureSource::retire_handoff() noexcept
{
    auto retired =
        handoff_view_.exchange(
            std::shared_ptr<WasapiPacketHandoff>{},
            std::memory_order_acq_rel);
    if (!retired)
        retired = handoff_;

    if (retired) {
        terminal_queue_high_water_.store(
            retired->queue_high_water(),
            std::memory_order_release);
        terminal_pool_high_water_.store(
            retired->pool_high_water(),
            std::memory_order_release);
        terminal_pool_exhaustions_.store(
            retired->pool_exhaustions(),
            std::memory_order_release);
        terminal_ring_overflows_.store(
            retired->ring_overflows(),
            std::memory_order_release);
    }

    handoff_.reset();
}

void WasapiCaptureSource::reset_terminal_handoff_telemetry() noexcept
{
    terminal_queue_high_water_.store(
        0,
        std::memory_order_release);
    terminal_pool_high_water_.store(
        0,
        std::memory_order_release);
    terminal_pool_exhaustions_.store(
        0,
        std::memory_order_release);
    terminal_ring_overflows_.store(
        0,
        std::memory_order_release);
}

Status WasapiCaptureSource::prepare_for_start() noexcept
{
    // Retire the previous generation atomically. Existing consumer leases keep
    // that exact handoff/pool alive until they release; no control-plane drain
    // races the SPSC consumer and no packet can be released into a new pool.
    retire_handoff();
    reset_terminal_handoff_telemetry();

    {
        std::lock_guard metadata_lock(
            metadata_mutex_);
        native_format_ = {};
        endpoint_buffer_frames_ = 0;
    }

    last_hresult_.store(
        0,
        std::memory_order_release);
    mmcss_active_.store(
        false,
        std::memory_order_release);

    captured_packets_.store(
        0,
        std::memory_order_release);
    published_packets_.store(
        0,
        std::memory_order_release);
    silent_packets_.store(
        0,
        std::memory_order_release);
    discontinuity_packets_.store(
        0,
        std::memory_order_release);
    timestamp_error_packets_.store(
        0,
        std::memory_order_release);
    dropped_packets_.store(
        0,
        std::memory_order_release);

    return Status::success();
}

Status WasapiCaptureSource::start(
    std::wstring endpoint_id,
    core::audio::AudioSourceId source_id,
    WasapiCaptureMode mode,
    WasapiCaptureOptions options)
{
    std::unique_lock lifecycle_lock(
        lifecycle_mutex_);

    if (start_in_progress_ ||
        worker_.joinable())
        return Status::failure(
            StatusCode::InvalidStateTransition);

    const bool mode_matches_source =
        (mode == WasapiCaptureMode::Microphone &&
         source_id == core::audio::AudioSourceId::Microphone) ||
        (mode == WasapiCaptureMode::Loopback &&
         source_id == core::audio::AudioSourceId::SystemAudio);

    if (endpoint_id.empty() ||
        !options.valid() ||
        !mode_matches_source)
        return Status::failure(
            StatusCode::InvalidArgument);

    const Status reset_status =
        prepare_for_start();
    if (!reset_status.ok())
        return reset_status;

    if (stop_event_ != nullptr) {
        CloseHandle(stop_event_);
        stop_event_ = nullptr;
    }

    stop_event_ =
        CreateEventW(
            nullptr,
            TRUE,
            FALSE,
            nullptr);
    if (stop_event_ == nullptr)
        return status_from_hresult(
            StatusCode::PlatformFailure,
            HRESULT_FROM_WIN32(GetLastError()));

    {
        std::lock_guard startup_lock(
            startup_mutex_);
        startup_done_ = false;
        startup_status_ =
            Status::success();
    }
    startup_cancel_requested_.store(
        false,
        std::memory_order_release);
    start_in_progress_ = true;

    state_.store(
        WasapiCaptureState::Starting,
        std::memory_order_release);

    StartRequest request{
        .endpoint_id = std::move(endpoint_id),
        .source_id = source_id,
        .mode = mode,
        .options = options,
    };

    try {
        worker_ = std::thread(
            [this, request = std::move(request)]() mutable noexcept {
                worker_main(std::move(request));
            });
    } catch (...) {
        start_in_progress_ = false;
        CloseHandle(stop_event_);
        stop_event_ = nullptr;
        state_.store(
            WasapiCaptureState::Failed,
            std::memory_order_release);
        return Status::failure(
            StatusCode::InternalError);
    }

    lifecycle_lock.unlock();

    {
        std::unique_lock startup_lock(
            startup_mutex_);
        startup_cv_.wait(
            startup_lock,
            [this]() noexcept {
                return startup_done_;
            });
    }

    Status status =
        startup_status_;

    // A start call owns its startup generation until this cleanup completes.
    // This prevents a later start from reusing worker/event/handoff state while
    // a concurrent stop is still joining the previous worker.
    std::unique_lock relock(
        lifecycle_mutex_);

    // A concurrent stop may legitimately win after native startup publication
    // but before the initiating start() call has returned. Do not hand a stale
    // success back to the caller in that case.
    if (status.ok() &&
        (startup_cancel_requested_.load(
             std::memory_order_acquire) ||
         state_.load(
             std::memory_order_acquire) !=
             WasapiCaptureState::Running)) {
        status =
            Status::failure(
                StatusCode::InvalidStateTransition);
    }

    if (!status.ok()) {
        request_stop();

        if (worker_.joinable())
            worker_.join();

        if (stop_event_ != nullptr) {
            CloseHandle(stop_event_);
            stop_event_ = nullptr;
        }

        retire_handoff();
    }

    start_in_progress_ = false;
    return status;
}

void WasapiCaptureSource::stop() noexcept
{
    std::unique_lock lifecycle_lock(
        lifecycle_mutex_);

    if (!worker_.joinable()) {
        const auto state =
            state_.load(
                std::memory_order_acquire);
        if (state == WasapiCaptureState::Running ||
            state == WasapiCaptureState::Starting ||
            state == WasapiCaptureState::Stopping) {
            state_.store(
                WasapiCaptureState::Stopped,
                std::memory_order_release);
        }

        if (stop_event_ != nullptr) {
            CloseHandle(stop_event_);
            stop_event_ = nullptr;
        }

        retire_handoff();
        return;
    }

    const auto state_before_stop =
        state_.load(
            std::memory_order_acquire);
    if (state_before_stop !=
            WasapiCaptureState::DeviceInvalidated &&
        state_before_stop !=
            WasapiCaptureState::Failed) {
        state_.store(
            WasapiCaptureState::Stopping,
            std::memory_order_release);
    }
    request_stop();

    // Worker teardown is a control-plane operation. Keep the lifecycle mutex
    // held while joining so another start/stop cannot replace the event or
    // handoff while the old worker still owns them.
    worker_.join();

    const auto state =
        state_.load(
            std::memory_order_acquire);
    if (state != WasapiCaptureState::DeviceInvalidated &&
        state != WasapiCaptureState::Failed) {
        state_.store(
            WasapiCaptureState::Stopped,
            std::memory_order_release);
    }

    if (stop_event_ != nullptr) {
        CloseHandle(stop_event_);
        stop_event_ = nullptr;
    }

    // Stop completes producer shutdown before retiring the published
    // generation. Already-popped packet leases keep their exact old pool alive,
    // while queued stale media and the source-owned pool are released now.
    retire_handoff();
}

void WasapiCaptureSource::request_stop() noexcept
{
    startup_cancel_requested_.store(
        true,
        std::memory_order_release);

    if (stop_event_ != nullptr)
        SetEvent(stop_event_);
}

bool WasapiCaptureSource::stop_requested() const noexcept
{
    return
        stop_event_ != nullptr &&
        WaitForSingleObject(
            stop_event_,
            0) == WAIT_OBJECT_0;
}

bool WasapiCaptureSource::signal_startup(
    Status status) noexcept
{
    bool accepted_success = false;

    {
        std::lock_guard lock(
            startup_mutex_);
        if (startup_done_)
            return false;

        if (status.ok() &&
            startup_cancel_requested_.load(
                std::memory_order_acquire)) {
            startup_status_ =
                Status::failure(
                    StatusCode::InvalidStateTransition);
            state_.store(
                WasapiCaptureState::Stopped,
                std::memory_order_release);
        } else {
            startup_status_ = status;
            accepted_success = status.ok();
            if (accepted_success) {
                state_.store(
                    WasapiCaptureState::Running,
                    std::memory_order_release);
            }
        }

        startup_done_ = true;
    }

    startup_cv_.notify_all();
    return accepted_success;
}

void WasapiCaptureSource::set_terminal_error(
    HRESULT hr,
    bool device_invalidated) noexcept
{
    last_hresult_.store(
        hresult_detail(hr),
        std::memory_order_release);

    state_.store(
        device_invalidated
            ? WasapiCaptureState::DeviceInvalidated
            : WasapiCaptureState::Failed,
        std::memory_order_release);
}

bool WasapiCaptureSource::try_pop(
    WasapiPacketLease &lease) noexcept
{
    auto handoff =
        handoff_view_.load(
            std::memory_order_acquire);
    if (!handoff)
        return false;

    core::audio::AudioSourcePacket packet;
    if (!handoff->try_pop(packet))
        return false;

    lease =
        WasapiPacketLease{
            std::move(handoff),
            packet};
    return true;
}

WasapiCaptureSnapshot
WasapiCaptureSource::snapshot() const noexcept
{
    // Snapshot is control-plane telemetry, so it may serialize with start/stop
    // to keep the handoff owner pointer alive while high-water counters are read.
    std::lock_guard lifecycle_lock(
        lifecycle_mutex_);

    WasapiCaptureSnapshot result;
    result.state =
        state_.load(
            std::memory_order_acquire);
    result.last_hresult =
        last_hresult_.load(
            std::memory_order_acquire);
    result.mmcss_active =
        mmcss_active_.load(
            std::memory_order_acquire);

    {
        std::lock_guard metadata_lock(
            metadata_mutex_);
        result.native_format =
            native_format_;
        result.endpoint_buffer_frames =
            endpoint_buffer_frames_;
    }

    result.captured_packets =
        captured_packets_.load(
            std::memory_order_relaxed);
    result.published_packets =
        published_packets_.load(
            std::memory_order_relaxed);
    result.silent_packets =
        silent_packets_.load(
            std::memory_order_relaxed);
    result.discontinuity_packets =
        discontinuity_packets_.load(
            std::memory_order_relaxed);
    result.timestamp_error_packets =
        timestamp_error_packets_.load(
            std::memory_order_relaxed);
    result.dropped_packets =
        dropped_packets_.load(
            std::memory_order_relaxed);

    auto handoff =
        handoff_view_.load(
            std::memory_order_acquire);
    if (handoff) {
        result.queue_depth =
            handoff->queue_depth_approx();
        result.queue_high_water =
            handoff->queue_high_water();
        result.pool_high_water =
            handoff->pool_high_water();
        result.pool_exhaustions =
            handoff->pool_exhaustions();
        result.ring_overflows =
            handoff->ring_overflows();
    } else {
        result.queue_depth = 0;
        result.queue_high_water =
            terminal_queue_high_water_.load(
                std::memory_order_acquire);
        result.pool_high_water =
            terminal_pool_high_water_.load(
                std::memory_order_acquire);
        result.pool_exhaustions =
            terminal_pool_exhaustions_.load(
                std::memory_order_acquire);
        result.ring_overflows =
            terminal_ring_overflows_.load(
                std::memory_order_acquire);
    }

    return result;
}

void WasapiCaptureSource::worker_main(
    StartRequest request) noexcept
{
    bool startup_signaled = false;

    const auto fail =
        [this, &startup_signaled](
            StatusCode code,
            HRESULT hr) noexcept {
            const bool invalidated =
                is_device_invalidation(hr);
            mmcss_active_.store(
                false,
                std::memory_order_release);
            set_terminal_error(
                hr,
                invalidated);

            if (!startup_signaled) {
                (void)signal_startup(
                    status_from_hresult(
                        code,
                        hr));
                startup_signaled = true;
            }
        };

    ScopedComMta com;
    if (FAILED(com.status())) {
        fail(
            StatusCode::PlatformFailure,
            com.status());
        return;
    }

    ScopedMmcssAudio mmcss;
    mmcss_active_.store(
        mmcss.active(),
        std::memory_order_release);

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

    ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT hr =
        CoCreateInstance(
            __uuidof(MMDeviceEnumerator),
            nullptr,
            CLSCTX_ALL,
            IID_PPV_ARGS(
                enumerator.GetAddressOf()));
    if (FAILED(hr)) {
        fail(
            StatusCode::PlatformFailure,
            hr);
        return;
    }

    ComPtr<IMMDevice> endpoint;
    hr = enumerator->GetDevice(
        request.endpoint_id.c_str(),
        endpoint.GetAddressOf());
    if (FAILED(hr)) {
        fail(
            StatusCode::PlatformFailure,
            hr);
        return;
    }

    DWORD endpoint_state = 0;
    hr = endpoint->GetState(
        &endpoint_state);
    if (FAILED(hr) ||
        (endpoint_state &
         DEVICE_STATE_ACTIVE) == 0) {
        fail(
            StatusCode::PlatformFailure,
            FAILED(hr)
                ? hr
                : AUDCLNT_E_DEVICE_INVALIDATED);
        return;
    }

    ComPtr<IMMEndpoint> endpoint_flow;
    hr = endpoint.As(
        &endpoint_flow);
    if (FAILED(hr)) {
        fail(
            StatusCode::PlatformFailure,
            hr);
        return;
    }

    EDataFlow data_flow = eAll;
    hr = endpoint_flow->GetDataFlow(
        &data_flow);
    const EDataFlow expected_flow =
        request.mode == WasapiCaptureMode::Loopback
            ? eRender
            : eCapture;
    if (FAILED(hr) ||
        data_flow != expected_flow) {
        fail(
            StatusCode::InvalidArgument,
            FAILED(hr)
                ? hr
                : E_INVALIDARG);
        return;
    }

    ComPtr<IAudioClient> audio_client;
    hr = endpoint->Activate(
        __uuidof(IAudioClient),
        CLSCTX_ALL,
        nullptr,
        reinterpret_cast<void **>(
            audio_client.GetAddressOf()));
    if (FAILED(hr)) {
        fail(
            StatusCode::PlatformFailure,
            hr);
        return;
    }

    WAVEFORMATEX *raw_mix = nullptr;
    hr = audio_client->GetMixFormat(
        &raw_mix);
    if (FAILED(hr) ||
        raw_mix == nullptr) {
        fail(
            StatusCode::Unsupported,
            FAILED(hr)
                ? hr
                : E_FAIL);
        return;
    }

    std::unique_ptr<
        WAVEFORMATEX,
        CoTaskMemWaveDeleter>
        mix_format(raw_mix);

    const auto parsed_format =
        audio_format_from_wave_format(
            mix_format.get());
    if (!parsed_format.has_value()) {
        fail(
            StatusCode::Unsupported,
            AUDCLNT_E_UNSUPPORTED_FORMAT);
        return;
    }

    ScopedHandle audio_event(
        CreateEventW(
            nullptr,
            FALSE,
            FALSE,
            nullptr));
    if (!audio_event.valid()) {
        fail(
            StatusCode::PlatformFailure,
            HRESULT_FROM_WIN32(
                GetLastError()));
        return;
    }

    const DWORD stream_flags =
        AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
        AUDCLNT_STREAMFLAGS_NOPERSIST |
        (request.mode == WasapiCaptureMode::Loopback
             ? AUDCLNT_STREAMFLAGS_LOOPBACK
             : 0U);

    hr = audio_client->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        stream_flags,
        0,
        0,
        mix_format.get(),
        nullptr);
    if (FAILED(hr)) {
        fail(
            StatusCode::PlatformFailure,
            hr);
        return;
    }

    hr = audio_client->SetEventHandle(
        audio_event.get());
    if (FAILED(hr)) {
        fail(
            StatusCode::PlatformFailure,
            hr);
        return;
    }

    UINT32 buffer_frames = 0;
    hr = audio_client->GetBufferSize(
        &buffer_frames);
    if (FAILED(hr) ||
        buffer_frames == 0) {
        fail(
            StatusCode::PlatformFailure,
            FAILED(hr)
                ? hr
                : E_FAIL);
        return;
    }

    ComPtr<IAudioCaptureClient> capture_client;
    hr = audio_client->GetService(
        IID_PPV_ARGS(
            capture_client.GetAddressOf()));
    if (FAILED(hr)) {
        fail(
            StatusCode::PlatformFailure,
            hr);
        return;
    }

    const std::size_t bytes_per_slot =
        parsed_format->bytes_for_frames(
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
            *parsed_format;
        endpoint_buffer_frames_ =
            buffer_frames;
    }

    handoff_ =
        std::move(handoff);
    handoff_view_.store(
        handoff_,
        std::memory_order_release);

    WasapiTimestampClassifier timing(
        parsed_format->sample_rate);

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

    hr = audio_client->Start();
    if (FAILED(hr)) {
        fail(
            StatusCode::PlatformFailure,
            hr);
        return;
    }

    // signal_startup() checks the cancellation flag under the startup
    // publication mutex. If stop raced IAudioClient::Start, startup is reported
    // as canceled rather than briefly claiming a dead microphone is Running.
    if (!signal_startup(
            Status::success())) {
        audio_client->Stop();
        mmcss_active_.store(
            false,
            std::memory_order_release);
        return;
    }
    startup_signaled = true;

    HANDLE waits[2]{
        stop_event_,
        audio_event.get()};

    bool terminal = false;

    while (!terminal) {
        const DWORD wait_result =
            WaitForMultipleObjects(
                2,
                waits,
                FALSE,
                INFINITE);

        if (wait_result == WAIT_OBJECT_0)
            break;

        if (wait_result !=
            WAIT_OBJECT_0 + 1) {
            const HRESULT wait_hr =
                HRESULT_FROM_WIN32(
                    GetLastError());
            fail(
                StatusCode::PlatformFailure,
                wait_hr);
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

            UINT32 next_frames = 0;
            hr = capture_client->GetNextPacketSize(
                &next_frames);
            if (FAILED(hr)) {
                fail(
                    StatusCode::PlatformFailure,
                    hr);
                terminal = true;
                break;
            }

            if (next_frames == 0)
                break;

            BYTE *data = nullptr;
            UINT32 frame_count = 0;
            DWORD flags = 0;
            UINT64 device_position = 0;
            UINT64 qpc_position = 0;

            hr = capture_client->GetBuffer(
                &data,
                &frame_count,
                &flags,
                &device_position,
                &qpc_position);

            if (hr == AUDCLNT_S_BUFFER_EMPTY)
                break;

            if (FAILED(hr)) {
                fail(
                    StatusCode::PlatformFailure,
                    hr);
                terminal = true;
                break;
            }

            // A stop request may race GetBuffer after the loop-level check.
            // Return the acquired endpoint buffer immediately and publish no
            // post-stop media.
            if (stop_requested()) {
                const HRESULT release_hr =
                    capture_client->ReleaseBuffer(
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
                    flags);

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
                    device_position,
                    qpc_position,
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
                    *parsed_format,
                .frame_count =
                    frame_count,
                .timing =
                    timing_evidence,
                .flags =
                    packet_flags,
            };

            const auto bytes =
                packet_bytes(
                    data,
                    *parsed_format,
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
                capture_client->ReleaseBuffer(
                    frame_count);
            if (FAILED(release_hr)) {
                fail(
                    StatusCode::PlatformFailure,
                    release_hr);
                terminal = true;
            }
        }
    }

    audio_client->Stop();

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
