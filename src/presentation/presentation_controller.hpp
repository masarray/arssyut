#pragma once

#include "core/time/monotonic_clock.hpp"
#include "presentation/arzoom_camera_adapter.hpp"
#include "presentation/presentation_state.hpp"
#include "presentation/shortcut.hpp"

#include <array>
#include <cstdint>

namespace arssyut::presentation {

struct PresentationSettings {
    bool smart_zoom = true;
    bool click_visual = true;
    bool shortcut_keys = true;
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

    float cursor_impact_age_seconds_ = 0.0f;
    float cursor_velocity_boost_ = 0.0f;
    float previous_cursor_x_ = 0.5f;
    float previous_cursor_y_ = 0.5f;
    bool cursor_impact_active_ = false;
    bool have_previous_cursor_ = false;

    arssyut::core::TimePoint zoom_until_{};
    arssyut::core::TimePoint keyboard_started_{};
    arssyut::core::TimePoint keyboard_until_{};
    arssyut::core::TimePoint last_shortcut_time_{};

    KeyboardOverlayFrame keyboard_{};
    ShortcutChord last_shortcut_{};
    std::uint32_t keyboard_generation_ = 0;
    bool have_last_shortcut_ = false;
    bool emphasis_pending_ = false;
};

} // namespace arssyut::presentation
