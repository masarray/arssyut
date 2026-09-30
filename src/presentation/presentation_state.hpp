#pragma once

#include <array>
#include <cstdint>

namespace arssyut::presentation {

enum class ClickKind : std::uint8_t {
    None = 0,
    Left = 1,
    Right = 2,
    Middle = 3,
};

struct ClickFrame {
    float content_x = 0.5f;
    float content_y = 0.5f;
    float age_seconds = 0.0f;
    float lifetime_seconds = 0.0f;
    ClickKind kind = ClickKind::None;
};

struct KeyboardOverlayFrame {
    std::array<wchar_t, 64> text{};
    float opacity = 0.0f;
    std::uint32_t generation = 0;
};

struct PresentationFrameState {
    float camera_center_x = 0.5f;
    float camera_center_y = 0.5f;
    float camera_zoom = 1.0f;

    std::array<ClickFrame, 4> clicks{};
    KeyboardOverlayFrame keyboard{};
};

} // namespace arssyut::presentation
