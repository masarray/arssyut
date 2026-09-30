#pragma once

#include <algorithm>
#include <cstdint>

namespace arssyut::core {

struct FrameSize {
    std::uint32_t width = 0;
    std::uint32_t height = 0;

    [[nodiscard]] constexpr bool valid() const noexcept
    {
        return width > 0 && height > 0;
    }

    [[nodiscard]] friend constexpr bool operator==(
        FrameSize lhs,
        FrameSize rhs) noexcept = default;
};

struct CropRect {
    std::uint32_t left = 0;
    std::uint32_t top = 0;
    std::uint32_t right = 0;
    std::uint32_t bottom = 0;

    [[nodiscard]] constexpr std::uint32_t width() const noexcept
    {
        return right > left ? right - left : 0;
    }

    [[nodiscard]] constexpr std::uint32_t height() const noexcept
    {
        return bottom > top ? bottom - top : 0;
    }

    [[nodiscard]] constexpr bool valid() const noexcept
    {
        return width() > 0 && height() > 0;
    }

    [[nodiscard]] friend constexpr bool operator==(
        CropRect lhs,
        CropRect rhs) noexcept = default;
};

[[nodiscard]] constexpr CropRect full_frame_crop(
    FrameSize size) noexcept
{
    return {0, 0, size.width, size.height};
}

[[nodiscard]] constexpr CropRect clamp_crop(
    CropRect crop,
    FrameSize source) noexcept
{
    if (!source.valid())
        return {};

    crop.left = std::min(crop.left, source.width);
    crop.top = std::min(crop.top, source.height);
    crop.right = std::min(crop.right, source.width);
    crop.bottom = std::min(crop.bottom, source.height);

    if (crop.right <= crop.left || crop.bottom <= crop.top)
        return full_frame_crop(source);

    return crop;
}

} // namespace arssyut::core
