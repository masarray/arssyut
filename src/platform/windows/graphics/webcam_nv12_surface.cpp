#include "platform/windows/graphics/webcam_nv12_surface.hpp"
#ifdef _WIN32
#include <dxgi.h>
#include <utility>
#include <wrl/client.h>
namespace arssyut::windows {
using arssyut::core::Status;
using arssyut::core::StatusCode;
void WebcamNv12Surface::clear() noexcept {
    y_.Reset(); uv_.Reset(); retained_.Reset();
    width_ = 0; height_ = 0;
}
Status WebcamNv12Surface::update(
    ID3D11Device *device, ID3D11DeviceContext *context,
    ID3D11Texture2D *source, std::uint32_t subresource) noexcept
{
    if (!device || !context || !source)
        return Status::failure(StatusCode::InvalidArgument);
    D3D11_TEXTURE2D_DESC src{};
    source->GetDesc(&src);
    if (src.Format != DXGI_FORMAT_NV12 || src.Width < 320 ||
        src.Height < 240 || src.Width > 3840 || src.Height > 2160 ||
        src.Width % 2 || src.Height % 2 ||
        src.SampleDesc.Count != 1 || src.MipLevels != 1 ||
        subresource >= src.ArraySize)
        return Status::failure(StatusCode::Unsupported);

    Microsoft::WRL::ComPtr<ID3D11Device> owner;
    source->GetDevice(&owner);
    if (owner.Get() != device)
        return Status::failure(StatusCode::GraphicsDeviceUnavailable);

    if (!ready() || width_ != src.Width || height_ != src.Height) {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = src.Width; desc.Height = src.Height;
        desc.MipLevels = desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_NV12;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> target;
        HRESULT hr = device->CreateTexture2D(
            &desc, nullptr, target.GetAddressOf());
        if (FAILED(hr))
            return Status::failure(StatusCode::GraphicsDeviceUnavailable,
                static_cast<std::uint32_t>(hr));
        D3D11_SHADER_RESOURCE_VIEW_DESC view{};
        view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        view.Texture2D.MostDetailedMip = 0;
        view.Texture2D.MipLevels = 1;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> y, uv;
        view.Format = DXGI_FORMAT_R8_UNORM;
        hr = device->CreateShaderResourceView(
            target.Get(), &view, y.GetAddressOf());
        if (SUCCEEDED(hr)) {
            view.Format = DXGI_FORMAT_R8G8_UNORM;
            hr = device->CreateShaderResourceView(
                target.Get(), &view, uv.GetAddressOf());
        }
        if (FAILED(hr))
            return Status::failure(StatusCode::Unsupported,
                static_cast<std::uint32_t>(hr));
        // Publish the cache as one transaction, never half-initialized.
        retained_ = std::move(target);
        y_ = std::move(y);
        uv_ = std::move(uv);
        width_ = src.Width; height_ = src.Height;
    }

    // A single GPU copy only for each *new* webcam frame. The desktop/video
    // cadence can exceed webcam cadence without extra conversion passes.
    context->CopySubresourceRegion(
        retained_.Get(), 0, 0, 0, 0, source, subresource, nullptr);
    return Status::success();
}
}
#endif
