#pragma once
#ifdef _WIN32

#include "platform/windows/capture/webcam_capture_source.hpp"
#include "platform/windows/graphics/d3d11_device.hpp"
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace arssyut::windows {
struct WebcamPreviewRenderer;

// One physical MF camera source is retained across Preview <-> Recording.
// Exactly ONE thread owns its D3D11 immediate context at any instant:
// preview worker before/after recording, RecorderSession worker during it.
// The existing GPU mailbox remains strictly SPSC.
class WebcamPreviewSession final {
public:
    static constexpr std::uint32_t width = 192, height = 108;
    static constexpr std::size_t bytes = width * height * 4U;
    WebcamPreviewSession() = default;
    ~WebcamPreviewSession();
    WebcamPreviewSession(const WebcamPreviewSession &) = delete;
    WebcamPreviewSession &operator=(const WebcamPreviewSession &) = delete;

    [[nodiscard]] static bool validate_gpu_shader() noexcept;
    [[nodiscard]] bool start(std::wstring symbolic_link) noexcept;
    void stop() noexcept;

    // Called on the bridge's serialized recording-start worker, NOT the UI
    // dispatcher. Waits for the preview renderer to retire before passing
    // the exact same D3D11 device and GPU mailbox to RecorderSession.
    [[nodiscard]] bool begin_recording() noexcept;
    void end_recording() noexcept;
    [[nodiscard]] std::shared_ptr<D3D11Device> recording_device() const noexcept;
    [[nodiscard]] WebcamGpuFrameSlot &frames() noexcept { return frames_; }
    [[nodiscard]] WebcamCaptureSnapshot capture_snapshot() const noexcept {
        return camera_.snapshot();
    }
    [[nodiscard]] const std::wstring &symbolic_link() const noexcept {
        return symbolic_link_;
    }

    // Sole active recorder GPU worker calls this on a fresh camera lease.
    // No camera/driver open, no parallel renderer, no full-size CPU copy.
    void publish_from_recorder(ID3D11DeviceContext *context,
                               const WebcamGpuFrame &frame) noexcept;

    // Bridge preview state: 0 Off, 1 Opening, 2 PreviewActive,
    // 3 Unavailable, 4 RecordingActive (also valid live pixel sequences).
    [[nodiscard]] bool copy_frame(std::uint8_t *out, std::uint32_t capacity,
                                  std::uint64_t &sequence,
                                  std::uint32_t &state) noexcept;
    [[nodiscard]] std::uint32_t failure_stage() const noexcept {
        return failure_stage_.load(std::memory_order_acquire);
    }
    [[nodiscard]] std::uint32_t failure_detail() const noexcept {
        return failure_detail_.load(std::memory_order_acquire);
    }

private:
    void run(std::wstring symbolic_link) noexcept;
    void publish_pixels(const std::array<std::uint8_t, bytes> &pixels) noexcept;

    mutable std::mutex pixels_mutex_;
    std::array<std::uint8_t, bytes> frame_{};
    std::uint64_t sequence_ = 0;

    mutable std::mutex handoff_mutex_;
    std::condition_variable handoff_cv_;
    bool graphics_ready_ = false;
    bool recording_requested_ = false;
    bool recording_acknowledged_ = false;

    std::shared_ptr<D3D11Device> graphics_;
    std::unique_ptr<WebcamPreviewRenderer> renderer_;
    WebcamGpuFrameSlot frames_{};
    WebcamCaptureSource camera_{};
    std::wstring symbolic_link_;
    std::thread worker_;
    std::atomic<std::uint32_t> state_{0};
    std::atomic<bool> stopping_{false};
    std::atomic<std::uint32_t> failure_stage_{0};
    std::atomic<std::uint32_t> failure_detail_{0};
    // Operates only on the current GPU owner thread.
    std::int64_t next_preview_ticks_ = 0;
};
}
#endif
