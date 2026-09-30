#pragma once

#include "core/time/monotonic_clock.hpp"
#include "presentation/arzoom/arzoom_click_visual.hpp"
#include "presentation/arzoom/arzoom_scene_viewport_planner.hpp"
#include "presentation/presentation_state.hpp"

#include <cstdint>

namespace arssyut::presentation {

struct PresentationSettings {
    bool smart_zoom = true;
    bool click_visual = true;
    bool shortcut_keys = true;
    float zoom = 2.0f;
};

struct ShortcutChord {
    std::uint16_t key = 0;
    std::uint8_t modifiers = 0;
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
    void update_keyboard(
        ShortcutChord chord) noexcept;

    PresentationSettings settings_{};

    arzoom::SceneViewportPlanner camera_;
    arzoom::ClickVisualState clicks_;

    arssyut::core::TimePoint zoom_until_{};
    arssyut::core::TimePoint keyboard_until_{};

    KeyboardOverlayFrame keyboard_{};
    std::uint32_t keyboard_generation_ = 0;
};

} // namespace arssyut::presentation
