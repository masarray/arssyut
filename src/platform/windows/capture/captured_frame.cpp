#include "platform/windows/capture/captured_frame.hpp"

#ifdef _WIN32

#include "core/result/status.hpp"

#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/base.h>

namespace arssyut::windows {

arssyut::core::Result<Microsoft::WRL::ComPtr<ID3D11Texture2D>>
capture_texture(const CapturedFrame &captured) noexcept
{
    using arssyut::core::Result;
    using arssyut::core::Status;
    using arssyut::core::StatusCode;

    if (!captured.valid()) {
        return Result<Microsoft::WRL::ComPtr<ID3D11Texture2D>>::failure(
            Status::failure(StatusCode::InvalidArgument));
    }

    try {
        const auto surface = captured.frame.Surface();
        const auto access =
            surface.try_as<
                Windows::Graphics::DirectX::Direct3D11::
                    IDirect3DDxgiInterfaceAccess>();

        if (!access) {
            return Result<Microsoft::WRL::ComPtr<ID3D11Texture2D>>::failure(
                Status::failure(StatusCode::PlatformFailure));
        }

        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        const HRESULT hr = access->GetInterface(
            __uuidof(ID3D11Texture2D),
            reinterpret_cast<void **>(texture.GetAddressOf()));

        if (FAILED(hr) || !texture) {
            return Result<Microsoft::WRL::ComPtr<ID3D11Texture2D>>::failure(
                Status::failure(
                    StatusCode::PlatformFailure,
                    static_cast<std::uint32_t>(hr)));
        }

        return Result<Microsoft::WRL::ComPtr<ID3D11Texture2D>>::success(
            std::move(texture));
    } catch (const winrt::hresult_error &error) {
        return Result<Microsoft::WRL::ComPtr<ID3D11Texture2D>>::failure(
            Status::failure(
                StatusCode::PlatformFailure,
                static_cast<std::uint32_t>(error.code())));
    } catch (...) {
        return Result<Microsoft::WRL::ComPtr<ID3D11Texture2D>>::failure(
            Status::failure(StatusCode::InternalError));
    }
}

} // namespace arssyut::windows

#endif
