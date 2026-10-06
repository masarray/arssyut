#pragma once

#include <algorithm>
#include <cmath>

namespace arzoom {

enum class CinematicFocusSpeed {
    Smooth,
    Balanced,
    Snappy,
};

inline float cinematic_minimum_jerk(float t)
{
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * t * (10.0f + t * (-15.0f + 6.0f * t));
}

inline float cinematic_close_duration(CinematicFocusSpeed speed)
{
    switch (speed) {
    case CinematicFocusSpeed::Smooth: return 0.48f;
    case CinematicFocusSpeed::Snappy: return 0.26f;
    case CinematicFocusSpeed::Balanced:
    default: return 0.36f;
    }
}

inline float cinematic_open_duration(CinematicFocusSpeed speed)
{
    switch (speed) {
    case CinematicFocusSpeed::Smooth: return 0.40f;
    case CinematicFocusSpeed::Snappy: return 0.22f;
    case CinematicFocusSpeed::Balanced:
    default: return 0.30f;
    }
}

/*
 * Maximum corner radius from the actual output-space focus center.  Unlike a
 * center-only half-diagonal formula this still covers the complete frame when
 * the presenter focus is near an edge or corner.
 */
inline float cinematic_full_radius_px(float width, float height,
                                      float center_x, float center_y,
                                      float feather_px,
                                      float safety_margin_px = 24.0f)
{
    const float w = std::max(width, 1.0f);
    const float h = std::max(height, 1.0f);
    const float cx = std::clamp(center_x, 0.0f, 1.0f) * w;
    const float cy = std::clamp(center_y, 0.0f, 1.0f) * h;

    const float dx0 = cx;
    const float dx1 = w - cx;
    const float dy0 = cy;
    const float dy1 = h - cy;
    const float max_dx = std::max(dx0, dx1);
    const float max_dy = std::max(dy0, dy1);
    return std::sqrt(max_dx * max_dx + max_dy * max_dy) +
           std::max(feather_px, 1.0f) +
           std::max(safety_margin_px, 0.0f);
}

inline float cinematic_full_area_percent(float base_radius_px,
                                         float full_radius_px)
{
    const float base = std::max(base_radius_px, 1.0f);
    const float full = std::max(full_radius_px, base);
    return std::clamp((full / base) * 100.0f, 100.0f, 2000.0f);
}

/* Dimming deliberately trails the aperture motion on close.  Because it is a
 * pure function of focus_mix it also fades immediately when the animation is
 * reversed/opened. */
inline float cinematic_dim_mix(float focus_mix)
{
    const float delayed = std::clamp((focus_mix - 0.12f) / 0.88f,
                                     0.0f, 1.0f);
    return cinematic_minimum_jerk(delayed);
}

struct CinematicSpotlightState {
    float value = 0.0f;       // 0 = full frame, 1 = configured focus area
    float start_value = 0.0f;
    float target_value = 0.0f;
    float elapsed = 0.0f;
    float duration = 0.0f;

    void reset(float focused = 0.0f)
    {
        value = std::clamp(focused, 0.0f, 1.0f);
        start_value = value;
        target_value = value;
        elapsed = 0.0f;
        duration = 0.0f;
    }

    void set_target(bool focused, CinematicFocusSpeed speed)
    {
        const float next = focused ? 1.0f : 0.0f;
        if (std::fabs(next - target_value) <= 1.0e-5f)
            return;

        /* Reversal starts exactly from the current visual state.  Scale the
         * duration by remaining distance so a half-finished reversal does not
         * take a full animation duration again. */
        start_value = value;
        target_value = next;
        elapsed = 0.0f;
        const float base = focused ? cinematic_close_duration(speed)
                                   : cinematic_open_duration(speed);
        const float distance = std::fabs(target_value - start_value);
        duration = base * std::max(distance, 0.18f);
    }

    void step(float dt)
    {
        if (std::fabs(target_value - start_value) <= 1.0e-5f ||
            duration <= 1.0e-5f) {
            value = target_value;
            return;
        }

        elapsed += std::clamp(dt, 0.0f, 0.10f);
        const float t = std::clamp(elapsed / duration, 0.0f, 1.0f);
        const float eased = cinematic_minimum_jerk(t);
        value = start_value + (target_value - start_value) * eased;
        if (t >= 1.0f) {
            value = target_value;
            start_value = target_value;
            elapsed = 0.0f;
            duration = 0.0f;
        }
    }

    bool visually_active() const
    {
        return target_value > 0.0f || value > 1.0e-4f;
    }
};

} // namespace arzoom
