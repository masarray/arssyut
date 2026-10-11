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
    // First-failure details, set only on the recorder/audio worker.
    // Phase: 0=none, 1=running, 2=stop_drain, 3=src_flush,
    //        4=program_close, 5=writer_submit, 6=mf_finalize.
    // Source: 0=unknown, 1=microphone, 2=system, 3=program/writer.
    std::uint32_t failure_phase=0, failure_source=0;
    std::uint32_t failure_packet_ingress_status=0;
    std::uint32_t failure_native_hresult=0;
    std::uint32_t mic_stage_frames=0, system_stage_frames=0;
    std::uint32_t mic_stage_peak=0, system_stage_peak=0;
    std::uint32_t pending_audio_program_blocks=0;
};
} // namespace arssyut::app
