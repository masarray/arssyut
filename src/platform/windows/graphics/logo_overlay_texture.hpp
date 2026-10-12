#pragma once
#ifdef _WIN32
#include "core/result/status.hpp"
#include <d3d11.h>
#include <wrl/client.h>
#include <cstdint>
#include <string>

namespace arssyut::windows {
// PNG/WIC decoding happens ONCE while the recorder is Preparing, never
// in the 60fps frame path. The shader owns a retained immutable GPU SRV.
class LogoOverlayTexture final {
public:
    [[nodiscard]] arssyut::core::Status load(
        ID3D11Device *device, const std::wstring &path) noexcept;
    void clear() noexcept {
        srv_.Reset(); width_ = 0; height_ = 0;
    }
    [[nodiscard]] bool ready() const noexcept { return srv_ != nullptr; }
    [[nodiscard]] ID3D11ShaderResourceView *srv() const noexcept {
        return srv_.Get();
    }
    [[nodiscard]] std::uint32_t width() const noexcept { return width_; }
    [[nodiscard]] std::uint32_t height() const noexcept { return height_; }
private:
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv_;
    std::uint32_t width_ = 0, height_ = 0;
};
} // namespace arssyut::windows
#endif
