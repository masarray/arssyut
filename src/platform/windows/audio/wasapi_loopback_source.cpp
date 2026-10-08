#include "platform/windows/audio/wasapi_loopback_source.hpp"

#ifdef _WIN32

#include <utility>

namespace arssyut::windows {

core::Status WasapiLoopbackSource::start(
    std::wstring endpoint_id,
    WasapiLoopbackOptions options)
{
    return source_.start(
        std::move(endpoint_id),
        core::audio::AudioSourceId::SystemAudio,
        WasapiCaptureMode::Loopback,
        options);
}

void WasapiLoopbackSource::stop() noexcept
{
    source_.stop();
}

bool WasapiLoopbackSource::try_pop(
    WasapiPacketLease &lease) noexcept
{
    return source_.try_pop(lease);
}

WasapiLoopbackSnapshot
WasapiLoopbackSource::snapshot() const noexcept
{
    return source_.snapshot();
}

} // namespace arssyut::windows

#endif
