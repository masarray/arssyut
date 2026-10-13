# ADR-011 — P8W native camera ownership, continuous preview and composited overlays

Status: **ACCEPTED FOR IMPLEMENTATION DESIGN; NOT hardware/release acceptance**
Date: 2026-10-12
Execution contract: [P8W F1–F5 plan](../P8W_F1_F5_EXECUTION_PLAN_2026-10-12.md)
Tracks: [Draft PR #120](https://github.com/masarray/arssyut/pull/120), [Issue #116](https://github.com/masarray/arssyut/issues/116).
Supersedes the **provisional webcam lifecycle and acceptance statements** of [ADR-010](ADR-010-native-webcam-overlay.md), not its GPU-first/one-compositor architectural invariants. Audio and native Smart Camera (ArZoom) remain distinct.

## Decision and rejected alternatives

**Decision:** extend the *accepted A/V-first* \`RecorderSession\` with one native \`WebcamCoordinator\` source/lifetime authority reachable through the native bridge. The physical webcam is opened exactly once while enabled. Preview-only and recording are different **GPU-consumer states** of the same source. Arssyut continues to have one WGC source, canonical QPC media clock, one video scheduler, one compositor output, one H.264/AAC writer and one MP4.

OBS's [source lifecycle API](https://docs.obsproject.com/reference-sources) is the **design reference** for activate/deactivate, source ownership and image/overlay types. Microsoft's [Source Reader contract](https://learn.microsoft.com/en-us/windows/win32/medfound/source-reader-attributes) is authoritative for Media Foundation DXGI manager, callbacks and hardware samples. No OBS code is copied or linked; Arssyut does not adopt a second OBS-like scene graph.

Rejected:
- GUI \`MediaCapture\`, DirectShow/CPU-BGRA parallel backend or one source per consumer: duplicate device owner, races, frame copies, license/maintenance and driver-busy risk.
- Giving preview and recorder independent \`try_acquire_latest()\` calls on the current \`WebcamGpuFrameSlot\`: it is **documented SPSC** (one consumer), so this silently violates a critical concurrency invariant.
- Supposing textures from different \`ID3D11Device\` objects are interoperable because their adapter LUIDs match: they are not; no unchecked cross-device \`CopyResource\`.
- Drawing the webcam or logo in additional full-screen render passes, encoding another video stream, or introducing a second recording clock.
- Resuming live thumbnail by preserving its last bitmap without a fresh-frame sequence/age check.
- Changing accepted audio DSP/mixer or increasing staging buffers merely to conceal overload.

## Owner/lifetime state machine

\`\`\`text
                    explicit camera ON
Off -> ResolvingDevice -> Opening -> PreviewActive
                                    |  |
                  Start recording ---+  +-> RecordingActive
                                        |
                         Stop recording -> PreviewActive
   explicit camera OFF / close / permission lost -> Closing -> Off
                     busy / unplug / GPU loss -> Unavailable -> Off
\`\`\`

\`NativeBridgeContext\` holds a single \`WebcamCoordinator\` with generation-tagged camera ID, actual MF source and GPU device/lease ownership. State changes are serialized on a bounded native control/worker path; no COM driver open/close/join while holding a UI dispatcher/bridge lock or inside a capture callback. \`RecorderSession\` retains ownership of WGC, A/V media zero, output cadence and writer. A user-selected camera is never reopened merely because the recording state transitions. On Stop, ownership of **GPU drawing** returns to preview; on Camera OFF, physical capture is stopped and its privacy lamp releases. Device selection by current generation-scoped symbolic-link token only; friendly name is display text, never identity.

The requested state and native **actual state** are distinct. State includes \`Off/Opening/PreviewActive/RecordingActive/Closing/Unavailable\`, epoch, source/adapter/device hash, last frame QPC, last publication sequence, last preview sequence, error origin and HRESULT, lifecycle timings, and subscription availability. Stale epoch/frame -> immediate preview clear. Driver reconnect/retry is explicit and bounded, not an infinite reopen loop.

## GPU work: one source, one *active* GPU consumer

The existing \`WebcamGpuFrameSlot\` stays one producer/one GPU-consumer and bounded (three slots); no accidental MPMC promotion. During PreviewActive the preview render worker owns the GPU read/resize and publishes only a bounded **tiny UI frame**; during RecordingActive the recorder's existing video worker is the **sole immediate-context renderer**, samples the webcam as part of the existing output cadence and publishes a tiny preview frame periodically. The producer MF callback only publishes retained \`IMFSample\`/DXGI texture lease + monotonic timestamp; it does not render, decode to CPU or wait. A handoff barrier ensures old preview GPU leases are retired **before** recorder takes GPU-consumer authority; the MF physical source itself remains open, and frame publication continues.

Device identity is explicit: choose a single D3D11 device whose owner/lifetime can span PreviewActive and RecordingActive, and provide a controlled device/context integration into RecorderSession **only when camera is enabled**. Validate exact \`ID3D11Device\` ownership and texture subresource, not only LUID. Never access a D3D11 immediate context simultaneously from preview and recorder threads. If code cannot reuse the same device without compromising accepted WGC/audio, stop and write a focused device-affinity decision with GPU interop failure tests; do **not** silently fall back to CPU pixels. Camera OFF must preserve the old graphics/writer path byte-for-byte where practical.

A preview-only source should render at **at most 10–15 Hz** into a retained ~192×108 BGRA staging surface; a two-slot staging/event-query/read-later pattern can avoid blocking the GPU. \`D3D11_ASYNC_GETDATA_DONOTFLUSH\` polling or equivalent readiness semantics; never synchronously read full camera frames on the recorder's 60-fps path. UI copies at most one newest ~83KB tiny frame per tick; writes \`WriteableBitmap\` only when \`sequence\` advances; no heavy format conversion on Avalonia dispatcher. Recording output remains 60-fps CFR by reusing the latest valid native camera texture without manufacturing capture frames.

**Freshness contract:** preview frame includes source epoch, sample sequence, capture timestamp and actual state. No new frame for >500ms (initial warning threshold; calibrate on real camera under poor light), source Off, or source Unavailable -> blank/placeholder with explicit reason; never label an old static bitmap “Live”. A disconnected camera cannot leave a stale face in the MP4; overlay expires from recorded output according to the same 500ms policy. The age timer is for **display truth**, not a reason to reset the writer/media clock.

## Canonical overlay model

Use one versioned \`OutputOverlayProfile\` resolved and validated once before session Start, with separately enabled \`CameraOverlayProfile\` and \`LogoOverlayProfile\`. Geometry is output-normalized (not ArZoom source-space), with placement, width fraction, margin, aspect handling, crop, mirror, opacity, border and mask. Any runtime changes are immutable snapshots adopted by the existing output worker; no UI code mutates constant buffers or GPU textures.

Camera placement: \`TopLeft, TopRight, BottomLeft, BottomRight\`. First accepted default **TopRight**, matching the current real recorded MP4; previous hidden/default \`BottomRight\` settings are **not** authoritative. Versioned migration must avoid surprise switching position. Camera mask: \`Circle\`, \`RoundedRectangle\`, \`DiagonalTopLeftBottomRight\` (TL+BR rounded, others square), \`DiagonalTopRightBottomLeft\` (TR+BL rounded, others square). Crop to maintain aspect; a Circle is circular in **output pixels**, not an ellipse resulting from output aspect. No invalid/NaN rectangle can reach shader constants. All masks use analytic corner SDF and bounded antialiasing, one existing compositor shader pass.

Logo overlay: independent source with PNG alpha as initial must-have (WebP optional, behind verified decoder); user file type/size/decoded dimensions/alpha validated on loading. Decode/reload outside realtime worker, upload to one retained GPU texture transactionally, premultiplied alpha blending with color-space sanity, no per-frame file I/O/decoder/texture creation; placement four corners plus Center, output-relative scale/margin and opacity. Logo visible independent of camera. Keep GPU bindings explicitly unbound after draw and existing keyboard/click/ArVisual layer order deterministic. ArVisual grades desktop content before overlays, not skin/brand overlay.

## Source truth and failure priority

1. A/V or writer failure: preserve a recoverable output and report **specific failure origin/phase**; do not call such a session Saved.
2. Optional webcam/device failure: clear camera from output/preview, preserve valid desktop+audio recording and set camera warning in diagnostics.
3. Invalid/corrupt logo or decode error: disable logo only, show validation error, never block A/V.
4. Any renderer/device ownership error: fail closed for affected overlay; if compositor itself cannot produce safe video, fail recording with exact status and bounded teardown.

This ADR authorizes the *design*, not that the above behaviors already exist in the binary. Exact F1–F5 tests/gates are in the execution plan. Do not merge Draft PR #120 or ship product just because CI is green.
