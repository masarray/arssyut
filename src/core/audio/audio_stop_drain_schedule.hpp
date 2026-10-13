#pragma once

// F1 bounded stop-drain closure policy. Portable and independently testable.
// No new media clock: the existing assembler's 1024-frame program timeline
// and RecorderSession's captured physical Stop QPC remain authoritative.
#include "core/audio/audio_canonical_program_assembler.hpp"
#include <cstdint>
#include <limits>

namespace arssyut::core::audio {

struct AudioStopDrainFrontier {
    bool microphone_enabled = false;
    bool system_enabled = false;
    bool microphone_complete = false;  // source popped and SRC fully drained
    bool system_complete = false;
    std::int64_t microphone_next_frame = 0;
    std::int64_t system_next_frame = 0;
    std::int64_t stop_ticks_100ns = 0;
};

[[nodiscard]] inline bool audio_stop_block_safe(
    const CanonicalProgramAssembler &program,
    const AudioStopDrainFrontier &f) noexcept
{
    if (program.next_due_100ns() > f.stop_ticks_100ns)
        return false;  // No full AAC block beyond physical Stop.
    constexpr std::uint64_t kMaxIndex =
        static_cast<std::uint64_t>(
            std::numeric_limits<std::int64_t>::max()) /
        CanonicalProgramAssembler::kFramesPerBlock - 1ULL;
    const auto index = program.mix_stats().blocks_closed;
    if (index > kMaxIndex)
        return false;
    const auto end_frame = static_cast<std::int64_t>(
        (index + 1ULL) * CanonicalProgramAssembler::kFramesPerBlock);
    // A source's SRC state may still contain pre-Stop audio. A block cannot
    // close as silence until that source has emitted past it OR its producer
    // and resampler are both finished. Never "guess" using process wall time.
    return (!f.microphone_enabled || f.microphone_complete ||
                f.microphone_next_frame >= end_frame) &&
           (!f.system_enabled || f.system_complete ||
                f.system_next_frame >= end_frame);
}

} // namespace arssyut::core::audio
