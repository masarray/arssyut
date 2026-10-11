#include "platform/windows/capture/webcam_preview_session.hpp"
#ifdef _WIN32

#include "platform/windows/capture/webcam_gpu_frame_slot.hpp"
#include "platform/windows/graphics/d3d11_device.hpp"
#include "platform/windows/graphics/webcam_nv12_surface.hpp"
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <utility>

namespace arssyut::windows {
namespace {
using Microsoft::WRL::ComPtr;

// Original small preview shader, not code copied from OBS. Full HD source
// stays on GPU; only 192x108 BGRA is read back for Avalonia Image.
constexpr char kPreviewShader[] = R"(
Texture2D camera_y : register(t0);
Texture2D camera_uv : register(t1);
SamplerState camera_sampler : register(s0);
struct VSOut { float4 position : SV_Position; float2 uv : TEXCOORD0; };
VSOut vs_main(uint id : SV_VertexID) {
    VSOut o;
    o.uv = float2((id << 1) & 2, id & 2);
    o.position = float4(o.uv.x * 2 - 1, 1 - o.uv.y * 2, 0, 1);
    return o;
}
float4 ps_main(VSOut input) : SV_Target {
    float y = camera_y.Sample(camera_sampler, input.uv).r;
    float2 uv = camera_uv.Sample(camera_sampler, input.uv).rg;
    float Y = max(0.0, (y - 16.0/255.0) * (255.0/219.0));
    float U = (uv.x - 128.0/255.0) * (255.0/224.0);
    float V = (uv.y - 128.0/255.0) * (255.0/224.0);
    return float4(saturate(float3(
        Y + 1.5748 * V,
        Y - 0.1873 * U - 0.4681 * V,
        Y + 1.8556 * U)), 1.0);
}
)";

struct PreviewRenderer {
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    ComPtr<ID3D11SamplerState> sampler;
    ComPtr<ID3D11Texture2D> render;
    ComPtr<ID3D11RenderTargetView> rtv;
    ComPtr<ID3D11Texture2D> staging;
    WebcamNv12Surface input;

    [[nodiscard]] bool init(ID3D11Device *device) noexcept {
        ComPtr<ID3DBlob> vs_code, ps_code;
        if (FAILED(D3DCompile(kPreviewShader, sizeof(kPreviewShader)-1,
             "P8W2_preview", nullptr, nullptr, "vs_main", "vs_4_0",
             D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vs_code, nullptr)) ||
            FAILED(D3DCompile(kPreviewShader, sizeof(kPreviewShader)-1,
             "P8W2_preview", nullptr, nullptr, "ps_main", "ps_4_0",
             D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &ps_code, nullptr)))
            return false;
        if (FAILED(device->CreateVertexShader(vs_code->GetBufferPointer(),
                  vs_code->GetBufferSize(), nullptr, &vs)) ||
            FAILED(device->CreatePixelShader(ps_code->GetBufferPointer(),
                  ps_code->GetBufferSize(), nullptr, &ps)))
            return false;

        D3D11_SAMPLER_DESC sampling{};
        sampling.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampling.AddressU = sampling.AddressV = sampling.AddressW =
            D3D11_TEXTURE_ADDRESS_CLAMP;
        sampling.MinLOD = 0;
        sampling.MaxLOD = D3D11_FLOAT32_MAX;
        if (FAILED(device->CreateSamplerState(&sampling, &sampler)))
            return false;

        D3D11_TEXTURE2D_DESC tex{};
        tex.Width = WebcamPreviewSession::width;
        tex.Height = WebcamPreviewSession::height;
        tex.MipLevels = tex.ArraySize = 1;
        tex.SampleDesc.Count = 1;
        tex.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        tex.BindFlags = D3D11_BIND_RENDER_TARGET;
        if (FAILED(device->CreateTexture2D(&tex, nullptr, &render)) ||
            FAILED(device->CreateRenderTargetView(render.Get(), nullptr, &rtv)))
            return false;
        tex.Usage = D3D11_USAGE_STAGING;
        tex.BindFlags = 0;
        tex.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        return SUCCEEDED(device->CreateTexture2D(&tex, nullptr, &staging));
    }

    [[nodiscard]] bool draw(ID3D11Device *device,
                            ID3D11DeviceContext *ctx,
                            const WebcamGpuFrame &frame,
                            std::uint8_t *pixels) noexcept {
        if (!input.update(device, ctx, frame.texture.Get(),
                          frame.subresource).ok())
            return false;
        auto *target = rtv.Get();
        ctx->OMSetRenderTargets(1, &target, nullptr);
        D3D11_VIEWPORT viewport{};
        viewport.Width = static_cast<float>(WebcamPreviewSession::width);
        viewport.Height = static_cast<float>(WebcamPreviewSession::height);
        viewport.MinDepth = 0;
        viewport.MaxDepth = 1;
        ctx->RSSetViewports(1, &viewport);
        ctx->IASetInputLayout(nullptr);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->VSSetShader(vs.Get(), nullptr, 0);
        ctx->PSSetShader(ps.Get(), nullptr, 0);
        auto *state = sampler.Get();
        ctx->PSSetSamplers(0, 1, &state);
        ID3D11ShaderResourceView *views[2] = {input.y(), input.uv()};
        ctx->PSSetShaderResources(0, 2, views);
        ctx->Draw(3, 0);
        ID3D11ShaderResourceView *null_views[2] = {nullptr,nullptr};
        ctx->PSSetShaderResources(0, 2, null_views);
        ID3D11RenderTargetView *null_target = nullptr;
        ctx->OMSetRenderTargets(1, &null_target, nullptr);
        ctx->CopyResource(staging.Get(), render.Get());

        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(ctx->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
            return false;
        constexpr auto pitch = WebcamPreviewSession::width * 4U;
        for (std::uint32_t row = 0;
             row < WebcamPreviewSession::height; ++row) {
            std::memcpy(pixels + row * pitch,
                static_cast<const std::uint8_t*>(mapped.pData) +
                    row * mapped.RowPitch, pitch);
        }
        ctx->Unmap(staging.Get(), 0);
        return true;
    }
};
}

bool WebcamPreviewSession::validate_gpu_shader() noexcept {
    ComPtr<ID3DBlob> vs, ps;
    return SUCCEEDED(D3DCompile(kPreviewShader, sizeof(kPreviewShader)-1,
        "P8W2_preview_CI", nullptr, nullptr, "vs_main", "vs_4_0",
        D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vs, nullptr)) &&
        SUCCEEDED(D3DCompile(kPreviewShader, sizeof(kPreviewShader)-1,
        "P8W2_preview_CI", nullptr, nullptr, "ps_main", "ps_4_0",
        D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &ps, nullptr));
}

bool WebcamPreviewSession::start(std::wstring symbolic_link) noexcept {
    if (symbolic_link.empty() || worker_.joinable())
        return false;
    stopping_.store(false, std::memory_order_release);
    state_.store(1, std::memory_order_release);
    try {
        worker_ = std::thread([this, id = std::move(symbolic_link)]() mutable {
            run(std::move(id));
        });
    } catch (...) {
        state_.store(3, std::memory_order_release);
        return false;
    }
    return true;
}

void WebcamPreviewSession::stop() noexcept {
    stopping_.store(true, std::memory_order_release);
    if (worker_.joinable())
        worker_.join();
    {
        std::lock_guard guard(mutex_);
        sequence_ = 0;
    }
    state_.store(0, std::memory_order_release);
}

bool WebcamPreviewSession::copy_frame(
    std::uint8_t *out, std::uint32_t capacity,
    std::uint64_t &sequence, std::uint32_t &state) noexcept {
    state = state_.load(std::memory_order_acquire);
    sequence = 0;
    if (!out || capacity < bytes)
        return false;
    std::lock_guard guard(mutex_);
    if (sequence_ == 0)
        return true; // Opening: no phantom frame, caller hides Image.
    std::memcpy(out, frame_.data(), bytes);
    sequence = sequence_;
    return true;
}

void WebcamPreviewSession::run(std::wstring symbolic_link) noexcept {
    auto graphics_result = D3D11Device::create(
        D3D11DevicePreference::HardwareOnly);
    if (!graphics_result) {
        state_.store(3, std::memory_order_release);
        return;
    }
    auto graphics = std::move(graphics_result).value();
    PreviewRenderer renderer;
    if (!renderer.init(graphics->device())) {
        state_.store(3, std::memory_order_release);
        return;
    }
    WebcamGpuFrameSlot frame_slot;
    WebcamCaptureSource camera;
    if (!camera.start(graphics->device(),
                      std::move(symbolic_link), frame_slot).ok()) {
        state_.store(3, std::memory_order_release);
        return;
    }

    std::array<std::uint8_t, bytes> prepared{};
    auto next_preview = std::chrono::steady_clock::now();
    std::uint64_t previous_camera_sequence = 0;
    while (!stopping_.load(std::memory_order_acquire)) {
        const auto camera_status = camera.snapshot();
        if (camera_status.state == WebcamCaptureState::Unavailable) {
            state_.store(3, std::memory_order_release);
            break;
        }
        const auto now = std::chrono::steady_clock::now();
        if (now >= next_preview) {
            auto lease = frame_slot.try_acquire_latest();
            if (lease && lease->sequence > previous_camera_sequence) {
                previous_camera_sequence = lease->sequence;
                if (renderer.draw(graphics->device(),
                                  graphics->immediate_context(),
                                  lease.get(), prepared.data())) {
                    // Tiny, fixed-size CPU publication outside GPU lock.
                    std::lock_guard guard(mutex_);
                    frame_ = prepared;
                    ++sequence_;
                    state_.store(2, std::memory_order_release);
                } else {
                    state_.store(3, std::memory_order_release);
                    break;
                }
            }
            next_preview = now + std::chrono::milliseconds(100);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(12));
    }
    // Do not shut down Media Foundation while the preview worker still owns
    // borrowed GPU frames. Source -> lease -> device order is deterministic.
    camera.stop();
    if (stopping_.load(std::memory_order_acquire))
        state_.store(0, std::memory_order_release);
}

} // namespace arssyut::windows
#endif
