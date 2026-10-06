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
#include "visual/arvisual_grade.hpp"
#include "visual/arvisual_modes.hpp"
#include "visual/arvisual_scene_analysis.hpp"

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

enum class PresenterCommand : std::uint8_t {
    ToggleZoom = 0,
    ZoomIn,
    ZoomOut,
    ResetFullFrame,
    ToggleFreezeCamera,
};

struct RecorderTarget {
    arssyut::windows::CaptureTargetKind kind =
        arssyut::windows::CaptureTargetKind::Monitor;
    HMONITOR monitor = nullptr;
    HWND window = nullptr;
    std::wstring label;
};

struct PresenterMomentaryBinding {
    std::uint16_t virtual_key = 0;
    std::uint8_t modifiers = 0;

    [[nodiscard]] bool configured() const noexcept
    {
        return virtual_key != 0;
    }
};

struct RecorderConfig {
    RecorderTarget target;
    std::filesystem::path output_path;
    arssyut::core::FrameSize output_size{1920, 1080};
    arssyut::core::FrameRate frame_rate{60, 1};

    // Empty crop means full source. Region mode maps its virtual-screen
    // selection into source pixels before the worker starts.
    arssyut::core::CropRect crop{};

    // Presentation input must normalize against the same spatial region that
    // the compositor receives. This is especially important for custom Region.
    RECT presentation_screen_rect{};
    bool presentation_screen_rect_valid = false;
    std::uint32_t bitrate_bps = 18'000'000;
    arssyut::presentation::PresentationSettings presentation{};
    PresenterMomentaryBinding hold_zoom_hotkey{};
    PresenterMomentaryBinding overview_peek_hotkey{};
    arssyut::visual::ArVisualProductMode visual_mode =
        arssyut::visual::ArVisualProductMode::PixelAccurate;
    arssyut::visual::ArVisualGradeSettings visual =
        arssyut::visual::grade_for_mode(
            arssyut::visual::ArVisualProductMode::PixelAccurate);
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
    std::uint64_t presentation_input_dropped = 0;
    bool system_shortcut_hook_active = false;

    bool visual_analysis_available = false;
    std::uint64_t visual_analysis_submitted = 0;
    std::uint64_t visual_analysis_completed = 0;
    std::uint64_t visual_analysis_busy_skips = 0;
    std::uint64_t visual_analysis_map_failures = 0;
    std::uint32_t encoder_sample_buffer_length = 0;
    std::uint32_t encoder_sample_buffer_max_length = 0;

    std::uint32_t capture_p95_us = 0;
    std::uint32_t compositor_cpu_p95_us = 0;
    std::uint32_t compositor_gpu_p95_us = 0;

    float presentation_camera_center_x = 0.5f;
    float presentation_camera_center_y = 0.5f;
    float presentation_camera_zoom = 1.0f;
    bool worker_finished = true;

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

    // Lock-free bounded presenter command mailbox. Commands are consumed by
    // the recorder's existing presentation controller on its worker cadence.
    [[nodiscard]] bool request_presenter_command(
        PresenterCommand command) noexcept;

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
        std::uint64_t resource_generation,
        bool visual_analysis_primed = false,
        arssyut::visual::ArVisualSceneStats visual_scene_stats = {},
        arssyut::visual::ArVisualAdaptiveState visual_adaptive = {},
        arssyut::windows::MfH264Profile encoder_profile =
            arssyut::windows::MfH264Profile::Main,
        arssyut::windows::MfRateControlMode encoder_rate_control =
            arssyut::windows::MfRateControlMode::Default,
        bool encoder_quality_vs_speed_applied = false,
        std::uint32_t encoder_quality_vs_speed = 0,
        arssyut::windows::MfColorPipelineMode encoder_color_pipeline =
            arssyut::windows::MfColorPipelineMode::LegacyExplicit,
        bool encoder_color_pipeline_authoritative = false) noexcept;

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

    std::atomic<std::uint64_t> presentation_input_dropped_{0};
    std::atomic<bool> system_shortcut_hook_active_{false};

    std::atomic<bool> visual_analysis_available_{false};
    std::atomic<std::uint64_t> visual_analysis_submitted_{0};
    std::atomic<std::uint64_t> visual_analysis_completed_{0};
    std::atomic<std::uint64_t> visual_analysis_busy_skips_{0};
    std::atomic<std::uint64_t> visual_analysis_map_failures_{0};

    std::atomic<std::uint32_t> capture_p95_us_{0};
    std::atomic<std::uint32_t> compositor_cpu_p95_us_{0};
    std::atomic<std::uint32_t> compositor_gpu_p95_us_{0};

    std::atomic<float> presentation_camera_center_x_{0.5f};
    std::atomic<float> presentation_camera_center_y_{0.5f};
    std::atomic<float> presentation_camera_zoom_{1.0f};

    // Bounded O(1) presenter intent mailbox; no command queue/history grows
    // with recording duration or hotkey activity.
    std::atomic<std::uint32_t> presenter_toggle_zoom_requests_{0};
    std::atomic<std::int32_t> presenter_zoom_steps_{0};
    std::atomic<std::uint32_t> presenter_reset_requests_{0};
    std::atomic<std::uint32_t> presenter_freeze_toggle_requests_{0};

    std::atomic<bool> worker_finished_{true};

    std::atomic<std::uint64_t> memory_private_bytes_{0};
    std::atomic<std::uint64_t> memory_private_max_bytes_{0};
};

} // namespace arssyut::app

#endif
