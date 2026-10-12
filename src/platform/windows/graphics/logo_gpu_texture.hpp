#pragma once
#ifdef _WIN32
// One-shot PNG/WIC -> retained premultiplied BGRA GPU texture. Called in
// RecorderSession::Preparing, never on the 60fps render/capture hot path.
// Invalid logos degrade only the optional logo overlay.
#include <Windows.h>
#include <wincodec.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <filesystem>
#include <vector>
#include <cstdint>
#include <limits>

namespace arssyut::windows {
struct LogoGpuTexture {
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
    std::uint32_t width=0, height=0;
    void clear() noexcept { view.Reset(); width=height=0; }

    [[nodiscard]] bool load_png(ID3D11Device *device,
                                const std::wstring &path) noexcept {
        clear();
        if (!device || path.empty() || path.size()>=512) return false;
        try {
            std::error_code ec;
            const auto bytes=std::filesystem::file_size(path,ec);
            if (ec || bytes == 0 || bytes > 16ULL*1024ULL*1024ULL)
                return false;
            const HRESULT apartment=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
            if (FAILED(apartment) && apartment != RPC_E_CHANGED_MODE)
                return false;
            struct Uninit { bool active; ~Uninit() {
                if(active) CoUninitialize();
            }} uninit{SUCCEEDED(apartment)};
            Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
            HRESULT hr=CoCreateInstance(CLSID_WICImagingFactory,nullptr,
                CLSCTX_INPROC_SERVER,IID_PPV_ARGS(factory.GetAddressOf()));
            if (FAILED(hr)) return false;
            Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
            hr=factory->CreateDecoderFromFilename(path.c_str(),nullptr,
                GENERIC_READ,WICDecodeMetadataCacheOnLoad,&decoder);
            if (FAILED(hr)) return false;
            GUID container{};
            if (FAILED(decoder->GetContainerFormat(&container)) ||
                !IsEqualGUID(container,GUID_ContainerFormatPng)) return false;
            Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
            if (FAILED(decoder->GetFrame(0,&frame))) return false;
            UINT w=0,h=0;
            if (FAILED(frame->GetSize(&w,&h)) ||
                w==0 || h==0 || w>4096 || h>4096 ||
                static_cast<std::uint64_t>(w)*h>8'388'608ULL)
                return false;
            Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
            if (FAILED(factory->CreateFormatConverter(&converter))) return false;
            if (FAILED(converter->Initialize(frame.Get(),
                GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,
                nullptr,0.0,WICBitmapPaletteTypeCustom))) return false;
            std::vector<std::uint8_t> pixels(static_cast<std::size_t>(w)*h*4U);
            if (FAILED(converter->CopyPixels(nullptr,w*4,
                static_cast<UINT>(pixels.size()),pixels.data()))) return false;
            D3D11_TEXTURE2D_DESC desc{};
            desc.Width=w;desc.Height=h;desc.MipLevels=desc.ArraySize=1;
            desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;
            desc.SampleDesc.Count=1;
            desc.Usage=D3D11_USAGE_IMMUTABLE;
            desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
            D3D11_SUBRESOURCE_DATA init{};
            init.pSysMem=pixels.data();init.SysMemPitch=w*4;
            Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
            if (FAILED(device->CreateTexture2D(&desc,&init,&texture))) return false;
            if (FAILED(device->CreateShaderResourceView(
                texture.Get(),nullptr,&view))) return false;
            width=w;height=h;
            return true;
        } catch(...) {
            clear();return false;
        }
    }
};
} // namespace arssyut::windows
#endif
