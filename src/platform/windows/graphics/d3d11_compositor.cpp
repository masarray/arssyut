#include "platform/windows/graphics/d3d11_compositor.hpp"

#ifdef _WIN32

#include "core/time/monotonic_clock.hpp"

#include <d3dcompiler.h>

#include <algorithm>
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
    float4 click0;
    float4 click1;
    float4 click2;
    float4 click3;
};

Texture2D source_texture : register(t0);
Texture2D keyboard_texture : register(t1);
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

float click_alpha(float2 uv, float4 click_value, float2 camera_center,
                  float zoom, float aspect)
{
    if (click_value.w <= 0.0f)
        return 0.0f;

    const float2 projected =
        0.5f + (click_value.xy - camera_center) * zoom;

    const float2 delta =
        float2((uv.x - projected.x) * aspect,
               uv.y - projected.y);

    const float distance = length(delta);
    const float progress = saturate(click_value.z);
    const float radius = lerp(0.010f, 0.040f, progress);
    const float thickness = lerp(0.0050f, 0.0025f, progress);

    const float ring =
        1.0f - smoothstep(
            thickness,
            thickness + 0.0020f,
            abs(distance - radius));

    const float center =
        (1.0f - smoothstep(
            0.0f,
            0.010f,
            distance)) * (1.0f - progress);

    return saturate((ring + 0.28f * center) * (1.0f - progress));
}

float3 click_color(float kind)
{
    if (kind < 1.5f)
        return float3(1.00f, 0.26f, 0.23f);
    if (kind < 2.5f)
        return float3(0.26f, 0.72f, 1.00f);
    return float3(0.95f, 0.72f, 0.22f);
}

float4 apply_click(float4 color, float2 uv, float4 click_value,
                   float2 camera_center, float zoom, float aspect)
{
    const float alpha =
        click_alpha(uv, click_value, camera_center, zoom, aspect);

    if (alpha <= 0.0001f)
        return color;

    const float3 tint = click_color(click_value.w);
    color.rgb = lerp(color.rgb, tint, saturate(alpha * 0.90f));
    return color;
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

    const float aspect = max(output_info.x, 0.1f);

    color = apply_click(
        color, input.uv, click0, camera_center, zoom, aspect);
    color = apply_click(
        color, input.uv, click1, camera_center, zoom, aspect);
    color = apply_click(
        color, input.uv, click2, camera_center, zoom, aspect);
    color = apply_click(
        color, input.uv, click3, camera_center, zoom, aspect);

    const float keyboard_opacity =
        saturate(camera_keyboard.w);

    if (keyboard_opacity > 0.001f &&
        input.uv.x >= keyboard_rect.x &&
        input.uv.x <= keyboard_rect.z &&
        input.uv.y >= keyboard_rect.y &&
        input.uv.y <= keyboard_rect.w) {
        const float2 keyboard_uv =
            (input.uv - keyboard_rect.xy) /
            max(keyboard_rect.zw - keyboard_rect.xy, 0.0001f);

        float4 overlay =
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

    float output_aspect;
    float reserved0;
    float reserved1;
    float reserved2;

    float keyboard_left;
    float keyboard_top;
    float keyboard_right;
    float keyboard_bottom;

    float clicks[16]{};
};

constexpr UINT kKeyboardWidth = 640;
constexpr UINT kKeyboardHeight = 112;

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

    return Status::success();
}

void D3D11Compositor::rasterize_keyboard_keycaps(
    const wchar_t *text) noexcept
{
    if (!keyboard_dc_ ||
        !keyboard_bits_ ||
        !text)
        return;

    auto *pixels =
        static_cast<std::uint32_t *>(keyboard_bits_);
    std::fill(
        pixels,
        pixels +
            static_cast<std::size_t>(kKeyboardWidth) *
                static_cast<std::size_t>(kKeyboardHeight),
        0u);

    RECT client{
        0,
        0,
        static_cast<LONG>(kKeyboardWidth),
        static_cast<LONG>(kKeyboardHeight)
    };

    HBRUSH background =
        CreateSolidBrush(RGB(22, 25, 30));
    HPEN border =
        CreatePen(PS_SOLID, 2, RGB(78, 84, 94));

    HGDIOBJ old_brush =
        SelectObject(keyboard_dc_, background);
    HGDIOBJ old_pen =
        SelectObject(keyboard_dc_, border);

    RoundRect(
        keyboard_dc_,
        1,
        1,
        client.right - 1,
        client.bottom - 1,
        24,
        24);

    SelectObject(
        keyboard_dc_,
        keyboard_font_);
    SetBkMode(
        keyboard_dc_,
        TRANSPARENT);
    SetTextColor(
        keyboard_dc_,
        RGB(244, 246, 249));

    std::array<wchar_t, 64> copy{};
    wcsncpy_s(
        copy.data(),
        copy.size(),
        text,
        _TRUNCATE);

    constexpr wchar_t separator[] = L"  +  ";
    wchar_t *context = nullptr;
    wchar_t *token = wcstok_s(
        copy.data(),
        separator,
        &context);

    struct Token {
        wchar_t *text = nullptr;
        int width = 0;
    };

    std::array<Token, 8> tokens{};
    std::size_t count = 0;
    int total_width = 0;

    while (token && count < tokens.size()) {
        SIZE extent{};
        GetTextExtentPoint32W(
            keyboard_dc_,
            token,
            static_cast<int>(wcslen(token)),
            &extent);

        const int width =
            std::clamp(
                extent.cx + 38,
                74,
                190);

        tokens[count++] = {token, width};
        total_width += width;

        token = wcstok_s(
            nullptr,
            separator,
            &context);
    }

    if (count > 1)
        total_width +=
            static_cast<int>(count - 1) * 14;

    int x =
        std::max(
            18,
            (static_cast<int>(kKeyboardWidth) -
             total_width) / 2);

    HBRUSH key_brush =
        CreateSolidBrush(RGB(50, 55, 64));
    HPEN key_pen =
        CreatePen(PS_SOLID, 1, RGB(93, 100, 112));

    SelectObject(keyboard_dc_, key_brush);
    SelectObject(keyboard_dc_, key_pen);

    for (std::size_t i = 0; i < count; ++i) {
        RECT key{
            x,
            24,
            x + tokens[i].width,
            88
        };

        RoundRect(
            keyboard_dc_,
            key.left,
            key.top,
            key.right,
            key.bottom,
            14,
            14);

        DrawTextW(
            keyboard_dc_,
            tokens[i].text,
            -1,
            &key,
            DT_CENTER |
                DT_VCENTER |
                DT_SINGLELINE |
                DT_NOPREFIX);

        x = key.right + 14;
    }

    SelectObject(keyboard_dc_, old_pen);
    SelectObject(keyboard_dc_, old_brush);

    DeleteObject(key_pen);
    DeleteObject(key_brush);
    DeleteObject(border);
    DeleteObject(background);

    // GDI does not preserve alpha in a 32-bit DIB. Promote every painted
    // pixel to an opaque source pixel; the shader applies temporal opacity.
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
        keyboard.text.data());

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

Status D3D11Compositor::render(
    ID3D11DeviceContext *context,
    ID3D11Texture2D *source,
    CropRect crop,
    FrameSize output_size) noexcept
{
    if (!context || !source)
        return Status::failure(StatusCode::InvalidArgument);

    const TimePoint started = MonotonicClock::now();

    resolve_gpu_queries(context);

    const Status input_status = ensure_input(source);
    if (!input_status.ok())
        return input_status;

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

    context->CopyResource(input_copy_.Get(), source);

    const CropConstants constants{
        static_cast<float>(crop.left) /
            static_cast<float>(source_size.width),
        static_cast<float>(crop.top) /
            static_cast<float>(source_size.height),
        static_cast<float>(crop.right) /
            static_cast<float>(source_size.width),
        static_cast<float>(crop.bottom) /
            static_cast<float>(source_size.height),
    };

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

    ID3D11ShaderResourceView *srv = input_srv_.Get();
    context->PSSetShaderResources(0, 1, &srv);

    context->Draw(3, 0);

    ID3D11ShaderResourceView *null_srv = nullptr;
    context->PSSetShaderResources(0, 1, &null_srv);

    ID3D11RenderTargetView *null_rtv = nullptr;
    context->OMSetRenderTargets(1, &null_rtv, nullptr);

    end_gpu_query(context, gpu_query);

    cpu_latency_.observe(
        elapsed_microseconds(started, MonotonicClock::now()));

    return Status::success();
}

} // namespace arssyut::windows

#endif
