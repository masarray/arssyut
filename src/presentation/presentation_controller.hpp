#pragma once

#include "core/time/monotonic_clock.hpp"
#include "presentation/arzoom_camera_adapter.hpp"
#include "arzoom-presenter-controls.hpp"
#include "presentation/presentation_state.hpp"
#include "presentation/shortcut.hpp"

#include <array>
#include <cstdint>

namespace arssyut::presentation {

struct PresentationSettings {
    bool smart_zoom = true;
    bool click_visual = true;
    bool shortcut_keys = true;
    bool presenter_controls = false;
    float zoom = 2.0f;
};

class PresentationController final {
public:
    void reset() noexcept;

    void set_settings(PresentationSettings settings) noexcept;

    void on_click(
        ClickKind kind,
        float content_x,
        float content_y,
        arssyut::core::TimePoint time) noexcept;

    void on_shortcut(
        ShortcutChord chord,
        arssyut::core::TimePoint time) noexcept;

    // ArZoom Presenter Controls intent. These methods only change presenter
    // intent consumed by the existing ArZoomCameraAdapter; they never create a
    // second camera/planner or mutate capture geometry.
    void toggle_manual_zoom() noexcept;
    void adjust_manual_zoom(float delta) noexcept;
    void reset_full_frame() noexcept;
    void set_hold_zoom(bool active) noexcept;
    void set_overview_peek(bool active) noexcept;

    [[nodiscard]] bool manual_zoom_latched() const noexcept
    {
        return manual_zoom_latched_;
    }

    [[nodiscard]] float configured_zoom() const noexcept
    {
        return runtime_zoom_;
    }

    [[nodiscard]] bool overview_active() const noexcept
    {
        return overview_.active();
    }

    [[nodiscard]] PresentationFrameState step(
        float dt,
        float cursor_x,
        float cursor_y,
        bool cursor_valid,
        arssyut::core::TimePoint now,
        arssyut::core::TimePoint last_pointer_activity) noexcept;

private:
    struct ClickPulse {
        ClickKind kind = ClickKind::None;
        float content_x = 0.5f;
        float content_y = 0.5f;
        float age_seconds = 0.0f;
        std::uint32_t generation = 0;

        [[nodiscard]] bool active() const noexcept
        {
            return kind != ClickKind::None;
        }
    };

    void update_keyboard(
        ShortcutChord chord) noexcept;
    void push_click(
        ClickKind kind,
        float content_x,
        float content_y) noexcept;

    PresentationSettings settings_{};

    ArZoomCameraAdapter camera_;
    std::array<ClickPulse, 4> clicks_{};
    std::uint32_t click_generation_ = 0;

    arssyut::core::TimePoint zoom_until_{};
    arssyut::core::TimePoint keyboard_started_{};
    arssyut::core::TimePoint keyboard_until_{};
    arssyut::core::TimePoint last_shortcut_time_{};

    KeyboardOverlayFrame keyboard_{};
    ShortcutChord last_shortcut_{};
    std::uint32_t keyboard_generation_ = 0;
    bool have_last_shortcut_ = false;
    bool emphasis_pending_ = false;
    bool manual_zoom_latched_ = false;
    bool hold_zoom_active_ = false;
    bool overview_requested_ = false;
    float runtime_zoom_ = 2.0f;
    arzoom::OverviewPeekController overview_{};

    // Current rendered camera transform. Overview Peek pauses the underlying
    // camera, so release/cancel transitions must start from what the viewer is
    // actually seeing rather than camera_.output()'s frozen saved shot.
    float last_camera_center_x_ = 0.5f;
    float last_camera_center_y_ = 0.5f;
    float last_camera_zoom_ = 1.0f;
};

} // namespace arssyut::presentation
