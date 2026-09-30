#pragma once

#ifdef _WIN32

#include "core/diagnostics/latency_histogram.hpp"
#include "core/result/result.hpp"
#include "core/result/status.hpp"
#include "core/video/frame_geometry.hpp"

#include <d3d11.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>
#include <memory>

namespace arssyut::windows {

class D3D11Compositor final {
public:
    D3D11Compositor(const D3D11Compositor &) = delete;
    D3D11Compositor &operator=(const D3D11Compositor &) = delete;

    [[nodiscard]]
    static arssyut::core::Result<std::unique_ptr<D3D11Compositor>>
    create(ID3D11Device *device) noexcept;

    [[nodiscard]] arssyut::core::Status render(
        ID3D11DeviceContext *context,
        ID3D11Texture2D *source,
        arssyut::core::CropRect crop,
        arssyut::core::FrameSize output_size) noexcept;

    [[nodiscard]] ID3D11Texture2D *output_texture() const noexcept
    {
        return output_texture_.Get();
    }

    [[nodiscard]] arssyut::core::FrameSize output_size() const noexcept
    {
        return output_size_;
    }

    [[nodiscard]] std::uint64_t resource_generation() const noexcept
    {
        return resource_generation_;
    }

    [[nodiscard]] arssyut::core::LatencyHistogram::Snapshot
    cpu_submit_latency() const noexcept
    {
        return cpu_latency_.snapshot();
    }

    [[nodiscard]] arssyut::core::LatencyHistogram::Snapshot
    gpu_execution_latency() const noexcept
    {
        return gpu_latency_.snapshot();
    }

private:
    D3D11Compositor() = default;

    [[nodiscard]] arssyut::core::Status initialize(
        ID3D11Device *device) noexcept;

    [[nodiscard]] arssyut::core::Status ensure_input(
        ID3D11Texture2D *source) noexcept;

    [[nodiscard]] arssyut::core::Status ensure_output(
        arssyut::core::FrameSize output_size) noexcept;

    void resolve_gpu_queries(ID3D11DeviceContext *context) noexcept;
    [[nodiscard]] std::size_t begin_gpu_query(
        ID3D11DeviceContext *context) noexcept;
    void end_gpu_query(
        ID3D11DeviceContext *context,
        std::size_t index) noexcept;

    struct GpuQuerySlot {
        Microsoft::WRL::ComPtr<ID3D11Query> disjoint;
        Microsoft::WRL::ComPtr<ID3D11Query> start;
        Microsoft::WRL::ComPtr<ID3D11Query> end;
        bool pending = false;
    };

    static constexpr std::size_t gpu_query_slots = 4;
    static constexpr std::size_t invalid_query_slot =
        static_cast<std::size_t>(-1);

    Microsoft::WRL::ComPtr<ID3D11Device> device_;

    Microsoft::WRL::ComPtr<ID3D11VertexShader> vertex_shader_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> pixel_shader_;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> crop_constant_buffer_;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> input_copy_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> input_srv_;
    D3D11_TEXTURE2D_DESC input_desc_{};

    Microsoft::WRL::ComPtr<ID3D11Texture2D> output_texture_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> output_rtv_;
    arssyut::core::FrameSize output_size_{};

    std::uint64_t resource_generation_ = 0;
    std::array<GpuQuerySlot, gpu_query_slots> gpu_queries_{};
    arssyut::core::LatencyHistogram cpu_latency_;
    arssyut::core::LatencyHistogram gpu_latency_;
};

} // namespace arssyut::windows

#endif
