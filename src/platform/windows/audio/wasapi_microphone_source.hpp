#pragma once

#ifdef _WIN32

#include "platform/windows/audio/wasapi_capture_source.hpp"

#include <string>

namespace arssyut::windows {

using WasapiMicrophoneState = WasapiCaptureState;
using WasapiMicrophoneOptions = WasapiCaptureOptions;
using WasapiMicrophoneSnapshot = WasapiCaptureSnapshot;

class WasapiMicrophoneSource final {
public:
    [[nodiscard]] core::Status start(
        std::wstring endpoint_id,
        WasapiMicrophoneOptions options = {});

    void stop() noexcept;

    [[nodiscard]] bool try_pop(
        WasapiPacketLease &lease) noexcept;

    [[nodiscard]] WasapiMicrophoneSnapshot snapshot() const noexcept;

private:
    WasapiCaptureSource source_;
};

} // namespace arssyut::windows

#endif
