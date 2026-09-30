#include "platform/windows/capture/wgc_capture_source.hpp"

#ifdef _WIN32

#include "core/result/status.hpp"
#include "core/time/monotonic_clock.hpp"

#include <dxgi.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <winrt/base.h>

#include <algorithm>
#include <chrono>
#include <limits>
#include <utility>

namespace arssyut::windows {

namespace {

using arssyut::core::DiagnosticMetric;
using arssyut::core::FrameSize;
using arssyut::core::MonotonicClock;
using arssyut::core::Status;
using arssyut::core::StatusCode;
using arssyut::core::TimePoint;

using winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool;
using winrt::Windows::Graphics::Capture::GraphicsCaptureItem;
using winrt::Windows::Graphics::Capture::GraphicsCaptureSession;
using winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice;
using winrt::Windows::Graphics::DirectX::DirectXPixelFormat;

[[nodiscard]] std::uint32_t hresult_detail(HRESULT hr) noexcept
{
    return static_cast<std::uint32_t>(hr);
}

[[nodiscard]] Status status_from_hresult(
    StatusCode code,
    HRESULT hr) noexcept
{
    return Status::failure(code, hresult_detail(hr));
}

[[nodiscard]] std::uint32_t callback_microseconds(
    TimePoint start,
    TimePoint end) noexcept
{
    const std::int64_t ticks =
        std::max<std::int64_t>(
            0,
            MonotonicClock::duration_ticks(start, end));

    const std::uint64_t microseconds =
        static_cast<std::uint64_t>(ticks) / 10ULL;

    return static_cast<std::uint32_t>(
        std::min<std::uint64_t>(
            microseconds,
            static_cast<std::uint64_t>(
                std::numeric_limits<std::uint32_t>::max())));
}

[[nodiscard]] Status create_capture_item(
    CaptureTargetKind kind,
    HWND window,
    HMONITOR monitor,
    GraphicsCaptureItem &item) noexcept
{
    try {
        auto factory =
            winrt::get_activation_factory<GraphicsCaptureItem>();
        auto interop = factory.as<IGraphicsCaptureItemInterop>();

        HRESULT hr = E_INVALIDARG;

        if (kind == CaptureTargetKind::Window) {
            if (!window)
                return Status::failure(StatusCode::InvalidArgument);

            hr = interop->CreateForWindow(
                window,
                winrt::guid_of<GraphicsCaptureItem>(),
                winrt::put_abi(item));
        } else {
            if (!monitor)
                return Status::failure(StatusCode::InvalidArgument);

            hr = interop->CreateForMonitor(
                monitor,
                winrt::guid_of<GraphicsCaptureItem>(),
                winrt::put_abi(item));
        }

        if (FAILED(hr))
            return status_from_hresult(StatusCode::PlatformFailure, hr);

        if (!item)
            return Status::failure(StatusCode::PlatformFailure);

        return Status::success();
    } catch (const winrt::hresult_error &error) {
        return status_from_hresult(
            StatusCode::PlatformFailure,
            error.code());
    } catch (...) {
        return Status::failure(StatusCode::InternalError);
    }
}

[[nodiscard]] Status create_winrt_device(
    ID3D11Device *native_device,
    IDirect3DDevice &device) noexcept
{
    if (!native_device)
        return Status::failure(StatusCode::InvalidArgument);

    Microsoft::WRL::ComPtr<IDXGIDevice> dxgi_device;
    const HRESULT query_hr = native_device->QueryInterface(
        IID_PPV_ARGS(dxgi_device.GetAddressOf()));

    if (FAILED(query_hr))
        return status_from_hresult(
            StatusCode::GraphicsDeviceUnavailable,
            query_hr);

    winrt::com_ptr<::IInspectable> inspectable;
    const HRESULT wrap_hr =
        CreateDirect3D11DeviceFromDXGIDevice(
            dxgi_device.Get(),
            inspectable.put());

    if (FAILED(wrap_hr))
        return status_from_hresult(
            StatusCode::GraphicsDeviceUnavailable,
            wrap_hr);

    try {
        device = inspectable.as<IDirect3DDevice>();
        return device
            ? Status::success()
            : Status::failure(StatusCode::GraphicsDeviceUnavailable);
    } catch (const winrt::hresult_error &error) {
        return status_from_hresult(
            StatusCode::GraphicsDeviceUnavailable,
            error.code());
    } catch (...) {
        return Status::failure(StatusCode::InternalError);
    }
}

} // namespace

WgcCaptureSource::~WgcCaptureSource()
{
    stop();
}

Status WgcCaptureSource::start_window(
    ID3D11Device *device,
    HWND window,
    LatestFrameSlot &frame_slot,
    arssyut::core::Diagnostics &diagnostics,
    WgcCaptureOptions options)
{
    StartRequest request;
    request.device = device;
    request.kind = CaptureTargetKind::Window;
    request.window = window;
    request.options = options;
    return start(std::move(request), frame_slot, diagnostics);
}

Status WgcCaptureSource::start_monitor(
    ID3D11Device *device,
    HMONITOR monitor,
    LatestFrameSlot &frame_slot,
    arssyut::core::Diagnostics &diagnostics,
    WgcCaptureOptions options)
{
    StartRequest request;
    request.device = device;
    request.kind = CaptureTargetKind::Monitor;
    request.monitor = monitor;
    request.options = options;
    return start(std::move(request), frame_slot, diagnostics);
}

Status WgcCaptureSource::start(
    StartRequest request,
    LatestFrameSlot &frame_slot,
    arssyut::core::Diagnostics &diagnostics)
{
    std::unique_lock lifecycle_lock(lifecycle_mutex_);

    if (worker_.joinable() || running_.load(std::memory_order_acquire))
        return Status::failure(StatusCode::InvalidStateTransition);

    if (!request.device)
        return Status::failure(StatusCode::InvalidArgument);

    request.options.frame_pool_buffers =
        std::clamp<std::uint32_t>(
            request.options.frame_pool_buffers,
            2U,
            4U);

    frame_slot_ = &frame_slot;
    diagnostics_ = &diagnostics;

    stop_requested_.store(false, std::memory_order_release);
    running_.store(false, std::memory_order_release);
    source_closed_.store(false, std::memory_order_release);
    sequence_.store(0, std::memory_order_release);

    {
        std::lock_guard startup_lock(startup_mutex_);
        startup_done_ = false;
        startup_status_ = Status::success();
    }

    try {
        worker_ = std::thread(
            [this, request = std::move(request)]() mutable noexcept {
                owner_thread(std::move(request));
            });
    } catch (...) {
        frame_slot_ = nullptr;
        diagnostics_ = nullptr;
        return Status::failure(StatusCode::InternalError);
    }

    lifecycle_lock.unlock();

    {
        std::unique_lock startup_lock(startup_mutex_);
        startup_cv_.wait(
            startup_lock,
            [this]() noexcept { return startup_done_; });
    }

    const Status status = startup_status_;
    if (!status.ok()) {
        request_owner_stop();
        if (worker_.joinable())
            worker_.join();

        std::lock_guard relock(lifecycle_mutex_);
        frame_slot_ = nullptr;
        diagnostics_ = nullptr;
    }

    return status;
}

void WgcCaptureSource::stop() noexcept
{
    std::unique_lock lifecycle_lock(lifecycle_mutex_);
    if (!worker_.joinable()) {
        running_.store(false, std::memory_order_release);
        return;
    }

    request_owner_stop();
    std::thread worker = std::move(worker_);
    lifecycle_lock.unlock();

    if (worker.joinable())
        worker.join();

    lifecycle_lock.lock();
    running_.store(false, std::memory_order_release);
    frame_slot_ = nullptr;
    diagnostics_ = nullptr;
}

void WgcCaptureSource::request_owner_stop() noexcept
{
    stop_requested_.store(true, std::memory_order_release);
    owner_wait_cv_.notify_all();
}

void WgcCaptureSource::signal_startup(Status status) noexcept
{
    {
        std::lock_guard lock(startup_mutex_);
        if (startup_done_)
            return;
        startup_status_ = status;
        startup_done_ = true;
    }
    startup_cv_.notify_all();
}

void WgcCaptureSource::owner_thread(StartRequest request) noexcept
{
    bool startup_signaled = false;

    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);

        if (!GraphicsCaptureSession::IsSupported()) {
            signal_startup(Status::failure(StatusCode::Unsupported));
            startup_signaled = true;
            winrt::uninit_apartment();
            return;
        }

        IDirect3DDevice winrt_device{nullptr};
        const Status device_status =
            create_winrt_device(request.device.Get(), winrt_device);
        if (!device_status.ok()) {
            signal_startup(device_status);
            startup_signaled = true;
            winrt::uninit_apartment();
            return;
        }

        GraphicsCaptureItem item{nullptr};
        const Status item_status =
            create_capture_item(
                request.kind,
                request.window,
                request.monitor,
                item);
        if (!item_status.ok()) {
            signal_startup(item_status);
            startup_signaled = true;
            winrt::uninit_apartment();
            return;
        }

        auto current_size = item.Size();
        if (current_size.Width <= 0 || current_size.Height <= 0) {
            signal_startup(Status::failure(StatusCode::InvalidArgument));
            startup_signaled = true;
            winrt::uninit_apartment();
            return;
        }

        auto frame_pool =
            Direct3D11CaptureFramePool::CreateFreeThreaded(
                winrt_device,
                DirectXPixelFormat::B8G8R8A8UIntNormalized,
                static_cast<int>(request.options.frame_pool_buffers),
                current_size);

        std::atomic<std::int32_t> current_width{current_size.Width};
        std::atomic<std::int32_t> current_height{current_size.Height};

        auto frame_revoker = frame_pool.FrameArrived(
            winrt::auto_revoke,
            [this,
             winrt_device,
             buffers = request.options.frame_pool_buffers,
             &current_width,
             &current_height](
                const Direct3D11CaptureFramePool &sender,
                const winrt::Windows::Foundation::IInspectable &) noexcept {
                const TimePoint callback_start = MonotonicClock::now();

                try {
                    auto frame = sender.TryGetNextFrame();
                    if (!frame) {
                        callback_latency_.observe(
                            callback_microseconds(
                                callback_start,
                                MonotonicClock::now()));
                        return;
                    }

                    if (diagnostics_) {
                        diagnostics_->increment(
                            DiagnosticMetric::CaptureFramesReceived);
                    }

                    const auto size = frame.ContentSize();
                    const bool resized =
                        size.Width > 0 &&
                        size.Height > 0 &&
                        (size.Width !=
                             current_width.load(std::memory_order_acquire) ||
                         size.Height !=
                             current_height.load(std::memory_order_acquire));

                    if (resized) {
                        if (frame_slot_)
                            frame_slot_->producer_discard_unread();

                        if (frame_slot_ && !frame_slot_->has_in_flight()) {
                            frame.Close();

                            sender.Recreate(
                                winrt_device,
                                DirectXPixelFormat::
                                    B8G8R8A8UIntNormalized,
                                static_cast<int>(buffers),
                                size);

                            current_width.store(
                                size.Width,
                                std::memory_order_release);
                            current_height.store(
                                size.Height,
                                std::memory_order_release);

                            if (diagnostics_) {
                                diagnostics_->increment(
                                    DiagnosticMetric::
                                        CaptureSourceResizes);
                            }
                        }

                        callback_latency_.observe(
                            callback_microseconds(
                                callback_start,
                                MonotonicClock::now()));
                        return;
                    }

                    if (!frame_slot_) {
                        callback_latency_.observe(
                            callback_microseconds(
                                callback_start,
                                MonotonicClock::now()));
                        return;
                    }

                    CapturedFrame captured;
                    captured.frame = frame;
                    captured.captured_at = {
                        frame.SystemRelativeTime().count()
                    };
                    captured.content_size = {
                        static_cast<std::uint32_t>(size.Width),
                        static_cast<std::uint32_t>(size.Height)
                    };
                    captured.sequence =
                        sequence_.fetch_add(
                            1,
                            std::memory_order_relaxed) + 1;

                    const auto publish_result =
                        frame_slot_->publish(std::move(captured));

                    if (diagnostics_) {
                        if (publish_result ==
                            LatestFrameSlot::PublishResult::
                                ReplacedUnread) {
                            diagnostics_->increment(
                                DiagnosticMetric::
                                    CaptureFramesReplaced);
                        } else if (
                            publish_result ==
                            LatestFrameSlot::PublishResult::
                                DroppedBusy) {
                            diagnostics_->increment(
                                DiagnosticMetric::
                                    CaptureFramesDroppedBusy);
                        }
                    }
                } catch (...) {
                    if (diagnostics_) {
                        diagnostics_->increment(
                            DiagnosticMetric::
                                CaptureCallbackFailures);
                    }
                }

                callback_latency_.observe(
                    callback_microseconds(
                        callback_start,
                        MonotonicClock::now()));
            });

        auto closed_revoker = item.Closed(
            winrt::auto_revoke,
            [this](
                const GraphicsCaptureItem &,
                const winrt::Windows::Foundation::IInspectable &) noexcept {
                source_closed_.store(true, std::memory_order_release);
                if (diagnostics_) {
                    diagnostics_->increment(
                        DiagnosticMetric::CaptureSourceClosed);
                }
                request_owner_stop();
            });

        auto session = frame_pool.CreateCaptureSession(item);
        session.StartCapture();

        running_.store(true, std::memory_order_release);
        signal_startup(Status::success());
        startup_signaled = true;

        {
            std::unique_lock wait_lock(owner_wait_mutex_);
            owner_wait_cv_.wait(
                wait_lock,
                [this]() noexcept {
                    return stop_requested_.load(
                        std::memory_order_acquire);
                });
        }

        running_.store(false, std::memory_order_release);

        frame_revoker.revoke();
        closed_revoker.revoke();

        if (frame_slot_)
            frame_slot_->producer_discard_unread();

        try {
            session.Close();
        } catch (...) {
        }

        try {
            frame_pool.Close();
        } catch (...) {
        }

        winrt::uninit_apartment();
    } catch (const winrt::hresult_error &error) {
        running_.store(false, std::memory_order_release);
        if (!startup_signaled) {
            signal_startup(
                status_from_hresult(
                    StatusCode::PlatformFailure,
                    error.code()));
        } else if (diagnostics_) {
            diagnostics_->increment(
                DiagnosticMetric::CaptureCallbackFailures);
        }
        try {
            winrt::uninit_apartment();
        } catch (...) {
        }
    } catch (...) {
        running_.store(false, std::memory_order_release);
        if (!startup_signaled) {
            signal_startup(Status::failure(StatusCode::InternalError));
        } else if (diagnostics_) {
            diagnostics_->increment(
                DiagnosticMetric::CaptureCallbackFailures);
        }
        try {
            winrt::uninit_apartment();
        } catch (...) {
        }
    }
}

} // namespace arssyut::windows

#endif
