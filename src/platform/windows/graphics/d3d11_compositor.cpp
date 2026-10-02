#include "platform/windows/graphics/d3d11_compositor.hpp"

#ifdef _WIN32

#include "core/time/monotonic_clock.hpp"
#include "platform/windows/graphics/cursor_shape_cache.hpp"

#include <d3dcompiler.h>

#include <algorithm>
#include <cwchar>
#include <limits>
#include <new>
#include <utility>

namespace arssyut::windows {

namespace {

using arssyut::core::CropRect;
using arssyut::core::FrameSize;
using arssyut::core::MonotonicClock;
using arssyut::core::Result;
using arssyut::core::Status;
using arssyut::core::StatusCode;
using arssyut::core::TimePoint;

constexpr char shader_source[] = R"(
cbuffer PresentationConstants : register(b0)
{
    float4 uv_rect;
    float4 camera_keyboard;
    float4 output_info;
    float4 keyboard_rect;
    float4 cursor_data;
    float4 cursor_geometry;
    float4 click0;
    float4 click1;
    float4 click2;
    float4 click3;
};

Texture2D source_texture : register(t0);
Texture2D keyboard_texture : register(t1);
Texture2D cursor_texture : register(t2);
SamplerState source_sampler : register(s0);

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

VertexOutput vs_main(uint vertex_id : SV_VertexID)
{
    VertexOutput output;
    float2 uv = float2((vertex_id << 1) & 2, vertex_id & 2);
    output.uv = uv;
    output.position = float4(
        uv.x * 2.0f - 1.0f,
        1.0f - uv.y * 2.0f,
        0.0f,
        1.0f);
    return output;
}

/* Arssyut presentation click skin.
 * Camera/content anchoring remains the P3R ArZoom parity path, but click
 * appearance intentionally uses a single larger satisfying ring requested
 * during direct visual validation. */
float smooth_out(float t)
{
    t = saturate(t);
    const float inv = 1.0 - t;
    return 1.0 - inv * inv * inv;
}

float vector_ring(
    float distance_px,
    float radius_px,
    float half_width_px)
{
    const float edge =
        abs(distance_px - radius_px);
    return 1.0 - smoothstep(
        half_width_px,
        half_width_px + 1.25,
        edge);
}

float2 project_content(
    float2 content_position,
    float2 camera_center,
    float safe_zoom)
{
    return float2(0.5, 0.5) +
           (content_position - camera_center) * safe_zoom;
}

float2 event_delta_px(
    float2 output_uv,
    float4 event_data,
    float2 camera_center,
    float safe_zoom,
    float2 safe_viewport)
{
    const float2 center = project_content(
        event_data.xy,
        camera_center,
        safe_zoom);
    return (output_uv - center) * safe_viewport;
}

float3 click_color(float type)
{
    if (type < 1.5)
        return float3(0.196, 0.722, 1.000); // #32B8FF
    if (type < 2.5)
        return float3(1.000, 0.361, 0.541); // #FF5C8A
    return float3(1.000, 0.784, 0.341);     // #FFC857
}

float3 composite_single_ring(
    float3 base,
    float2 delta_px,
    float progress,
    float type,
    float2 safe_viewport)
{
    progress = saturate(progress);

    const float scale = clamp(
        min(safe_viewport.x, safe_viewport.y) / 1080.0,
        0.85,
        1.60);

    const float expansion =
        smooth_out(progress);
    const float radius_px =
        lerp(11.0, 62.0, expansion) * scale;
    const float half_width_px =
        lerp(2.25, 1.00, progress) * scale;
    const float distance_px =
        length(delta_px);
    const float edge =
        abs(distance_px - radius_px);

    const float ignition =
        smoothstep(0.0, 0.050, progress);
    const float core_fade =
        1.0 - smoothstep(0.28, 1.0, progress);
    const float glow_fade =
        1.0 - smoothstep(0.18, 1.0, progress);

    const float core =
        vector_ring(
            distance_px,
            radius_px,
            half_width_px) *
        ignition *
        core_fade;

    const float glow_spread_px =
        lerp(5.0, 18.0, expansion) * scale;
    const float glow_distance =
        max(edge - half_width_px, 0.0);
    const float glow_shape =
        saturate(
            1.0 -
            glow_distance /
                max(glow_spread_px, 0.001));
    const float glow =
        glow_shape *
        glow_shape *
        ignition *
        glow_fade;

    const float support =
        vector_ring(
            distance_px,
            radius_px,
            half_width_px + 1.75 * scale) *
        ignition *
        core_fade;

    const float3 tint =
        click_color(type);

    const float luminance =
        dot(
            base,
            float3(0.2126, 0.7152, 0.0722));
    const float bright_surface =
        smoothstep(0.60, 0.90, luminance);

    const float3 visible =
        lerp(
            tint,
            tint * 0.68,
            bright_surface);
    const float3 support_color =
        lerp(
            tint * 0.18,
            float3(0.025, 0.035, 0.055),
            bright_surface);

    base = lerp(
        base,
        support_color,
        saturate(
            support *
            (0.050 + 0.20 * bright_surface)));

    base = lerp(
        base,
        visible,
        saturate(core * 0.98));

    base = lerp(
        base,
        tint,
        saturate(
            glow *
            (0.16 +
             0.08 * (1.0 - bright_surface))));

    base +=
        tint *
        glow *
        0.10 *
        (1.0 - bright_surface);

    return saturate(base);
}

float3 apply_click(
    float3 base,
    float2 output_uv,
    float4 event_data,
    float2 camera_center,
    float safe_zoom,
    float2 safe_viewport)
{
    const float type = event_data.w;
    if (type < 0.5)
        return base;

    const float2 delta_px =
        event_delta_px(
            output_uv,
            event_data,
            camera_center,
            safe_zoom,
            safe_viewport);

    return composite_single_ring(
        base,
        delta_px,
        event_data.z,
        type,
        safe_viewport);
}

float4 composite_cursor(
    float4 base,
    float2 output_uv,
    float2 camera_center,
    float safe_zoom,
    float2 safe_viewport)
{
    const float opacity =
        saturate(cursor_data.w);
    if (opacity <= 0.001)
        return base;

    const float2 shape_size =
        max(
            cursor_geometry.xy,
            float2(1.0, 1.0));
    const float2 hotspot =
        cursor_geometry.zw;

    const float presentation_scale =
        max(cursor_data.z, 0.01) *
        clamp(
            min(safe_viewport.x, safe_viewport.y) / 1080.0,
            0.85,
            1.60);

    const float2 hotspot_uv =
        project_content(
            cursor_data.xy,
            camera_center,
            safe_zoom);
    const float2 hotspot_px =
        hotspot_uv * safe_viewport;

    const float2 top_left_px =
        hotspot_px -
        hotspot * presentation_scale;
    const float2 size_px =
        shape_size * presentation_scale;

    const float2 pixel =
        output_uv * safe_viewport;
    const float2 local =
        (pixel - top_left_px) /
        max(size_px, float2(0.001, 0.001));

    if (local.x < 0.0 ||
        local.y < 0.0 ||
        local.x > 1.0 ||
        local.y > 1.0) {
        return base;
    }

    const float2 texture_uv =
        local *
        (shape_size / 256.0);
    const float4 cursor =
        cursor_texture.Sample(
            source_sampler,
            texture_uv);

    const float alpha =
        saturate(cursor.a * opacity);

    base.rgb =
        lerp(
            base.rgb,
            cursor.rgb,
            alpha);
    return base;
}

float4 ps_main(VertexOutput input) : SV_Target
{
    const float2 camera_center = camera_keyboard.xy;
    const float zoom = max(camera_keyboard.z, 1.0f);

    const float2 camera_uv =
        camera_center + (input.uv - 0.5f) / zoom;
    const float2 uv =
        lerp(uv_rect.xy, uv_rect.zw, camera_uv);

    float4 color =
        source_texture.Sample(source_sampler, uv);

    const float2 safe_viewport =
        max(output_info.xy, float2(1.0f, 1.0f));

    color.rgb = apply_click(
        color.rgb,
        input.uv,
        click0,
        camera_center,
        zoom,
        safe_viewport);
    color.rgb = apply_click(
        color.rgb,
        input.uv,
        click1,
        camera_center,
        zoom,
        safe_viewport);
    color.rgb = apply_click(
        color.rgb,
        input.uv,
        click2,
        camera_center,
        zoom,
        safe_viewport);
    color.rgb = apply_click(
        color.rgb,
        input.uv,
        click3,
        camera_center,
        zoom,
        safe_viewport);

    color = composite_cursor(
        color,
        input.uv,
        camera_center,
        zoom,
        safe_viewport);

    const float keyboard_opacity =
        saturate(camera_keyboard.w);

    if (keyboard_opacity > 0.001f &&
        input.uv.x >= keyboard_rect.x &&
        input.uv.x <= keyboard_rect.z &&
        input.uv.y >= keyboard_rect.y &&
        input.uv.y <= keyboard_rect.w) {
        float2 keyboard_uv =
            (input.uv - keyboard_rect.xy) /
            max(
                keyboard_rect.zw - keyboard_rect.xy,
                0.0001f);
        keyboard_uv *=
            max(output_info.zw, float2(0.0001f, 0.0001f));

        const float4 overlay =
            keyboard_texture.Sample(
                source_sampler,
                keyboard_uv);

        const float alpha =
            saturate(overlay.a * keyboard_opacity);

        color.rgb =
            lerp(color.rgb, overlay.rgb, alpha);
    }

    return color;
}
)";

struct PresentationConstants {
    float uv_left;
    float uv_top;
    float uv_right;
    float uv_bottom;

    float camera_center_x;
    float camera_center_y;
    float camera_zoom;
    float keyboard_opacity;

    float output_width;
    float output_height;
    float keyboard_uv_scale_x;
    float keyboard_uv_scale_y;

    float keyboard_left;
    float keyboard_top;
    float keyboard_right;
    float keyboard_bottom;

    float cursor_x;
    float cursor_y;
    float cursor_scale;
    float cursor_opacity;

    float cursor_width;
    float cursor_height;
    float cursor_hotspot_x;
    float cursor_hotspot_y;

    float clicks[16]{};
};

constexpr UINT kKeyboardWidth = 768;
constexpr UINT kKeyboardHeight = 128;

[[nodiscard]] std::uint32_t hresult_detail(HRESULT hr) noexcept
{
    return static_cast<std::uint32_t>(hr);
}

[[nodiscard]] Status d3d_failure(HRESULT hr) noexcept
{
    return Status::failure(
        StatusCode::PlatformFailure,
        hresult_detail(hr));
}

[[nodiscard]] Result<Microsoft::WRL::ComPtr<ID3DBlob>>
compile_shader(const char *entry, const char *target) noexcept
{
    Microsoft::WRL::ComPtr<ID3DBlob> bytecode;
    Microsoft::WRL::ComPtr<ID3DBlob> errors;

    const UINT flags =
#ifdef _DEBUG
        D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
        D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

    const HRESULT hr = D3DCompile(
        shader_source,
        sizeof(shader_source) - 1,
        "arssyut-p1-compositor",
        nullptr,
        nullptr,
        entry,
        target,
        flags,
        0,
        bytecode.GetAddressOf(),
        errors.GetAddressOf());

    if (FAILED(hr) || !bytecode) {
        return Result<Microsoft::WRL::ComPtr<ID3DBlob>>::failure(
            d3d_failure(hr));
    }

    return Result<Microsoft::WRL::ComPtr<ID3DBlob>>::success(
        std::move(bytecode));
}

[[nodiscard]] std::uint32_t elapsed_microseconds(
    TimePoint start,
    TimePoint end) noexcept
{
    const std::int64_t ticks = std::max<std::int64_t>(
        0,
        MonotonicClock::duration_ticks(start, end));

    return static_cast<std::uint32_t>(
        std::min<std::uint64_t>(
            static_cast<std::uint64_t>(ticks) / 10ULL,
            std::numeric_limits<std::uint32_t>::max()));
}

} // namespace

D3D11Compositor::~D3D11Compositor()
{
    if (keyboard_dc_) {
        if (keyboard_old_bitmap_)
            SelectObject(keyboard_dc_, keyboard_old_bitmap_);
        DeleteDC(keyboard_dc_);
        keyboard_dc_ = nullptr;
    }

    if (keyboard_font_) {
        DeleteObject(keyboard_font_);
        keyboard_font_ = nullptr;
    }
    if (keyboard_light_pen_) {
        DeleteObject(keyboard_light_pen_);
        keyboard_light_pen_ = nullptr;
    }
    if (keyboard_shadow_brush_) {
        DeleteObject(keyboard_shadow_brush_);
        keyboard_shadow_brush_ = nullptr;
    }
    if (keyboard_light_brush_) {
        DeleteObject(keyboard_light_brush_);
        keyboard_light_brush_ = nullptr;
    }

    if (keyboard_bitmap_) {
        DeleteObject(keyboard_bitmap_);
        keyboard_bitmap_ = nullptr;
    }

    keyboard_old_bitmap_ = nullptr;
    keyboard_bits_ = nullptr;
}

Result<std::unique_ptr<D3D11Compositor>>
D3D11Compositor::create(ID3D11Device *device) noexcept
{
    if (!device) {
        return Result<std::unique_ptr<D3D11Compositor>>::failure(
            Status::failure(StatusCode::InvalidArgument));
    }

    std::unique_ptr<D3D11Compositor> compositor(
        new (std::nothrow) D3D11Compositor{});
    if (!compositor) {
        return Result<std::unique_ptr<D3D11Compositor>>::failure(
            Status::failure(StatusCode::InternalError));
    }

    const Status status = compositor->initialize(device);
    if (!status.ok()) {
        return Result<std::unique_ptr<D3D11Compositor>>::failure(status);
    }

    return Result<std::unique_ptr<D3D11Compositor>>::success(
        std::move(compositor));
}

Status D3D11Compositor::initialize(ID3D11Device *device) noexcept
{
    device_ = device;

    auto vs = compile_shader("vs_main", "vs_5_0");
    if (!vs)
        return vs.status();

    auto ps = compile_shader("ps_main", "ps_5_0");
    if (!ps)
        return ps.status();

    HRESULT hr = device_->CreateVertexShader(
        vs.value()->GetBufferPointer(),
        vs.value()->GetBufferSize(),
        nullptr,
        vertex_shader_.GetAddressOf());
    if (FAILED(hr))
        return d3d_failure(hr);

    hr = device_->CreatePixelShader(
        ps.value()->GetBufferPointer(),
        ps.value()->GetBufferSize(),
        nullptr,
        pixel_shader_.GetAddressOf());
    if (FAILED(hr))
        return d3d_failure(hr);

    D3D11_SAMPLER_DESC sampler_desc{};
    sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;

    hr = device_->CreateSamplerState(
        &sampler_desc,
        sampler_.GetAddressOf());
    if (FAILED(hr))
        return d3d_failure(hr);

    D3D11_BUFFER_DESC constant_desc{};
    constant_desc.ByteWidth = sizeof(PresentationConstants);
    constant_desc.Usage = D3D11_USAGE_DEFAULT;
    constant_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

    hr = device_->CreateBuffer(
        &constant_desc,
        nullptr,
        crop_constant_buffer_.GetAddressOf());
    if (FAILED(hr))
        return d3d_failure(hr);

    const Status keyboard_status =
        initialize_keyboard_overlay();
    if (!keyboard_status.ok())
        return keyboard_status;

    auto cursor_result =
        CursorShapeCache::create(
            device_.Get());
    if (!cursor_result)
        return cursor_result.status();
    cursor_cache_ =
        std::move(cursor_result).value();

    for (auto &slot : gpu_queries_) {
        D3D11_QUERY_DESC disjoint_desc{};
        disjoint_desc.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;

        hr = device_->CreateQuery(
            &disjoint_desc,
            slot.disjoint.GetAddressOf());
        if (FAILED(hr))
            return d3d_failure(hr);

        D3D11_QUERY_DESC timestamp_desc{};
        timestamp_desc.Query = D3D11_QUERY_TIMESTAMP;

        hr = device_->CreateQuery(
            &timestamp_desc,
            slot.start.GetAddressOf());
        if (FAILED(hr))
            return d3d_failure(hr);

        hr = device_->CreateQuery(
            &timestamp_desc,
            slot.end.GetAddressOf());
        if (FAILED(hr))
            return d3d_failure(hr);
    }

    return Status::success();
}

Status D3D11Compositor::initialize_keyboard_overlay() noexcept
{
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = kKeyboardWidth;
    desc.Height = kKeyboardHeight;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    HRESULT hr = device_->CreateTexture2D(
        &desc,
        nullptr,
        keyboard_texture_.GetAddressOf());
    if (FAILED(hr))
        return d3d_failure(hr);

    hr = device_->CreateShaderResourceView(
        keyboard_texture_.Get(),
        nullptr,
        keyboard_srv_.GetAddressOf());
    if (FAILED(hr))
        return d3d_failure(hr);

    keyboard_dc_ = CreateCompatibleDC(nullptr);
    if (!keyboard_dc_) {
        return Status::failure(
            StatusCode::PlatformFailure,
            GetLastError());
    }

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth =
        static_cast<LONG>(kKeyboardWidth);
    info.bmiHeader.biHeight =
        -static_cast<LONG>(kKeyboardHeight);
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    keyboard_bitmap_ = CreateDIBSection(
        keyboard_dc_,
        &info,
        DIB_RGB_COLORS,
        &keyboard_bits_,
        nullptr,
        0);
    if (!keyboard_bitmap_ || !keyboard_bits_) {
        return Status::failure(
            StatusCode::PlatformFailure,
            GetLastError());
    }

    keyboard_old_bitmap_ =
        SelectObject(
            keyboard_dc_,
            keyboard_bitmap_);

    keyboard_font_ = CreateFontW(
        -30,
        0,
        0,
        0,
        FW_SEMIBOLD,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        ANTIALIASED_QUALITY,
        DEFAULT_PITCH,
        L"Segoe UI");
    if (!keyboard_font_) {
        return Status::failure(
            StatusCode::PlatformFailure,
            GetLastError());
    }

    keyboard_shadow_brush_ =
        CreateSolidBrush(RGB(16, 19, 24));
    keyboard_light_brush_ =
        CreateSolidBrush(RGB(246, 247, 245));
    keyboard_light_pen_ =
        CreatePen(PS_SOLID, 2, RGB(198, 202, 207));

    if (!keyboard_shadow_brush_ ||
        !keyboard_light_brush_ ||
        !keyboard_light_pen_) {
        return Status::failure(
            StatusCode::PlatformFailure,
            GetLastError());
    }

    return Status::success();
}

void D3D11Compositor::rasterize_keyboard_keycaps(
    const arssyut::presentation::KeyboardOverlayFrame &keyboard) noexcept
{
    if (!keyboard_dc_ ||
        !keyboard_bits_ ||
        !keyboard_font_ ||
        !keyboard_shadow_brush_ ||
        !keyboard_light_brush_ ||
        !keyboard_light_pen_) {
        return;
    }

    auto *pixels =
        static_cast<std::uint32_t *>(keyboard_bits_);
    std::fill(
        pixels,
        pixels +
            static_cast<std::size_t>(kKeyboardWidth) *
                static_cast<std::size_t>(kKeyboardHeight),
        0u);

    keyboard_content_width_ = 1;
    keyboard_content_height_ = 1;

    const std::size_t count =
        std::min(
            keyboard.keycap_count,
            keyboard.keycaps.size());
    if (count == 0)
        return;

    HGDIOBJ old_font =
        SelectObject(
            keyboard_dc_,
            keyboard_font_);
    HGDIOBJ old_brush =
        GetCurrentObject(
            keyboard_dc_,
            OBJ_BRUSH);
    HGDIOBJ old_pen =
        GetCurrentObject(
            keyboard_dc_,
            OBJ_PEN);

    SetBkMode(
        keyboard_dc_,
        TRANSPARENT);

    constexpr int kPadding = 8;
    constexpr int kGap = 10;
    constexpr int kTop = 12;
    constexpr int kFaceHeight = 82;
    constexpr int kDepth = 6;
    constexpr int kCorner = 16;

    int x = kPadding;

    for (std::size_t i = 0; i < count; ++i) {
        const auto &keycap =
            keyboard.keycaps[i];

        const bool windows_logo =
            keycap.glyph ==
            arssyut::presentation::KeycapGlyph::WindowsLogo;

        if (!windows_logo &&
            keycap.label[0] == L'\0') {
            continue;
        }

        SIZE extent{};
        if (!windows_logo) {
            GetTextExtentPoint32W(
                keyboard_dc_,
                keycap.label.data(),
                static_cast<int>(
                    wcsnlen_s(
                        keycap.label.data(),
                        keycap.label.size())),
                &extent);
        }

        const int width =
            windows_logo
                ? 76
                : std::clamp(
                      static_cast<int>(extent.cx) + 34,
                      64,
                      160);

        RECT shadow{
            x + 2,
            kTop + kDepth,
            x + width + 2,
            kTop + kFaceHeight + kDepth
        };

        SelectObject(
            keyboard_dc_,
            keyboard_shadow_brush_);
        SelectObject(
            keyboard_dc_,
            GetStockObject(NULL_PEN));
        RoundRect(
            keyboard_dc_,
            shadow.left,
            shadow.top,
            shadow.right,
            shadow.bottom,
            kCorner,
            kCorner);

        SelectObject(
            keyboard_dc_,
            keyboard_light_brush_);
        SelectObject(
            keyboard_dc_,
            keyboard_light_pen_);

        RECT face{
            x,
            kTop,
            x + width,
            kTop + kFaceHeight
        };

        RoundRect(
            keyboard_dc_,
            face.left,
            face.top,
            face.right,
            face.bottom,
            kCorner,
            kCorner);

        SetTextColor(
            keyboard_dc_,
            RGB(31, 34, 39));

        if (windows_logo) {
            const int cx =
                (face.left + face.right) / 2;
            const int cy =
                (face.top + face.bottom) / 2;
            constexpr int pane_w = 11;
            constexpr int pane_h = 13;
            constexpr int gap = 3;

            HGDIOBJ old_logo_brush =
                SelectObject(
                    keyboard_dc_,
                    GetStockObject(DC_BRUSH));
            HGDIOBJ old_logo_pen =
                SelectObject(
                    keyboard_dc_,
                    GetStockObject(NULL_PEN));
            SetDCBrushColor(
                keyboard_dc_,
                RGB(31, 34, 39));

            const int left =
                cx - pane_w - gap / 2;
            const int right =
                cx + gap / 2;
            const int top =
                cy - pane_h - gap / 2;
            const int bottom =
                cy + gap / 2;

            Rectangle(
                keyboard_dc_,
                left,
                top,
                left + pane_w,
                top + pane_h);
            Rectangle(
                keyboard_dc_,
                right,
                top - 1,
                right + pane_w + 1,
                top + pane_h);
            Rectangle(
                keyboard_dc_,
                left,
                bottom,
                left + pane_w,
                bottom + pane_h);
            Rectangle(
                keyboard_dc_,
                right,
                bottom,
                right + pane_w + 1,
                bottom + pane_h + 1);

            SelectObject(
                keyboard_dc_,
                old_logo_pen);
            SelectObject(
                keyboard_dc_,
                old_logo_brush);
        } else {
            RECT text_rect = face;
            text_rect.top -= 1;

            DrawTextW(
                keyboard_dc_,
                keycap.label.data(),
                -1,
                &text_rect,
                DT_CENTER |
                    DT_VCENTER |
                    DT_SINGLELINE |
                    DT_NOPREFIX);
        }

        x = face.right + kGap;
    }

    SelectObject(
        keyboard_dc_,
        old_pen);
    SelectObject(
        keyboard_dc_,
        old_brush);
    SelectObject(
        keyboard_dc_,
        old_font);

    keyboard_content_width_ =
        static_cast<std::uint32_t>(
            std::clamp(
                x - kGap + kPadding,
                1,
                static_cast<int>(kKeyboardWidth)));
    keyboard_content_height_ =
        static_cast<std::uint32_t>(
            std::min(
                kTop + kFaceHeight + kDepth + 8,
                static_cast<int>(kKeyboardHeight)));

    // GDI draws RGB but does not preserve alpha in the 32-bit DIB. Promote
    // only painted pixels. The shader applies the temporal overlay opacity.
    for (std::size_t i = 0;
         i <
         static_cast<std::size_t>(kKeyboardWidth) *
             static_cast<std::size_t>(kKeyboardHeight);
         ++i) {
        if ((pixels[i] & 0x00FFFFFFu) != 0)
            pixels[i] |= 0xFF000000u;
    }
}

Status D3D11Compositor::update_keyboard_overlay(
    ID3D11DeviceContext *context,
    const arssyut::presentation::KeyboardOverlayFrame &keyboard) noexcept
{
    if (!context ||
        !keyboard_texture_)
        return Status::failure(
            StatusCode::InvalidArgument);

    if (keyboard.generation == 0 ||
        keyboard.generation == keyboard_generation_) {
        return Status::success();
    }

    rasterize_keyboard_keycaps(
        keyboard);

    context->UpdateSubresource(
        keyboard_texture_.Get(),
        0,
        nullptr,
        keyboard_bits_,
        kKeyboardWidth * 4,
        0);

    keyboard_generation_ =
        keyboard.generation;

    return Status::success();
}

void D3D11Compositor::resolve_gpu_queries(
    ID3D11DeviceContext *context) noexcept
{
    if (!context)
        return;

    for (auto &slot : gpu_queries_) {
        if (!slot.pending)
            continue;

        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint{};
        UINT64 start = 0;
        UINT64 end = 0;

        const HRESULT disjoint_hr = context->GetData(
            slot.disjoint.Get(),
            &disjoint,
            sizeof(disjoint),
            D3D11_ASYNC_GETDATA_DONOTFLUSH);
        if (disjoint_hr != S_OK)
            continue;

        const HRESULT start_hr = context->GetData(
            slot.start.Get(),
            &start,
            sizeof(start),
            D3D11_ASYNC_GETDATA_DONOTFLUSH);
        if (start_hr != S_OK)
            continue;

        const HRESULT end_hr = context->GetData(
            slot.end.Get(),
            &end,
            sizeof(end),
            D3D11_ASYNC_GETDATA_DONOTFLUSH);
        if (end_hr != S_OK)
            continue;

        if (!disjoint.Disjoint &&
            disjoint.Frequency > 0 &&
            end >= start) {
            const std::uint64_t elapsed =
                end - start;
            const std::uint64_t microseconds =
                (elapsed * 1'000'000ULL) /
                disjoint.Frequency;

            gpu_latency_.observe(
                static_cast<std::uint32_t>(
                    std::min<std::uint64_t>(
                        microseconds,
                        std::numeric_limits<std::uint32_t>::max())));
        }

        slot.pending = false;
    }
}

std::size_t D3D11Compositor::begin_gpu_query(
    ID3D11DeviceContext *context) noexcept
{
    if (!context)
        return invalid_query_slot;

    for (std::size_t i = 0; i < gpu_queries_.size(); ++i) {
        auto &slot = gpu_queries_[i];
        if (slot.pending)
            continue;

        context->Begin(slot.disjoint.Get());
        context->End(slot.start.Get());
        return i;
    }

    return invalid_query_slot;
}

void D3D11Compositor::end_gpu_query(
    ID3D11DeviceContext *context,
    std::size_t index) noexcept
{
    if (!context ||
        index == invalid_query_slot ||
        index >= gpu_queries_.size()) {
        return;
    }

    auto &slot = gpu_queries_[index];
    context->End(slot.end.Get());
    context->End(slot.disjoint.Get());
    slot.pending = true;
}

Status D3D11Compositor::ensure_input(
    ID3D11Texture2D *source) noexcept
{
    if (!source)
        return Status::failure(StatusCode::InvalidArgument);

    D3D11_TEXTURE2D_DESC desc{};
    source->GetDesc(&desc);

    if (desc.Width == 0 || desc.Height == 0)
        return Status::failure(StatusCode::InvalidArgument);

    const bool same =
        input_copy_ &&
        desc.Width == input_desc_.Width &&
        desc.Height == input_desc_.Height &&
        desc.Format == input_desc_.Format &&
        desc.SampleDesc.Count == input_desc_.SampleDesc.Count;

    if (same)
        return Status::success();

    input_srv_.Reset();
    input_copy_.Reset();

    D3D11_TEXTURE2D_DESC copy_desc = desc;
    copy_desc.Usage = D3D11_USAGE_DEFAULT;
    copy_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    copy_desc.CPUAccessFlags = 0;
    copy_desc.MiscFlags = 0;
    copy_desc.MipLevels = 1;
    copy_desc.ArraySize = 1;

    HRESULT hr = device_->CreateTexture2D(
        &copy_desc,
        nullptr,
        input_copy_.GetAddressOf());
    if (FAILED(hr))
        return d3d_failure(hr);

    D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc{};
    srv_desc.Format = copy_desc.Format;
    srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Texture2D.MostDetailedMip = 0;
    srv_desc.Texture2D.MipLevels = 1;

    hr = device_->CreateShaderResourceView(
        input_copy_.Get(),
        &srv_desc,
        input_srv_.GetAddressOf());
    if (FAILED(hr)) {
        input_copy_.Reset();
        return d3d_failure(hr);
    }

    input_desc_ = desc;
    ++resource_generation_;
    return Status::success();
}

Status D3D11Compositor::ensure_output(FrameSize output_size) noexcept
{
    if (!output_size.valid())
        return Status::failure(StatusCode::InvalidArgument);

    if (output_texture_ && output_size == output_size_)
        return Status::success();

    output_rtv_.Reset();
    output_texture_.Reset();

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = output_size.width;
    desc.Height = output_size.height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags =
        D3D11_BIND_RENDER_TARGET |
        D3D11_BIND_SHADER_RESOURCE;

    HRESULT hr = device_->CreateTexture2D(
        &desc,
        nullptr,
        output_texture_.GetAddressOf());
    if (FAILED(hr))
        return d3d_failure(hr);

    hr = device_->CreateRenderTargetView(
        output_texture_.Get(),
        nullptr,
        output_rtv_.GetAddressOf());
    if (FAILED(hr)) {
        output_texture_.Reset();
        return d3d_failure(hr);
    }

    output_size_ = output_size;
    ++resource_generation_;
    return Status::success();
}

Status D3D11Compositor::update_cursor_shape(
    ID3D11DeviceContext *context,
    HCURSOR cursor) noexcept
{
    if (!context || !cursor_cache_) {
        return Status::failure(
            StatusCode::InvalidArgument);
    }

    return cursor_cache_->select(
        context,
        cursor);
}

Status D3D11Compositor::update_source(
    ID3D11DeviceContext *context,
    ID3D11Texture2D *source) noexcept
{
    if (!context || !source)
        return Status::failure(StatusCode::InvalidArgument);

    const Status input_status = ensure_input(source);
    if (!input_status.ok())
        return input_status;

    context->CopyResource(input_copy_.Get(), source);
    return Status::success();
}

Status D3D11Compositor::render_retained(
    ID3D11DeviceContext *context,
    CropRect crop,
    FrameSize output_size,
    const arssyut::presentation::PresentationFrameState *presentation) noexcept
{
    if (!context || !has_source())
        return Status::failure(StatusCode::InvalidArgument);

    const TimePoint started = MonotonicClock::now();

    resolve_gpu_queries(context);

    const Status output_status = ensure_output(output_size);
    if (!output_status.ok())
        return output_status;

    const FrameSize source_size{
        input_desc_.Width,
        input_desc_.Height
    };
    crop = arssyut::core::clamp_crop(crop, source_size);

    const std::size_t gpu_query =
        begin_gpu_query(context);

    arssyut::presentation::PresentationFrameState neutral{};
    const auto &state =
        presentation ? *presentation : neutral;

    const Status keyboard_status =
        update_keyboard_overlay(
            context,
            state.keyboard);
    if (!keyboard_status.ok())
        return keyboard_status;

    PresentationConstants constants{};
    constants.uv_left =
        static_cast<float>(crop.left) /
        static_cast<float>(source_size.width);
    constants.uv_top =
        static_cast<float>(crop.top) /
        static_cast<float>(source_size.height);
    constants.uv_right =
        static_cast<float>(crop.right) /
        static_cast<float>(source_size.width);
    constants.uv_bottom =
        static_cast<float>(crop.bottom) /
        static_cast<float>(source_size.height);

    constants.camera_center_x =
        std::clamp(
            state.camera_center_x,
            0.0f,
            1.0f);
    constants.camera_center_y =
        std::clamp(
            state.camera_center_y,
            0.0f,
            1.0f);
    constants.camera_zoom =
        std::clamp(
            state.camera_zoom,
            1.0f,
            4.0f);
    constants.keyboard_opacity =
        std::clamp(
            state.keyboard.opacity,
            0.0f,
            1.0f);

    constants.output_width =
        static_cast<float>(output_size.width);
    constants.output_height =
        static_cast<float>(output_size.height);

    constants.keyboard_uv_scale_x =
        static_cast<float>(keyboard_content_width_) /
        static_cast<float>(kKeyboardWidth);
    constants.keyboard_uv_scale_y =
        static_cast<float>(keyboard_content_height_) /
        static_cast<float>(kKeyboardHeight);

    const float content_aspect =
        static_cast<float>(keyboard_content_width_) /
        std::max(
            static_cast<float>(keyboard_content_height_),
            1.0f);

    float display_height_px = std::clamp(
        constants.output_height * 0.090f,
        72.0f,
        132.0f);
    float display_width_px =
        display_height_px * content_aspect;

    const float max_width_px =
        constants.output_width * 0.78f;
    if (display_width_px > max_width_px &&
        display_width_px > 0.0f) {
        const float scale =
            max_width_px / display_width_px;
        display_width_px *= scale;
        display_height_px *= scale;
    }

    const float bottom_margin_px = std::clamp(
        constants.output_height * 0.035f,
        18.0f,
        54.0f);

    constants.keyboard_left =
        (constants.output_width - display_width_px) *
        0.5f /
        constants.output_width;
    constants.keyboard_right =
        (constants.output_width + display_width_px) *
        0.5f /
        constants.output_width;
    constants.keyboard_bottom =
        (constants.output_height - bottom_margin_px) /
        constants.output_height;
    constants.keyboard_top =
        (constants.output_height -
         bottom_margin_px -
         display_height_px) /
        constants.output_height;

    const auto cursor_shape =
        cursor_cache_
            ? cursor_cache_->active()
            : CursorShapeView{};

    constants.cursor_x =
        std::clamp(
            state.cursor.content_x,
            0.0f,
            1.0f);
    constants.cursor_y =
        std::clamp(
            state.cursor.content_y,
            0.0f,
            1.0f);
    constants.cursor_scale =
        std::clamp(
            state.cursor.scale,
            0.80f,
            1.30f);
    constants.cursor_opacity =
        cursor_shape.valid()
            ? std::clamp(
                  state.cursor.opacity,
                  0.0f,
                  1.0f)
            : 0.0f;
    constants.cursor_width =
        static_cast<float>(
            cursor_shape.width);
    constants.cursor_height =
        static_cast<float>(
            cursor_shape.height);
    constants.cursor_hotspot_x =
        static_cast<float>(
            cursor_shape.hotspot_x);
    constants.cursor_hotspot_y =
        static_cast<float>(
            cursor_shape.hotspot_y);

    for (std::size_t i = 0;
         i < state.clicks.size();
         ++i) {
        const auto &click =
            state.clicks[i];
        const std::size_t base = i * 4;

        constants.clicks[base + 0] =
            click.content_x;
        constants.clicks[base + 1] =
            click.content_y;
        const float lifetime =
            std::max(
                click.lifetime_seconds,
                0.0001f);
        constants.clicks[base + 2] =
            std::clamp(
                click.age_seconds / lifetime,
                0.0f,
                1.0f);
        constants.clicks[base + 3] =
            static_cast<float>(
                static_cast<std::uint8_t>(
                    click.kind));
    }

    context->UpdateSubresource(
        crop_constant_buffer_.Get(),
        0,
        nullptr,
        &constants,
        0,
        0);

    ID3D11RenderTargetView *rtv = output_rtv_.Get();
    context->OMSetRenderTargets(1, &rtv, nullptr);

    D3D11_VIEWPORT viewport{};
    viewport.Width = static_cast<float>(output_size.width);
    viewport.Height = static_cast<float>(output_size.height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    context->RSSetViewports(1, &viewport);

    context->IASetInputLayout(nullptr);
    context->IASetPrimitiveTopology(
        D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    context->VSSetShader(vertex_shader_.Get(), nullptr, 0);
    context->PSSetShader(pixel_shader_.Get(), nullptr, 0);

    ID3D11Buffer *constant_buffer = crop_constant_buffer_.Get();
    context->PSSetConstantBuffers(0, 1, &constant_buffer);

    ID3D11SamplerState *sampler = sampler_.Get();
    context->PSSetSamplers(0, 1, &sampler);

    ID3D11ShaderResourceView *srvs[3] = {
        input_srv_.Get(),
        keyboard_srv_.Get(),
        cursor_shape.valid()
            ? cursor_shape.srv
            : nullptr
    };
    context->PSSetShaderResources(0, 3, srvs);

    context->Draw(3, 0);

    ID3D11ShaderResourceView *null_srvs[3] = {
        nullptr,
        nullptr,
        nullptr
    };
    context->PSSetShaderResources(0, 3, null_srvs);

    ID3D11RenderTargetView *null_rtv = nullptr;
    context->OMSetRenderTargets(1, &null_rtv, nullptr);

    end_gpu_query(context, gpu_query);

    cpu_latency_.observe(
        elapsed_microseconds(started, MonotonicClock::now()));

    return Status::success();
}

Status D3D11Compositor::render(
    ID3D11DeviceContext *context,
    ID3D11Texture2D *source,
    CropRect crop,
    FrameSize output_size,
    const arssyut::presentation::PresentationFrameState *presentation) noexcept
{
    const Status update_status =
        update_source(context, source);
    if (!update_status.ok())
        return update_status;

    return render_retained(
        context,
        crop,
        output_size,
        presentation);
}

} // namespace arssyut::windows

#endif
