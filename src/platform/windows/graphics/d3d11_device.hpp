#pragma once

#ifdef _WIN32

#include "core/result/result.hpp"

#include <d3d11.h>
#include <wrl/client.h>

#include <cstdint>
#include <memory>

namespace arssyut::windows {

enum class D3D11DevicePreference : std::uint8_t {
    HardwareOnly = 0,
    WarpForTesting,
};

class D3D11Device final {
public:
    D3D11Device(const D3D11Device &) = delete;
    D3D11Device &operator=(const D3D11Device &) = delete;
    D3D11Device(D3D11Device &&) = delete;
    D3D11Device &operator=(D3D11Device &&) = delete;

    [[nodiscard]] static arssyut::core::Result<std::unique_ptr<D3D11Device>>
    create(
        D3D11DevicePreference preference =
            D3D11DevicePreference::HardwareOnly,
        bool enable_debug_layer = false) noexcept;

    [[nodiscard]] ID3D11Device *device() const noexcept
    {
        return device_.Get();
    }

    [[nodiscard]] ID3D11DeviceContext *immediate_context() const noexcept
    {
        return immediate_context_.Get();
    }

    [[nodiscard]] D3D_FEATURE_LEVEL feature_level() const noexcept
    {
        return feature_level_;
    }

    [[nodiscard]] bool valid() const noexcept
    {
        return device_ != nullptr && immediate_context_ != nullptr;
    }

private:
    D3D11Device() = default;

    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> immediate_context_;
    D3D_FEATURE_LEVEL feature_level_ = D3D_FEATURE_LEVEL_10_0;
};

} // namespace arssyut::windows

#endif
