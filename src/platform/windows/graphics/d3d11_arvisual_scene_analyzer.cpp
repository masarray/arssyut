#include "platform/windows/graphics/d3d11_arvisual_scene_analyzer.hpp"

#ifdef _WIN32

#include <d3dcompiler.h>

#include <algorithm>
#include <new>
#include <utility>

namespace arssyut::windows {

namespace {

using arssyut::core::Result;
using arssyut::core::Status;
using arssyut::core::StatusCode;
using arssyut::core::TimePoint;

constexpr char analysis_shader[] = R"(
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
    float2 uv = float2(
        (vertex_id << 1) & 2,
        vertex_id & 2);
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
    return source_texture.Sample(
        source_sampler,
        input.uv);
}
)";

[[nodiscard]] std::uint32_t detail(
    HRESULT hr) noexcept
{
    return static_cast<std::uint32_t>(hr);
}

[[nodiscard]] Status d3d_failure(
    HRESULT hr) noexcept
{
    return Status::failure(
        StatusCode::PlatformFailure,
        detail(hr));
}

[[nodiscard]] Result<Microsoft::WRL::ComPtr<ID3DBlob>>
compile_analysis_shader(
    const char *entry,
    const char *target) noexcept
{
    Microsoft::WRL::ComPtr<ID3DBlob> bytecode;
    Microsoft::WRL::ComPtr<ID3DBlob> errors;

    const UINT flags =
#ifdef _DEBUG
        D3DCOMPILE_DEBUG |
        D3DCOMPILE_SKIP_OPTIMIZATION;
#else
        D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

    const HRESULT hr =
        D3DCompile(
            analysis_shader,
            sizeof(analysis_shader) - 1,
            "arssyut-arvisual-analysis",
            nullptr,
            nullptr,
            entry,
            target,
            flags,
            0,
            bytecode.GetAddressOf(),
            errors.GetAddressOf());

    if (FAILED(hr) || !bytecode) {
        return Result<
            Microsoft::WRL::ComPtr<ID3DBlob>>::failure(
                d3d_failure(hr));
    }

    return Result<
        Microsoft::WRL::ComPtr<ID3DBlob>>::success(
            std::move(bytecode));
}

} // namespace

Result<std::unique_ptr<D3D11ArVisualSceneAnalyzer>>
D3D11ArVisualSceneAnalyzer::create(
    ID3D11Device *device) noexcept
{
    if (!device) {
        return Result<
            std::unique_ptr<
                D3D11ArVisualSceneAnalyzer>>::failure(
            Status::failure(
                StatusCode::InvalidArgument));
    }

    std::unique_ptr<D3D11ArVisualSceneAnalyzer> analyzer(
        new (std::nothrow)
            D3D11ArVisualSceneAnalyzer{});
    if (!analyzer) {
        return Result<
            std::unique_ptr<
                D3D11ArVisualSceneAnalyzer>>::failure(
            Status::failure(
                StatusCode::InternalError));
    }

    const Status status =
        analyzer->initialize(device);
    if (!status.ok()) {
        return Result<
            std::unique_ptr<
                D3D11ArVisualSceneAnalyzer>>::failure(
            status);
    }

    return Result<
        std::unique_ptr<
            D3D11ArVisualSceneAnalyzer>>::success(
        std::move(analyzer));
}

Status D3D11ArVisualSceneAnalyzer::initialize(
    ID3D11Device *device) noexcept
{
    device_ = device;

    auto vs =
        compile_analysis_shader(
            "vs_main",
            "vs_5_0");
    if (!vs)
        return vs.status();

    auto ps =
        compile_analysis_shader(
            "ps_main",
            "ps_5_0");
    if (!ps)
        return ps.status();

    HRESULT hr =
        device_->CreateVertexShader(
            vs.value()->GetBufferPointer(),
            vs.value()->GetBufferSize(),
            nullptr,
            vertex_shader_.GetAddressOf());
    if (FAILED(hr))
        return d3d_failure(hr);

    hr =
        device_->CreatePixelShader(
            ps.value()->GetBufferPointer(),
            ps.value()->GetBufferSize(),
            nullptr,
            pixel_shader_.GetAddressOf());
    if (FAILED(hr))
        return d3d_failure(hr);

    D3D11_SAMPLER_DESC sampler_desc{};
    sampler_desc.Filter =
        D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler_desc.AddressU =
        D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.AddressV =
        D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.AddressW =
        D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.MaxLOD =
        D3D11_FLOAT32_MAX;

    hr =
        device_->CreateSamplerState(
            &sampler_desc,
            sampler_.GetAddressOf());
    if (FAILED(hr))
        return d3d_failure(hr);

    D3D11_TEXTURE2D_DESC analysis_desc{};
    analysis_desc.Width = analysis_width;
    analysis_desc.Height = analysis_height;
    analysis_desc.MipLevels = 1;
    analysis_desc.ArraySize = 1;
    analysis_desc.Format =
        DXGI_FORMAT_B8G8R8A8_UNORM;
    analysis_desc.SampleDesc.Count = 1;
    analysis_desc.Usage =
        D3D11_USAGE_DEFAULT;
    analysis_desc.BindFlags =
        D3D11_BIND_RENDER_TARGET |
        D3D11_BIND_SHADER_RESOURCE;

    hr =
        device_->CreateTexture2D(
            &analysis_desc,
            nullptr,
            analysis_texture_.GetAddressOf());
    if (FAILED(hr))
        return d3d_failure(hr);

    hr =
        device_->CreateRenderTargetView(
            analysis_texture_.Get(),
            nullptr,
            analysis_rtv_.GetAddressOf());
    if (FAILED(hr))
        return d3d_failure(hr);

    D3D11_TEXTURE2D_DESC staging_desc =
        analysis_desc;
    staging_desc.Usage =
        D3D11_USAGE_STAGING;
    staging_desc.BindFlags = 0;
    staging_desc.CPUAccessFlags =
        D3D11_CPU_ACCESS_READ;

    D3D11_QUERY_DESC query_desc{};
    query_desc.Query =
        D3D11_QUERY_EVENT;

    for (auto &slot : slots_) {
        hr =
            device_->CreateTexture2D(
                &staging_desc,
                nullptr,
                slot.texture.GetAddressOf());
        if (FAILED(hr))
            return d3d_failure(hr);

        hr =
            device_->CreateQuery(
                &query_desc,
                slot.ready.GetAddressOf());
        if (FAILED(hr))
            return d3d_failure(hr);
    }

    return Status::success();
}

Status D3D11ArVisualSceneAnalyzer::submit_if_due(
    ID3D11DeviceContext *context,
    ID3D11ShaderResourceView *source,
    TimePoint now) noexcept
{
    if (!context || !source) {
        return Status::failure(
            StatusCode::InvalidArgument);
    }

    if (next_submit_.ticks_100ns != 0 &&
        now < next_submit_) {
        return Status::success();
    }

    std::size_t selected = staging_slots;
    for (std::size_t offset = 0;
         offset < staging_slots;
         ++offset) {
        const std::size_t candidate =
            (next_slot_ + offset) %
            staging_slots;
        if (!slots_[candidate].pending) {
            selected = candidate;
            break;
        }
    }

    // The GPU is still working through both read-later slots.
    // Skip the analysis sample rather than flushing or waiting.
    if (selected >= staging_slots) {
        ++busy_skips_;
        next_submit_ = {
            now.ticks_100ns +
            arssyut::core::MonotonicClock::
                ticks_per_second / 5
        };
        return Status::success();
    }

    ID3D11RenderTargetView *rtv =
        analysis_rtv_.Get();
    context->OMSetRenderTargets(
        1,
        &rtv,
        nullptr);

    D3D11_VIEWPORT viewport{};
    viewport.Width =
        static_cast<float>(analysis_width);
    viewport.Height =
        static_cast<float>(analysis_height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    context->RSSetViewports(
        1,
        &viewport);

    context->IASetInputLayout(nullptr);
    context->IASetPrimitiveTopology(
        D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    context->VSSetShader(
        vertex_shader_.Get(),
        nullptr,
        0);
    context->PSSetShader(
        pixel_shader_.Get(),
        nullptr,
        0);

    ID3D11SamplerState *sampler =
        sampler_.Get();
    context->PSSetSamplers(
        0,
        1,
        &sampler);

    context->PSSetShaderResources(
        0,
        1,
        &source);

    context->Draw(3, 0);

    ID3D11ShaderResourceView *null_srv =
        nullptr;
    context->PSSetShaderResources(
        0,
        1,
        &null_srv);

    ID3D11RenderTargetView *null_rtv =
        nullptr;
    context->OMSetRenderTargets(
        1,
        &null_rtv,
        nullptr);

    auto &slot =
        slots_[selected];

    context->CopyResource(
        slot.texture.Get(),
        analysis_texture_.Get());
    context->End(
        slot.ready.Get());

    slot.pending = true;
    slot.submitted_at = now;
    slot.sequence = ++sequence_;

    ++submitted_;
    next_slot_ =
        (selected + 1) %
        staging_slots;
    next_submit_ = {
        now.ticks_100ns +
        arssyut::core::MonotonicClock::
            ticks_per_second / 5
    };

    return Status::success();
}

void D3D11ArVisualSceneAnalyzer::poll_nonblocking(
    ID3D11DeviceContext *context) noexcept
{
    if (!context)
        return;

    for (std::size_t pass = 0;
         pass < staging_slots;
         ++pass) {
        std::size_t selected = staging_slots;
        std::uint64_t oldest =
            static_cast<std::uint64_t>(-1);

        for (std::size_t i = 0;
             i < staging_slots;
             ++i) {
            if (slots_[i].pending &&
                slots_[i].sequence < oldest) {
                oldest =
                    slots_[i].sequence;
                selected = i;
            }
        }

        if (selected >= staging_slots)
            return;

        auto &slot =
            slots_[selected];

        BOOL ready = FALSE;
        const HRESULT ready_hr =
            context->GetData(
                slot.ready.Get(),
                &ready,
                sizeof(ready),
                D3D11_ASYNC_GETDATA_DONOTFLUSH);

        if (ready_hr == S_FALSE ||
            (SUCCEEDED(ready_hr) && !ready)) {
            // Oldest slot is not ready. Do not inspect or wait
            // for newer work; return to the 60-fps render path.
            return;
        }

        if (FAILED(ready_hr)) {
            slot.pending = false;
            ++map_failures_;
            continue;
        }

        D3D11_MAPPED_SUBRESOURCE mapped{};
        const HRESULT map_hr =
            context->Map(
                slot.texture.Get(),
                0,
                D3D11_MAP_READ,
                0,
                &mapped);

        if (FAILED(map_hr) || !mapped.pData) {
            slot.pending = false;
            ++map_failures_;
            continue;
        }

        float dt = 0.20f;
        if (last_observation_.ticks_100ns != 0) {
            const auto ticks =
                std::max<std::int64_t>(
                    1,
                    arssyut::core::MonotonicClock::
                        duration_ticks(
                            last_observation_,
                            slot.submitted_at));
            dt =
                std::clamp(
                    static_cast<float>(ticks) /
                        static_cast<float>(
                            arssyut::core::MonotonicClock::
                                ticks_per_second),
                    0.001f,
                    1.0f);
        }

        const bool observed =
            model_.observe_bgra8(
                static_cast<
                    const std::uint8_t *>(
                        mapped.pData),
                mapped.RowPitch,
                analysis_width,
                analysis_height,
                dt);

        context->Unmap(
            slot.texture.Get(),
            0);

        slot.pending = false;

        if (observed) {
            last_observation_ =
                slot.submitted_at;
            ++completed_;
        }
    }
}

bool D3D11ArVisualSceneAnalyzer::apply_latest(
    arssyut::visual::ArVisualGradeSettings &grade) const noexcept
{
    if (!model_.stats().primed)
        return false;

    arssyut::visual::apply_adaptive(
        grade,
        model_.adaptive());
    return true;
}

} // namespace arssyut::windows

#endif
