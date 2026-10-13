# P8W F2B — continuous single-source native webcam (candidate)

Parent: F1.2 source/CI-green `d610a6ccc0ac5bacf5ae1f65bbf8d49618bd64e9` (Draft PR #123). Reference: ADR-011. **Draft development, not physical camera acceptance or release.**

## Confirmed product problem
The user's 16.53s H264/AAC 1080p60 MP4 was healthy (991 encoded video frames, 269 new webcam GPU frames, 0 skipped/backpressure, 0 audio overflow, `result=ready`). The GUI still displayed a seemingly static webcam frame. Source audit confirmed `MainWindow` and `native_bridge.cpp` explicitly stopped independent `WebcamPreviewSession` on recording Start, then `RecorderSession` opened `WebcamCaptureSource` on a separate GPU device. UI could not receive live capture frames during recording. A retained Avalonia bitmap also needed visual invalidation after pixel writes. These are implementation causes, not device inability. No Pause test was performed in that 16.53s session (pause_count=0).

## F2B source/consumer authority
- `WebcamPreviewSession` now retains the existing Media Foundation `WebcamCaptureSource`, *exact* D3D11 device, one three-slot SPSC `WebcamGpuFrameSlot`, and one GPU thumbnail renderer across Preview->Recording->Preview.
- A bounded condition-variable handoff parks the preview GPU renderer and acknowledges that its GPU frame lease has been released **before** the recorder acquires the same device and SPSC mailbox. Camera source and DXGI sample producer never reopen on a normal transition. GPU preview rendering is serialized with the recorder's existing immediate context.
- The existing `NativeVideoPipeline` consumes camera frames on its one video worker, uses them in the existing output compositor, and publishes 192x108 thumbnails through its same active GPU worker at ≤10Hz. The recorder also consumes/publishes physical camera frames while **media clock paused** and during Armed preroll. No extra output clock or writer.
- Tiny preview readback uses two fixed-size retained staging buffers with `D3D11_QUERY_EVENT`, `D3D11_ASYNC_GETDATA_DONOTFLUSH` and `D3D11_MAP_FLAG_DO_NOT_WAIT`; a pending GPU copy never stalls the 60fps encoder. Failed tiny preview publication does not itself fail the MP4.
- `RecorderSession` uses the exact same D3D11 device and GPU mailbox whenever the bridge supplies a shared source, rather than assuming same-adapter D3D11 devices can share textures. Camera-OFF recorder retains independent accepted graphics creation and video path. A scoped lifetime guard returns GPU ownership only after recorder-owned locals/finalization are destroyed, including all early-exit paths.
- The bridge's preview Start is idempotent after successful Stop; OFF/refresh/close cannot stop a source while the recorder still actively consumes it. User Camera ON intent remains separate from unavailable actual state. GUI draws only new sequences, calls `InvalidateVisual()` after bitmap writes and supports native state 4 (RecordingActive). Unsafe mid-record camera device/toggle changes are explicitly rejected pending a later dynamic-source-switch feature.

## Tests, CI and real hardware
The existing native/portable CTest and QA plus pinned FFmpeg/AAC audio-enabled Avalonia package remain required. Device-free native test rejects phantom camera ownership when OFF; new QA validates one-source ownership, exact shared device, a single writer, nonblocking tiny readback, Pause publication, GUI state/bitmap refresh and unchanged A/V critical calls.

Real hardware must demonstrate:
1. First Camera ON, move a hand or face, observe changing GUI pixels/sequence before recording.
2. Start 1080p60 recording; GUI thumbnail must still move during recording, also during Pause; recorded PIP must move after Resume; after Stop preview must continue without re-opening camera, and privacy light must not blink on the transition.
3. Rapid OFF/ON when idle, switched device after Stop, no invalid stale preview, no hang/force-close.
4. Inspect MP4 and diagnostics for `status_code=0`, `audio_staging_overflows=0`, `audio_dropped_program_blocks=0`, webcam GPU copy errors 0, video skipped 0, correct AAC/Mic Mute and compressed Pause seam. Verify webcam physically on the chosen adapter.
5. Run longer soak and 100 synthetic/physical cycles before public release. **CI green is not USB camera acceptance.**

**Not in this milestone:** F3 Camera PIP geometry/masks/settings or F4 Logo Overlay; dynamic camera replacement DURING recording is intentionally unsupported rather than silently dropping the writer's camera. Keep PR Draft, main untouched.
