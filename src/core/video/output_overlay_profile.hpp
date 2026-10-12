#pragma once
// Canonical output-space overlay configuration. Camera != ArZoom viewport.
// Stable numeric enums are shared verbatim with the v12 native bridge.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

namespace arssyut::core::video {

enum class OverlayCorner : std::uint32_t {
    TopLeft=0, TopRight=1, BottomLeft=2, BottomRight=3, Center=4
};
enum class CameraMask : std::uint32_t {
    Circle=0, RoundedRectangle=1,
    DiagonalTopLeftBottomRight=2,
    DiagonalTopRightBottomLeft=3
};
struct CameraOverlayProfile {
    OverlayCorner placement=OverlayCorner::TopRight;
    CameraMask mask=CameraMask::RoundedRectangle;
    float width_fraction=0.235f; // output width
    float margin_fraction=0.03f;
    float corner_fraction=0.15f; // fraction of shorter half-dimension
    float border_pixels=2.0f;
    float opacity=1.0f;
    bool mirror=false;
    [[nodiscard]] bool valid() const noexcept {
        return static_cast<std::uint32_t>(placement)<=3 &&
               static_cast<std::uint32_t>(mask)<=3 &&
               std::isfinite(width_fraction) &&
               width_fraction>=0.15f && width_fraction<=0.40f &&
               std::isfinite(margin_fraction) &&
               margin_fraction>=0.0f && margin_fraction<=0.12f &&
               std::isfinite(corner_fraction) &&
               corner_fraction>=0.0f && corner_fraction<=0.50f &&
               std::isfinite(border_pixels) &&
               border_pixels>=0.0f && border_pixels<=12.0f &&
               std::isfinite(opacity) &&
               opacity>=0.0f && opacity<=1.0f;
    }
};
struct LogoOverlayProfile {
    bool enabled=false;
    std::wstring path;
    OverlayCorner placement=OverlayCorner::BottomRight;
    float width_fraction=0.13f;
    float margin_fraction=0.025f;
    float opacity=1.0f;
    [[nodiscard]] bool valid() const noexcept {
        return static_cast<std::uint32_t>(placement)<=4 &&
               path.size() < 512 &&
               std::isfinite(width_fraction) &&
               width_fraction>=0.03f && width_fraction<=0.40f &&
               std::isfinite(margin_fraction) &&
               margin_fraction>=0.0f && margin_fraction<=0.12f &&
               std::isfinite(opacity) &&
               opacity>=0.0f && opacity<=1.0f &&
               (!enabled || !path.empty());
    }
};
struct OutputOverlayProfile {
    CameraOverlayProfile camera;
    LogoOverlayProfile logo;
    [[nodiscard]] bool valid() const noexcept {
        return camera.valid() && logo.valid();
    }
};

struct OutputRect {float left=0,top=0,right=0,bottom=0;};
[[nodiscard]] inline OutputRect place_output_overlay(
    OverlayCorner placement, float width_fraction, float height_fraction,
    float margin_fraction) noexcept {
    const float w=(std::clamp)(width_fraction,0.0f,1.0f);
    const float h=(std::clamp)(height_fraction,0.0f,1.0f);
    const float m=(std::clamp)(margin_fraction,0.0f,0.12f);
    const bool right=placement==OverlayCorner::TopRight ||
                     placement==OverlayCorner::BottomRight;
    const bool bottom=placement==OverlayCorner::BottomLeft ||
                      placement==OverlayCorner::BottomRight;
    const float x=placement==OverlayCorner::Center
        ? (1.0f-w)*0.5f : right ? 1.0f-m-w : m;
    const float y=placement==OverlayCorner::Center
        ? (1.0f-h)*0.5f : bottom ? 1.0f-m-h : m;
    return {x,y,x+w,y+h};
}
} // namespace arssyut::core::video
