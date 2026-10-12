# F2B.1 / F3–F4 native overlay workstream (2026-10-13)

Base: Draft PR #124 `11b8976528c5d00d36e41859514510fed261565f`, built from the actual hardware-successful 46.38s H.264/AAC recording with 2 Pause/Resume, zero audio staging overflow, zero webcam GPU copy failures, and zero video skip. MAIN is unchanged. This remains an internal candidate.

## F2B.1 preview post-Stop
Post-Stop GUI placeholder despite Camera ON is a fresh-frame publication failure (not proof that the physical camera stopped). Camera now submits bounded ~192x108 GPU thumbnail query batches via `ID3D11DeviceContext::Flush()` on the **sole** context owner after `End(D3D11_QUERY_EVENT)` so explicit `DONOTFLUSH` polling can observe completion without synchronous Map/wait. Returning from recorder resets preview cadence and marks Opening until a new readback actually publishes pixels. No second camera source, no CPU full-frame fallback, no altered video or audio clock. Native device-free QA guards contract. USB hardware post-Stop acceptance **still pending**.

## F3/F4 original scope restored — NOT fully implemented
Created `src/core/overlay/output_overlay_profile.hpp` with versioned, finite validated independent Camera/Logo profiles and exact stable enum identities. Camera defaults TopRight and 23.5% width (current real MP4 position), circle, rounded rectangle, diagonal TL+BR rounded, and **user-specific diagonal TR+BL rounded with TL+BR square**. Logo independent and default off, corners+center, opacity/size/margin. Native test compiles models and checks invalid floats and diagonal identities.

**IMPORTANT:** These profile structs are only the first domain-model/contract slice. Settings UI remains capability-locked and compositor remains hardcoded; **do not claim real F3 Camera Settings or F4 Logo GPU Overlay implemented in this build.** Their completion requires persisted Settings, additive managed/native ABI, validated immutable RecorderConfig values, analytic four-corner shader SDF, transactional PNG decode and retained GPU SRV, real encoded MP4 tests. Never unlock dead controls merely to look finished.

### No-regression / acceptance
- Windows native CTest and pinned FFmpeg **AUDIO-ENABLED** Avalonia package on exact PR SHA.
- Hardware webcam ON, live before/during/Pause/after Stop, sequence progression within 1.5 seconds, 100x stop cycles, screen H264 1080p60 + 48k AAC+System+Mic + Camera PIP, no StageFull or lost frames.
- Do not merge into main/release until F2B.1 hardware accepted. Implement F3 and F4 as subsequent actual source/bridge/shader slices and test on Windows hardware.
