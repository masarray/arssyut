#pragma once

#ifdef _WIN32

#include "platform/windows/audio/wasapi_capture_source.hpp"

#include <string>

namespace arssyut::windows {

using WasapiLoopbackState = WasapiCaptureState;
using WasapiLoopbackOptions = WasapiCaptureOptions;
using WasapiLoopbackSnapshot = WasapiCaptureSnapshot;

class WasapiLoopbackSource final {
public:
    // Empty endpoint_id resolves the current default render endpoint once at
    // Start. The resolved IMMDevice is then pinned for the recording lifetime;
    // later Windows default-device changes do not retarget the active source.
    [[nodiscard]] core::Status start(
        std::wstring endpoint_id = {},
        WasapiLoopbackOptions options = {});

    void stop() noexcept;

    [[nodiscard]] bool try_pop(
        WasapiPacketLease &lease) noexcept;

    [[nodiscard]] WasapiLoopbackSnapshot snapshot() const noexcept;

private:
    WasapiCaptureSource source_;
};

} // namespace arssyut::windows

#endif
