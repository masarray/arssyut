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
cbuffer CropConstants : register(b0)
{
    float4 uv_rect;
};

Texture2D source_texture : register(t0);
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

float4 ps_main(VertexOutput input) : SV_Target
{
    float2 uv = lerp(uv_rect.xy, uv_rect.zw, input.uv);
    return source_texture.Sample(source_sampler, uv);
}
)";

struct CropConstants {
    float left;
    float top;
    float right;
    float bottom;
};

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
    constant_desc.ByteWidth = sizeof(CropConstants);
    constant_desc.Usage = D3D11_USAGE_DEFAULT;
    constant_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

    hr = device_->CreateBuffer(
        &constant_desc,
        nullptr,
        crop_constant_buffer_.GetAddressOf());
    if (FAILED(hr))
        return d3d_failure(hr);

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
