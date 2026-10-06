#pragma once

#include "presentation/shortcut.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace arssyut::presentation {

enum class ClickKind : std::uint8_t {
    None = 0,
    Left = 1,
    Right = 2,
    Middle = 3,
};

enum class SpotlightMode : std::uint8_t {
    SmartFocus = 0,
    Cursor,
    Click,
};

enum class SpotlightSize : std::uint8_t {
    Compact = 0,
    Balanced,
    Wide,
};

enum class SpotlightShape : std::uint8_t {
    Circle = 0,
    Ellipse,
    RoundedRectangle,
};

enum class SpotlightCinematicSpeed : std::uint8_t {
    Smooth = 0,
    Balanced,
    Snappy,
};

/*
 * Bounded renderer-facing Spotlight snapshot.
 *
 * The center is always the same canonical content-space coordinate consumed by
 * the camera/click pipeline. It is deliberately NOT an output-space position:
 * the single compositor will project it through the authoritative camera once.
 *
 * P6UI.6D-B carries intent/style only. focus_mix/dim_mix remain pass-through
 * until P6UI.6D-C owns the pinned upstream cinematic choreography.
 */
struct SpotlightFrameState {
    bool enabled = false;
    bool runtime_requested = false;
    bool focus_valid = false;
    bool link_to_zoom = true;

    float content_x = 0.5f;
    float content_y = 0.5f;
    float focus_mix = 0.0f;
    float dim_mix = 0.0f;
    float area_scale_percent = 100.0f;
    float zoom_resize_scale = 1.0f;
    float feather_short_edge_fraction = 0.12f;
    float dim_strength = 0.38f;

    SpotlightMode mode = SpotlightMode::SmartFocus;
    SpotlightSize size = SpotlightSize::Balanced;
    SpotlightShape shape = SpotlightShape::Circle;
    SpotlightCinematicSpeed cinematic_speed =
        SpotlightCinematicSpeed::Balanced;
};

struct ClickFrame {
    float content_x = 0.5f;
    float content_y = 0.5f;
    float age_seconds = 0.0f;
    float lifetime_seconds = 0.0f;
    ClickKind kind = ClickKind::None;
};

struct KeyboardOverlayFrame {
    std::array<KeycapFrame, kMaxShortcutKeycaps> keycaps{};
    std::size_t keycap_count = 0;
    float opacity = 0.0f;
    std::uint32_t generation = 0;
};

struct PresentationFrameState {
    float camera_center_x = 0.5f;
    float camera_center_y = 0.5f;
    float camera_zoom = 1.0f;

    SpotlightFrameState spotlight{};
    std::array<ClickFrame, 4> clicks{};
    KeyboardOverlayFrame keyboard{};
};

} // namespace arssyut::presentation
