#include "app/region_geometry.hpp"

#ifdef _WIN32

#include <algorithm>
#include <cmath>

namespace arssyut::app {

RECT clamp_region_rect(
    RECT rect,
    RECT bounds,
    LONG minimum_width,
    LONG minimum_height) noexcept
{
    const LONG bounds_width =
        bounds.right - bounds.left;
    const LONG bounds_height =
        bounds.bottom - bounds.top;

    if (bounds_width <= 0 ||
        bounds_height <= 0) {
        return {};
    }

    const LONG min_width =
        std::min(
            std::max<LONG>(
                2,
                minimum_width),
            bounds_width);
    const LONG min_height =
        std::min(
            std::max<LONG>(
                2,
                minimum_height),
            bounds_height);

    LONG width =
        std::max(
            min_width,
            rect.right - rect.left);
    LONG height =
        std::max(
            min_height,
            rect.bottom - rect.top);

    width =
        std::min(
            width,
            bounds_width);
    height =
        std::min(
            height,
            bounds_height);

    const LONG max_left =
        bounds.right - width;
    const LONG max_top =
        bounds.bottom - height;

    const LONG left =
        std::clamp(
            rect.left,
            bounds.left,
            max_left);
    const LONG top =
        std::clamp(
            rect.top,
            bounds.top,
            max_top);

    return {
        left,
        top,
        left + width,
        top + height};
}

RECT default_region_rect(
    RECT bounds) noexcept
{
    const LONG bounds_width =
        bounds.right - bounds.left;
    const LONG bounds_height =
        bounds.bottom - bounds.top;

    if (bounds_width <= 0 ||
        bounds_height <= 0) {
        return {};
    }

    LONG width =
        std::max<LONG>(
            320,
            static_cast<LONG>(
                std::lround(
                    static_cast<double>(
                        bounds_width) *
                    0.68)));

    width =
        std::min(
            width,
            bounds_width);

    LONG height =
        std::max<LONG>(
            180,
            static_cast<LONG>(
                std::lround(
                    static_cast<double>(
                        width) *
                    9.0 /
                    16.0)));

    if (height > bounds_height) {
        height =
            bounds_height;
        width =
            std::min(
                bounds_width,
                static_cast<LONG>(
                    std::lround(
                        static_cast<double>(
                            height) *
                        16.0 /
                        9.0)));
    }

    RECT rect{};
    rect.left =
        bounds.left +
        (bounds_width - width) / 2;
    rect.top =
        bounds.top +
        (bounds_height - height) / 2;
    rect.right =
        rect.left + width;
    rect.bottom =
        rect.top + height;

    return clamp_region_rect(
        rect,
        bounds);
}

bool map_region_to_crop(
    RECT source_screen_bounds,
    RECT selection_screen_rect,
    RegionCropMapping &mapping) noexcept
{
    mapping = {};

    const LONG source_width =
        source_screen_bounds.right -
        source_screen_bounds.left;
    const LONG source_height =
        source_screen_bounds.bottom -
        source_screen_bounds.top;

    if (source_width <= 0 ||
        source_height <= 0) {
        return false;
    }

    RECT region =
        clamp_region_rect(
            selection_screen_rect,
            source_screen_bounds);

    LONG width =
        region.right - region.left;
    LONG height =
        region.bottom - region.top;

    // NV12 is 4:2:0, so output dimensions must be even. Keep left/top stable
    // and trim at most one pixel from right/bottom.
    width &= ~1L;
    height &= ~1L;

    if (width < 2 ||
        height < 2) {
        return false;
    }

    region.right =
        region.left + width;
    region.bottom =
        region.top + height;

    const LONG crop_left =
        region.left -
        source_screen_bounds.left;
    const LONG crop_top =
        region.top -
        source_screen_bounds.top;

    if (crop_left < 0 ||
        crop_top < 0 ||
        crop_left + width >
            source_width ||
        crop_top + height >
            source_height) {
        return false;
    }

    mapping.screen_rect =
        region;
    mapping.crop = {
        static_cast<std::uint32_t>(
            crop_left),
        static_cast<std::uint32_t>(
            crop_top),
        static_cast<std::uint32_t>(
            crop_left + width),
        static_cast<std::uint32_t>(
            crop_top + height)};
    mapping.output_size = {
        static_cast<std::uint32_t>(
            width),
        static_cast<std::uint32_t>(
            height)};

    return true;
}

RECT camera_viewport_rect(
    RECT source_screen_rect,
    float center_x,
    float center_y,
    float zoom) noexcept
{
    const LONG source_width =
        source_screen_rect.right -
        source_screen_rect.left;
    const LONG source_height =
        source_screen_rect.bottom -
        source_screen_rect.top;

    if (source_width <= 0 ||
        source_height <= 0) {
        return source_screen_rect;
    }

    const float safe_zoom =
        std::clamp(
            zoom,
            1.0f,
            4.0f);
    center_x =
        std::clamp(
            center_x,
            0.0f,
            1.0f);
    center_y =
        std::clamp(
            center_y,
            0.0f,
            1.0f);

    const float viewport_width =
        static_cast<float>(
            source_width) /
        safe_zoom;
    const float viewport_height =
        static_cast<float>(
            source_height) /
        safe_zoom;

    float left =
        static_cast<float>(
            source_screen_rect.left) +
        center_x *
            static_cast<float>(
                source_width) -
        viewport_width * 0.5f;

    float top =
        static_cast<float>(
            source_screen_rect.top) +
        center_y *
            static_cast<float>(
                source_height) -
        viewport_height * 0.5f;

    const float min_left =
        static_cast<float>(
            source_screen_rect.left);
    const float max_left =
        static_cast<float>(
            source_screen_rect.right) -
        viewport_width;
    const float min_top =
        static_cast<float>(
            source_screen_rect.top);
    const float max_top =
        static_cast<float>(
            source_screen_rect.bottom) -
        viewport_height;

    left =
        std::clamp(
            left,
            min_left,
            std::max(
                min_left,
                max_left));
    top =
        std::clamp(
            top,
            min_top,
            std::max(
                min_top,
                max_top));

    RECT result{};
    result.left =
        static_cast<LONG>(
            std::lround(left));
    result.top =
        static_cast<LONG>(
            std::lround(top));
    result.right =
        result.left +
        static_cast<LONG>(
            std::lround(
                viewport_width));
    result.bottom =
        result.top +
        static_cast<LONG>(
            std::lround(
                viewport_height));

    return result;
}

} // namespace arssyut::app

#endif
