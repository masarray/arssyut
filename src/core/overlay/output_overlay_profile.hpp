#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace arssyut::core::overlay {

// The versioned product contract precedes the native bridge and compositor
// wiring. IDs are permanent; never reuse a former mask value on migration.
enum class Corner : std::uint8_t {
    TopLeft = 0, TopRight = 1, BottomLeft = 2, BottomRight = 3,
    Center = 4
};
enum class CameraMask : std::uint8_t {
    Circle = 0,
    RoundedRectangle = 1,
    DiagonalTopLeftBottomRight = 2,
    DiagonalTopRightBottomLeft = 3
};

struct CameraOverlayProfile {
    std::uint32_t version = 1;
    bool enabled = true;
    Corner placement = Corner::TopRight;
    CameraMask mask = CameraMask::RoundedRectangle;
    float width_fraction = 0.235f;
    float margin_fraction = 0.035f;
    float corner_radius_fraction = 0.15f;
    float border_width_px = 2.0f;
    float opacity = 1.0f;
    bool mirror = false;
};

struct LogoOverlayProfile {
    std::uint32_t version = 1;
    bool enabled = false;
    Corner placement = Corner::BottomRight;
    float width_fraction = 0.12f;
    float margin_fraction = 0.025f;
    float opacity = 1.0f;
    // File identity and decoded pixel metadata must be validated outside
    // the realtime renderer. This POD intentionally owns no UI/file handle.
};

constexpr bool valid_camera_mask(CameraMask x) noexcept {
    return static_cast<std::uint8_t>(x) <= 3U;
}
constexpr bool valid_corner(Corner x, bool center) noexcept {
    return static_cast<std::uint8_t>(x) <= (center ? 4U : 3U);
}
inline bool valid(const CameraOverlayProfile &x) noexcept {
    return x.version == 1 && valid_corner(x.placement, false) &&
        valid_camera_mask(x.mask) && std::isfinite(x.width_fraction) &&
        x.width_fraction >= .15f && x.width_fraction <= .40f &&
        std::isfinite(x.margin_fraction) &&
        x.margin_fraction >= 0.f && x.margin_fraction <= .15f &&
        std::isfinite(x.corner_radius_fraction) &&
        x.corner_radius_fraction >= 0.f && x.corner_radius_fraction <= .5f &&
        std::isfinite(x.border_width_px) &&
        x.border_width_px >= 0.f && x.border_width_px <= 32.f &&
        std::isfinite(x.opacity) && x.opacity >= 0.f && x.opacity <= 1.f;
}
inline bool valid(const LogoOverlayProfile &x) noexcept {
    return x.version == 1 && valid_corner(x.placement, true) &&
        std::isfinite(x.width_fraction) &&
        x.width_fraction >= .02f && x.width_fraction <= .5f &&
        std::isfinite(x.margin_fraction) &&
        x.margin_fraction >= 0.f && x.margin_fraction <= .15f &&
        std::isfinite(x.opacity) && x.opacity >= 0.f && x.opacity <= 1.f;
}

// Per-corner roundness in (TL, TR, BR, BL) order. The user's specified
// diagonal has SQUARE top-left/bottom-right and ROUND top-right/bottom-left.
constexpr bool corner_rounded(CameraMask mask, std::uint32_t index) noexcept {
    if (index > 3U) return false;
    switch (mask) {
    case CameraMask::Circle:
    case CameraMask::RoundedRectangle: return true;
    case CameraMask::DiagonalTopLeftBottomRight:
        return index == 0U || index == 2U;
    case CameraMask::DiagonalTopRightBottomLeft:
        return index == 1U || index == 3U;
    }
    return false;
}

} // namespace arssyut::core::overlay
