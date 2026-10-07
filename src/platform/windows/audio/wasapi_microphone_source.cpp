#include "platform/windows/audio/wasapi_microphone_source.hpp"

#ifdef _WIN32

#include <utility>

namespace arssyut::windows {

core::Status WasapiMicrophoneSource::start(
    std::wstring endpoint_id,
    WasapiMicrophoneOptions options)
{
    return source_.start(
        std::move(endpoint_id),
        WasapiEndpointRole::CaptureEndpoint,
        core::audio::AudioSourceId::Microphone,
        options);
}

void WasapiMicrophoneSource::stop() noexcept
{
    source_.stop();
}

bool WasapiMicrophoneSource::try_pop(
    WasapiPacketLease &lease) noexcept
{
    return source_.try_pop(lease);
}

WasapiMicrophoneSnapshot
WasapiMicrophoneSource::snapshot() const noexcept
{
    return source_.snapshot();
}

} // namespace arssyut::windows

#endif
