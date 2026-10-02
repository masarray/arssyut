#include "platform/windows/graphics/cursor_shape_cache.hpp"

#ifdef _WIN32

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>
#include <utility>

namespace arssyut::windows {

namespace {

using arssyut::core::Result;
using arssyut::core::Status;
using arssyut::core::StatusCode;

[[nodiscard]] std::uint32_t detail(HRESULT hr) noexcept
{
    return static_cast<std::uint32_t>(hr);
}

[[nodiscard]] Status d3d_failure(HRESULT hr) noexcept
{
    return Status::failure(
        StatusCode::PlatformFailure,
        detail(hr));
}

[[nodiscard]] std::uint8_t channel(
    std::uint32_t pixel,
    unsigned shift) noexcept
{
    return static_cast<std::uint8_t>(
        (pixel >> shift) & 0xFFu);
}

[[nodiscard]] std::uint8_t recover_alpha(
    std::uint32_t over_black,
    std::uint32_t over_white) noexcept
{
    const int db =
        static_cast<int>(channel(over_white, 0)) -
        static_cast<int>(channel(over_black, 0));
    const int dg =
        static_cast<int>(channel(over_white, 8)) -
        static_cast<int>(channel(over_black, 8));
    const int dr =
        static_cast<int>(channel(over_white, 16)) -
        static_cast<int>(channel(over_black, 16));

    const int transparent =
        std::clamp(
            std::max({db, dg, dr}),
            0,
            255);
    return static_cast<std::uint8_t>(
        255 - transparent);
}

[[nodiscard]] std::uint8_t unpremultiply(
    std::uint8_t value,
    std::uint8_t alpha) noexcept
{
    if (alpha == 0)
        return 0;

    const unsigned scaled =
        (static_cast<unsigned>(value) * 255u +
         static_cast<unsigned>(alpha) / 2u) /
        static_cast<unsigned>(alpha);
    return static_cast<std::uint8_t>(
        std::min(scaled, 255u));
}

} // namespace

CursorShapeCache::~CursorShapeCache()
{
    if (dc_) {
        if (old_bitmap_)
            SelectObject(dc_, old_bitmap_);
        DeleteDC(dc_);
        dc_ = nullptr;
    }

    if (bitmap_) {
        DeleteObject(bitmap_);
        bitmap_ = nullptr;
    }

    old_bitmap_ = nullptr;
    bits_ = nullptr;
}

Result<std::unique_ptr<CursorShapeCache>>
CursorShapeCache::create(
    ID3D11Device *device) noexcept
{
    if (!device) {
        return Result<std::unique_ptr<CursorShapeCache>>::failure(
            Status::failure(StatusCode::InvalidArgument));
    }

    std::unique_ptr<CursorShapeCache> cache(
        new (std::nothrow) CursorShapeCache{});
    if (!cache) {
        return Result<std::unique_ptr<CursorShapeCache>>::failure(
            Status::failure(StatusCode::InternalError));
    }

    const Status status =
        cache->initialize(device);
    if (!status.ok()) {
        return Result<std::unique_ptr<CursorShapeCache>>::failure(
            status);
    }

    return Result<std::unique_ptr<CursorShapeCache>>::success(
        std::move(cache));
}

Status CursorShapeCache::initialize(
    ID3D11Device *device) noexcept
{
    device_ = device;

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = canvas_size;
    desc.Height = canvas_size;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    for (auto &slot : slots_) {
        HRESULT hr = device_->CreateTexture2D(
            &desc,
            nullptr,
            slot.texture.GetAddressOf());
        if (FAILED(hr))
            return d3d_failure(hr);

        hr = device_->CreateShaderResourceView(
            slot.texture.Get(),
            nullptr,
            slot.srv.GetAddressOf());
        if (FAILED(hr))
            return d3d_failure(hr);
    }

    dc_ = CreateCompatibleDC(nullptr);
    if (!dc_) {
        return Status::failure(
            StatusCode::PlatformFailure,
            GetLastError());
    }

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth =
        static_cast<LONG>(canvas_size);
    info.bmiHeader.biHeight =
        -static_cast<LONG>(canvas_size);
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    bitmap_ = CreateDIBSection(
        dc_,
        &info,
        DIB_RGB_COLORS,
        &bits_,
        nullptr,
        0);
    if (!bitmap_ || !bits_) {
        return Status::failure(
            StatusCode::PlatformFailure,
            GetLastError());
    }

    old_bitmap_ =
        SelectObject(dc_, bitmap_);

    scratch_.reset(
        new (std::nothrow) std::uint32_t[
            static_cast<std::size_t>(canvas_size) *
            static_cast<std::size_t>(canvas_size)]);
    if (!scratch_) {
        return Status::failure(
            StatusCode::InternalError);
    }

    return Status::success();
}

Status CursorShapeCache::rasterize(
    ID3D11DeviceContext *context,
    HCURSOR cursor,
    Slot &slot) noexcept
{
    if (!context ||
        !cursor ||
        !dc_ ||
        !bits_ ||
        !scratch_) {
        return Status::failure(
            StatusCode::InvalidArgument);
    }

    ICONINFO icon{};
    if (!GetIconInfo(cursor, &icon)) {
        return Status::failure(
            StatusCode::PlatformFailure,
            GetLastError());
    }

    BITMAP bitmap_info{};
    int original_width = 0;
    int original_height = 0;

    if (icon.hbmColor &&
        GetObjectW(
            icon.hbmColor,
            sizeof(bitmap_info),
            &bitmap_info) == sizeof(bitmap_info)) {
        original_width =
            std::abs(bitmap_info.bmWidth);
        original_height =
            std::abs(bitmap_info.bmHeight);
    } else if (
        icon.hbmMask &&
        GetObjectW(
            icon.hbmMask,
            sizeof(bitmap_info),
            &bitmap_info) == sizeof(bitmap_info)) {
        original_width =
            std::abs(bitmap_info.bmWidth);
        original_height =
            std::max(
                1,
                static_cast<int>(
                    std::abs(
                        bitmap_info.bmHeight) / 2));
    }

    if (original_width <= 0)
        original_width =
            std::max(GetSystemMetrics(SM_CXCURSOR), 1);
    if (original_height <= 0)
        original_height =
            std::max(GetSystemMetrics(SM_CYCURSOR), 1);

    const int width =
        std::clamp(
            original_width,
            1,
            static_cast<int>(canvas_size));
    const int height =
        std::clamp(
            original_height,
            1,
            static_cast<int>(canvas_size));

    const float scale_x =
        static_cast<float>(width) /
        static_cast<float>(
            std::max(original_width, 1));
    const float scale_y =
        static_cast<float>(height) /
        static_cast<float>(
            std::max(original_height, 1));

    const std::size_t pixel_count =
        static_cast<std::size_t>(canvas_size) *
        static_cast<std::size_t>(canvas_size);
    auto *pixels =
        static_cast<std::uint32_t *>(bits_);

    std::fill(
        pixels,
        pixels + pixel_count,
        0xFF000000u);

    const BOOL black_drawn =
        DrawIconEx(
            dc_,
            0,
            0,
            cursor,
            width,
            height,
            0,
            nullptr,
            DI_NORMAL);

    if (black_drawn) {
        std::memcpy(
            scratch_.get(),
            pixels,
            pixel_count * sizeof(std::uint32_t));
    }

    std::fill(
        pixels,
        pixels + pixel_count,
        0xFFFFFFFFu);

    const BOOL white_drawn =
        DrawIconEx(
            dc_,
            0,
            0,
            cursor,
            width,
            height,
            0,
            nullptr,
            DI_NORMAL);

    if (icon.hbmColor)
        DeleteObject(icon.hbmColor);
    if (icon.hbmMask)
        DeleteObject(icon.hbmMask);

    if (!black_drawn || !white_drawn) {
        return Status::failure(
            StatusCode::PlatformFailure,
            GetLastError());
    }

    for (std::size_t y = 0;
         y < canvas_size;
         ++y) {
        for (std::size_t x = 0;
             x < canvas_size;
             ++x) {
            const std::size_t index =
                y * canvas_size + x;

            if (x >= static_cast<std::size_t>(width) ||
                y >= static_cast<std::size_t>(height)) {
                pixels[index] = 0;
                continue;
            }

            const std::uint32_t black =
                scratch_[index];
            const std::uint32_t white =
                pixels[index];
            const std::uint8_t alpha =
                recover_alpha(
                    black,
                    white);

            if (alpha <= 2) {
                pixels[index] = 0;
                continue;
            }

            const std::uint8_t blue =
                unpremultiply(
                    channel(black, 0),
                    alpha);
            const std::uint8_t green =
                unpremultiply(
                    channel(black, 8),
                    alpha);
            const std::uint8_t red =
                unpremultiply(
                    channel(black, 16),
                    alpha);

            pixels[index] =
                (static_cast<std::uint32_t>(alpha) << 24) |
                (static_cast<std::uint32_t>(red) << 16) |
                (static_cast<std::uint32_t>(green) << 8) |
                static_cast<std::uint32_t>(blue);
        }
    }

    context->UpdateSubresource(
        slot.texture.Get(),
        0,
        nullptr,
        pixels,
        canvas_size * sizeof(std::uint32_t),
        0);

    slot.handle = cursor;
    slot.width =
        static_cast<std::uint32_t>(width);
    slot.height =
        static_cast<std::uint32_t>(height);
    slot.hotspot_x =
        static_cast<std::uint32_t>(
            std::clamp(
                static_cast<int>(
                    std::lround(
                        static_cast<float>(
                            icon.xHotspot) *
                        scale_x)),
                0,
                width));
    slot.hotspot_y =
        static_cast<std::uint32_t>(
            std::clamp(
                static_cast<int>(
                    std::lround(
                        static_cast<float>(
                            icon.yHotspot) *
                        scale_y)),
                0,
                height));
    slot.valid = true;

    return Status::success();
}

Status CursorShapeCache::select(
    ID3D11DeviceContext *context,
    HCURSOR cursor) noexcept
{
    if (!context) {
        return Status::failure(
            StatusCode::InvalidArgument);
    }

    if (!cursor) {
        active_slot_ = slot_count;
        return Status::success();
    }

    if (active_slot_ < slots_.size() &&
        slots_[active_slot_].valid &&
        slots_[active_slot_].handle == cursor) {
        slots_[active_slot_].last_used =
            ++use_generation_;
        return Status::success();
    }

    for (std::size_t i = 0;
         i < slots_.size();
         ++i) {
        if (slots_[i].valid &&
            slots_[i].handle == cursor) {
            active_slot_ = i;
            slots_[i].last_used =
                ++use_generation_;
            return Status::success();
        }
    }

    std::size_t target = slots_.size();
    std::uint64_t oldest =
        std::numeric_limits<std::uint64_t>::max();

    for (std::size_t i = 0;
         i < slots_.size();
         ++i) {
        if (!slots_[i].valid) {
            target = i;
            break;
        }

        if (slots_[i].last_used < oldest) {
            oldest = slots_[i].last_used;
            target = i;
        }
    }

    if (target >= slots_.size())
        target = 0;

    const Status status =
        rasterize(
            context,
            cursor,
            slots_[target]);
    if (!status.ok()) {
        active_slot_ = slot_count;
        return status;
    }

    slots_[target].last_used =
        ++use_generation_;
    active_slot_ = target;

    return Status::success();
}

CursorShapeView CursorShapeCache::active() const noexcept
{
    CursorShapeView result;
    if (active_slot_ >= slots_.size())
        return result;

    const auto &slot =
        slots_[active_slot_];
    if (!slot.valid)
        return result;

    result.srv = slot.srv.Get();
    result.width = slot.width;
    result.height = slot.height;
    result.hotspot_x = slot.hotspot_x;
    result.hotspot_y = slot.hotspot_y;
    return result;
}

} // namespace arssyut::windows

#endif
