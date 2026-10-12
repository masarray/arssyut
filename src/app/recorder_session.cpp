#include "app/recorder_session.hpp"
#include "app/recording_pause_clock.hpp"

#ifdef _WIN32

#if defined(ARSSYUT_ENABLE_PRODUCT_AUDIO)
#include "app/audio_product_runtime.hpp"
#endif

#include "core/result/status.hpp"
#include "platform/windows/capture/latest_frame_slot.hpp"
#include "platform/windows/capture/webcam_capture_source.hpp"
#include "platform/windows/capture/webcam_gpu_frame_slot.hpp"
#include "platform/windows/graphics/d3d11_device.hpp"
#include "platform/windows/media/mf_h264_mp4_writer.hpp"
#include "platform/windows/input/presentation_input_worker.hpp"
#include "platform/windows/video/native_video_pipeline.hpp"
#include "presentation/presentation_controller.hpp"
#include "presentation/momentary_presenter_gate.hpp"

#include <Psapi.h>
#include <dwmapi.h>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <memory>
#include <new>
#include <sstream>
#include <system_error>
#include <utility>

namespace arssyut::app {

namespace {

using arssyut::core::DiagnosticMetric;
using arssyut::core::MonotonicClock;
using arssyut::core::Status;
using arssyut::core::StatusCode;
using arssyut::core::TimePoint;
using arssyut::windows::D3D11Device;
using arssyut::windows::D3D11DevicePreference;
using arssyut::windows::LatestFrameSlot;
using arssyut::windows::MfH264Mp4Writer;
using arssyut::windows::MfVideoWriterConfig;
using arssyut::windows::NativeVideoPipeline;
using arssyut::windows::PresentationInputWorker;
using arssyut::presentation::PresentationController;
using arssyut::presentation::PresentationFrameState;
using arssyut::windows::VideoSlotAction;
using arssyut::windows::WgcCaptureSource;

[[nodiscard]] std::uint64_t private_bytes() noexcept
{
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);

    if (!GetProcessMemoryInfo(
            GetCurrentProcess(),
            reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&counters),
            sizeof(counters))) {
        return 0;
    }

    return static_cast<std::uint64_t>(
        counters.PrivateUsage);
}


void observe_memory_peak(
    std::atomic<std::uint64_t> &peak,
    std::uint64_t value) noexcept
{
    std::uint64_t current =
        peak.load(std::memory_order_relaxed);

    while (value > current &&
           !peak.compare_exchange_weak(
               current,
               value,
               std::memory_order_relaxed,
               std::memory_order_relaxed)) {
    }
}

[[nodiscard]] std::string utf8(
    const std::wstring &value)
{
    if (value.empty())
        return {};

    const int required = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0,
        nullptr,
        nullptr);

    if (required <= 0)
        return {};

    std::string result(
        static_cast<std::size_t>(required),
        '\0');

    const int written = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        result.data(),
        required,
        nullptr,
        nullptr);

    return written == required
        ? result
        : std::string{};
}

[[nodiscard]] bool target_screen_rect(
    const RecorderTarget &target,
    RECT &rect) noexcept
{
    rect = {};

    if (target.kind ==
        arssyut::windows::CaptureTargetKind::Monitor) {
        MONITORINFO info{};
        info.cbSize = sizeof(info);

        if (!target.monitor ||
            !GetMonitorInfoW(
                target.monitor,
                &info)) {
            return false;
        }

        rect = info.rcMonitor;
        return rect.right > rect.left &&
               rect.bottom > rect.top;
    }

    if (!target.window ||
        !IsWindow(target.window)) {
        return false;
    }

    if (SUCCEEDED(DwmGetWindowAttribute(
            target.window,
            DWMWA_EXTENDED_FRAME_BOUNDS,
            &rect,
            sizeof(rect))) &&
        rect.right > rect.left &&
        rect.bottom > rect.top) {
        return true;
    }

    if (!GetWindowRect(
            target.window,
            &rect)) {
        return false;
    }

    return rect.right > rect.left &&
           rect.bottom > rect.top;
}

[[nodiscard]] bool screen_to_content(
    const RECT &rect,
    LONG screen_x,
    LONG screen_y,
    float &content_x,
    float &content_y) noexcept
{
    const LONG width =
        rect.right - rect.left;
    const LONG height =
        rect.bottom - rect.top;

    if (width <= 0 || height <= 0)
        return false;

    content_x =
        static_cast<float>(
            screen_x - rect.left) /
        static_cast<float>(width);
    content_y =
        static_cast<float>(
            screen_y - rect.top) /
        static_cast<float>(height);

    const bool inside =
        content_x >= 0.0f &&
        content_x <= 1.0f &&
        content_y >= 0.0f &&
        content_y <= 1.0f;

    content_x =
        std::clamp(content_x, 0.0f, 1.0f);
    content_y =
        std::clamp(content_y, 0.0f, 1.0f);

    return inside;
}

[[nodiscard]] std::string json_escape(
    const std::string &value)
{
    std::string result;
    result.reserve(value.size() + 16);

    for (char c : value) {
        switch (c) {
        case '\\':
            result += "\\\\";
            break;
        case '"':
            result += "\\\"";
            break;
        case '\n':
            result += "\\n";
            break;
        case '\r':
            result += "\\r";
            break;
        case '\t':
            result += "\\t";
            break;
        default:
            result += c;
            break;
        }
    }

    return result;
}

} // namespace

RecorderSession::~RecorderSession()
{
    request_stop();
    wait();
}

Status RecorderSession::start(
    RecorderConfig config)
{
    if (worker_.joinable() ||
        state_.load(std::memory_order_acquire) !=
            RecorderState::Idle ||
        config.output_path.empty() ||
        !config.output_size.valid() ||
        !config.frame_rate.valid()) {
        return Status::failure(
            StatusCode::InvalidArgument);
    }

#if !defined(ARSSYUT_ENABLE_PRODUCT_AUDIO)
    if (config.audio_microphone || config.audio_system)
        return Status::failure(StatusCode::Unsupported);
#endif

    // Product mode is the single public visual authority. Always rebuild the
    // internal P5A/P5B grade from the mode so UI/config callers cannot drift
    // into an undocumented hybrid preset.
    config.visual =
        arssyut::visual::grade_for_mode(
            config.visual_mode);

    config_ = std::move(config);
    pause_requested_.store(false, std::memory_order_release);
    pause_began_at_ticks_.store(0, std::memory_order_release);
    pause_excluded_ticks_.store(0, std::memory_order_release);
    pause_count_.store(0, std::memory_order_release);
    pause_resume_events_ = 0;
    pause_resume_seams_.fill(0);
    microphone_mute_requested_.store(
        config_.audio_microphone_muted, std::memory_order_release);
    microphone_mute_applied_.store(
        config_.audio_microphone_muted, std::memory_order_release);
    stop_requested_.store(
        false,
        std::memory_order_release);
    start_commit_requested_.store(
        false,
        std::memory_order_release);
    const auto requested_at =
        MonotonicClock::now();
    start_requested_at_ticks_.store(
        requested_at.ticks_100ns,
        std::memory_order_release);
    armed_at_ticks_.store(
        0,
        std::memory_order_release);
    first_frame_submitted_at_ticks_.store(
        0,
        std::memory_order_release);
    capture_preroll_received_.store(
        0,
        std::memory_order_release);
    started_at_ticks_.store(
        0,
        std::memory_order_release);
    stopped_at_ticks_.store(
        0,
        std::memory_order_release);
    error_code_.store(
        static_cast<std::uint32_t>(
            StatusCode::Ok),
        std::memory_order_release);
    error_detail_.store(
        0,
        std::memory_order_release);
    encoder_failure_stage_.store(
        arssyut::windows::MfWriterStage::None,
        std::memory_order_release);
    encoder_sample_buffer_length_.store(
        0,
        std::memory_order_release);
    encoder_sample_buffer_max_length_.store(
        0,
        std::memory_order_release);
    presentation_input_dropped_.store(
        0,
        std::memory_order_release);
    system_shortcut_hook_active_.store(
        false,
        std::memory_order_release);
    visual_analysis_available_.store(
        false,
        std::memory_order_release);
    visual_analysis_submitted_.store(
        0,
        std::memory_order_release);
    visual_analysis_completed_.store(
        0,
        std::memory_order_release);
    visual_analysis_busy_skips_.store(
        0,
        std::memory_order_release);
    visual_analysis_map_failures_.store(
        0,
        std::memory_order_release);
    presentation_camera_center_x_.store(
        0.5f,
        std::memory_order_release);
    presentation_camera_center_y_.store(
        0.5f,
        std::memory_order_release);
    presentation_camera_zoom_.store(
        1.0f,
        std::memory_order_release);
    presenter_toggle_zoom_requests_.store(
        0,
        std::memory_order_release);
    presenter_zoom_steps_.store(
        0,
        std::memory_order_release);
    presenter_reset_requests_.store(
        0,
        std::memory_order_release);
    presenter_freeze_toggle_requests_.store(
        0,
        std::memory_order_release);
    worker_finished_.store(
        false,
        std::memory_order_release);

    state_.store(
        RecorderState::Preparing,
        std::memory_order_release);

    try {
        worker_ = std::thread(
            [this]() noexcept {
                worker_main();
            });
    } catch (...) {
        worker_finished_.store(
            true,
            std::memory_order_release);
        state_.store(
            RecorderState::Failed,
            std::memory_order_release);
        return Status::failure(
            StatusCode::InternalError);
    }

    return Status::success();
}

void RecorderSession::request_stop() noexcept
{
    stop_requested_.store(
        true,
        std::memory_order_release);
}

bool RecorderSession::request_toggle_microphone_mute() noexcept
{
    const auto state = state_.load(std::memory_order_acquire);
    if (!config_.audio_microphone ||
        (state != RecorderState::Recording &&
         state != RecorderState::Paused) ||
        stop_requested_.load(std::memory_order_acquire))
        return false;
    // A toggle while Paused is retained as intent. The sole writer worker
    // applies it to the existing canonical mixer on the next Resume; it
    // neither opens a WASAPI source nor advances the paused media clock.

    bool current = microphone_mute_requested_.load(std::memory_order_acquire);
    while (!microphone_mute_requested_.compare_exchange_weak(
        current, !current, std::memory_order_acq_rel,
        std::memory_order_acquire)) {}
    return true;
}

bool RecorderSession::request_toggle_pause() noexcept
{
    const auto state = state_.load(std::memory_order_acquire);
    if ((state != RecorderState::Recording &&
         state != RecorderState::Paused) ||
        stop_requested_.load(std::memory_order_acquire))
        return false;
    bool value = pause_requested_.load(std::memory_order_acquire);
    while (!pause_requested_.compare_exchange_weak(
        value, !value, std::memory_order_acq_rel,
        std::memory_order_acquire)) {}
    return true;
}

bool RecorderSession::request_start_commit() noexcept
{
    if (state_.load(
            std::memory_order_acquire) !=
        RecorderState::Armed) {
        return false;
    }

    bool expected = false;
    return start_commit_requested_.
        compare_exchange_strong(
            expected,
            true,
            std::memory_order_acq_rel,
            std::memory_order_relaxed);
}

bool RecorderSession::request_presenter_command(
    PresenterCommand command) noexcept
{
    const auto state =
        state_.load(
            std::memory_order_acquire);

    // Armed product start must begin from a clean presenter state. Legacy
    // direct-start callers keep the historical Preparing acceptance.
    if (state != RecorderState::Recording &&
        !(state == RecorderState::Preparing &&
          !config_.start_armed)) {
        return false;
    }

    switch (command) {
    case PresenterCommand::ToggleZoom:
        presenter_toggle_zoom_requests_.fetch_add(
            1,
            std::memory_order_release);
        return true;
    case PresenterCommand::ZoomIn:
        presenter_zoom_steps_.fetch_add(
            1,
            std::memory_order_release);
        return true;
    case PresenterCommand::ZoomOut:
        presenter_zoom_steps_.fetch_sub(
            1,
            std::memory_order_release);
        return true;
    case PresenterCommand::ResetFullFrame:
        presenter_reset_requests_.fetch_add(
            1,
            std::memory_order_release);
        return true;
    case PresenterCommand::ToggleFreezeCamera:
        presenter_freeze_toggle_requests_.fetch_add(
            1,
            std::memory_order_release);
        return true;
    default:
        return false;
    }
}

void RecorderSession::wait() noexcept
{
    if (worker_.joinable())
        worker_.join();
}

RecorderSnapshot RecorderSession::snapshot() const noexcept
{
    RecorderSnapshot result;
    result.state =
        state_.load(std::memory_order_acquire);
    result.microphone_enabled = config_.audio_microphone;
    result.microphone_muted = microphone_mute_requested_.load(
        std::memory_order_acquire);

    const std::int64_t started =
        started_at_ticks_.load(
            std::memory_order_acquire);
    std::int64_t stopped =
        stopped_at_ticks_.load(
            std::memory_order_acquire);

    if (started > 0) {
        if (stopped <= 0) {
            stopped = MonotonicClock::now().ticks_100ns;
            const auto pause_start = pause_began_at_ticks_.load(
                std::memory_order_acquire);
            if (pause_start > 0)
                stopped = (std::min)(stopped, pause_start);
            stopped -= pause_excluded_ticks_.load(std::memory_order_acquire);
        }
        // Completed recording uses already-translated media stop QPC.
        result.elapsed_ticks =
            std::max<std::int64_t>(0, stopped - started);
    }

    result.capture_received =
        diagnostics_.load(
            DiagnosticMetric::CaptureFramesReceived);
    result.capture_replaced =
        diagnostics_.load(
            DiagnosticMetric::CaptureFramesReplaced);
    result.capture_busy_drops =
        diagnostics_.load(
            DiagnosticMetric::CaptureFramesDroppedBusy);
    result.video_rendered =
        diagnostics_.load(
            DiagnosticMetric::VideoFramesRendered);
    result.video_reused =
        diagnostics_.load(
            DiagnosticMetric::VideoFramesReused);
    result.video_skipped =
        diagnostics_.load(
            DiagnosticMetric::VideoFramesSkipped);
    result.encoder_submitted =
        diagnostics_.load(
            DiagnosticMetric::EncoderFramesSubmitted);
    result.encoder_backpressure =
        diagnostics_.load(
            DiagnosticMetric::EncoderFramesBackpressured);
    result.presentation_input_dropped =
        presentation_input_dropped_.load(
            std::memory_order_relaxed);
    result.system_shortcut_hook_active =
        system_shortcut_hook_active_.load(
            std::memory_order_relaxed);

    result.visual_analysis_available =
        visual_analysis_available_.load(
            std::memory_order_relaxed);
    result.visual_analysis_submitted =
        visual_analysis_submitted_.load(
            std::memory_order_relaxed);
    result.visual_analysis_completed =
        visual_analysis_completed_.load(
            std::memory_order_relaxed);
    result.visual_analysis_busy_skips =
        visual_analysis_busy_skips_.load(
            std::memory_order_relaxed);
    result.visual_analysis_map_failures =
        visual_analysis_map_failures_.load(
            std::memory_order_relaxed);

    result.encoder_sample_buffer_length =
        encoder_sample_buffer_length_.load(
            std::memory_order_relaxed);
    result.encoder_sample_buffer_max_length =
        encoder_sample_buffer_max_length_.load(
            std::memory_order_relaxed);

    result.capture_p95_us =
        capture_p95_us_.load(
            std::memory_order_relaxed);
    result.compositor_cpu_p95_us =
        compositor_cpu_p95_us_.load(
            std::memory_order_relaxed);
    result.compositor_gpu_p95_us =
        compositor_gpu_p95_us_.load(
            std::memory_order_relaxed);

    result.presentation_camera_center_x =
        presentation_camera_center_x_.load(
            std::memory_order_relaxed);
    result.presentation_camera_center_y =
        presentation_camera_center_y_.load(
            std::memory_order_relaxed);
    result.presentation_camera_zoom =
        presentation_camera_zoom_.load(
            std::memory_order_relaxed);
    result.worker_finished =
        worker_finished_.load(
            std::memory_order_acquire);

    result.memory_private_bytes =
        memory_private_bytes_.load(
            std::memory_order_relaxed);
    result.memory_private_max_bytes =
        memory_private_max_bytes_.load(
            std::memory_order_relaxed);

    result.last_error = Status::failure(
        static_cast<StatusCode>(
            error_code_.load(
                std::memory_order_acquire)),
        error_detail_.load(
            std::memory_order_acquire));
    result.encoder_failure_stage =
        encoder_failure_stage_.load(
            std::memory_order_acquire);

    return result;
}

void RecorderSession::fail(Status status) noexcept
{
    error_code_.store(
        static_cast<std::uint32_t>(status.code),
        std::memory_order_release);
    error_detail_.store(
        status.detail,
        std::memory_order_release);
}

void RecorderSession::worker_main() noexcept
{
    struct WorkerFinishGuard {
        std::atomic<bool> &flag;
        ~WorkerFinishGuard() noexcept
        {
            flag.store(
                true,
                std::memory_order_release);
        }
    } finish_guard{worker_finished_};

    // This guard is constructed BEFORE all GPU/video locals. It transfers
    // consumer ownership back to the preview worker only AFTER compositor,
    // WGC, MF writer and recorder-owned device references are destroyed.
    struct SharedCameraReturn {
        std::shared_ptr<arssyut::windows::WebcamPreviewSession> camera;
        ~SharedCameraReturn() noexcept {
            if (camera) camera->end_recording();
        }
    } camera_return{config_.shared_webcam};

    std::error_code file_ec;
    std::filesystem::create_directories(
        config_.output_path.parent_path(),
        file_ec);

    if (file_ec) {
        fail(Status::failure(
            StatusCode::StorageFailure,
            static_cast<std::uint32_t>(
                file_ec.value())));
        state_.store(
            RecorderState::Failed,
            std::memory_order_release);
        return;
    }

    const std::uint64_t memory_start =
        private_bytes();
    memory_private_bytes_.store(
        memory_start,
        std::memory_order_release);
    memory_private_max_bytes_.store(
        memory_start,
        std::memory_order_release);

    // Reuse the EXACT physical-camera D3D11 device, not just its adapter
    // LUID. Recorder is now the sole GPU immediate-context consumer.
    std::shared_ptr<D3D11Device> device;
    if (config_.shared_webcam) {
        device = config_.shared_webcam->recording_device();
    } else {
        auto created = D3D11Device::create(
            D3D11DevicePreference::HardwareOnly, false);
        if (created)
            device = std::move(created).value();
    }
    if (!device) {
        fail(Status::failure(StatusCode::PlatformFailure));
        state_.store(
            RecorderState::Failed,
            std::memory_order_release);
        return;
    }

    LatestFrameSlot frame_slot;
    // Mailbox and camera lifetime both belong to this one recorder session;
    // destruct camera first so the callback never sees a freed slot/device.
    arssyut::windows::WebcamGpuFrameSlot webcam_slot;
    arssyut::windows::WebcamCaptureSource webcam_source;

    auto pipeline_result =
        NativeVideoPipeline::create(
            device->device(),
            frame_slot,
            diagnostics_);
    if (!pipeline_result) {
        fail(pipeline_result.status());
        state_.store(
            RecorderState::Failed,
            std::memory_order_release);
        return;
    }

    auto pipeline =
        std::move(pipeline_result).value();
    // Immutable profile resolved before writer Start; never mutate GPU state
    // from the settings dispatcher or allocate per composited frame.
    if (!pipeline->compositor().set_camera_overlay_profile(config_.camera_overlay)) {
        fail(Status::failure(StatusCode::InvalidArgument));
        state_.store(RecorderState::Failed, std::memory_order_release);
        return;
    }
    // Optional PNG overlay: a corrupt/missing image may disable the logo,
    // NEVER abort microphone, desktop capture or the single MP4 writer.
    (void)pipeline->compositor().set_logo_overlay_profile(
        config_.logo_overlay, config_.logo_png_path);

    visual_analysis_available_.store(
        pipeline->compositor().
            scene_analysis_available(),
        std::memory_order_release);

    MfH264Mp4Writer writer;
    MfVideoWriterConfig writer_config;
    writer_config.size = config_.output_size;
    writer_config.frame_rate =
        config_.frame_rate;
    writer_config.bitrate_bps =
        config_.bitrate_bps;

#if defined(ARSSYUT_ENABLE_PRODUCT_AUDIO)
    // This is the SAME RecorderSession worker as video/MF. Preparing pins
    // default/explicit devices once, starts the existing WASAPI wrappers and
    // checks readiness before opening the one AV Sink Writer.
    std::unique_ptr<audio::ProductAudioRuntime> audio_runtime;
    if (config_.audio_microphone || config_.audio_system) {
        audio_runtime.reset(new (std::nothrow) audio::ProductAudioRuntime);
        if (!audio_runtime) {
            fail(Status::failure(StatusCode::InternalError));
            state_.store(RecorderState::Failed, std::memory_order_release);
            return;
        }
        const audio::AudioEndpointRequest request{
            .microphone = config_.audio_microphone,
            .system_audio = config_.audio_system,
            .microphone_id = config_.audio_microphone_id,
            .system_audio_id = config_.audio_system_id,
        };
        const auto preflight = audio_runtime->prepare(request);
        if (!preflight.ok()) {
            fail(preflight);
            state_.store(RecorderState::Failed, std::memory_order_release);
            return;
        }
    }
#endif

    Status status = writer.open(
        device->device(),
        config_.output_path,
        writer_config
#if defined(ARSSYUT_ENABLE_PRODUCT_AUDIO)
        , arssyut::windows::MfAudioWriterConfig{
              .enabled = static_cast<bool>(audio_runtime),
              .sample_rate = 48'000,
              .channels = 2,
              .bitrate_bps = 192'000
          }
#endif
    );
    if (!status.ok()) {
        encoder_failure_stage_.store(
            writer.failure_stage(),
            std::memory_order_release);
        fail(status);

        const std::uint64_t memory_end =
            private_bytes();
        memory_private_bytes_.store(
            memory_end,
            std::memory_order_relaxed);
        observe_memory_peak(
            memory_private_max_bytes_,
            memory_end);

        encoder_sample_buffer_length_.store(
            writer.last_sample_buffer_length(),
            std::memory_order_relaxed);
        encoder_sample_buffer_max_length_.store(
            writer.last_sample_buffer_max_length(),
            std::memory_order_relaxed);

        write_diagnostics(
            0,
            memory_start,
            memory_end,
            writer.submitted_frames(),
            writer.backpressure_events(),
            pipeline->compositor().resource_generation(),
            false,
            {},
            {},
            writer.active_profile(),
            writer.active_rate_control(),
            writer.quality_vs_speed_applied(),
            writer.requested_quality_vs_speed(),
            writer.active_color_pipeline(),
            writer.color_pipeline_authoritative());

        state_.store(
            RecorderState::Failed,
            std::memory_order_release);
        return;
    }

#if defined(ARSSYUT_ENABLE_PRODUCT_AUDIO)
    if (audio_runtime) {
        audio_writer_enabled_.store(
            writer.audio_enabled(), std::memory_order_relaxed);
        status = audio_runtime->writer_opened();
        if (!status.ok()) {
            fail(status);
            (void)writer.finalize();
            state_.store(RecorderState::Failed, std::memory_order_release);
            return;
        }
    }
#endif

    const bool presentation_enabled =
        config_.presentation.needs_presentation_frames();

    arssyut::windows::WgcCaptureOptions capture_options;
    // P4R.3A: keep the Windows/WGC cursor as the single cursor authority.
    // ArZoom samples the captured desktop, so the native cursor naturally
    // scales with the same camera transform without a second cursor layer.
    capture_options.capture_cursor = true;

    WgcCaptureSource capture;
    if (config_.target.kind ==
        arssyut::windows::CaptureTargetKind::Window) {
        status = capture.start_window(
            device->device(),
            config_.target.window,
            frame_slot,
            diagnostics_,
            capture_options);
    } else {
        status = capture.start_monitor(
            device->device(),
            config_.target.monitor,
            frame_slot,
            diagnostics_,
            capture_options);
    }

    if (!status.ok()) {
        fail(status);
        (void)writer.finalize();

        const std::uint64_t memory_end =
            private_bytes();
        memory_private_bytes_.store(
            memory_end,
            std::memory_order_relaxed);
        observe_memory_peak(
            memory_private_max_bytes_,
            memory_end);

        encoder_sample_buffer_length_.store(
            writer.last_sample_buffer_length(),
            std::memory_order_relaxed);
        encoder_sample_buffer_max_length_.store(
            writer.last_sample_buffer_max_length(),
            std::memory_order_relaxed);

        write_diagnostics(
            0,
            memory_start,
            memory_end,
            writer.submitted_frames(),
            writer.backpressure_events(),
            pipeline->compositor().resource_generation(),
            false,
            {},
            {},
            writer.active_profile(),
            writer.active_rate_control(),
            writer.quality_vs_speed_applied(),
            writer.requested_quality_vs_speed(),
            writer.active_color_pipeline(),
            writer.color_pipeline_authoritative());

        state_.store(
            RecorderState::Failed,
            std::memory_order_release);
        return;
    }

    const auto camera_before = config_.shared_webcam
        ? config_.shared_webcam->capture_snapshot()
        : arssyut::windows::WebcamCaptureSnapshot{};
    if (config_.webcam_enabled) {
        if (config_.shared_webcam) {
            pipeline->set_webcam_source(&config_.shared_webcam->frames());
            pipeline->set_webcam_preview_sink(config_.shared_webcam.get());
        } else {
            const auto camera_status = webcam_source.start(
                device->device(), config_.webcam_symbolic_link, webcam_slot);
            if (camera_status.ok())
                pipeline->set_webcam_source(&webcam_slot);
        }
        // The no-preview fallback keeps the established optional camera
        // behavior. A shared coordinator never creates a second MF source.
    }

    PresentationInputWorker presentation_input;
    PresentationController presentation_controller;
    presentation_controller.reset();
    presentation_controller.set_settings(
        config_.presentation);

    if (presentation_enabled) {
        status = presentation_input.start();
        if (!status.ok()) {
            fail(status);
            capture.stop();
            (void)writer.finalize();

            const std::uint64_t memory_end =
                private_bytes();
            memory_private_bytes_.store(
                memory_end,
                std::memory_order_relaxed);
            observe_memory_peak(
                memory_private_max_bytes_,
                memory_end);

            write_diagnostics(
                0,
                memory_start,
                memory_end,
                writer.submitted_frames(),
                writer.backpressure_events(),
                pipeline->compositor().resource_generation(),
                false,
                {},
                {},
                writer.active_profile(),
                writer.active_rate_control(),
                writer.quality_vs_speed_applied(),
                writer.requested_quality_vs_speed(),
                writer.active_color_pipeline(),
                writer.color_pipeline_authoritative());

            state_.store(
                RecorderState::Failed,
                std::memory_order_release);
            return;
        }

        system_shortcut_hook_active_.store(
            presentation_input.system_shortcut_hook_active(),
            std::memory_order_relaxed);
        presentation_input_dropped_.store(
            presentation_input.dropped_events(),
            std::memory_order_relaxed);
    }

    // Product countdown uses an explicit Armed gate. All expensive startup
    // work is already complete here: encoder open, WGC running, compositor
    // created and presentation input warm. Require one real WGC source frame
    // before publishing Armed so ACTION can deterministically define frame 0.
    if (config_.start_armed) {
        const auto first_frame_deadline =
            MonotonicClock::now().ticks_100ns +
            MonotonicClock::ticks_per_second * 5;

        while (!frame_slot.has_in_flight() &&
               !stop_requested_.load(
                   std::memory_order_acquire) &&
               !capture.source_closed() &&
               MonotonicClock::now().ticks_100ns <
                   first_frame_deadline) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(1));
        }

        const auto cancel_before_start =
            [&]() noexcept {
                presentation_input.stop();
                capture.stop();
                (void)writer.finalize();

                std::error_code remove_ec;
                std::filesystem::remove(
                    config_.output_path,
                    remove_ec);
                remove_ec.clear();
                std::filesystem::remove(
                    diagnostics_path(),
                    remove_ec);

                state_.store(
                    RecorderState::Idle,
                    std::memory_order_release);
            };

        if (stop_requested_.load(
                std::memory_order_acquire)) {
            cancel_before_start();
            return;
        }

        if (!frame_slot.has_in_flight() ||
            capture.source_closed()) {
            fail(Status::failure(
                StatusCode::PlatformFailure,
                capture.source_closed()
                    ? 0U
                    : static_cast<std::uint32_t>(
                          WAIT_TIMEOUT)));

            presentation_input.stop();
            capture.stop();
            (void)writer.finalize();

            const std::uint64_t memory_end =
                private_bytes();
            memory_private_bytes_.store(
                memory_end,
                std::memory_order_relaxed);
            observe_memory_peak(
                memory_private_max_bytes_,
                memory_end);

            write_diagnostics(
                0,
                memory_start,
                memory_end,
                writer.submitted_frames(),
                writer.backpressure_events(),
                pipeline->compositor().
                    resource_generation(),
                false,
                {},
                {},
                writer.active_profile(),
                writer.active_rate_control(),
                writer.quality_vs_speed_applied(),
                writer.requested_quality_vs_speed(),
                writer.active_color_pipeline(),
                writer.color_pipeline_authoritative());

            state_.store(
                RecorderState::Failed,
                std::memory_order_release);
            return;
        }

        const auto armed =
            MonotonicClock::now();
        armed_at_ticks_.store(
            armed.ticks_100ns,
            std::memory_order_release);
        state_.store(
            RecorderState::Armed,
            std::memory_order_release);

        while (!start_commit_requested_.load(
                   std::memory_order_acquire) &&
               !stop_requested_.load(
                   std::memory_order_acquire) &&
               !capture.source_closed()) {
#if defined(ARSSYUT_ENABLE_PRODUCT_AUDIO)
            if (audio_runtime)
                audio_runtime->discard_preroll();
#endif
            // Countdown freezes the MEDIA clock, not the camera's
            // physical preview. Keep a fresh UI thumbnail on one GPU owner.
            pipeline->preview_while_paused(device->immediate_context());
            std::this_thread::sleep_for(
                std::chrono::milliseconds(1));
        }

        if (stop_requested_.load(
                std::memory_order_acquire)) {
            cancel_before_start();
            return;
        }

        if (capture.source_closed()) {
            fail(Status::failure(
                StatusCode::PlatformFailure));
            presentation_input.stop();
            capture.stop();
            (void)writer.finalize();
            state_.store(
                RecorderState::Failed,
                std::memory_order_release);
            return;
        }

        if (presentation_enabled)
            presentation_input.discard_pending_events();

        // Do not let Zoom/Reset/Freeze intents pressed during pre-roll become
        // the first semantic action in the recording.
        presenter_toggle_zoom_requests_.store(
            0,
            std::memory_order_release);
        presenter_zoom_steps_.store(
            0,
            std::memory_order_release);
        presenter_reset_requests_.store(
            0,
            std::memory_order_release);
        presenter_freeze_toggle_requests_.store(
            0,
            std::memory_order_release);

        capture_preroll_received_.store(
            diagnostics_.load(
                DiagnosticMetric::
                    CaptureFramesReceived),
            std::memory_order_relaxed);
    }

    const TimePoint start =
        MonotonicClock::now();

    status = pipeline->reset_timeline(
        start,
        config_.frame_rate);
#if defined(ARSSYUT_ENABLE_PRODUCT_AUDIO)
    if (status.ok() && audio_runtime)
        status = audio_runtime->begin(start.ticks_100ns,
            config_.audio_microphone_gain, config_.audio_microphone_muted,
            config_.audio_system_gain, config_.audio_system_muted);
#endif
    if (!status.ok()) {
        fail(status);
        presentation_input.stop();
        capture.stop();
        (void)writer.finalize();

        const std::uint64_t memory_end =
            private_bytes();
        memory_private_bytes_.store(
            memory_end,
            std::memory_order_relaxed);
        observe_memory_peak(
            memory_private_max_bytes_,
            memory_end);

        encoder_sample_buffer_length_.store(
            writer.last_sample_buffer_length(),
            std::memory_order_relaxed);
        encoder_sample_buffer_max_length_.store(
            writer.last_sample_buffer_max_length(),
            std::memory_order_relaxed);

        write_diagnostics(
            0,
            memory_start,
            memory_end,
            writer.submitted_frames(),
            writer.backpressure_events(),
            pipeline->compositor().resource_generation(),
            false,
            {},
            {},
            writer.active_profile(),
            writer.active_rate_control(),
            writer.quality_vs_speed_applied(),
            writer.requested_quality_vs_speed(),
            writer.active_color_pipeline(),
            writer.color_pipeline_authoritative());

        state_.store(
            RecorderState::Failed,
            std::memory_order_release);
        return;
    }

    arssyut::app::RecordingPauseClock pause_clock;
    pause_clock.reset(start.ticks_100ns);
    started_at_ticks_.store(
        start.ticks_100ns,
        std::memory_order_release);
    state_.store(
        RecorderState::Recording,
        std::memory_order_release);

    const std::int64_t frame_duration =
        (MonotonicClock::ticks_per_second *
         static_cast<std::int64_t>(
             config_.frame_rate.denominator)) /
        static_cast<std::int64_t>(
            config_.frame_rate.numerator);

    TimePoint next_telemetry = {
        start.ticks_100ns +
        MonotonicClock::ticks_per_second / 4
    };

    TimePoint next_presentation = start;
    TimePoint previous_presentation = start;
    TimePoint next_target_rect_refresh = start;

    RECT presentation_target_rect{};
    bool presentation_target_valid = false;

    if (config_.
            presentation_screen_rect_valid) {
        presentation_target_rect =
            config_.
                presentation_screen_rect;
        presentation_target_valid =
            presentation_target_rect.right >
                presentation_target_rect.left &&
            presentation_target_rect.bottom >
                presentation_target_rect.top;
    } else {
        presentation_target_valid =
            target_screen_rect(
                config_.target,
                presentation_target_rect);
    }

    PresentationFrameState presentation_state{};

    // Reset / Full Frame in upstream ArZoom clears momentary state even when
    // the user is still physically holding the chord. These deterministic
    // interlocks prevent re-arm until each chord is actually released once.
    arssyut::presentation::MomentaryReleaseGate
        hold_zoom_gate;
    arssyut::presentation::MomentaryReleaseGate
        overview_gate;

    bool failed = false;
    std::int64_t last_resume_physical_qpc = start.ticks_100ns;
    std::uint32_t consecutive_video_backpressure = 0;

    while (!stop_requested_.load(
        std::memory_order_acquire)) {
        const TimePoint physical_now = MonotonicClock::now();
        const bool want_pause = pause_requested_.load(std::memory_order_acquire);
        if (want_pause && !pause_clock.paused()) {
            if (!pause_clock.pause(physical_now.ticks_100ns)) {
                fail(Status::failure(StatusCode::InvalidStateTransition));
                failed = true;
                break;
            }
#if defined(ARSSYUT_ENABLE_PRODUCT_AUDIO)
            if (audio_runtime) audio_runtime->pause();
#endif
            pause_began_at_ticks_.store(
                physical_now.ticks_100ns, std::memory_order_release);
            pause_count_.fetch_add(1, std::memory_order_release);
            state_.store(RecorderState::Paused, std::memory_order_release);
        } else if (!want_pause && pause_clock.paused()) {
            if (!pause_clock.resume(physical_now.ticks_100ns)) {
                fail(Status::failure(StatusCode::InvalidStateTransition));
                failed = true;
                break;
            }
#if defined(ARSSYUT_ENABLE_PRODUCT_AUDIO)
            if (audio_runtime) audio_runtime->resume(
                pause_clock.committed_excluded_ticks(),
                physical_now.ticks_100ns);
#endif
            pause_excluded_ticks_.store(
                pause_clock.committed_excluded_ticks(), std::memory_order_release);
            pause_began_at_ticks_.store(0, std::memory_order_release);
            // Exact seek locations for post-capture per-seam waveform and
            // packet-PTS audit. Recorder worker owns these; no heap/locks.
            if (pause_resume_events_ < kMaxPauseResumeSeams)
                pause_resume_seams_[static_cast<std::size_t>(
                    pause_resume_events_)] =
                    pause_clock.elapsed(physical_now.ticks_100ns);
            ++pause_resume_events_;
            last_resume_physical_qpc = physical_now.ticks_100ns;
            previous_presentation = physical_now;
            next_presentation = physical_now;
            state_.store(RecorderState::Recording, std::memory_order_release);
        }
        if (pause_clock.paused()) {
#if defined(ARSSYUT_ENABLE_PRODUCT_AUDIO)
            if (audio_runtime) audio_runtime->discard_while_paused();
#endif
            // Input during excluded time must never replay as post-resume
            // clicks/keycap sounds or build an unbounded semantic backlog.
            arssyut::windows::MouseClickEvent ignored_click{};
            arssyut::windows::ShortcutEvent ignored_shortcut{};
            for (int n=0; n<16 && presentation_input.try_pop_click(ignored_click); ++n) {}
            for (int n=0; n<16 && presentation_input.try_pop_shortcut(ignored_shortcut); ++n) {}
            // Physical camera keeps publishing a tiny preview without
            // encoding frames or advancing the compressed A/V timeline.
            pipeline->preview_while_paused(device->immediate_context());
            // Existing producer threads remain live but their bounded queues
            // cannot grow; the one writer and A/V media clock stay frozen.
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        const TimePoint media_now{
            pause_clock.media_now(physical_now.ticks_100ns)};
#if defined(ARSSYUT_ENABLE_PRODUCT_AUDIO)
        if (audio_runtime && config_.audio_microphone) {
            const bool desired = microphone_mute_requested_.load(
                std::memory_order_acquire);
            if (desired != microphone_mute_applied_.load(
                    std::memory_order_relaxed)) {
                audio_runtime->set_microphone_muted(
                    desired, config_.audio_microphone_gain);
                microphone_mute_applied_.store(
                    desired, std::memory_order_release);
            }
        }
        // Keep the AAC mux timeline fed BEFORE reserving another MF video
        // surface. Audio-only work is bounded; video-only retains its
        // existing due-work and timing path unchanged.
        if (audio_runtime) {
            const auto audio_status =
                audio_runtime->service(media_now, writer);
            if (!audio_status.ok()) {
                fail(audio_status);
                failed = true;
                break;
            }
        }
#endif
        const TimePoint now = physical_now;

        if (presentation_enabled &&
            !(now < next_target_rect_refresh)) {
            if (!config_.
                    presentation_screen_rect_valid) {
                presentation_target_valid =
                    target_screen_rect(
                        config_.target,
                        presentation_target_rect);
            }

            next_target_rect_refresh = {
                now.ticks_100ns +
                MonotonicClock::ticks_per_second / 4
            };
        }

        if (presentation_enabled &&
            !(now < next_presentation)) {
            // Consume bridge presenter intent at the same cadence as the
            // existing ArZoom camera. Reset is applied last so Full Frame wins
            // a same-tick race without changing the configured zoom amount.
            const auto toggle_requests =
                presenter_toggle_zoom_requests_.exchange(
                    0,
                    std::memory_order_acq_rel);
            if ((toggle_requests & 1U) != 0)
                presentation_controller.toggle_manual_zoom();

            const auto zoom_steps =
                presenter_zoom_steps_.exchange(
                    0,
                    std::memory_order_acq_rel);
            if (zoom_steps != 0) {
                presentation_controller.adjust_manual_zoom(
                    0.25f *
                    static_cast<float>(
                        zoom_steps));
            }

            const auto freeze_toggle_requests =
                presenter_freeze_toggle_requests_.exchange(
                    0,
                    std::memory_order_acq_rel);
            if ((freeze_toggle_requests & 1U) != 0)
                presentation_controller.toggle_freeze_camera();

            if (presenter_reset_requests_.exchange(
                    0,
                    std::memory_order_acq_rel) != 0) {
                presentation_controller.reset_full_frame();
                hold_zoom_gate.block_until_release();
                overview_gate.block_until_release();
            }

            const bool hold_zoom_pressed =
                config_.hold_zoom_hotkey.configured() &&
                presentation_input.chord_pressed(
                    config_.hold_zoom_hotkey.virtual_key,
                    config_.hold_zoom_hotkey.modifiers);

            const bool overview_pressed =
                config_.overview_peek_hotkey.configured() &&
                presentation_input.chord_pressed(
                    config_.overview_peek_hotkey.virtual_key,
                    config_.overview_peek_hotkey.modifiers);

            presentation_controller.set_hold_zoom(
                hold_zoom_gate.accept(
                    hold_zoom_pressed));
            presentation_controller.set_overview_peek(
                overview_gate.accept(
                    overview_pressed));

            arssyut::windows::MouseClickEvent click_event;
            while (presentation_input.try_pop_click(
                click_event)) {
                float click_x = 0.5f;
                float click_y = 0.5f;

                if (presentation_target_valid &&
                    screen_to_content(
                        presentation_target_rect,
                        click_event.screen_x,
                        click_event.screen_y,
                        click_x,
                        click_y)) {
                    presentation_controller.on_click(
                        click_event.kind,
                        click_x,
                        click_y,
                        click_event.time);
#if defined(ARSSYUT_ENABLE_PRODUCT_AUDIO)
                    if (audio_runtime && config_.presentation.click_visual &&
                        click_event.time.ticks_100ns >= last_resume_physical_qpc)
                        audio_runtime->record_click(
                            click_event.kind,
                            click_event.time.ticks_100ns -
                                pause_clock.committed_excluded_ticks());
#endif
                }
            }

            arssyut::windows::ShortcutEvent shortcut_event;
            while (presentation_input.try_pop_shortcut(
                shortcut_event)) {
                presentation_controller.on_shortcut(
                    shortcut_event.chord,
                    shortcut_event.time);
#if defined(ARSSYUT_ENABLE_PRODUCT_AUDIO)
                if (audio_runtime && config_.presentation.shortcut_keys &&
                    shortcut_event.time.ticks_100ns >= last_resume_physical_qpc)
                    audio_runtime->record_keycap(
                        shortcut_event.time.ticks_100ns -
                            pause_clock.committed_excluded_ticks());
#endif
            }

            const auto pointer =
                presentation_input.pointer();

            float cursor_x = 0.5f;
            float cursor_y = 0.5f;
            const bool cursor_valid =
                pointer.valid &&
                presentation_target_valid &&
                screen_to_content(
                    presentation_target_rect,
                    pointer.screen_x,
                    pointer.screen_y,
                    cursor_x,
                    cursor_y);

            const auto delta_ticks =
                MonotonicClock::duration_ticks(
                    previous_presentation,
                    now);
            const float presentation_dt =
                std::clamp(
                    static_cast<float>(delta_ticks) /
                        static_cast<float>(
                            MonotonicClock::ticks_per_second),
                    0.0f,
                    0.10f);

            presentation_state =
                presentation_controller.step(
                    presentation_dt,
                    cursor_x,
                    cursor_y,
                    cursor_valid,
                    now,
                    pointer.last_activity);

            presentation_camera_center_x_.store(
                presentation_state.camera_center_x,
                std::memory_order_relaxed);
            presentation_camera_center_y_.store(
                presentation_state.camera_center_y,
                std::memory_order_relaxed);
            presentation_camera_zoom_.store(
                presentation_state.camera_zoom,
                std::memory_order_relaxed);

            previous_presentation = now;
            next_presentation = {
                now.ticks_100ns + frame_duration
            };
        }

        auto frame_result =
            pipeline->process_due(
                device->immediate_context(),
                media_now,
                config_.crop,
                config_.output_size,
                presentation_enabled
                    ? &presentation_state
                    : nullptr,
                &config_.visual);

        if (!frame_result) {
            fail(frame_result.status());
            failed = true;
            break;
        }

        const auto &frame =
            frame_result.value();

        if (frame.action ==
                VideoSlotAction::RenderedNewFrame ||
            frame.action ==
                VideoSlotAction::RenderedRetainedSource) {
            const TimePoint relative_pts{
                frame.pts.ticks_100ns -
                start.ticks_100ns
            };

            const Status write_status =
                writer.write_frame(
                    device->immediate_context(),
                    pipeline->output_texture(),
                    relative_pts,
                    frame_duration);

        encoder_sample_buffer_length_.store(
            writer.last_sample_buffer_length(),
            std::memory_order_relaxed);
        encoder_sample_buffer_max_length_.store(
            writer.last_sample_buffer_max_length(),
            std::memory_order_relaxed);

            if (write_status.code ==
                StatusCode::EncoderBackpressure) {
                diagnostics_.increment(
                    DiagnosticMetric::
                        EncoderFramesBackpressured);
#if defined(ARSSYUT_ENABLE_PRODUCT_AUDIO)
                if (audio_runtime && ++consecutive_video_backpressure >= 30) {
                    fail(Status::failure(
                        StatusCode::EncoderBackpressure, 0xA002));
                    failed = true;
                    break;
                }
#endif
            } else if (!write_status.ok()) {
                diagnostics_.increment(
                    DiagnosticMetric::
                        EncoderWriteFailures);
                encoder_failure_stage_.store(
                    writer.failure_stage(),
                    std::memory_order_release);
                fail(write_status);
                failed = true;
                break;
            } else {
                consecutive_video_backpressure = 0;
                diagnostics_.increment(
                    DiagnosticMetric::
                        EncoderFramesSubmitted);

                if (first_frame_submitted_at_ticks_.load(
                        std::memory_order_relaxed) == 0) {
                    first_frame_submitted_at_ticks_.store(
                        MonotonicClock::now().ticks_100ns,
                        std::memory_order_relaxed);
                }
            }
        }

        if (capture.source_closed()) {
            fail(Status::failure(
                StatusCode::PlatformFailure));
            failed = true;
            break;
        }

        if (!(now < next_telemetry)) {
            const auto capture_latency =
                capture.capture_callback_latency();
            const auto cpu_latency =
                pipeline->compositor().
                    cpu_submit_latency();
            const auto gpu_latency =
                pipeline->compositor().
                    gpu_execution_latency();

            capture_p95_us_.store(
                capture_latency.quantile_upper_bound(
                    95,
                    100),
                std::memory_order_relaxed);
            compositor_cpu_p95_us_.store(
                cpu_latency.quantile_upper_bound(
                    95,
                    100),
                std::memory_order_relaxed);
            compositor_gpu_p95_us_.store(
                gpu_latency.quantile_upper_bound(
                    95,
                    100),
                std::memory_order_relaxed);

            presentation_input_dropped_.store(
                presentation_input.dropped_events(),
                std::memory_order_relaxed);
            system_shortcut_hook_active_.store(
                presentation_input.system_shortcut_hook_active(),
                std::memory_order_relaxed);

            visual_analysis_submitted_.store(
                pipeline->compositor().
                    scene_analysis_submitted(),
                std::memory_order_relaxed);
            visual_analysis_completed_.store(
                pipeline->compositor().
                    scene_analysis_completed(),
                std::memory_order_relaxed);
            visual_analysis_busy_skips_.store(
                pipeline->compositor().
                    scene_analysis_busy_skips(),
                std::memory_order_relaxed);
            visual_analysis_map_failures_.store(
                pipeline->compositor().
                    scene_analysis_map_failures(),
                std::memory_order_relaxed);

            const std::uint64_t current_memory =
                private_bytes();
            memory_private_bytes_.store(
                current_memory,
                std::memory_order_relaxed);

            observe_memory_peak(
                memory_private_max_bytes_,
                current_memory);

            next_telemetry = {
                now.ticks_100ns +
                MonotonicClock::ticks_per_second / 4
            };
        }

        std::this_thread::sleep_for(
            std::chrono::milliseconds(1));
    }

    // Recording duration ends with the render/write loop, not after MP4
    // finalization. This keeps the user-visible duration aligned with media.
    const TimePoint physical_stop = MonotonicClock::now();
    const bool stopped_during_pause = pause_clock.paused();
    const TimePoint recording_stopped{
        pause_clock.media_now(physical_stop.ticks_100ns)};
    pause_began_at_ticks_.store(0, std::memory_order_release);
    pause_excluded_ticks_.store(
        pause_clock.excluded_ticks(physical_stop.ticks_100ns),
        std::memory_order_release);
    stopped_at_ticks_.store(
        recording_stopped.ticks_100ns,
        std::memory_order_release);

    state_.store(
        RecorderState::Stopping,
        std::memory_order_release);

    presentation_input_dropped_.store(
        presentation_input.dropped_events(),
        std::memory_order_relaxed);
    system_shortcut_hook_active_.store(
        presentation_input.system_shortcut_hook_active(),
        std::memory_order_relaxed);

    visual_analysis_submitted_.store(
        pipeline->compositor().
            scene_analysis_submitted(),
        std::memory_order_relaxed);
    visual_analysis_completed_.store(
        pipeline->compositor().
            scene_analysis_completed(),
        std::memory_order_relaxed);
    visual_analysis_busy_skips_.store(
        pipeline->compositor().
            scene_analysis_busy_skips(),
        std::memory_order_relaxed);
    visual_analysis_map_failures_.store(
        pipeline->compositor().
            scene_analysis_map_failures(),
        std::memory_order_relaxed);

    presentation_input.stop();
    // Take a real source snapshot BEFORE stop() overwrites its state,
    // allowing user MP4 diagnostics to prove camera was actually captured.
    const auto webcam_health = config_.shared_webcam
        ? config_.shared_webcam->capture_snapshot()
        : webcam_source.snapshot();
    webcam_received_.store(webcam_health.received -
        (std::min)(webcam_health.received, camera_before.received));
    webcam_published_.store(webcam_health.published -
        (std::min)(webcam_health.published, camera_before.published));
    webcam_ticks_startup_.store(webcam_health.stream_ticks_startup);
    webcam_ticks_streaming_.store(webcam_health.stream_ticks_streaming);
    webcam_adapter_mismatch_.store(webcam_health.adapter_mismatch);
    webcam_foreign_device_.store(
        webcam_health.frames_other_device_same_adapter);
    webcam_hresult_.store(webcam_health.last_hresult);
    webcam_new_gpu_frames_.store(pipeline->webcam_new_gpu_frames());
    webcam_rendered_slots_.store(pipeline->webcam_rendered_slots());
    webcam_copy_failures_.store(pipeline->webcam_gpu_copy_failures());
    pipeline->set_webcam_source(nullptr);
    pipeline->set_webcam_preview_sink(nullptr);
    if (!config_.shared_webcam) webcam_source.stop();
    capture.stop();

#if defined(ARSSYUT_ENABLE_PRODUCT_AUDIO)
    if (audio_runtime) {
        const auto audio_status =
            audio_runtime->finish(recording_stopped, writer,
                stopped_during_pause);
        audio_health_ = audio_runtime->health_snapshot();
        if (!audio_status.ok() && !failed) {
            fail(audio_status);
            failed = true;
        }
    }
#endif

    state_.store(
        RecorderState::Finalizing,
        std::memory_order_release);

#if defined(ARSSYUT_ENABLE_PRODUCT_AUDIO)
    // A successful video MP4 is NOT a successful audio recording.
    // Fail the session if Mic/System was selected but the sole writer
    // never accepted a single AAC input sample.
    if (audio_runtime) {
        const auto audio_samples = writer.submitted_audio_samples();
        audio_samples_submitted_.store(
            audio_samples, std::memory_order_relaxed);
        audio_frames_submitted_.store(
            writer.submitted_audio_frames(), std::memory_order_relaxed);
        const auto captured_span = static_cast<std::uint64_t>(
            (std::max)(std::int64_t{0},
                recording_stopped.ticks_100ns - start.ticks_100ns));
        const auto audio_span = core::audio::frames_to_ticks_floor(
            writer.submitted_audio_frames(), 48'000);
        // A 170 ms AAC fragment does not qualify as a successful
        // 22-second recording. A/V must cover the real session.
        if ((!writer.audio_enabled() || audio_samples == 0 ||
             (captured_span > 20'000'000ULL &&
              audio_span < (captured_span * 8ULL) / 10ULL)) &&
            !failed) {
            fail(Status::failure(StatusCode::PlatformFailure, 0xA001));
            failed = true;
        }
        const auto expected_video =
            (captured_span * config_.frame_rate.numerator) /
            (10'000'000ULL * config_.frame_rate.denominator);
        if (!failed && captured_span > 20'000'000ULL &&
            writer.submitted_frames() < (expected_video * 8ULL) / 10ULL) {
            fail(Status::failure(StatusCode::EncoderBackpressure, 0xA003));
            failed = true;
        }
    }
#endif

    const std::uint64_t writer_submitted =
        writer.submitted_frames();
    const std::uint64_t writer_backpressure =
        writer.backpressure_events();
    const std::uint64_t resource_generation =
        pipeline->compositor().
            resource_generation();

    const bool visual_analysis_primed =
        pipeline->compositor().
            scene_analysis_primed();
    const auto visual_scene_stats =
        pipeline->compositor().
            scene_analysis_stats();
    const auto visual_adaptive =
        pipeline->compositor().
            scene_analysis_adaptive();

    const Status finalize_status =
        writer.finalize();

    if (!finalize_status.ok() && !failed) {
#if defined(ARSSYUT_ENABLE_PRODUCT_AUDIO)
        // Distinguish MF finalization from audio ingress errors in
        // the optional product-A/V build, without breaking video-only CI.
        if (audio_runtime && audio_health_.failure_phase == 0) {
            audio_health_.failure_phase = 6;
            audio_health_.failure_source = 3;
            audio_health_.failure_native_hresult = finalize_status.detail;
        }
#endif
        encoder_failure_stage_.store(
            writer.failure_stage(),
            std::memory_order_release);
        fail(finalize_status);
        failed = true;
    }

    const std::uint64_t memory_end =
        private_bytes();
    memory_private_bytes_.store(
        memory_end,
        std::memory_order_relaxed);
    observe_memory_peak(
        memory_private_max_bytes_,
        memory_end);

    encoder_sample_buffer_length_.store(
        writer.last_sample_buffer_length(),
        std::memory_order_relaxed);
    encoder_sample_buffer_max_length_.store(
        writer.last_sample_buffer_max_length(),
        std::memory_order_relaxed);

    std::uint64_t output_bytes = 0;
    const auto bytes =
        std::filesystem::file_size(
            config_.output_path,
            file_ec);
    if (!file_ec)
        output_bytes =
            static_cast<std::uint64_t>(bytes);

    write_diagnostics(
        output_bytes,
        memory_start,
        memory_end,
        writer_submitted,
        writer_backpressure,
        resource_generation,
        visual_analysis_primed,
        visual_scene_stats,
        visual_adaptive,
        writer.active_profile(),
        writer.active_rate_control(),
        writer.quality_vs_speed_applied(),
        writer.requested_quality_vs_speed(),
        writer.active_color_pipeline(),
        writer.color_pipeline_authoritative());

    state_.store(
        failed
            ? RecorderState::Failed
            : RecorderState::Ready,
        std::memory_order_release);
}

void RecorderSession::write_diagnostics(
    std::uint64_t output_bytes,
    std::uint64_t memory_start,
    std::uint64_t memory_end,
    std::uint64_t writer_submitted,
    std::uint64_t writer_backpressure,
    std::uint64_t resource_generation,
    bool visual_analysis_primed,
    arssyut::visual::ArVisualSceneStats visual_scene_stats,
    arssyut::visual::ArVisualAdaptiveState visual_adaptive,
    arssyut::windows::MfH264Profile encoder_profile,
    arssyut::windows::MfRateControlMode encoder_rate_control,
    bool encoder_quality_vs_speed_applied,
    std::uint32_t encoder_quality_vs_speed,
    arssyut::windows::MfColorPipelineMode encoder_color_pipeline,
    bool encoder_color_pipeline_authoritative) noexcept
{
    try {
        const auto snapshot_value =
            snapshot();

        std::ofstream out(
            diagnostics_path(),
            std::ios::binary |
            std::ios::trunc);
        if (!out)
            return;

        const std::string output =
            json_escape(
                utf8(
                    config_.output_path.wstring()));
        const std::string source =
            json_escape(
                utf8(
                    config_.target.label));

        auto applied_visual =
            config_.visual;
        arssyut::visual::apply_adaptive(
            applied_visual,
            visual_adaptive);
        applied_visual =
            arssyut::visual::sanitize(
                applied_visual);

        out
            << "{\n"
            << "  \"schema\": \"arssyut-diagnostics-v1\",\n"
            << "  \"result\": \""
            << (snapshot_value.last_error.code ==
                        StatusCode::Ok
                    ? "ready"
                    : "failed")
            << "\",\n"
            << "  \"source\": \"" << source << "\",\n"
            << "  \"output\": \"" << output << "\",\n"
            << "  \"output_bytes\": " << output_bytes << ",\n"
            << "  \"width\": " << config_.output_size.width << ",\n"
            << "  \"height\": " << config_.output_size.height << ",\n"
            << "  \"source_crop_active\": "
            << (config_.crop.valid()
                    ? "true"
                    : "false")
            << ",\n"
            << "  \"source_crop_left\": "
            << config_.crop.left << ",\n"
            << "  \"source_crop_top\": "
            << config_.crop.top << ",\n"
            << "  \"source_crop_right\": "
            << config_.crop.right << ",\n"
            << "  \"source_crop_bottom\": "
            << config_.crop.bottom << ",\n"
            << "  \"fps_num\": " << config_.frame_rate.numerator << ",\n"
            << "  \"fps_den\": " << config_.frame_rate.denominator << ",\n"
            << "  \"bitrate_bps\": " << config_.bitrate_bps << ",\n"
            << "  \"audio_microphone_requested\": "
            << (config_.audio_microphone ? "true" : "false") << ",\n"
            << "  \"audio_system_requested\": "
            << (config_.audio_system ? "true" : "false") << ",\n"
            << "  \"audio_writer_enabled\": "
            << (audio_writer_enabled_.load(std::memory_order_relaxed)
                ? "true" : "false") << ",\n"
            << "  \"audio_samples_submitted\": "
            << audio_samples_submitted_.load(std::memory_order_relaxed)
            << ",\n"
            << "  \"audio_frames_submitted\": "
            << audio_frames_submitted_.load(std::memory_order_relaxed)
            << ",\n"
            << "  \"audio_system_endpoint_mode\": \""
            << (config_.audio_system_id.empty() ? "windows_default" : "explicit")
            << "\",\n"
            << "  \"audio_system_zero_packets_warning\": "
            << (config_.audio_system && audio_health_.system.captured == 0
                ? "true" : "false") << ",\n"
            << "  \"audio_system_packets_captured\": " << audio_health_.system.captured << ",\n"
            << "  \"audio_system_packets_published\": " << audio_health_.system.published << ",\n"
            << "  \"audio_system_packets_silent\": " << audio_health_.system.silent << ",\n"
            << "  \"audio_system_packets_discontinuous\": " << audio_health_.system.discontinuities << ",\n"
            << "  \"audio_system_timestamp_errors\": " << audio_health_.system.timestamp_errors << ",\n"
            << "  \"audio_system_packets_dropped\": " << audio_health_.system.dropped << ",\n"
            << "  \"audio_system_pool_exhaustions\": " << audio_health_.system.pool_exhaustions << ",\n"
            << "  \"audio_system_ring_overflows\": " << audio_health_.system.ring_overflows << ",\n"
            << "  \"audio_microphone_packets_captured\": " << audio_health_.microphone.captured << ",\n"
            << "  \"audio_microphone_packets_published\": " << audio_health_.microphone.published << ",\n"
            << "  \"audio_microphone_packets_silent\": " << audio_health_.microphone.silent << ",\n"
            << "  \"audio_microphone_packets_discontinuous\": " << audio_health_.microphone.discontinuities << ",\n"
            << "  \"audio_microphone_timestamp_errors\": " << audio_health_.microphone.timestamp_errors << ",\n"
            << "  \"audio_microphone_packets_dropped\": " << audio_health_.microphone.dropped << ",\n"
            << "  \"audio_microphone_pool_exhaustions\": " << audio_health_.microphone.pool_exhaustions << ",\n"
            << "  \"audio_microphone_ring_overflows\": " << audio_health_.microphone.ring_overflows << ",\n"
            << "  \"audio_missing_source_intervals\": " << audio_health_.missing_source_intervals << ",\n"
            << "  \"audio_silent_program_blocks\": " << audio_health_.silent_program_blocks << ",\n"
            << "  \"audio_stale_canonical_frames\": " << audio_health_.stale_canonical_frames << ",\n"
            << "  \"audio_duplicate_canonical_frames\": " << audio_health_.duplicate_canonical_frames << ",\n"
            << "  \"audio_staging_gaps\": " << audio_health_.staging_gaps << ",\n"
            << "  \"audio_staging_overflows\": " << audio_health_.staging_overflows << ",\n"
            << "  \"audio_dropped_program_blocks\": " << audio_health_.dropped_program_blocks << ",\n"
            << "  \"audio_failure_phase\": " << audio_health_.failure_phase << ",\n"
            << "  \"audio_failure_source\": " << audio_health_.failure_source << ",\n"
            << "  \"audio_failure_packet_ingress_status\": " << audio_health_.failure_packet_ingress_status << ",\n"
            << "  \"audio_failure_native_hresult\": " << audio_health_.failure_native_hresult << ",\n"
            << "  \"audio_stage_mic_frames\": " << audio_health_.mic_stage_frames << ",\n"
            << "  \"audio_stage_system_frames\": " << audio_health_.system_stage_frames << ",\n"
            << "  \"audio_stage_mic_peak\": " << audio_health_.mic_stage_peak << ",\n"
            << "  \"audio_stage_system_peak\": " << audio_health_.system_stage_peak << ",\n"
            << "  \"audio_pending_program_blocks\": " << audio_health_.pending_audio_program_blocks << ",\n"
            << "  \"encoder_h264_profile\": \""
            << arssyut::windows::mf_h264_profile_name(
                   encoder_profile)
            << "\",\n"
            << "  \"encoder_rate_control\": \""
            << arssyut::windows::mf_rate_control_mode_name(
                   encoder_rate_control)
            << "\",\n"
            << "  \"encoder_bitrate_vbr\": "
            << (encoder_rate_control ==
                        arssyut::windows::MfRateControlMode::
                            UnconstrainedVbr
                    ? "true"
                    : "false")
            << ",\n"
            << "  \"encoder_quality_vs_speed_applied\": "
            << (encoder_quality_vs_speed_applied
                    ? "true"
                    : "false")
            << ",\n"
            << "  \"encoder_quality_vs_speed\": "
            << encoder_quality_vs_speed << ",\n"
            << "  \"encoder_color_pipeline\": \""
            << arssyut::windows::mf_color_pipeline_mode_name(
                   encoder_color_pipeline)
            << "\",\n"
            << "  \"encoder_color_pipeline_authoritative\": "
            << (encoder_color_pipeline_authoritative
                    ? "true"
                    : "false")
            << ",\n"
            << "  \"encoder_rgb_input_range\": \"full_0_255\",\n"
            << "  \"encoder_yuv_output_range\": \"studio_16_235\",\n"
            << "  \"encoder_color_primaries\": \"bt709\",\n"
            << "  \"encoder_transfer_function\": \"bt709\",\n"
            << "  \"encoder_yuv_matrix\": \"bt709\",\n"
            << "  \"encoder_quality_vbr\": false,\n"
            << "  \"encoder_quality\": 0,\n"
            << "  \"encoder_pixel_format\": \"nv12\",\n"
            << "  \"encoder_chroma_subsampling\": \"4:2:0\",\n"
            << "  \"arvisual_mode\": \""
            << arssyut::visual::product_mode_name(
                   config_.visual_mode)
            << "\",\n"
            << "  \"arvisual_enabled\": "
            << (config_.visual.enabled ? "true" : "false")
            << ",\n"
            << "  \"arvisual_smart_auto\": "
            << (config_.visual.smart_auto ? "true" : "false")
            << ",\n"
            << std::setprecision(6)
            << "  \"arvisual_base_master\": "
            << config_.visual.master << ",\n"
            << "  \"arvisual_base_enhance\": "
            << config_.visual.enhance << ",\n"
            << "  \"arvisual_base_color_pop\": "
            << config_.visual.color_pop << ",\n"
            << "  \"arvisual_base_clean_white\": "
            << config_.visual.clean_white << ",\n"
            << "  \"arvisual_base_clarity\": "
            << config_.visual.clarity << ",\n"
            << "  \"arvisual_base_skin_protect\": "
            << config_.visual.skin_protect << ",\n"
            << "  \"arvisual_base_skin_beauty\": "
            << config_.visual.skin_beauty << ",\n"
            << "  \"arvisual_base_healthy_tone\": "
            << config_.visual.healthy_tone << ",\n"
            << "  \"arvisual_base_toy_gloss\": "
            << config_.visual.toy_gloss << ",\n"
            << "  \"arvisual_base_depth_pop\": "
            << config_.visual.depth_pop << ",\n"
            << "  \"arvisual_base_highlight_guard\": "
            << config_.visual.highlight_guard << ",\n"
            << "  \"arvisual_base_performance\": "
            << config_.visual.performance << ",\n"
            << "  \"arvisual_base_text_legibility\": "
            << config_.visual.text_legibility << ",\n"
            << "  \"arvisual_base_ui_structure\": "
            << config_.visual.ui_structure << ",\n"
            << "  \"arvisual_base_screen_native\": "
            << config_.visual.screen_native << ",\n"
            << "  \"arvisual_base_neutral_surface_anchor\": "
            << config_.visual.neutral_surface_anchor << ",\n"
            << "  \"visual_analysis_primed\": "
            << (visual_analysis_primed ? "true" : "false")
            << ",\n"
            << "  \"visual_scene_p10_luma\": "
            << visual_scene_stats.p10_luma << ",\n"
            << "  \"visual_scene_median_luma\": "
            << visual_scene_stats.median_luma << ",\n"
            << "  \"visual_scene_p90_luma\": "
            << visual_scene_stats.p90_luma << ",\n"
            << "  \"visual_scene_p98_luma\": "
            << visual_scene_stats.p98_luma << ",\n"
            << "  \"visual_scene_mean_saturation\": "
            << visual_scene_stats.mean_saturation << ",\n"
            << "  \"visual_scene_p90_saturation\": "
            << visual_scene_stats.p90_saturation << ",\n"
            << "  \"visual_scene_shadow_frac\": "
            << visual_scene_stats.shadow_frac << ",\n"
            << "  \"visual_scene_near_clip_frac\": "
            << visual_scene_stats.near_clip_frac << ",\n"
            << "  \"visual_scene_vivid_frac\": "
            << visual_scene_stats.vivid_frac << ",\n"
            << "  \"visual_scene_hot_vivid_frac\": "
            << visual_scene_stats.hot_vivid_frac << ",\n"
            << "  \"visual_scene_neutral_frac\": "
            << visual_scene_stats.neutral_frac << ",\n"
            << "  \"visual_scene_colored_frac\": "
            << visual_scene_stats.colored_frac << ",\n"
            << "  \"visual_scene_flat_frac\": "
            << visual_scene_stats.flat_frac << ",\n"
            << "  \"visual_scene_neutral_flat_frac\": "
            << visual_scene_stats.neutral_flat_frac << ",\n"
            << "  \"visual_scene_bright_neutral_flat_frac\": "
            << visual_scene_stats.bright_neutral_flat_frac << ",\n"
            << "  \"visual_scene_dark_neutral_flat_frac\": "
            << visual_scene_stats.dark_neutral_flat_frac << ",\n"
            << "  \"visual_scene_edge_frac\": "
            << visual_scene_stats.edge_frac << ",\n"
            << "  \"visual_adaptive_exposure\": "
            << visual_adaptive.exposure << ",\n"
            << "  \"visual_adaptive_pop\": "
            << visual_adaptive.pop << ",\n"
            << "  \"visual_adaptive_highlight\": "
            << visual_adaptive.highlight << ",\n"
            << "  \"visual_adaptive_shadow\": "
            << visual_adaptive.shadow << ",\n"
            << "  \"visual_adaptive_strength\": "
            << visual_adaptive.strength << ",\n"
            << "  \"visual_adaptive_chroma_limit\": "
            << visual_adaptive.chroma_limit << ",\n"
            << "  \"visual_adaptive_clean\": "
            << visual_adaptive.clean << ",\n"
            << "  \"visual_adaptive_separation\": "
            << visual_adaptive.separation << ",\n"
            << "  \"visual_adaptive_white_ui\": "
            << visual_adaptive.white_ui << ",\n"
            << "  \"visual_adaptive_screen_ui\": "
            << visual_adaptive.screen_ui << ",\n"
            << "  \"visual_adaptive_mixed_ui\": "
            << visual_adaptive.mixed_ui << ",\n"
            << "  \"visual_adaptive_color_risk\": "
            << visual_adaptive.color_risk << ",\n"
            << "  \"visual_adaptive_hot_risk\": "
            << visual_adaptive.hot_risk << ",\n"
            << "  \"visual_applied_smart_exposure\": "
            << applied_visual.smart_exposure << ",\n"
            << "  \"visual_applied_smart_pop\": "
            << applied_visual.smart_pop << ",\n"
            << "  \"visual_applied_smart_highlight\": "
            << applied_visual.smart_highlight << ",\n"
            << "  \"visual_applied_smart_shadow\": "
            << applied_visual.smart_shadow << ",\n"
            << "  \"visual_applied_smart_strength\": "
            << applied_visual.smart_strength << ",\n"
            << "  \"visual_applied_smart_chroma_limit\": "
            << applied_visual.smart_chroma_limit << ",\n"
            << "  \"visual_applied_screen_ui\": "
            << applied_visual.smart_screen_ui << ",\n"
            << "  \"visual_analysis_available\": "
            << (snapshot_value.visual_analysis_available ? "true" : "false")
            << ",\n"
            << "  \"visual_analysis_submitted\": "
            << snapshot_value.visual_analysis_submitted << ",\n"
            << "  \"visual_analysis_completed\": "
            << snapshot_value.visual_analysis_completed << ",\n"
            << "  \"visual_analysis_busy_skips\": "
            << snapshot_value.visual_analysis_busy_skips << ",\n"
            << "  \"visual_analysis_map_failures\": "
            << snapshot_value.visual_analysis_map_failures << ",\n"
            << "  \"elapsed_ticks_100ns\": "
            << snapshot_value.elapsed_ticks << ",\n"
            << "  \"pause_count\": "
            << pause_count_.load(std::memory_order_relaxed) << ",\n"
            << "  \"pause_excluded_ticks_100ns\": "
            << pause_excluded_ticks_.load(std::memory_order_relaxed) << ",\n"
            << "  \"pause_resume_events\": "
            << pause_resume_events_ << ",\n"
            << "  \"pause_resume_seam_overflow\": "
            << (pause_resume_events_ > kMaxPauseResumeSeams
                    ? pause_resume_events_ - kMaxPauseResumeSeams : 0)
            << ",\n"
            << "  \"pause_resume_media_ticks_100ns\": [";
        for (std::size_t i = 0;
             i < (std::min)(static_cast<std::size_t>(pause_resume_events_),
                              kMaxPauseResumeSeams); ++i) {
            if (i != 0) out << ", ";
            out << pause_resume_seams_[i];
        }
        out << "],\n"
            << "  \"prepare_latency_ms\": "
            << ([&]() -> std::int64_t {
                   const auto requested =
                       start_requested_at_ticks_.load(
                           std::memory_order_relaxed);
                   const auto armed =
                       armed_at_ticks_.load(
                           std::memory_order_relaxed);
                   const auto started =
                       started_at_ticks_.load(
                           std::memory_order_relaxed);
                   const auto ready =
                       armed > 0 ? armed : started;
                   return requested > 0 && ready >= requested
                       ? (ready - requested) / 10'000
                       : 0;
               })()
            << ",\n"
            << "  \"armed_wait_ms\": "
            << ([&]() -> std::int64_t {
                   const auto armed =
                       armed_at_ticks_.load(
                           std::memory_order_relaxed);
                   const auto started =
                       started_at_ticks_.load(
                           std::memory_order_relaxed);
                   return armed > 0 && started >= armed
                       ? (started - armed) / 10'000
                       : 0;
               })()
            << ",\n"
            << "  \"commit_to_first_frame_us\": "
            << ([&]() -> std::int64_t {
                   const auto started =
                       started_at_ticks_.load(
                           std::memory_order_relaxed);
                   const auto first =
                       first_frame_submitted_at_ticks_.load(
                           std::memory_order_relaxed);
                   return started > 0 && first >= started
                       ? (first - started) / 10
                       : 0;
               })()
            << ",\n"
            << "  \"capture_preroll_received\": "
            << capture_preroll_received_.load(
                   std::memory_order_relaxed)
            << ",\n"
            << "  \"webcam_requested\": "
            << (config_.webcam_enabled ? "true" : "false") << ",\n"
            << "  \"webcam_received\": "
            << webcam_received_.load() << ",\n"
            << "  \"webcam_published\": "
            << webcam_published_.load() << ",\n"
            << "  \"webcam_ticks_startup\": "
            << webcam_ticks_startup_.load() << ",\n"
            << "  \"webcam_ticks_streaming\": "
            << webcam_ticks_streaming_.load() << ",\n"
            << "  \"webcam_adapter_mismatch\": "
            << webcam_adapter_mismatch_.load() << ",\n"
            << "  \"webcam_foreign_device\": "
            << webcam_foreign_device_.load() << ",\n"
            << "  \"webcam_new_gpu_frames\": "
            << webcam_new_gpu_frames_.load() << ",\n"
            << "  \"webcam_rendered_slots\": "
            << webcam_rendered_slots_.load() << ",\n"
            << "  \"webcam_copy_failures\": "
            << webcam_copy_failures_.load() << ",\n"
            << "  \"webcam_hresult\": "
            << webcam_hresult_.load() << ",\n"
            << "  \"capture_received\": "
            << snapshot_value.capture_received << ",\n"
            << "  \"capture_replaced\": "
            << snapshot_value.capture_replaced << ",\n"
            << "  \"capture_busy_drops\": "
            << snapshot_value.capture_busy_drops << ",\n"
            << "  \"video_rendered\": "
            << snapshot_value.video_rendered << ",\n"
            << "  \"video_reused\": "
            << snapshot_value.video_reused << ",\n"
            << "  \"video_skipped\": "
            << snapshot_value.video_skipped << ",\n"
            << "  \"encoder_submitted\": "
            << writer_submitted << ",\n"
            << "  \"encoder_backpressure\": "
            << writer_backpressure << ",\n"
            << "  \"presentation_input_dropped\": "
            << snapshot_value.presentation_input_dropped << ",\n"
            << "  \"system_shortcut_hook_active\": "
            << (snapshot_value.system_shortcut_hook_active
                    ? "true"
                    : "false")
            << ",\n"
            << "  \"encoder_sample_buffer_length\": "
            << snapshot_value.encoder_sample_buffer_length << ",\n"
            << "  \"encoder_sample_buffer_max_length\": "
            << snapshot_value.encoder_sample_buffer_max_length << ",\n"
            << "  \"capture_p95_us\": "
            << snapshot_value.capture_p95_us << ",\n"
            << "  \"compositor_cpu_p95_us\": "
            << snapshot_value.compositor_cpu_p95_us << ",\n"
            << "  \"compositor_gpu_p95_us\": "
            << snapshot_value.compositor_gpu_p95_us << ",\n"
            << "  \"memory_private_start\": "
            << memory_start << ",\n"
            << "  \"memory_private_end\": "
            << memory_end << ",\n"
            << "  \"memory_private_max\": "
            << snapshot_value.memory_private_max_bytes << ",\n"
            << "  \"resource_generation\": "
            << resource_generation << ",\n"
            << "  \"status_code\": "
            << static_cast<std::uint32_t>(
                   snapshot_value.last_error.code)
            << ",\n"
            << "  \"status_detail\": "
            << snapshot_value.last_error.detail << ",\n"
            << "  \"encoder_failure_stage\": \""
            << arssyut::windows::mf_writer_stage_name(
                   snapshot_value.encoder_failure_stage)
            << "\"\n"
            << "}\n";
    } catch (...) {
    }
}

} // namespace arssyut::app

#endif
