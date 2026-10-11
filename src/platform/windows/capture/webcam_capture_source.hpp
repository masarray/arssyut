#pragma once

#ifdef _WIN32

#include "core/result/status.hpp"
#include "core/video/webcam_capture_policy.hpp"
#include "platform/windows/capture/webcam_gpu_frame_slot.hpp"

#include <d3d11.h>
#include <wrl/client.h>

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

namespace arssyut::windows {

enum class WebcamCaptureState : std::uint8_t {
    Off = 0,
    Opening,
    Active,
    Unavailable,
    Closing,
};

struct WebcamCaptureSnapshot {
    WebcamCaptureState state = WebcamCaptureState::Off;
    arssyut::core::WebcamCaptureCandidate format{};
    std::uint64_t received = 0;
    std::uint64_t published = 0;
    std::uint64_t replaced = 0;
    std::uint64_t busy_drops = 0;
    std::uint64_t stream_ticks = 0;
    std::uint64_t stream_ticks_startup = 0;
    std::uint64_t stream_ticks_streaming = 0;
    std::uint64_t frame_intervals_over_100ms = 0;
    std::uint64_t max_frame_interval_ms = 0;
    std::uint32_t source_open_ms = 0;
    std::uint32_t first_frame_ms = 0;
    std::uint32_t streaming_fps_x100 = 0;
    std::uint64_t frames_other_device_same_adapter = 0;
    std::uint64_t invalid_samples = 0;
    std::uint64_t adapter_mismatch = 0;
    std::uint64_t source_changes = 0;
    std::uint64_t callback_timeouts = 0;
    std::uint32_t last_hresult = 0;
};

// Independent native camera *input*. No AVI/MP4 output, audio clock, window
// or compositor. A RecorderSession may own one instance and pass the exact
// recorder ID3D11Device, then consume only its latest GPU sample lease.
// All MF objects are created and released on one worker COM/MTA thread.
// A camera is OFF by default and never opens from enumeration alone.
class WebcamCaptureSource final {
public:
    WebcamCaptureSource() = default;
    ~WebcamCaptureSource();

    WebcamCaptureSource(const WebcamCaptureSource &) = delete;
    WebcamCaptureSource &operator=(const WebcamCaptureSource &) = delete;

    // Starts one owned worker without waiting for device permission/driver.
    // Caller must outlive source with gpu_frames; call stop() before
    // destroying the mailbox or D3D device.
    [[nodiscard]] arssyut::core::Status start(
        ID3D11Device *device,
        std::wstring symbolic_link,
        WebcamGpuFrameSlot &gpu_frames) noexcept;

    // Joining can depend on a misbehaving camera driver during COM shutdown.
    // Never call stop() on the render/capture/audio hot path.
    void stop() noexcept;

    [[nodiscard]] WebcamCaptureSnapshot snapshot() const noexcept;

private:
    void worker(
        Microsoft::WRL::ComPtr<ID3D11Device> device,
        std::wstring symbolic_link) noexcept;

    void fail(HRESULT hr) noexcept;
    void reset_counters() noexcept;

    std::thread worker_;
    std::atomic<bool> stop_requested_{false};
    std::atomic<WebcamCaptureState> state_{WebcamCaptureState::Off};
    WebcamGpuFrameSlot *slot_ = nullptr;
    // start/stop only on one lifecycle/controller thread; never concurrent.
    std::atomic<std::uint32_t> mode_width_{0};
    std::atomic<std::uint32_t> mode_height_{0};
    std::atomic<std::uint32_t> mode_rate_num_{0};
    std::atomic<std::uint32_t> mode_rate_den_{0};
    std::atomic<std::uint8_t> mode_format_{3};

    std::atomic<std::uint64_t> received_{0};
    std::atomic<std::uint64_t> published_{0};
    std::atomic<std::uint64_t> replaced_{0};
    std::atomic<std::uint64_t> busy_drops_{0};
    std::atomic<std::uint64_t> stream_ticks_{0};
    std::atomic<std::uint64_t> stream_ticks_startup_{0};
    std::atomic<std::uint64_t> stream_ticks_streaming_{0};
    std::atomic<std::uint64_t> frame_intervals_over_100ms_{0};
    std::atomic<std::uint64_t> max_frame_interval_ticks_{0};
    std::atomic<std::uint64_t> frames_other_device_same_adapter_{0};
    std::atomic<std::int64_t> open_request_ticks_{0};
    std::atomic<std::int64_t> source_ready_ticks_{0};
    std::atomic<std::int64_t> first_frame_ticks_{0};
    std::atomic<std::int64_t> last_frame_ticks_{0};
    std::atomic<std::uint64_t> invalid_samples_{0};
    std::atomic<std::uint64_t> adapter_mismatch_{0};
    std::atomic<std::uint64_t> source_changes_{0};
    std::atomic<std::uint64_t> callback_timeouts_{0};
    std::atomic<std::uint32_t> last_hresult_{0};
};

} // namespace arssyut::windows

#endif
