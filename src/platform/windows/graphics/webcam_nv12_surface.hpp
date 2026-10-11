#pragma once
#ifdef _WIN32
#include "core/result/status.hpp"
#include <d3d11.h>
#include <wrl/client.h>
#include <cstdint>

namespace arssyut::windows {
// GPU-only NV12 cache for ONE camera input, used exclusively by the existing
// video/compositor worker. No context operations from MF async callbacks.
// A new frame is copied once, output frames reuse the retained GPU texture.
class WebcamNv12Surface final {
public:
    [[nodiscard]] arssyut::core::Status update(
        ID3D11Device *device,
        ID3D11DeviceContext *context,
        ID3D11Texture2D *source,
        std::uint32_t subresource) noexcept;
    void clear() noexcept;
    [[nodiscard]] bool ready() const noexcept {
        return y_ && uv_ && retained_;
    }
    [[nodiscard]] ID3D11ShaderResourceView *y() const noexcept { return y_.Get(); }
    [[nodiscard]] ID3D11ShaderResourceView *uv() const noexcept { return uv_.Get(); }
    [[nodiscard]] std::uint32_t width() const noexcept { return width_; }
    [[nodiscard]] std::uint32_t height() const noexcept { return height_; }
private:
    Microsoft::WRL::ComPtr<ID3D11Texture2D> retained_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> y_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> uv_;
    std::uint32_t width_ = 0, height_ = 0;
};
}
#endif
