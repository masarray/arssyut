#pragma once

#ifdef _WIN32

#include "core/diagnostics/diagnostics.hpp"
#include "core/result/result.hpp"
#include "core/time/monotonic_clock.hpp"
#include "core/video/frame_geometry.hpp"
#include "core/video/frame_scheduler.hpp"
#include "platform/windows/capture/latest_frame_slot.hpp"
#include "platform/windows/graphics/d3d11_compositor.hpp"
#include "presentation/presentation_state.hpp"

#include <d3d11.h>

#include <cstdint>
#include <memory>

namespace arssyut::windows {

enum class VideoSlotAction : std::uint8_t {
    NotDue = 0,
    RenderedNewFrame,
    RenderedRetainedSource,
    NoFrameAvailable,
};

struct VideoSlotResult {
    VideoSlotAction action = VideoSlotAction::NotDue;
    std::uint64_t frame_index = 0;
    arssyut::core::TimePoint pts{};
    std::uint64_t skipped_intervals = 0;
    std::uint64_t source_sequence = 0;
};

class NativeVideoPipeline final {
public:
    NativeVideoPipeline(const NativeVideoPipeline &) = delete;
    NativeVideoPipeline &operator=(const NativeVideoPipeline &) = delete;

    [[nodiscard]]
    static arssyut::core::Result<std::unique_ptr<NativeVideoPipeline>>
    create(
        ID3D11Device *device,
        LatestFrameSlot &frame_slot,
        arssyut::core::Diagnostics &diagnostics) noexcept;

    [[nodiscard]] arssyut::core::Status reset_timeline(
        arssyut::core::TimePoint start,
        arssyut::core::FrameRate frame_rate) noexcept;

    [[nodiscard]] arssyut::core::Result<VideoSlotResult> process_due(
        ID3D11DeviceContext *context,
        arssyut::core::TimePoint now,
        arssyut::core::CropRect crop,
        arssyut::core::FrameSize output_size,
        const arssyut::presentation::PresentationFrameState *presentation = nullptr) noexcept;

    [[nodiscard]] ID3D11Texture2D *output_texture() const noexcept
    {
        return compositor_->output_texture();
    }

    [[nodiscard]] D3D11Compositor &compositor() noexcept
    {
        return *compositor_;
    }

private:
    NativeVideoPipeline() = default;

    LatestFrameSlot *frame_slot_ = nullptr;
    arssyut::core::Diagnostics *diagnostics_ = nullptr;
    arssyut::core::FrameScheduler scheduler_;
    std::unique_ptr<D3D11Compositor> compositor_;
    bool have_source_ = false;
    std::uint64_t last_source_sequence_ = 0;
};

} // namespace arssyut::windows

#endif
