#include "platform/windows/video/native_video_pipeline.hpp"

#ifdef _WIN32

#include "core/result/status.hpp"
#include "platform/windows/capture/captured_frame.hpp"

#include <new>
#include <utility>

namespace arssyut::windows {

using arssyut::core::DiagnosticMetric;
using arssyut::core::Result;
using arssyut::core::Status;
using arssyut::core::StatusCode;

Result<std::unique_ptr<NativeVideoPipeline>>
NativeVideoPipeline::create(
    ID3D11Device *device,
    LatestFrameSlot &frame_slot,
    arssyut::core::Diagnostics &diagnostics) noexcept
{
    auto compositor = D3D11Compositor::create(device);
    if (!compositor) {
        return Result<std::unique_ptr<NativeVideoPipeline>>::failure(
            compositor.status());
    }

    std::unique_ptr<NativeVideoPipeline> pipeline(
        new (std::nothrow) NativeVideoPipeline{});
    if (!pipeline) {
        return Result<std::unique_ptr<NativeVideoPipeline>>::failure(
            Status::failure(StatusCode::InternalError));
    }

    pipeline->frame_slot_ = &frame_slot;
    pipeline->diagnostics_ = &diagnostics;
    pipeline->compositor_ = std::move(compositor).value();

    return Result<std::unique_ptr<NativeVideoPipeline>>::success(
        std::move(pipeline));
}

Status NativeVideoPipeline::reset_timeline(
    arssyut::core::TimePoint start,
    arssyut::core::FrameRate frame_rate) noexcept
{
    have_source_ = false;
    last_source_sequence_ = 0;
    return scheduler_.reset(start, frame_rate);
}

Result<VideoSlotResult> NativeVideoPipeline::process_due(
    ID3D11DeviceContext *context,
    arssyut::core::TimePoint now,
    arssyut::core::CropRect crop,
    arssyut::core::FrameSize output_size,
    const arssyut::presentation::PresentationFrameState *presentation,
    const arssyut::visual::ArVisualGradeSettings *visual) noexcept
{
    if (!context || !frame_slot_ || !diagnostics_ || !compositor_) {
        return Result<VideoSlotResult>::failure(
            Status::failure(StatusCode::InvalidArgument));
    }

    const auto scheduled = scheduler_.poll(now);
    if (!scheduled.emit) {
        return Result<VideoSlotResult>::success(
            VideoSlotResult{});
    }

    if (scheduled.skipped_intervals > 0) {
        diagnostics_->increment(
            DiagnosticMetric::VideoFramesSkipped,
            scheduled.skipped_intervals);
    }

    bool updated_source = false;

    auto lease = frame_slot_->try_acquire_latest();
    if (lease) {
        auto texture = capture_texture(lease.get());
        if (!texture) {
            return Result<VideoSlotResult>::failure(
                texture.status());
        }

        const Status update_status =
            compositor_->update_source(
                context,
                texture.value().Get());
        if (!update_status.ok()) {
            return Result<VideoSlotResult>::failure(
                update_status);
        }

        have_source_ = true;
        updated_source = true;
        last_source_sequence_ = lease->sequence;

        // Smart Auto analysis is submitted only when a fresh WGC frame
        // arrives. The analyzer itself enforces low cadence and never waits
        // for either staging slot; static retained desktops need no repeated
        // analysis work.
        (void)compositor_->submit_scene_analysis(
            context,
            now,
            visual,
            crop);
    }

    VideoSlotResult result;
    result.frame_index = scheduled.frame_index;
    result.pts = scheduled.pts;
    result.skipped_intervals = scheduled.skipped_intervals;
    result.source_sequence = last_source_sequence_;

    if (!have_source_) {
        result.action = VideoSlotAction::NoFrameAvailable;
        diagnostics_->increment(
            DiagnosticMetric::VideoFramesUnavailable);
        return Result<VideoSlotResult>::success(result);
    }

    // Camera, click and presentation state are output-timeline state. Re-render
    // them on every CFR output slot even when Windows Graphics Capture did not
    // publish a new desktop frame. This is the critical ArZoom parity rule:
    // a 60-fps virtual camera must not inherit a lower/irregular WGC cadence.
    const Status render_status =
        compositor_->render_retained(
            context,
            crop,
            output_size,
            presentation,
            visual);
    if (!render_status.ok()) {
        return Result<VideoSlotResult>::failure(
            render_status);
    }

    diagnostics_->increment(
        DiagnosticMetric::VideoFramesRendered);

    if (updated_source) {
        result.action =
            VideoSlotAction::RenderedNewFrame;
    } else {
        result.action =
            VideoSlotAction::RenderedRetainedSource;
        diagnostics_->increment(
            DiagnosticMetric::VideoFramesReused);
    }

    return Result<VideoSlotResult>::success(result);
}

} // namespace arssyut::windows

#endif
