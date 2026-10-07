#pragma once

#include "core/time/monotonic_clock.hpp"
#include "presentation/arzoom_camera_adapter.hpp"
#include "arzoom-presenter-controls.hpp"
#include "arzoom-cinematic-spotlight.hpp"
#include "arzoom-spotlight-zoom-resize.hpp"
#include "presentation/presentation_state.hpp"
#include "presentation/shortcut.hpp"

#include <array>
#include <cstdint>

namespace arssyut::presentation {

struct SpotlightSettings {
    // Existing-user compatibility: Spotlight remains opt-in until the
    // product-facing controls are wired in P6UI.6D-H.
    bool enabled = false;
    bool link_to_zoom = true;
    SpotlightMode mode = SpotlightMode::SmartFocus;
    SpotlightSize size = SpotlightSize::Balanced;
    SpotlightShape shape = SpotlightShape::Circle;
    SpotlightCinematicSpeed cinematic_speed =
        SpotlightCinematicSpeed::Balanced;

    // Upstream P5 starting contract. The compositor resolves actual output
    // pixels later; the presentation authority never owns render geometry.
    float area_scale_percent = 100.0f;
    float feather_short_edge_fraction = 0.12f;
    float dim_strength = 0.38f;
};

struct PresentationSettings {
    bool smart_zoom = true;
    bool click_visual = true;
    bool shortcut_keys = true;
    bool presenter_controls = false;
    float zoom = 2.0f;
    SpotlightSettings spotlight{};

    // One existing recorder input/step authority also runs for Spotlight-only
    // recordings; no managed or compositor-side input loop is created.
    [[nodiscard]] bool needs_presentation_frames() const noexcept
    {
        return smart_zoom || click_visual || shortcut_keys ||
               presenter_controls || spotlight.enabled;
    }
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
    void toggle_freeze_camera() noexcept;

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

    [[nodiscard]] bool camera_frozen() const noexcept
    {
        return camera_frozen_;
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
    bool camera_frozen_ = false;
    float runtime_zoom_ = 2.0f;
    arzoom::OverviewPeekController overview_{};

    // Exactly one bounded Spotlight focus snapshot lives inside the existing
    // presentation authority. It is read-only with respect to camera intent.
    bool spotlight_focus_valid_ = false;
    float spotlight_focus_x_ = 0.5f;
    float spotlight_focus_y_ = 0.5f;
    bool spotlight_click_anchor_valid_ = false;
    float spotlight_click_anchor_x_ = 0.5f;
    float spotlight_click_anchor_y_ = 0.5f;
    bool spotlight_zoom_was_requested_ = false;

    // P6UI.6D-C owns only Spotlight visual choreography. These bounded
    // upstream states observe the authoritative camera; neither can write
    // camera intent or create an independent motion plan.
    arzoom::CinematicSpotlightState spotlight_cinematic_{};
    arzoom::SpotlightZoomResizeState spotlight_zoom_resize_{};
    bool spotlight_runtime_was_requested_ = false;
    bool spotlight_close_armed_ = false;

    // Current rendered camera transform. Overview Peek pauses the underlying
    // camera, so release/cancel transitions must start from what the viewer is
    // actually seeing rather than camera_.output()'s frozen saved shot.
    float last_camera_center_x_ = 0.5f;
    float last_camera_center_y_ = 0.5f;
    float last_camera_zoom_ = 1.0f;
};

} // namespace arssyut::presentation