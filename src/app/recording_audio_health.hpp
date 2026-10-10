#pragma once
// Native audio quality forensics: single-writer snapshot, no media mutation.
#include <cstdint>
namespace arssyut::app {
struct AudioSourceHealth {
    std::uint64_t captured=0, published=0, silent=0;
    std::uint64_t discontinuities=0, timestamp_errors=0, dropped=0;
    std::uint64_t pool_exhaustions=0, ring_overflows=0;
};
struct RecordingAudioHealth {
    AudioSourceHealth system{}, microphone{};
    std::uint64_t missing_source_intervals=0, silent_program_blocks=0;
    std::uint64_t stale_canonical_frames=0, duplicate_canonical_frames=0;
    std::uint64_t staging_gaps=0, staging_overflows=0, dropped_program_blocks=0;
};
} // namespace arssyut::app
