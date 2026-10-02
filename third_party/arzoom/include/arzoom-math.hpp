#pragma once

#include <algorithm>
#include <cmath>

namespace arzoom {

struct Vec2 {
    float x = 0.5f;
    float y = 0.5f;
};

inline Vec2 add(Vec2 a, Vec2 b)
{
    return {a.x + b.x, a.y + b.y};
}

inline Vec2 sub(Vec2 a, Vec2 b)
{
    return {a.x - b.x, a.y - b.y};
}

inline Vec2 mul(Vec2 value, float scalar)
{
    return {value.x * scalar, value.y * scalar};
}

inline float length(Vec2 value)
{
    return std::sqrt(value.x * value.x + value.y * value.y);
}

inline Vec2 clamp_center(Vec2 center, float zoom)
{
    const float safe_zoom = std::max(zoom, 1.0f);
    const float half = 0.5f / safe_zoom;
    center.x = std::clamp(center.x, half, 1.0f - half);
    center.y = std::clamp(center.y, half, 1.0f - half);
    return center;
}


inline float minimum_zoom_for_center(Vec2 center)
{
    const float margin = std::min(
        std::min(center.x, 1.0f - center.x),
        std::min(center.y, 1.0f - center.y));

    if (margin <= 0.000001f)
        return 1000.0f;

    return std::max(1.0f, 0.5f / margin);
}

inline Vec2 cursor_output_position(Vec2 cursor, Vec2 center, float zoom)
{
    return {
        (cursor.x - center.x) * zoom + 0.5f,
        (cursor.y - center.y) * zoom + 0.5f,
    };
}

inline Vec2 centered_target(Vec2 cursor, Vec2 anchor, float zoom)
{
    const float safe_zoom = std::max(zoom, 1.0f);
    Vec2 target = {
        cursor.x - (anchor.x - 0.5f) / safe_zoom,
        cursor.y - (anchor.y - 0.5f) / safe_zoom,
    };
    return clamp_center(target, safe_zoom);
}

/*
 * Smart Follow moves only enough to return the cursor to the nearest edge of
 * a stable safe zone. Small cursor movement therefore does not shake the frame.
 */
inline Vec2 smart_follow_target(Vec2 cursor, Vec2 current_center, Vec2 anchor,
                                float safe_zone, float zoom)
{
    const float safe_zoom = std::max(zoom, 1.0f);
    const float half_zone = std::clamp(safe_zone, 0.05f, 0.80f) * 0.5f;
    const float left = anchor.x - half_zone;
    const float right = anchor.x + half_zone;
    const float top = anchor.y - half_zone;
    const float bottom = anchor.y + half_zone;

    const Vec2 output = cursor_output_position(cursor, current_center, safe_zoom);
    Vec2 target = current_center;

    if (output.x < left)
        target.x = cursor.x - (left - 0.5f) / safe_zoom;
    else if (output.x > right)
        target.x = cursor.x - (right - 0.5f) / safe_zoom;

    if (output.y < top)
        target.y = cursor.y - (top - 0.5f) / safe_zoom;
    else if (output.y > bottom)
        target.y = cursor.y - (bottom - 0.5f) / safe_zoom;

    return clamp_center(target, safe_zoom);
}

inline float exponential_alpha(float dt, float settle_seconds)
{
    const float safe_dt = std::clamp(dt, 0.0f, 0.25f);
    const float duration = std::max(settle_seconds, 0.01f);
    return 1.0f - std::exp(-6.0f * safe_dt / duration);
}

inline float smooth_scalar(float current, float target, float dt, float settle_seconds)
{
    return current + (target - current) * exponential_alpha(dt, settle_seconds);
}

/*
 * Pan smoothing combines time-based interpolation with an apparent on-screen
 * speed limit. Dividing by zoom prevents high zoom levels from whipping across
 * the screen faster than low zoom levels.
 */
inline Vec2 smooth_center(Vec2 current, Vec2 target, float dt,
                          float settle_seconds, float max_output_speed,
                          float zoom)
{
    const Vec2 desired_step =
        mul(sub(target, current), exponential_alpha(dt, settle_seconds));

    const float safe_zoom = std::max(zoom, 1.0f);
    const float max_step =
        std::max(max_output_speed, 0.1f) * std::clamp(dt, 0.0f, 0.25f) /
        safe_zoom;

    const float step_length = length(desired_step);
    Vec2 step = desired_step;
    if (step_length > max_step && step_length > 0.0f)
        step = mul(desired_step, max_step / step_length);

    return clamp_center(add(current, step), safe_zoom);
}

inline bool nearly_equal(float a, float b, float epsilon = 0.0005f)
{
    return std::fabs(a - b) <= epsilon;
}

inline bool nearly_equal(Vec2 a, Vec2 b, float epsilon = 0.0005f)
{
    return nearly_equal(a.x, b.x, epsilon) &&
           nearly_equal(a.y, b.y, epsilon);
}

} // namespace arzoom
