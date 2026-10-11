#pragma once

// Windows-only P8W1A diagnostic event policy: classify Source Reader
// callbacks before touching GPU resources. This file is not a product API.
#include <mfreadwrite.h>

#include <cstdint>

namespace arssyut::webcam_probe {

enum class ReadEvent : std::uint8_t {
    Frame,
    StreamGap,
    UnexpectedEmpty,
    StreamConfigurationChanged,
    EndOfStream,
    ReaderError,
};

// Media Foundation explicitly permits pSample == nullptr on STREAMTICK
// and ENDOFSTREAM. A stream tick is a real source-time *gap* and must not
// silently count as a valid frame, even though it doesn't disprove GPU
// compatibility for the samples that were delivered.
[[nodiscard]] constexpr ReadEvent classify_read_event(
    bool has_sample,
    DWORD flags) noexcept
{
    constexpr DWORD kConfigurationFlags =
        MF_SOURCE_READERF_NEWSTREAM |
        MF_SOURCE_READERF_NATIVEMEDIATYPECHANGED |
        MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED |
        MF_SOURCE_READERF_ALLEFFECTSREMOVED;
    constexpr DWORD kRecognized =
        MF_SOURCE_READERF_ERROR |
        MF_SOURCE_READERF_ENDOFSTREAM |
        kConfigurationFlags |
        MF_SOURCE_READERF_STREAMTICK;

    if ((flags & MF_SOURCE_READERF_ERROR) != 0)
        return ReadEvent::ReaderError;
    if ((flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0)
        return ReadEvent::EndOfStream;
    if ((flags & kConfigurationFlags) != 0)
        return ReadEvent::StreamConfigurationChanged;
    // Unknown flag bits or an impossible sample+gap combination are REVIEW.
    if ((flags & ~kRecognized) != 0)
        return ReadEvent::UnexpectedEmpty;
    if ((flags & MF_SOURCE_READERF_STREAMTICK) != 0)
        return has_sample ? ReadEvent::UnexpectedEmpty
                          : ReadEvent::StreamGap;
    return has_sample ? ReadEvent::Frame : ReadEvent::UnexpectedEmpty;
}

} // namespace arssyut::webcam_probe
