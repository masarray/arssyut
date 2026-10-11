#include "platform/windows/graphics/d3d11_compositor.hpp"
#include <Windows.h>
#include <algorithm>
#include <memory>
#include <utility>
#include <d3d11.h>
#include <wrl/client.h>
#include <iostream>
#include <vector>
#include <cstdint>

using Microsoft::WRL::ComPtr;
using arssyut::windows::D3D11Compositor;

int main()
{
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    D3D_FEATURE_LEVEL level{};
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP,
        nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        nullptr, 0, D3D11_SDK_VERSION,
        &device, &level, &context);
    if (FAILED(hr)) return 1;

    // Creates and compiles the exact production HLSL, including the
    // camera-disabled branch. A shader error is a regression even when
    // synthetic NV12 support is absent on this particular WARP adapter.
    auto result = D3D11Compositor::create(device.Get());
    if (!result) {
        std::cerr << "Compositor shader compilation failed\n";
        return 1;
    }
    auto compositor = std::move(result).value();
    D3D11_TEXTURE2D_DESC bg{};
    bg.Width=640; bg.Height=360;
    bg.MipLevels=bg.ArraySize=1; bg.SampleDesc.Count=1;
    bg.Format=DXGI_FORMAT_B8G8R8A8_UNORM;
    bg.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    std::vector<std::uint32_t> pixels(bg.Width*bg.Height, 0xff000000u);
    D3D11_SUBRESOURCE_DATA bgInit{pixels.data(), bg.Width*4, 0};
    ComPtr<ID3D11Texture2D> desktop;
    if (FAILED(device->CreateTexture2D(&bg, &bgInit, &desktop)))
        return 1;
    if (!compositor->update_source(context.Get(), desktop.Get()).ok())
        return 1;
    if (!compositor->render_retained(context.Get(), {}, {640,360}).ok()) {
        std::cerr << "Baseline render failed\n";
        return 1;
    }

    // Create a neutral-gray NV12 physical texture, feed same compositor
    // and inspect one image output. We do CPU readback ONLY in this
    // deterministic offline test, never in the product capture path.
    D3D11_TEXTURE2D_DESC cam{};
    cam.Width=640; cam.Height=480;
    cam.MipLevels=cam.ArraySize=1; cam.SampleDesc.Count=1;
    cam.Format=DXGI_FORMAT_NV12;
    cam.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    std::vector<std::uint8_t> nv12(cam.Width*cam.Height*3/2,128);
    std::fill(nv12.begin(),nv12.begin()+cam.Width*cam.Height, 126);
    D3D11_SUBRESOURCE_DATA camData{nv12.data(), cam.Width, 0};
    ComPtr<ID3D11Texture2D> webcam;
    hr = device->CreateTexture2D(&cam,&camData,&webcam);
    if (FAILED(hr)) {
        std::cout << "P8W2: WARP lacks NV12 support; shader/baseline PASS, "
                     "physical NV12 output requires Windows GPU QA\n";
        return 0;
    }

    const auto source_status =
        compositor->update_webcam(context.Get(),webcam.Get(),0);
    if (!source_status.ok()) {
        std::cerr << "NV12 shader resource creation failed\n";
        return 1;
    }
    if (!compositor->render_retained(context.Get(), {}, {640,360}).ok())
        return 1;

    auto *out = compositor->output_texture();
    D3D11_TEXTURE2D_DESC staging{};
    out->GetDesc(&staging);
    staging.Usage=D3D11_USAGE_STAGING;
    staging.BindFlags=0;
    staging.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> readback;
    if (FAILED(device->CreateTexture2D(&staging,nullptr,&readback)))
        return 1;
    context->CopyResource(readback.Get(),out);
    D3D11_MAPPED_SUBRESOURCE m{};
    if (FAILED(context->Map(readback.Get(),0,D3D11_MAP_READ,0,&m)))
        return 1;
    const auto px = [&](unsigned x,unsigned y) {
        const auto *row = static_cast<const std::uint8_t *>(m.pData)
            + y*m.RowPitch;
        return row[x*4];
    };
    const auto cam_center=px(545,110);
    const auto background=px(30,30);
    context->Unmap(readback.Get(),0);
    if (cam_center < 75 || background > 15) {
        std::cerr << "Camera GPU overlay wrong: cam=" <<
            unsigned(cam_center) << " bg=" << unsigned(background) << "\n";
        return 1;
    }
    compositor->clear_webcam();
    if (!compositor->render_retained(context.Get(), {}, {640,360}).ok())
        return 1;
    context->CopyResource(readback.Get(),out);
    if (FAILED(context->Map(readback.Get(),0,D3D11_MAP_READ,0,&m)))
        return 1;
    const auto *row = static_cast<const std::uint8_t *>(m.pData)
        + 110*m.RowPitch;
    const bool returned_to_black=row[545*4]<15;
    context->Unmap(readback.Get(),0);
    if (!returned_to_black) {
        std::cerr << "Camera clear must restore desktop-only output\n";
        return 1;
    }
    std::cout << "P8W2 single-pass GPU NV12 compositor PASS\n";
    return 0;
}
