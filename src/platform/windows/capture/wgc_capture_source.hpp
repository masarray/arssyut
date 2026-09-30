#pragma once

#ifdef _WIN32

#include "core/diagnostics/diagnostics.hpp"
#include "core/diagnostics/latency_histogram.hpp"
#include "core/result/status.hpp"
#include "platform/windows/capture/latest_frame_slot.hpp"

#include <d3d11.h>
#include <wrl/client.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

namespace arssyut::windows {

enum class CaptureTargetKind : std::uint8_t {
    Window = 0,
    Monitor,
};

struct WgcCaptureOptions {
    std::uint32_t frame_pool_buffers = 3;
};

class WgcCaptureSource final {
public:
    WgcCaptureSource() = default;
    ~WgcCaptureSource();

    WgcCaptureSource(const WgcCaptureSource &) = delete;
    WgcCaptureSource &operator=(const WgcCaptureSource &) = delete;

    [[nodiscard]] arssyut::core::Status start_window(
        ID3D11Device *device,
        HWND window,
        LatestFrameSlot &frame_slot,
        arssyut::core::Diagnostics &diagnostics,
        WgcCaptureOptions options = {});

    [[nodiscard]] arssyut::core::Status start_monitor(
        ID3D11Device *device,
        HMONITOR monitor,
        LatestFrameSlot &frame_slot,
        arssyut::core::Diagnostics &diagnostics,
        WgcCaptureOptions options = {});

    void stop() noexcept;

    [[nodiscard]] bool running() const noexcept
    {
        return running_.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool source_closed() const noexcept
    {
        return source_closed_.load(std::memory_order_acquire);
    }

    [[nodiscard]] arssyut::core::LatencyHistogram::Snapshot
    capture_callback_latency() const noexcept
    {
        return callback_latency_.snapshot();
    }

private:
    struct StartRequest {
        Microsoft::WRL::ComPtr<ID3D11Device> device;
        CaptureTargetKind kind = CaptureTargetKind::Monitor;
        HWND window = nullptr;
        HMONITOR monitor = nullptr;
        WgcCaptureOptions options{};
    };

    [[nodiscard]] arssyut::core::Status start(
        StartRequest request,
        LatestFrameSlot &frame_slot,
        arssyut::core::Diagnostics &diagnostics);

    void owner_thread(StartRequest request) noexcept;

    void signal_startup(arssyut::core::Status status) noexcept;

    void request_owner_stop() noexcept;

    std::thread worker_;

    std::mutex lifecycle_mutex_;

    std::mutex startup_mutex_;
    std::condition_variable startup_cv_;
    bool startup_done_ = false;
    arssyut::core::Status startup_status_{};

    std::mutex owner_wait_mutex_;
    std::condition_variable owner_wait_cv_;
    std::atomic<bool> stop_requested_{false};

    std::atomic<bool> running_{false};
    std::atomic<bool> source_closed_{false};
    std::atomic<std::uint64_t> sequence_{0};

    LatestFrameSlot *frame_slot_ = nullptr;
    arssyut::core::Diagnostics *diagnostics_ = nullptr;

    arssyut::core::LatencyHistogram callback_latency_;
};

} // namespace arssyut::windows

#endif
