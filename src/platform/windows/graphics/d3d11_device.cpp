#include "platform/windows/graphics/d3d11_device.hpp"

#ifdef _WIN32

#include "core/result/status.hpp"

#include <d3d10.h>

#include <iterator>
#include <new>

namespace arssyut::windows {

namespace {

HRESULT create_device(
    D3D_DRIVER_TYPE driver_type,
    UINT flags,
    ID3D11Device **device,
    D3D_FEATURE_LEVEL *feature_level,
    ID3D11DeviceContext **context) noexcept
{
    constexpr D3D_FEATURE_LEVEL preferred_levels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };

    HRESULT hr = D3D11CreateDevice(
        nullptr,
        driver_type,
        nullptr,
        flags,
        preferred_levels,
        static_cast<UINT>(std::size(preferred_levels)),
        D3D11_SDK_VERSION,
        device,
        feature_level,
        context);

    if (hr != E_INVALIDARG)
        return hr;

    constexpr D3D_FEATURE_LEVEL fallback_levels[] = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };

    return D3D11CreateDevice(
        nullptr,
        driver_type,
        nullptr,
        flags,
        fallback_levels,
        static_cast<UINT>(std::size(fallback_levels)),
        D3D11_SDK_VERSION,
        device,
        feature_level,
        context);
}

} // namespace

arssyut::core::Result<std::unique_ptr<D3D11Device>>
D3D11Device::create(
    D3D11DevicePreference preference,
    bool enable_debug_layer) noexcept
{
    using arssyut::core::Result;
    using arssyut::core::Status;
    using arssyut::core::StatusCode;

    std::unique_ptr<D3D11Device> owner(new (std::nothrow) D3D11Device{});
    if (!owner) {
        return Result<std::unique_ptr<D3D11Device>>::failure(
            Status::failure(StatusCode::InternalError));
    }

    UINT flags =
        D3D11_CREATE_DEVICE_BGRA_SUPPORT |
        D3D11_CREATE_DEVICE_VIDEO_SUPPORT;
    if (enable_debug_layer)
        flags |= D3D11_CREATE_DEVICE_DEBUG;

    const D3D_DRIVER_TYPE driver_type =
        preference == D3D11DevicePreference::WarpForTesting
            ? D3D_DRIVER_TYPE_WARP
            : D3D_DRIVER_TYPE_HARDWARE;

    const HRESULT hr = create_device(
        driver_type,
        flags,
        owner->device_.GetAddressOf(),
        &owner->feature_level_,
        owner->immediate_context_.GetAddressOf());

    if (FAILED(hr)) {
        return Result<std::unique_ptr<D3D11Device>>::failure(
            Status::failure(
                StatusCode::GraphicsDeviceUnavailable,
                static_cast<std::uint32_t>(hr)));
    }

    Microsoft::WRL::ComPtr<ID3D10Multithread> multithread;
    if (SUCCEEDED(owner->device_.As(&multithread)) && multithread) {
        multithread->SetMultithreadProtected(TRUE);
    }

    return Result<std::unique_ptr<D3D11Device>>::success(
        std::move(owner));
}

} // namespace arssyut::windows

#endif
