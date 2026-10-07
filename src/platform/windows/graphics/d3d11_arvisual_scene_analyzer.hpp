#pragma once

#ifdef _WIN32

#include "core/result/result.hpp"
#include "core/result/status.hpp"
#include "core/time/monotonic_clock.hpp"
#include "visual/arvisual_grade.hpp"
#include "visual/arvisual_scene_analysis.hpp"

#include <d3d11.h>
#include <d3d11_3.h>
#include <wrl/client.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace arssyut::windows {

class D3D11ArVisualSceneAnalyzer final {
public:
    static constexpr std::uint32_t analysis_width = 64;
    static constexpr std::uint32_t analysis_height = 36;
    static constexpr std::size_t staging_slots = 2;

    D3D11ArVisualSceneAnalyzer(
        const D3D11ArVisualSceneAnalyzer &) = delete;
    D3D11ArVisualSceneAnalyzer &operator=(
        const D3D11ArVisualSceneAnalyzer &) = delete;

    [[nodiscard]]
    static arssyut::core::Result<
        std::unique_ptr<D3D11ArVisualSceneAnalyzer>>
    create(ID3D11Device *device) noexcept;

    [[nodiscard]] arssyut::core::Status submit_if_due(
        ID3D11DeviceContext *context,
        ID3D11ShaderResourceView *source,
        arssyut::core::TimePoint now,
        float uv_left,
        float uv_top,
        float uv_right,
        float uv_bottom) noexcept;

    void poll_nonblocking(
        ID3D11DeviceContext *context) noexcept;

    [[nodiscard]] bool apply_latest(
        arssyut::visual::ArVisualGradeSettings &grade) const noexcept;

    [[nodiscard]] std::uint64_t submitted() const noexcept
    {
        return submitted_;
    }

    [[nodiscard]] std::uint64_t completed() const noexcept
    {
        return completed_;
    }

    [[nodiscard]] std::uint64_t busy_skips() const noexcept
    {
        return busy_skips_;
    }

    [[nodiscard]] std::uint64_t map_failures() const noexcept
    {
        return map_failures_;
    }

    [[nodiscard]] bool primed() const noexcept
    {
        return model_.stats().primed;
    }

    [[nodiscard]] const arssyut::visual::ArVisualSceneStats &
    latest_stats() const noexcept
    {
        return model_.stats();
    }

    [[nodiscard]] const arssyut::visual::ArVisualAdaptiveState &
    latest_adaptive() const noexcept
    {
        return model_.adaptive();
    }

private:
    D3D11ArVisualSceneAnalyzer() = default;

    [[nodiscard]] arssyut::core::Status initialize(
        ID3D11Device *device) noexcept;

    struct StagingSlot {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        Microsoft::WRL::ComPtr<ID3D11Query> ready;
        arssyut::core::TimePoint submitted_at{};
        std::uint64_t sequence = 0;
        bool pending = false;
    };

    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext3> context3_;
    bool context3_checked_ = false;

    Microsoft::WRL::ComPtr<ID3D11VertexShader> vertex_shader_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> pixel_shader_;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> crop_constant_buffer_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> analysis_texture_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> analysis_rtv_;

    std::array<StagingSlot, staging_slots> slots_{};
    std::size_t next_slot_ = 0;
    arssyut::core::TimePoint next_submit_{};
    arssyut::core::TimePoint last_observation_{};
    std::uint64_t sequence_ = 0;

    arssyut::visual::ArVisualSceneModel model_;

    std::uint64_t submitted_ = 0;
    std::uint64_t completed_ = 0;
    std::uint64_t busy_skips_ = 0;
    std::uint64_t map_failures_ = 0;
};

} // namespace arssyut::windows

#endif
