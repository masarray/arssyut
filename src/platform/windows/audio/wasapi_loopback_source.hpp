#pragma once

#ifdef _WIN32

#include "platform/windows/audio/wasapi_capture_source.hpp"

namespace arssyut::windows {

using WasapiLoopbackState = WasapiCaptureState;
using WasapiLoopbackOptions = WasapiCaptureOptions;
using WasapiLoopbackSnapshot = WasapiCaptureSnapshot;

class WasapiLoopbackSource final {
public:
    WasapiLoopbackSource() = default;

    explicit WasapiLoopbackSource(
        std::shared_ptr<IWasapiCaptureClient> client_override) noexcept
        : source_(std::move(client_override))
    {
    }

    ~WasapiLoopbackSource() = default;

    WasapiLoopbackSource(
        const WasapiLoopbackSource &) = delete;
    WasapiLoopbackSource &operator=(
        const WasapiLoopbackSource &) = delete;

    [[nodiscard]] core::Status start(
        std::wstring endpoint_id,
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
