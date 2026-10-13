# P8W-F1–F5 — final A/V-first webcam, Camera Settings and Logo Overlay execution contract

Status: **DOCUMENTATION / architecture gate complete, IMPLEMENTATION AND HARDWARE ACCEPTANCE NOT COMPLETE**
Date: 2026-10-12
Parent: [ADR-011](adr/ADR-011-webcam-source-continuity-and-output-overlays.md), [ADR-010](adr/ADR-010-native-webcam-overlay.md), [AGENTS.md](../AGENTS.md), [P7A audio lock](P7A_AUDIO_ACCEPTANCE_LOCK.md), [AV hardware acceptance](AV_RELEASE_ACCEPTANCE_2026-10-11.md).
Workstream: [Draft PR #120](https://github.com/masarray/arssyut/pull/120), [Issue #116](https://github.com/masarray/arssyut/issues/116); PR #119 is historical experimental webcam evidence only, PR #115 Pause remains **Draft/unmerged**.
This document is the **implementation source of truth** for F1–F5. Older point-in-time handoffs describe their own dates and must not be construed as current webcam release acceptance.

## 0. Immutable baseline, actual user evidence, no false PASS

- Stable parent of PR #120: \`main@72bef9a1b8f6dcaddad3a1693b6f9dedacfdda9a\`, incorporating the previously accepted dual audio and microphone mute. Real dual A/V checkpoint: \`checkpoint/av-dual-audio-accepted-20261011@12e1fa98e33735cade2ec5bdca61a76861d8bf23\`; exact saved initial webcam/GUI-start checkpoint: \`checkpoint/p8-av-first-doubleclick-launch-ci-green-20261011@36e25a85a43c941629e300dcdcc336c7fb81e8f8\`. **Re-check current GitHub refs before modifying code.**
- The user previously demonstrated a Windows-native EXE launched by **double-click**, connected 720p NV12 DXGI camera, GUI preview image, and a real 1920×1080/60 H.264 + 48kHz AAC file with face-cam **moving in the output**, 923 submitted video frames, 389 webcam frames received, 385 new GPU frames, 923 webcam-rendered slots, 0 webcam GPU copy failures, 0 video skips and 0 encoder backpressure. These are **not** acceptance of GUI preview freshness or Stop.
- The 2026-10-12 real recording diagnostics: \`result=failed\`, \`status_code=4\`, \`status_detail=8\`, \`audio_staging_overflows=1\`, 718 submitted AAC samples / 735,232 PCM frames, 0 audio packet/pool/ring drops, 0 video skips, valid readable MP4. In \`src/core/result/status.hpp\`, code 4 = \`PlatformFailure\`; \`src/core/audio/audio_packet_ingress.hpp\` maps \`PacketIngressStatus::StageFull\` to **8**; \`src/app/audio_product_runtime.hpp::ingress_failure()\` wraps that value as \`PlatformFailure\`. **Therefore StageFull is strongly identified as failure type; we do not yet know whether its first occurrence is in live \`service()\` or Stop \`finish()\`.** The current \`finish()\` batch-pumps up to 32 rounds of two packets/source **before** closing due blocks; this is a concrete likely overflow mechanism, not yet a proven first-failure phase. \`encoder_failure_stage=none\` and healthy video confirm this was not an H.264 failure. Never convert StageFull into success or ignore the overflow.
- Screenshots: webcam preview appears as a snapshot or stops being updated while the camera is visibly live in recorded output. Existing GUI deliberately stops its separate \`WebcamPreviewSession\` before recorder Start; \`WebcamPreviewTimer_OnTick\` also disables updates during the start sequence. Distinguish failure **before** Start (timer/driver/sequence) from absence of preview **during** recording (explicit ownership handoff) with real sequence-age telemetry.
- Settings → Camera is hard-locked by \`SettingsWindow.axaml\` and hidden old controls, despite a real compositor already drawing camera **hard-coded in the upper-right**. Persisted \`CameraPlacement.BottomRight\` is not reflected in the MP4. Camera Settings and Logo Overlay need authoritative profile→bridge→compositor wiring, not just visual layout.

### Baseline non-negotiables

Preserve accepted System+Mic audio, native Mic Mute, WGC/ArZoom/ArVisual, single QPC/48k canonical audio program, GPU 60fps scheduler, H.264/AAC writer, pixel-accurate SDR BT.709, Start/Stop/GUI and packaging. Public/main source untouched while working on the draft. Launch product by **double-click \`Arssyut.UI.exe\`**, never a diagnostic EXE or user-supplied PowerShell environment override. Accept neither web app regression nor the old VIDEO-ONLY artifact. Stop on startup crash or A/V regression.

## 1. Architecture choice — one actual webcam, one writer

Adopt [ADR-011](adr/ADR-011-webcam-source-continuity-and-output-overlays.md):

\`\`\`text
generation-validated camera token -> WebcamCoordinator (ONE MF open)
                                  -> one bounded newest DXGI sample + QPC + epoch
 PreviewActive -> preview GPU worker (one active context owner)
 RecordingActive -> existing recorder video worker (one active context owner)
                                  -> retained GPU NV12 Y/UV -> ONE compositor
 desktop/WGC + Camera PIP + Logo overlay -> ONE H264/AAC writer -> ONE MP4
                              \`-> bounded tiny preview (192x108 at <=15Hz)
\`\`\`

No second camera opener for preview, no MPMC use of the existing SPSC three-slot mailbox, no concurrent immediate-context calls, no implicit same-adapter cross-device texture use, no CPU full-image readback, no duplicate clock/writer. Preview frames are **actual native state** with epoch/sequence/QPC and explicit stale/Off/unavailable state. Integrate an owned GPU device across preview→record as detailed in ADR-011; if device sharing requires touching accepted WGC/A/V, first prove the isolated same-device injection and camera-OFF identity in deterministic tests. No speculative CPU fallback.

An added logo is not a second recording source/track. It is a predecoded cached graphics layer in the existing compositor draw.

## 2. F1 — P0 audio Stop/Finalize integrity (first and blocking)

**Observed failure mapping:** \`PlatformFailure(4) / PacketIngressStatus::StageFull(8)\`. \`audio_staging_overflows=1\`. This is a true error even when MP4 opens. Owner: \`src/app/audio_product_runtime.hpp::service/finish\`, \`src/core/audio/audio_canonical_program_assembler.hpp\`, \`src/core/audio/audio_packet_ingress.hpp\`; \`RecorderSession\` only owns lifecycle and diagnostic status. **Do not change libswresample, accepted DSP/drift, source timestamps, mixer gain or audio buffer size without a disproving test.**

### Necessary instrumentation before changing algorithms
- First-error-only snapshot: \`failure_origin=audio_ingress\`, \`failure_phase=running|stop_drain|src_flush|program_close|writer_submit|mf_finalize\`, \`source=mic|system|both\`, \`packet_ingress_status\`, \`native_source_hresult\`, staging occupancy/high-water and due/closed program frame, number of pending writer blocks. Include in diagnostics JSON and UI concise actionable text.
- Keep \`StatusCode\` ABI stable. Never collapse \`StageFull\` to an unrelated Win32 error. Record native statuses independently.

### Intended bounded algorithm — NO batch-then-flush
Stop samples the session QPC deadline **once**. Producer quiescence and ownership of pending leases must be explicit; do not \`stop_sources()\` while outstanding payload leases depend on their lifetime.

Run a deterministic **interleaved pump / close-due / submit** step while residual data exists:
1. Process a bounded packet from each available source, tracking its original canonical frame/PTS without retiming.
2. Before admitting an offer that would overfill the fixed staging ring, close already due program intervals in chronological order and submit the oldest ready AAC block to the same writer; consume a bounded amount and retry only if the resampler state/payload was **not** already consumed. If it has been consumed, never replay or silently skip it.
3. Allow new due intervals to close only when source progress/end-of-stream + canonical QPC makes the interval safe. During the final phase use the one captured physical Stop deadline and source-drained state to close remaining intervals. **Do not close future blocks prematurely and fill them with silence while earlier audio packets are still in flight.**
4. Drain each accepted resampler tail, immediately interleaving close/submit; close/trim exactly to physical Stop. Finalize writer once, then retire source pools and handles in explicit ownership order. If there is no progress in a bounded iteration or valid capacity budget, fail closed with \`failure_phase\` and leave recoverable file; never inflate FIFO capacity or spin/sleep.
5. For live \`service()\`, apply the same progress/occupancy rule under a small fixed work budget so capture latency stays bounded. Original audio clock, 48k stereo and AAC 1024-block timeline remain authoritative.

The stop strategy must be proved by deterministic fixture replay before runtime editing; the exact safe source quiesce and zero-remaining control flow are finalized from actual \`WasapiCaptureSource::stop\` semantics. Do not assert that buffered audio can be retrieved after closing a WASAPI worker without a test.

### Tests and gate
- Fake mic+system 48k streams, staggered packet arrival, output writer temporarily delayed, 15s session with late backlog, soft/source discontinuities, 44.1→48k SRC tail, Mic Mute, silence, and Stop during in-flight packets.
- Reproduce the **old** finish overflow deterministically using a controlled 32-round burst; verify the **new** algorithm keeps \`staging_overflows=0\`, \`dropped_program_blocks=0\`, monotonic first/last AAC PTS, bounded queue, exactly one finalize, no sample duplication/truncation.
- Live recording and Stop both zero \`StageFull\`; A/V duration near physical session duration; no crash, hang, false Saved. Keep currently accepted system-only/mic-only/dual recordings as controls.
- Only after this gate is green may F2 become the integration critical path. Distinguish valid partial MP4 with \`Recording incomplete — audio ingress StageFull\` from fully saved file; don't erase a real failure to make UX look nicer.

## 3. F2 — P0 continuous fresh GUI webcam preview and physical-source ownership

Before Start: selecting a native camera and turning ON opens one MF capture. The preview must show changing sequence and timestamp (up to 10–15Hz) within a bounded start time, not a stale still frame. During Start, transfer **render/lease ownership** to existing recorder worker, not camera source ownership; recorder also periodically publishes a tiny preview image for GUI. After Stop, return render ownership to preview without closing/reopening the physical source. On Camera OFF / closing UI, explicitly stop one source and release lamp.

- Source states: \`Off -> Resolving -> Opening -> PreviewActive <-> RecordingActive -> Closing -> Off\`, plus \`Unavailable\`; every state/frame has generation+epoch. Camera failures do not fail a healthy desktop/audio recording, but are visible in UI and diagnostics.
- Existing 3-slot \`WebcamGpuFrameSlot\` is SPSC; **one** consumer at any instant. Do not let two threads \`try_acquire_latest\`. One D3D11 immediate-context owner per active state; no simultaneous preview draw/readback and 60fps output submission on it. No camera-open or \`worker.join()\` on the GUI dispatcher.
- Preview readback only at 192×108 via retained staging buffers and explicit GPU-ready polling. No unbounded queuing or sync \`Map\` in 60fps render. UI updates the bitmap only on sequence change, with capture age and epoch; if >500ms stale or backend Off/Unavailable, clear preview and display truthful state. Validate 10–15Hz on real hardware and fallback to fewer updates if renderer under load; never spin or starve A/V.
- Test toggle rapidly, start during Open, Stop while preview consumes, camera unplug/denied/busy, device switch/generation invalidation, app Close while recording and while source Opening, repeated 100x, no double-open or stale camera light, no resource growth. Camera absent leaves accepted A/V behavior and startup latency unchanged.
- **Hardware gate:** moving GUI thumbnail before/during/after 15s recording with actual PIP moving in final MP4. Evidence: \`webcam_preview_updates\`, \`webcam_preview_age_max_ms\`, native capture opened count, \`preview_state_transition\`, 15s MP4. No additional GPU probe JSON accepted as substitute.

## 4. F3 — Camera Settings: one canonical configuration controls MP4

Remove the misleading "Camera compositor backend is not connected yet" capability-lock **only in the integrated A/V internal app where camera is actually available**. Place device selection + native availability/permission indicator, preview thumbnail, 4 placement choices, 4 image-mask choices, size (initial user-visible range 15–40% of output width), margin, border and optional mirror/crop. Default is TopRight to preserve the user's actual recorded PIP, not the old hidden BottomRight preference.

Exact masks:
1. \`Circle\` (equal output pixel axes; crop-to-cover);
2. \`RoundedRectangle\`;
3. \`DiagonalTopLeftBottomRight\` (TL/BR rounded; TR/BL square);
4. \`DiagonalTopRightBottomLeft\` (TR/BL rounded; TL/BR square).

Canonical \`CameraOverlayProfile\` is validated/versioned and normalized to output pixels **once per session**, passed Settings → persisted product settings → native bridge → immutable recorder config → one compositor constant buffer. Reject NaN/infinite size, invalid enum, negative margin, or rect beyond output. Preserve source aspect with explicit \`Cover/Contain\` policy and no circular mask stretched into an ellipse; clamp minimum usable PIP width and safe output margins for 720/1080/4K. Compute one analytic four-corner SDF mask with antialiased border and opacity; **no four separate shader variants and no second full-frame render pass**. UI example reflects actual resolved geometry. During recording settings are next-session only initially; never silently change live GPU state.

Legacy settings migration: saved \`BottomRight\` from inactive pre-camera settings is NOT proof of a user-selected camera position. A versioned profile migration defaults to **TopRight** matching observed output; only explicit new-profile choices persist. Native bridge schema evolves additively (version/size/capability), not field repurposing that breaks old binary.

Tests: 4×4 masks/placements, 720p/1080p/4K and portrait outputs, source 4:3/16:9, both device and no-device states, edge/corner antialias, margin bounds and persistent reload. Pixel-level GPU tests plus a real camera-in-MP4 visual comparison.

## 5. F4 — Settings "Logo Overlay": independent cached GPU graphic

Add a **new Settings navigation page "Logo Overlay"** beneath Camera, with Enable, Choose image, placement \`TopLeft|TopRight|BottomLeft|BottomRight|Center\`, relative size, margin and opacity. Use PNG with alpha as minimum accepted format; enable WebP only after its decoder/dependency and alpha correctness are validated. No animated image scope initially.

Canonical immutable \`LogoOverlayProfile\` does not affect \`CameraOverlayProfile\`, ArZoom or ArVisual. Validate input path, decoded dimension, maximum bytes and alpha; decode and premultiply once on settings change, then upload to retained GPU SRV on approved graphics worker. Use explicit sRGB/premultiplied alpha semantics and finite positions. Changes are transactional: replace old cached texture **only** after new GPU resources and metadata are ready. No file reads, decode, upload or texture allocation on each 60fps frame; logo is a small independent layer in existing compositor draw after desktop grading, with deterministic ordering relative to Camera/keyboard effects.

Bad/missing logo -> disable just its layer with precise message; desktop+camera+A/V recording continues. Validate transparent edge halos, alpha 0/50/100%, texture change/missing file/corrupt file, output resolution scaling, overlap with face-cam and colorimetry. Do not ship feature as only a mock Settings menu.

## 6. F5 — one integrated release-candidate acceptance, not an endless probe loop

The only primary tester is the **integrated A/V** \`arssyut-AUDIO-ENABLED-TEST-win-x64\` artifact containing \`Arssyut.UI.exe\`, built on the exact PR head by existing pinned-FFmpeg Windows job. User unzips in a fresh directory and **double-clicks**. No PowerShell, \`--engine\` probe, video-only UI artifact, or separate codec installer. Camera and audio controls use the native token/bridge. Do not merge PR #115 Pause merely to claim comprehensive A/V: its per-seam hardware gate is independent.

Required acceptance:
- GUI startup and Settings responsive; native camera device selector selects real device with privacy indicator.
- Continuous live face-cam before/during/after record; actual webcam visible in a played MP4 with each of 4 placements + masks exercised across fixtures.
- Display 1080p60, one H264 + AAC 48k stereo stream, System+Mic + camera + independent logo, live Mic Mute, hotkeys and ordinary stop; \`status_code=0\`, \`audio_staging_overflows=0\`, no source/audio-program packet losses, no video skip/backpressure/camera GPU errors in nominal profile.
- Camera OFF still produces the accepted desktop+dual-audio output; logo-only and camera-only both work. No visual black flash when changing source/overlay; no stale preview after unplug/Off.
- 100 Start/Stop cycles, camera denied/busy/unplug, output path failure, app Close, device loss/error, and 30–60 minute resource/clock soak. Audio 60-minute drift and original acceptance thresholds from P7A lock still apply; do not lower them for camera.
- Match nominal per-frame CPU/GPU/compositor p95 and memory against A/V checkpoint; justify any increase from **actual real hardware** counters. No unbounded growth, no reinitialization every frame, no second camera open.
- Inspect real MP4 streams and listen for AAC quality; automatic CI/WARP verifies contracts but cannot replace hardware signoff. Preserve private media locally; GitHub issues receive only sanitized diagnostics summaries.

## 7. CI strategy — fewer cycles, stronger evidence

**One small doc-only commit FIRST**, checked in on Draft PR #120 before implementing source. Do not run code/build experiments to "validate" an architecture text change. Existing GitHub CI may trigger automatically on every branch push; this is a workflow behavior, not proof of implementation.

For implementation, use **local/static checks + deterministic unit/failure tests before pushing**, then batch a coherent atomic source+test+docs update. Preferred gate hierarchy:
1. cheap portable unit/geometry/config schema + packaging contract;
2. targeted Windows CTest for audio stop burst, webcam ownership/state, preview freshness, all overlay shapes and logo alpha;
3. existing one Windows Native Release job, plus **single [audio] pinned-FFmpeg integrated A/V GUI build** (no redundant video-only product build for these changes);
4. Windows process real \`Arssyut.UI.exe\` launch smoke (must remain running, no early .NET exception), Settings controls and native-binary ABI/packaging verification;
5. after exact-head CI green, one or few **task-specific** real Windows MP4 tests, not repeated unrelated probe runs.

To reduce unnecessary CI on documentation-only updates, plan a **future separately validated workflow \`docs-only\` diff classification** (fast markdown/link check; skip costly native/audio jobs without bypassing branch protection); do not rush CI YAML restructuring while F1 stability is unresolved. Preserve exact source-hash baseline guards and required checks. No forced green results, \`continue-on-error\`, blanket skipped audio contracts or downgrading GUI failures to warning. On failure, read exact native/.NET logs, batch fixes, then rerun once. Always report immutable SHA, CI run, matched artifact ID/digest and actual hardware evidence separately.

## 8. Execution handoff / no scope drift

| Gate | Status at documentation signoff | Owner | Required evidence |
|---|---|---|---|
| F1 — Stop/Finalize | **P0, NOT FIXED** | \`AudioProductRuntime\`, canonical assembler, RecorderSession first-error snapshot | deterministic burst at Stop + real dual A/V MP4, \`StageFull=0\` |
| F2 — Continuous Preview | **P0, NOT FIXED** | single \`WebcamCoordinator\`, GPU consumer handoff, native preview bridge | increasing preview sequence before/during/after recording |
| F3 — Camera Settings | **NOT IMPLEMENTED** | versioned product profile → ABI → compositor | 4×4 pixel mask/position matrix + real MP4 |
| F4 — Logo Overlay | **NOT IMPLEMENTED** | independent image/profile authority → retained GPU layer | alpha + reload/missing file + played MP4 |
| F5 — integrated QA | **NOT ACCEPTED** | exact-head A/V CI + Windows physical user acceptance | 15s → 100 cycles → 30–60m; no data loss |

Before any implementation: read [AGENTS](../AGENTS.md) and both ADRs, refresh PR #120 vs \`main\`, protect the accepted A/V checkpoint and confirm the current source tree. After F1 record a separate exact-green checkpoint; only then F2; keep implementation incremental but **coherent** (no one-line trial/error pushes). Documentation status does not authorize merging Draft PR #120 to \`main\`, releasing, or labelling the webcam feature complete.
