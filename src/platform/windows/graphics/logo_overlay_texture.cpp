#include "platform/windows/graphics/logo_overlay_texture.hpp"
#ifdef _WIN32
#include <wincodec.h>
#include <combaseapi.h>
#include <Windows.h>
#include <algorithm>
#include <cstdint>
#include <cwctype>
#include <limits>
#include <vector>

namespace arssyut::windows {
using arssyut::core::Status;
using arssyut::core::StatusCode;
using Microsoft::WRL::ComPtr;

Status LogoOverlayTexture::load(
    ID3D11Device *device, const std::wstring &path) noexcept
{
    if (!device || path.empty() || path.size() > 511)
        return Status::failure(StatusCode::InvalidArgument);
    // Initial release accepts strictly PNG; GIF animation and WebP decoder
    // behavior must not silently enter the real-time output contract.
    if (path.size() < 4) return Status::failure(StatusCode::InvalidArgument);
    std::wstring suffix = path.substr(path.size()-4);
    std::transform(suffix.begin(), suffix.end(), suffix.begin(),
                   [](wchar_t c) { return std::towlower(c); });
    if (suffix != L".png")
        return Status::failure(StatusCode::Unsupported);
    WIN32_FILE_ATTRIBUTE_DATA file{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &file) ||
        (file.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        return Status::failure(StatusCode::StorageFailure, GetLastError());
    const auto file_bytes =
        (static_cast<std::uint64_t>(file.nFileSizeHigh) << 32) |
        file.nFileSizeLow;
    if (file_bytes == 0 || file_bytes > 16ULL * 1024 * 1024)
        return Status::failure(StatusCode::CapacityExceeded);

    const HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(init) && init != RPC_E_CHANGED_MODE)
        return Status::failure(StatusCode::PlatformFailure,
                               static_cast<std::uint32_t>(init));
    struct ComScope { bool uninit; ~ComScope() {
        if (uninit) CoUninitialize();
    } } com{SUCCEEDED(init)};
    try {
        ComPtr<IWICImagingFactory> factory;
        HRESULT hr = CoCreateInstance(
            CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&factory));
        if (FAILED(hr)) return Status::failure(StatusCode::PlatformFailure, hr);
        ComPtr<IWICBitmapDecoder> decoder;
        hr = factory->CreateDecoderFromFilename(path.c_str(), nullptr,
             GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder);
        if (FAILED(hr)) return Status::failure(StatusCode::PlatformFailure, hr);
        GUID format{};
        hr = decoder->GetContainerFormat(&format);
        if (FAILED(hr) || format != GUID_ContainerFormatPng)
            return Status::failure(StatusCode::Unsupported);
        ComPtr<IWICBitmapFrameDecode> frame;
        hr = decoder->GetFrame(0, &frame);
        if (FAILED(hr)) return Status::failure(StatusCode::PlatformFailure, hr);
        UINT width=0, height=0;
        hr = frame->GetSize(&width,&height);
        if (FAILED(hr)) return Status::failure(StatusCode::PlatformFailure, hr);
        const std::uint64_t pixels =
            static_cast<std::uint64_t>(width) * height;
        if (!width || !height || width > 4096 || height > 4096 ||
            pixels > 8'388'608ULL)
            return Status::failure(StatusCode::CapacityExceeded);
        ComPtr<IWICFormatConverter> converter;
        hr = factory->CreateFormatConverter(&converter);
        if (FAILED(hr)) return Status::failure(StatusCode::PlatformFailure, hr);
        hr = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA,
            WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
        if (FAILED(hr)) return Status::failure(StatusCode::PlatformFailure, hr);

        const auto stride = width * 4U;
        const auto byte_size = static_cast<UINT>(pixels * 4U);
        std::vector<std::uint8_t> bgra(byte_size);
        hr = converter->CopyPixels(nullptr, stride, byte_size, bgra.data());
        if (FAILED(hr)) return Status::failure(StatusCode::PlatformFailure, hr);
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width=width; desc.Height=height; desc.MipLevels=1;
        desc.ArraySize=1; desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count=1; desc.Usage=D3D11_USAGE_IMMUTABLE;
        desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA data{bgra.data(),stride,0};
        ComPtr<ID3D11Texture2D> texture;
        hr = device->CreateTexture2D(&desc, &data, &texture);
        if (FAILED(hr)) return Status::failure(StatusCode::GraphicsDeviceUnavailable,hr);
        ComPtr<ID3D11ShaderResourceView> new_srv;
        hr = device->CreateShaderResourceView(
            texture.Get(), nullptr, &new_srv);
        if (FAILED(hr)) return Status::failure(StatusCode::GraphicsDeviceUnavailable,hr);
        // Transactional swap AFTER decode/upload: never show partially
        // decoded pixels or retain a dangling CPU frame buffer.
        srv_ = std::move(new_srv);
        width_ = width; height_ = height;
        return Status::success();
    } catch (...) {
        return Status::failure(StatusCode::InternalError);
    }
}
} // namespace arssyut::windows
#endif
