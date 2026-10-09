#pragma once

// P7A6 Preparing-only: pin endpoint identities before the recorder can Arm.
// An MMDevice default is resolved ONCE, never queried again by capture workers.
// This header owns no audio clock, source queue, or device-recovery policy.

#include "core/result/status.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace arssyut::app::audio {

enum class AudioEndpointKind : std::uint8_t {
    Microphone = 0,
    SystemAudio,
};

struct AudioEndpointRequest {
    bool microphone = false;
    bool system_audio = false;
    // Empty means resolve the Windows default ONCE at Preparing. Non-empty
    // must be a concrete, bridge-validated MMDevice ID, never a display name.
    std::wstring microphone_id;
    std::wstring system_audio_id;

    [[nodiscard]] bool requested() const noexcept
    {
        return microphone || system_audio;
    }
};

struct PinnedAudioEndpoints {
    bool microphone = false;
    bool system_audio = false;
    std::wstring microphone_id;
    std::wstring system_audio_id;

    void clear() noexcept
    {
        microphone = false;
        system_audio = false;
        microphone_id.clear();
        system_audio_id.clear();
    }
};

// Pluggable ONLY during synchronous Preparing; tests use a deterministic
// resolver, production uses WindowsAudioEndpointLookup below. Lookup returns
// the immutable concrete ID and verifies it is active and the correct flow.
class IAudioEndpointLookup {
public:
    virtual ~IAudioEndpointLookup() = default;

    [[nodiscard]] virtual core::Status pin(
        AudioEndpointKind kind,
        std::wstring_view explicit_id,
        std::wstring &concrete_id) = 0;
};

// No partial-success exposure: if either required endpoint is unavailable,
// both pinned IDs are cleared. Video-only MUST NOT touch MMDevice/COM.
[[nodiscard]] inline core::Status resolve_audio_endpoints(
    const AudioEndpointRequest &request,
    IAudioEndpointLookup &lookup,
    PinnedAudioEndpoints &output)
{
    output.clear();
    if (!request.requested())
        return core::Status::success();

    // Prevent truncated IDs (embedded NUL), oversized untrusted bridge input,
    // and a hidden default fallback when an explicit ID was malformed.
    constexpr std::size_t max_id_chars = 4096;
    const auto valid_id = [max_id_chars](const std::wstring &id) noexcept {
        return id.size() <= max_id_chars &&
               id.find(L'\0') == std::wstring::npos;
    };
    if (!valid_id(request.microphone_id) ||
        !valid_id(request.system_audio_id)) {
        return core::Status::failure(core::StatusCode::InvalidArgument);
    }

    PinnedAudioEndpoints prepared;
    if (request.microphone) {
        auto status = lookup.pin(
            AudioEndpointKind::Microphone,
            request.microphone_id, prepared.microphone_id);
        if (!status.ok())
            return status;
        if (prepared.microphone_id.empty())
            return core::Status::failure(core::StatusCode::PlatformFailure);
        prepared.microphone = true;
    }
    if (request.system_audio) {
        auto status = lookup.pin(
            AudioEndpointKind::SystemAudio,
            request.system_audio_id, prepared.system_audio_id);
        if (!status.ok())
            return status;
        if (prepared.system_audio_id.empty())
            return core::Status::failure(core::StatusCode::PlatformFailure);
        prepared.system_audio = true;
    }

    output = std::move(prepared);
    return core::Status::success();
}

} // namespace arssyut::app::audio

#ifdef _WIN32

#include <Windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

namespace arssyut::app::audio {

// Used on the RecorderSession startup worker, not a hot-path audio thread.
// COM is initialized only when a source is requested; when a worker already
// owns a COM apartment, RPC_E_CHANGED_MODE permits use of that apartment.
class WindowsAudioEndpointLookup final : public IAudioEndpointLookup {
public:
    [[nodiscard]] core::Status pin(
        AudioEndpointKind kind,
        std::wstring_view explicit_id,
        std::wstring &concrete_id) override
    {
        concrete_id.clear();

        const HRESULT apartment_hr = CoInitializeEx(
            nullptr, COINIT_MULTITHREADED);
        if (FAILED(apartment_hr) &&
            apartment_hr != RPC_E_CHANGED_MODE) {
            return failure(apartment_hr);
        }
        struct ApartmentGuard {
            bool owns;
            ~ApartmentGuard()
            {
                if (owns)
                    CoUninitialize();
            }
        } apartment{SUCCEEDED(apartment_hr)};

        using Microsoft::WRL::ComPtr;
        ComPtr<IMMDeviceEnumerator> enumerator;
        HRESULT hr = CoCreateInstance(
            __uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
            IID_PPV_ARGS(enumerator.GetAddressOf()));
        if (FAILED(hr))
            return failure(hr);

        const EDataFlow desired =
            kind == AudioEndpointKind::Microphone ? eCapture : eRender;
        ComPtr<IMMDevice> endpoint;
        if (explicit_id.empty()) {
            // eConsole is the Windows standard default endpoint for ordinary
            // desktop recording. The concrete ID is pinned for this session.
            hr = enumerator->GetDefaultAudioEndpoint(
                desired, eConsole, endpoint.GetAddressOf());
        } else {
            try {
                const std::wstring value(explicit_id);
                hr = enumerator->GetDevice(
                    value.c_str(), endpoint.GetAddressOf());
            } catch (...) {
                return core::Status::failure(
                    core::StatusCode::InternalError);
            }
        }
        if (FAILED(hr) || !endpoint)
            return failure(FAILED(hr) ? hr : E_UNEXPECTED);

        DWORD state = 0;
        hr = endpoint->GetState(&state);
        if (FAILED(hr))
            return failure(hr);
        if ((state & DEVICE_STATE_ACTIVE) == 0)
            return failure(AUDCLNT_E_DEVICE_INVALIDATED);

        ComPtr<IMMEndpoint> typed;
        hr = endpoint.As(&typed);
        if (FAILED(hr))
            return failure(hr);
        EDataFlow actual = eAll;
        hr = typed->GetDataFlow(&actual);
        if (FAILED(hr))
            return failure(hr);
        if (actual != desired)
            return core::Status::failure(
                core::StatusCode::InvalidArgument,
                static_cast<std::uint32_t>(E_INVALIDARG));

        LPWSTR id = nullptr;
        hr = endpoint->GetId(&id);
        if (FAILED(hr) || id == nullptr) {
            if (id)
                CoTaskMemFree(id);
            return failure(FAILED(hr) ? hr : E_UNEXPECTED);
        }
        struct IdGuard {
            LPWSTR value;
            ~IdGuard() { CoTaskMemFree(value); }
        } id_guard{id};

        try {
            concrete_id.assign(id);
        } catch (...) {
            return core::Status::failure(
                core::StatusCode::InternalError);
        }
        if (concrete_id.empty())
            return failure(E_UNEXPECTED);
        return core::Status::success();
    }

private:
    [[nodiscard]] static core::Status failure(HRESULT hr) noexcept
    {
        return core::Status::failure(
            core::StatusCode::PlatformFailure,
            static_cast<std::uint32_t>(hr));
    }
};

} // namespace arssyut::app::audio

#endif // _WIN32
