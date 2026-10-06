#pragma once

#include <algorithm>
#include <cmath>

namespace arzoom {

enum class SpotlightMode {
    SmartFocus,
    Cursor,
    Click,
};

enum class SpotlightSize {
    Compact,
    Balanced,
    Wide,
};

enum class SpotlightShape {
    Circle,
    Ellipse,
    RoundedRectangle,
};

struct SpotlightVec2 {
    float x = 0.5f;
    float y = 0.5f;
};

struct SpotlightGeometry {
    SpotlightVec2 center_output{0.5f, 0.45f};
    SpotlightVec2 half_size_px{170.0f, 170.0f};
    float feather_px = 92.0f;
    float corner_radius_px = 42.0f;
    float dim_strength = 0.38f;
    SpotlightShape shape = SpotlightShape::Circle;
};

/*
 * Master enable means "Spotlight is available for this filter".  It must not
 * itself put a mask on-air.  Runtime visibility is explicit presenter intent:
 * a latched Toggle Spotlight, a held Hold Spotlight, or a short GUI peek.
 */
inline bool spotlight_runtime_requested(bool master_enabled,
                                        bool latched_active,
                                        bool hold_active,
                                        bool peek_active)
{
    return master_enabled &&
           (latched_active || hold_active || peek_active);
}

/* The shared P2/P3.5 effect declares a cursor sampler even when the cursor is
 * visually hidden.  Any Spotlight frame activates that same effect pass, so a
 * non-ready cursor must use the permanent transparent fallback texture. */
inline bool spotlight_shared_pass_needs_cursor_fallback(bool spotlight_active,
                                                        bool cursor_ready)
{
    return spotlight_active && !cursor_ready;
}

/*
 * P5.2 render-ownership rule.
 *
 * Spotlight must never duplicate the accepted P4.1 camera/click/cursor pass.
 * If that pass is already needed for camera, click or a fully renderable
 * Presentation Cursor, Spotlight only supplies uniforms and delegates to it.
 * A minimal Spotlight-only pass is allowed solely when Spotlight is the only
 * visible presentation effect.  This keeps pointer-atlas lifecycle ownership
 * inside the proven P4.1 renderer and prevents the first-click/asset-change
 * black-frame regression seen in the first P5 trials.
 */
inline bool spotlight_needs_solo_pass(bool spotlight_active,
                                      bool camera_active,
                                      bool click_active,
                                      bool cursor_renderable)
{
    return spotlight_active &&
           !camera_active && !click_active && !cursor_renderable;
}

inline SpotlightVec2 spotlight_half_size_px(SpotlightSize size,
                                             float viewport_width,
                                             float viewport_height)
{
    const float w = std::max(viewport_width, 1.0f);
    const float h = std::max(viewport_height, 1.0f);

    float width_fraction = 0.27f;
    float height_fraction = 0.30f;
    switch (size) {
    case SpotlightSize::Compact:
        width_fraction = 0.22f;
        height_fraction = 0.24f;
        break;
    case SpotlightSize::Wide:
        width_fraction = 0.40f;
        height_fraction = 0.38f;
        break;
    case SpotlightSize::Balanced:
    default:
        break;
    }

    return {
        std::max(96.0f, w * width_fraction * 0.5f),
        std::max(72.0f, h * height_fraction * 0.5f),
    };
}

/* Beginner-facing area size.  100% is the premium default. Circle uses one
 * output-pixel radius derived from the short viewport edge, so it stays a true
 * circle on 16:9, ultrawide and portrait canvases. Ellipse/rectangle retain the
 * accepted balanced presentation proportions. */
inline SpotlightVec2 spotlight_focus_half_size_px(SpotlightShape shape,
                                                   float area_scale_percent,
                                                   float viewport_width,
                                                   float viewport_height)
{
    const float w = std::max(viewport_width, 1.0f);
    const float h = std::max(viewport_height, 1.0f);
    const float scale = std::clamp(area_scale_percent, 50.0f, 200.0f) / 100.0f;

    if (shape == SpotlightShape::Circle) {
        const float radius = std::max(84.0f, std::min(w, h) * 0.158f) * scale;
        return {radius, radius};
    }

    const SpotlightVec2 balanced = spotlight_half_size_px(
        SpotlightSize::Balanced, w, h);
    return {balanced.x * scale, balanced.y * scale};
}

inline float spotlight_circle_signed_distance_px(SpotlightVec2 delta_px,
                                                  float radius_px)
{
    const float radius = std::max(radius_px, 1.0f);
    return std::sqrt(delta_px.x * delta_px.x +
                     delta_px.y * delta_px.y) - radius;
}

inline float spotlight_ellipse_signed_distance_px(SpotlightVec2 delta_px,
                                                   SpotlightVec2 half_size_px)
{
    const float rx = std::max(half_size_px.x, 1.0f);
    const float ry = std::max(half_size_px.y, 1.0f);
    const float nx = delta_px.x / rx;
    const float ny = delta_px.y / ry;
    const float normalized = std::sqrt(nx * nx + ny * ny);
    return (normalized - 1.0f) * std::min(rx, ry);
}

inline float spotlight_rounded_rect_signed_distance_px(
    SpotlightVec2 delta_px, SpotlightVec2 half_size_px,
    float corner_radius_px)
{
    const float hx = std::max(half_size_px.x, 1.0f);
    const float hy = std::max(half_size_px.y, 1.0f);
    const float radius = std::clamp(corner_radius_px, 0.0f,
                                    std::min(hx, hy));
    const float qx = std::fabs(delta_px.x) - (hx - radius);
    const float qy = std::fabs(delta_px.y) - (hy - radius);
    const float ox = std::max(qx, 0.0f);
    const float oy = std::max(qy, 0.0f);
    const float outside = std::sqrt(ox * ox + oy * oy);
    const float inside = std::min(std::max(qx, qy), 0.0f);
    return outside + inside - radius;
}

inline float spotlight_smoothstep(float edge0, float edge1, float x)
{
    if (edge1 <= edge0)
        return x >= edge1 ? 1.0f : 0.0f;
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

inline float spotlight_focus_weight(float signed_distance_px,
                                    float feather_px)
{
    const float feather = std::max(feather_px, 1.0f);
    return 1.0f - spotlight_smoothstep(0.0f, feather,
                                       signed_distance_px);
}

inline float spotlight_scene_multiplier(float signed_distance_px,
                                        float feather_px,
                                        float dim_strength,
                                        bool enabled)
{
    if (!enabled)
        return 1.0f;
    const float dim = std::clamp(dim_strength, 0.0f, 0.75f);
    const float focus = spotlight_focus_weight(signed_distance_px, feather_px);
    return 1.0f - dim * (1.0f - focus);
}

} // namespace arzoom
