#pragma once
#ifdef _WIN32

#include "platform/windows/capture/webcam_capture_source.hpp"
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

namespace arssyut::windows {

// Internal-only GUI preview. Uses the exact native WebcamCaptureSource and
// NV12 GPU frame path as recording; copies only a tiny 192x108 thumbnail to
// Avalonia at most 10 times/second. No full-size CPU webcam readback.
// Ownership: MainWindow preview -> OFF before RecorderSession opens camera.
class WebcamPreviewSession final {
public:
    static constexpr std::uint32_t width = 192, height = 108;
    static constexpr std::size_t bytes = width * height * 4U;
    WebcamPreviewSession() = default;
    ~WebcamPreviewSession() { stop(); }
    WebcamPreviewSession(const WebcamPreviewSession &) = delete;
    WebcamPreviewSession &operator=(const WebcamPreviewSession &) = delete;

    // Deterministic compile contract: catches HLSL regressions on CI even
    // without a physical webcam connected to the Windows test runner.
    [[nodiscard]] static bool validate_gpu_shader() noexcept;
    [[nodiscard]] bool start(std::wstring symbolic_link) noexcept;
    void stop() noexcept;
    // State: 0=Off,1=Opening,2=Active,3=Unavailable.
    // Copies the most recent completed BGRA frame, even if caller's last
    // sequence is equal; GUI checks sequence to avoid pointless redraws.
    // Published only on the native camera worker. Queryable even after
    // failure; UI can report the exact stage/HRESULT, not merely "unavailable".
    // Stage 1=device, 2=renderer, 3=source start, 4=source capture, 5=GPU draw.
    [[nodiscard]] std::uint32_t failure_stage() const noexcept {
        return failure_stage_.load(std::memory_order_acquire);
    }
    [[nodiscard]] std::uint32_t failure_detail() const noexcept {
        return failure_detail_.load(std::memory_order_acquire);
    }
    [[nodiscard]] bool copy_frame(std::uint8_t *out,
                                  std::uint32_t capacity,
                                  std::uint64_t &sequence,
                                  std::uint32_t &state) noexcept;
private:
    void run(std::wstring symbolic_link) noexcept;
    mutable std::mutex mutex_;
    std::array<std::uint8_t, bytes> frame_{};
    std::uint64_t sequence_ = 0;
    std::atomic<std::uint32_t> state_{0};
    std::atomic<bool> stopping_{false};
    std::atomic<std::uint32_t> failure_stage_{0};
    std::atomic<std::uint32_t> failure_detail_{0};
    std::thread worker_;
};
}
#endif
