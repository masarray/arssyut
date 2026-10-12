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
#include <new>
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

} // namespace

struct WebcamPreviewRenderer {
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    ComPtr<ID3D11SamplerState> sampler;
    ComPtr<ID3D11Texture2D> render;
    ComPtr<ID3D11RenderTargetView> rtv;
    std::array<ComPtr<ID3D11Texture2D>, 2> staging{};
    std::array<ComPtr<ID3D11Query>, 2> ready{};
    std::array<bool, 2> pending{false, false};
    std::uint32_t head = 0;
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
        D3D11_QUERY_DESC query{};
        query.Query = D3D11_QUERY_EVENT;
        for (std::size_t i = 0; i < staging.size(); ++i) {
            if (FAILED(device->CreateTexture2D(
                    &tex, nullptr, staging[i].GetAddressOf())) ||
                FAILED(device->CreateQuery(
                    &query, ready[i].GetAddressOf())))
                return false;
        }
        return true;
    }

    [[nodiscard]] bool draw(ID3D11Device *device,
                            ID3D11DeviceContext *ctx,
                            const WebcamGpuFrame &frame,
                            std::uint8_t *pixels,
                            bool &available) noexcept {
        available = false;
        // Poll a PREVIOUS tiny GPU readback without flushing or waiting.
        // In particular, the recorder's 60Hz GPU worker never calls a
        // blocking Map on a just-copied staging texture.
        auto &readback = staging[head];
        if (pending[head]) {
            const auto query_status = ctx->GetData(
                ready[head].Get(), nullptr, 0,
                D3D11_ASYNC_GETDATA_DONOTFLUSH);
            if (FAILED(query_status)) return false;
            if (query_status == S_OK) {
                D3D11_MAPPED_SUBRESOURCE mapped{};
                const HRESULT map_status = ctx->Map(
                    readback.Get(), 0, D3D11_MAP_READ,
                    D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
                if (map_status == S_OK) {
                    constexpr auto pitch = WebcamPreviewSession::width * 4U;
                    for (std::uint32_t row = 0;
                         row < WebcamPreviewSession::height; ++row) {
                        std::memcpy(pixels + row * pitch,
                            static_cast<const std::uint8_t*>(mapped.pData) +
                                row * mapped.RowPitch, pitch);
                    }
                    ctx->Unmap(readback.Get(), 0);
                    pending[head] = false;
                    available = true;
                    head = (head + 1U) % 2U;
                } else if (map_status != DXGI_ERROR_WAS_STILL_DRAWING)
                    return false;
            }
        }
        const auto write = (head + (pending[head] ? 1U : 0U)) % 2U;
        if (pending[write]) return true; // latest-wins; never GPU stall.
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
        ctx->CopyResource(staging[write].Get(), render.Get());
        ctx->End(ready[write].Get());
        // The query uses DONOTFLUSH: explicitly submit the tiny GPU
        // thumbnail batch on its sole owner, but never wait on the GPU.
        ctx->Flush();
        pending[write] = true;
        return true;
    }
};

bool WebcamPreviewSession::validate_gpu_shader() noexcept {
    ComPtr<ID3DBlob> vs, ps;
    return SUCCEEDED(D3DCompile(kPreviewShader, sizeof(kPreviewShader)-1,
        "P8W2_preview_CI", nullptr, nullptr, "vs_main", "vs_4_0",
        D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vs, nullptr)) &&
        SUCCEEDED(D3DCompile(kPreviewShader, sizeof(kPreviewShader)-1,
        "P8W2_preview_CI", nullptr, nullptr, "ps_main", "ps_4_0",
        D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &ps, nullptr));
}

WebcamPreviewSession::WebcamPreviewSession() = default;
WebcamPreviewSession::~WebcamPreviewSession() { stop(); }

bool WebcamPreviewSession::start(std::wstring symbolic_link) noexcept {
    if (symbolic_link.empty() || worker_.joinable()) return false;
    stopping_.store(false, std::memory_order_release);
    failure_stage_.store(0, std::memory_order_release);
    failure_detail_.store(0, std::memory_order_release);
    state_.store(1, std::memory_order_release);
    symbolic_link_ = symbolic_link;
    {
        std::lock_guard lock(handoff_mutex_);
        graphics_ready_ = false;
        recording_requested_ = false;
        recording_acknowledged_ = false;
    }
    try {
        worker_ = std::thread([this, id = std::move(symbolic_link)]() mutable {
            run(std::move(id));
        });
    } catch (...) {
        state_.store(3, std::memory_order_release);
        return false;
    }
    return true; // worker opening is not a first-frame success.
}

void WebcamPreviewSession::stop() noexcept {
    stopping_.store(true, std::memory_order_release);
    {
        std::lock_guard lock(handoff_mutex_);
        recording_requested_ = false;
    }
    handoff_cv_.notify_all();
    if (worker_.joinable()) worker_.join();
    camera_.stop(); // finished worker, then release camera and GPU ownership.
    {
        std::lock_guard lock(handoff_mutex_);
        graphics_ready_ = false;
        renderer_.reset();
        graphics_.reset();
        recording_acknowledged_ = false;
    }
    {
        std::lock_guard lock(pixels_mutex_);
        sequence_ = 0;
    }
    state_.store(0, std::memory_order_release);
}

bool WebcamPreviewSession::begin_recording() noexcept {
    std::unique_lock lock(handoff_mutex_);
    if (stopping_.load(std::memory_order_acquire) ||
        recording_requested_) return false;
    // Wait on an actual ready/failure condition, not a guessed driver sleep.
    if (!handoff_cv_.wait_for(lock, std::chrono::seconds(5), [this] {
        return graphics_ready_ ||
               stopping_.load(std::memory_order_acquire) ||
               state_.load(std::memory_order_acquire) == 3;
    }) || !graphics_ready_ || stopping_.load() ||
        state_.load(std::memory_order_acquire) == 3) return false;
    recording_requested_ = true;
    handoff_cv_.notify_all();
    const bool handed_over = handoff_cv_.wait_for(
        lock, std::chrono::seconds(3), [this] {
            return recording_acknowledged_ ||
                   stopping_.load(std::memory_order_acquire) ||
                   state_.load(std::memory_order_acquire) == 3;
        }) && recording_acknowledged_ && !stopping_.load();
    if (!handed_over) {
        recording_requested_ = false;
        handoff_cv_.notify_all();
    }
    return handed_over;
}

void WebcamPreviewSession::end_recording() noexcept {
    {
        std::lock_guard lock(handoff_mutex_);
        recording_requested_ = false;
    }
    handoff_cv_.notify_all();
}

std::shared_ptr<D3D11Device>
WebcamPreviewSession::recording_device() const noexcept {
    std::lock_guard lock(handoff_mutex_);
    return recording_acknowledged_ ? graphics_ : nullptr;
}

void WebcamPreviewSession::publish_pixels(
    const std::array<std::uint8_t, bytes> &pixels) noexcept {
    std::lock_guard lock(pixels_mutex_);
    frame_ = pixels;
    ++sequence_;
    state_.store(2, std::memory_order_release);
}

void WebcamPreviewSession::publish_from_recorder(
    ID3D11DeviceContext *context, const WebcamGpuFrame &frame) noexcept {
    if (!context || !renderer_ || !graphics_) return;
    const auto now = frame.captured_at.ticks_100ns;
    if (now < next_preview_ticks_) return;
    next_preview_ticks_ = now + 1'000'000; // <=10Hz, physical capture QPC.
    std::array<std::uint8_t, bytes> pixels{};
    bool ready = false;
    if (renderer_->draw(graphics_->device(), context,
                        frame, pixels.data(), ready)) {
        if (ready) publish_pixels(pixels);
    } else {
        failure_detail_.store(static_cast<std::uint32_t>(E_FAIL));
        failure_stage_.store(5, std::memory_order_release);
        // Optional thumbnail failure must NEVER make the MP4 fail.
    }
}

bool WebcamPreviewSession::copy_frame(
    std::uint8_t *out, std::uint32_t capacity,
    std::uint64_t &sequence, std::uint32_t &state) noexcept {
    state = state_.load(std::memory_order_acquire);
    {
        std::lock_guard lock(handoff_mutex_);
        if (recording_acknowledged_) state = 4;
    }
    sequence = 0;
    if (!out || capacity < bytes) return false;
    std::lock_guard lock(pixels_mutex_);
    if (sequence_ == 0) return true;
    std::memcpy(out, frame_.data(), bytes);
    sequence = sequence_;
    return true;
}

void WebcamPreviewSession::run(std::wstring symbolic_link) noexcept {
    auto graphics_result = D3D11Device::create(
        D3D11DevicePreference::HardwareOnly);
    if (!graphics_result) {
        failure_detail_.store(graphics_result.status().detail);
        failure_stage_.store(1, std::memory_order_release);
        state_.store(3, std::memory_order_release);
        handoff_cv_.notify_all();
        return;
    }
    graphics_ = std::move(graphics_result).value();
    renderer_.reset(new (std::nothrow) WebcamPreviewRenderer{});
    if (!renderer_ || !renderer_->init(graphics_->device())) {
        failure_detail_.store(static_cast<std::uint32_t>(E_FAIL));
        failure_stage_.store(2, std::memory_order_release);
        state_.store(3, std::memory_order_release);
        handoff_cv_.notify_all();
        return;
    }
    const auto started = camera_.start(graphics_->device(),
                                      std::move(symbolic_link), frames_);
    if (!started.ok()) {
        failure_detail_.store(started.detail);
        failure_stage_.store(3, std::memory_order_release);
        state_.store(3, std::memory_order_release);
        handoff_cv_.notify_all();
        return;
    }
    {
        std::lock_guard lock(handoff_mutex_);
        graphics_ready_ = true;
    }
    handoff_cv_.notify_all();
    std::array<std::uint8_t, bytes> pixels{};
    std::uint64_t previous_camera_sequence = 0;
    while (!stopping_.load(std::memory_order_acquire)) {
        {
            std::unique_lock lock(handoff_mutex_);
            if (recording_requested_) {
                // The old SPSC consumer has finished its GPU lease and
                // rendering before publishing acknowledgement. Recorder
                // becomes the sole consumer of mailbox and context.
                recording_acknowledged_ = true;
                next_preview_ticks_ = 0;
                handoff_cv_.notify_all();
                handoff_cv_.wait(lock, [this] {
                    return !recording_requested_ || stopping_.load();
                });
                recording_acknowledged_ = false;
                next_preview_ticks_ = 0;
                state_.store(1, std::memory_order_release);
                handoff_cv_.notify_all();
                continue;
            }
        }
        const auto camera_status = camera_.snapshot();
        if (camera_status.state == WebcamCaptureState::Unavailable) {
            failure_detail_.store(camera_status.last_hresult);
            failure_stage_.store(4, std::memory_order_release);
            state_.store(3, std::memory_order_release);
            handoff_cv_.notify_all();
            break;
        }
        const auto now = arssyut::core::MonotonicClock::now().ticks_100ns;
        if (now >= next_preview_ticks_) {
            auto lease = frames_.try_acquire_latest();
            if (lease && lease->sequence > previous_camera_sequence) {
                previous_camera_sequence = lease->sequence;
                bool ready = false;
                if (renderer_->draw(graphics_->device(),
                                    graphics_->immediate_context(),
                                    lease.get(), pixels.data(), ready)) {
                    if (ready) publish_pixels(pixels);
                } else {
                    failure_detail_.store(static_cast<std::uint32_t>(E_FAIL));
                    failure_stage_.store(5, std::memory_order_release);
                    state_.store(3, std::memory_order_release);
                    handoff_cv_.notify_all();
                    break;
                }
            }
            next_preview_ticks_ = now + 1'000'000;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(12));
    }
    camera_.stop();
    if (stopping_.load(std::memory_order_acquire))
        state_.store(0, std::memory_order_release);
    handoff_cv_.notify_all();
}

} // namespace arssyut::windows
#endif
