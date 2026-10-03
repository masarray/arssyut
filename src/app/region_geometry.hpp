#pragma once

#ifdef _WIN32

#include "core/video/frame_geometry.hpp"

#include <Windows.h>

namespace arssyut::app {

struct RegionCropMapping {
    RECT screen_rect{};
    arssyut::core::CropRect crop{};
    arssyut::core::FrameSize output_size{};
};

[[nodiscard]] RECT clamp_region_rect(
    RECT rect,
    RECT bounds,
    LONG minimum_width = 320,
    LONG minimum_height = 180) noexcept;

[[nodiscard]] RECT default_region_rect(
    RECT bounds) noexcept;

[[nodiscard]] bool map_region_to_crop(
    RECT source_screen_bounds,
    RECT selection_screen_rect,
    RegionCropMapping &mapping) noexcept;

} // namespace arssyut::app

#endif
