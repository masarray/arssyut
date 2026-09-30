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
    have_output_ = false;
    last_source_sequence_ = 0;
    return scheduler_.reset(start, frame_rate);
}

Result<VideoSlotResult> NativeVideoPipeline::process_due(
    ID3D11DeviceContext *context,
    arssyut::core::TimePoint now,
    arssyut::core::CropRect crop,
    arssyut::core::FrameSize output_size) noexcept
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

    auto lease = frame_slot_->try_acquire_latest();
    if (!lease) {
        VideoSlotResult result;
        result.frame_index = scheduled.frame_index;
        result.pts = scheduled.pts;
        result.skipped_intervals = scheduled.skipped_intervals;
        result.source_sequence = last_source_sequence_;

        if (have_output_) {
            result.action = VideoSlotAction::ReusePreviousOutput;
            diagnostics_->increment(
                DiagnosticMetric::VideoFramesReused);
        } else {
            result.action = VideoSlotAction::NoFrameAvailable;
            diagnostics_->increment(
                DiagnosticMetric::VideoFramesUnavailable);
        }

        return Result<VideoSlotResult>::success(result);
    }

    auto texture = capture_texture(lease.get());
    if (!texture)
        return Result<VideoSlotResult>::failure(texture.status());

    const Status render_status = compositor_->render(
        context,
        texture.value().Get(),
        crop,
        output_size);
    if (!render_status.ok())
        return Result<VideoSlotResult>::failure(render_status);

    have_output_ = true;
    last_source_sequence_ = lease->sequence;

    diagnostics_->increment(
        DiagnosticMetric::VideoFramesRendered);

    VideoSlotResult result;
    result.action = VideoSlotAction::RenderedNewFrame;
    result.frame_index = scheduled.frame_index;
    result.pts = scheduled.pts;
    result.skipped_intervals = scheduled.skipped_intervals;
    result.source_sequence = last_source_sequence_;

    return Result<VideoSlotResult>::success(result);
}

} // namespace arssyut::windows

#endif
