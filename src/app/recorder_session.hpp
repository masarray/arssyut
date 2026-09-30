#pragma once

#ifdef _WIN32

#include "core/diagnostics/diagnostics.hpp"
#include "core/result/status.hpp"
#include "core/time/monotonic_clock.hpp"
#include "core/video/frame_geometry.hpp"
#include "core/video/frame_scheduler.hpp"
#include "platform/windows/capture/wgc_capture_source.hpp"
#include "platform/windows/media/mf_h264_mp4_writer.hpp"
#include "presentation/presentation_controller.hpp"

#include <Windows.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <thread>

namespace arssyut::app {

enum class RecorderState : std::uint8_t {
    Idle = 0,
    Preparing,
    Recording,
    Stopping,
    Finalizing,
    Ready,
    Failed,
};

struct RecorderTarget {
    arssyut::windows::CaptureTargetKind kind =
        arssyut::windows::CaptureTargetKind::Monitor;
    HMONITOR monitor = nullptr;
    HWND window = nullptr;
    std::wstring label;
};

struct RecorderConfig {
    RecorderTarget target;
    std::filesystem::path output_path;
    arssyut::core::FrameSize output_size{1920, 1080};
    arssyut::core::FrameRate frame_rate{60, 1};
    std::uint32_t bitrate_bps = 12'000'000;
    arssyut::presentation::PresentationSettings presentation{};
};

struct RecorderSnapshot {
    RecorderState state = RecorderState::Idle;
    std::int64_t elapsed_ticks = 0;

    std::uint64_t capture_received = 0;
    std::uint64_t capture_replaced = 0;
    std::uint64_t capture_busy_drops = 0;
    std::uint64_t video_rendered = 0;
    std::uint64_t video_reused = 0;
    std::uint64_t video_skipped = 0;
    std::uint64_t encoder_submitted = 0;
    std::uint64_t encoder_backpressure = 0;
    std::uint32_t encoder_sample_buffer_length = 0;
    std::uint32_t encoder_sample_buffer_max_length = 0;

    std::uint32_t capture_p95_us = 0;
    std::uint32_t compositor_cpu_p95_us = 0;
    std::uint32_t compositor_gpu_p95_us = 0;

    std::uint64_t memory_private_bytes = 0;
    std::uint64_t memory_private_max_bytes = 0;

    arssyut::core::Status last_error{};
    arssyut::windows::MfWriterStage encoder_failure_stage =
        arssyut::windows::MfWriterStage::None;
};

class RecorderSession final {
public:
    RecorderSession() = default;
    ~RecorderSession();

    RecorderSession(const RecorderSession &) = delete;
    RecorderSession &operator=(const RecorderSession &) = delete;

    [[nodiscard]] arssyut::core::Status start(
        RecorderConfig config);

    void request_stop() noexcept;
    void wait() noexcept;

    [[nodiscard]] RecorderSnapshot snapshot() const noexcept;

    [[nodiscard]] const std::filesystem::path &
    output_path() const noexcept
    {
        return config_.output_path;
    }

    [[nodiscard]] std::filesystem::path
    diagnostics_path() const
    {
        auto value = config_.output_path;
        value += L".diagnostics.json";
        return value;
    }

private:
    void worker_main() noexcept;

    void fail(arssyut::core::Status status) noexcept;

    void write_diagnostics(
        std::uint64_t output_bytes,
        std::uint64_t memory_start,
        std::uint64_t memory_end,
        std::uint64_t writer_submitted,
        std::uint64_t writer_backpressure,
        std::uint64_t resource_generation) noexcept;

    RecorderConfig config_{};
    std::thread worker_;

    arssyut::core::Diagnostics diagnostics_;

    std::atomic<RecorderState> state_{RecorderState::Idle};
    std::atomic<bool> stop_requested_{false};

    std::atomic<std::int64_t> started_at_ticks_{0};
    std::atomic<std::int64_t> stopped_at_ticks_{0};

    std::atomic<std::uint32_t> error_code_{0};
    std::atomic<std::uint32_t> error_detail_{0};
    std::atomic<arssyut::windows::MfWriterStage>
        encoder_failure_stage_{arssyut::windows::MfWriterStage::None};

    std::atomic<std::uint32_t> encoder_sample_buffer_length_{0};
    std::atomic<std::uint32_t> encoder_sample_buffer_max_length_{0};

    std::atomic<std::uint32_t> capture_p95_us_{0};
    std::atomic<std::uint32_t> compositor_cpu_p95_us_{0};
    std::atomic<std::uint32_t> compositor_gpu_p95_us_{0};

    std::atomic<std::uint64_t> memory_private_bytes_{0};
    std::atomic<std::uint64_t> memory_private_max_bytes_{0};
};

} // namespace arssyut::app

#endif
