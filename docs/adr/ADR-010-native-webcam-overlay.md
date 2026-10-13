# ADR-010 — Native webcam face-cam overlay: one recorder, one GPU compositor


> **Historical P8W0 ADR / capture probe choice, not final implementation authority.** For the now hardware-verified Media Foundation NV12 webcam, single-source ownership, preview→record→preview, true output-space Camera/Logo Settings and F1 audio stop blocker, follow [ADR-011 (2026-10-12)](ADR-011-webcam-source-continuity-and-output-overlays.md) and [F1–F5 execution plan](../P8W_F1_F5_EXECUTION_PLAN_2026-10-12.md). Old statements saying capture is provisional or product camera is missing describe the original 2026-10-11 audit point, not today's working PIP MP4.


Status: **P8W0 source audit complete; capture API selection is PROVISIONAL until Windows device/adapter probe.**  
Date: 2026-10-11  
Audit source: `main@f9833f0493ce46e13e2a5ca5990e94a04ec15b57` (documentation-only successor of accepted microphone-mute baseline `1d5ed67bcb47466c6cd210145d5f8fd80e90e526`)  
Tracks: [Issue #116](https://github.com/masarray/arssyut/issues/116). Pause/Resume [PR #115](https://github.com/masarray/arssyut/pull/115) remains **Draft, hardware-per-seam acceptance pending**.  
Prerequisites: [AGENTS.md](../../AGENTS.md), [ADR-008](ADR-008-native-ui-bridge.md), [P3R parity](../P3R_ARZOOM_PARITY.md), [PRD](../PRD.md), [canonical handoff](../HANDOFF_2026-10-11_PAUSE_WEBCAM.md).

## 1. Reality audit — what exists and what does not

| Owner / path | Verified implementation at audited main | P8W implication |
|---|---|---|
| `src/app/device_catalog.cpp` | `enumerate_cameras()` uses `MFEnumDeviceSources` and returns symbolic links/friendly labels. **Enumeration only**; no open/read of video samples. | Reuse its device identity; do not create C# / WMI camera enumeration. |
| `src/bridge/native_bridge.cpp/.h` | `NativeBridgeContext` has a camera list, generation-scoped device tokens and one `RecorderSession`. ABI v11 has a camera token and START_CAMERA flag, but the bridge explicitly returns UNSUPPORTED for camera. | Resolve selected token to native symbolic link under bridge mutex; reject stale/missing identities. Extend ABI additively with an explicit version/size contract, not a C# second authority. |
| `src/ui/Arssyut.UI/RecordingControllerWindow.axaml.cs` | Native Camera button is intentionally disabled, with accurate backend-not-wired tooltip. | Do not enable until native camera state/capability are real. |
| `src/ui/Arssyut.UI/SettingsWindow.axaml.cs` and `Preview/ProductSettingsStore.cs` | Camera placement and a **string preference** exist; the saved default device name is a literal `USB2.0 HD UVC Webcam`. Placement uses a UI badge, not recorded camera pixels. | A name is not a device handle; remove misleading synthetic/default device authority when wiring P8W3. UI preview must be tied to native capture state. |
| `src/platform/windows/video/native_video_pipeline.cpp` | Samples newest retained WGC source, renders each due output frame even without new desktop source, then submits the compositor texture to one writer through `RecorderSession`. | Webcam is a second **input source**, never a second output scheduler or writer. |
| `src/platform/windows/graphics/d3d11_compositor.cpp/.hpp` | One compositor owns retained desktop input, retained keyboard overlay and one output RTV. Current `ps_main` grades desktop, adds spotlight/clicks, then keyboard; it binds two SRVs. | Integrate webcam in the same output draw (additional optional SRV + constant geometry), after desktop grading/presentation and UI overlays, not a separate full-frame pass. |
| `src/app/recorder_session.cpp/.hpp` | One session owns D3D11 device, WGC, `NativeVideoPipeline`, media timeline and `MfH264Mp4Writer`; audio uses the session's writer. | Session owns webcam lifecycle and state snapshots; no webcam track and no second MF Sink Writer. |
| `src/presentation/arzoom_camera_adapter.hpp` | ArZoom `PresenterAwareSmartCamera` controls desktop viewport. | **Not webcam.** Face-cam is output-space anchored, unaffected by desktop pan/zoom. |
| `.github/workflows/ci.yml` | Windows-native tests and an integrated audio-enabled GUI lane exist. | Extend the existing integrated lane; do not invent a redundant video-only product build. |

No real webcam frames, preview or MP4 webcam overlay have been verified. This ADR must not be cited as implementation or hardware acceptance.

## 2. Authority and dataflow decision

```text
Native device catalog (Media Foundation camera IDs)
    -> generation-scoped camera token (ABI; never use friendly name as identity)
    -> one RecorderSession-owned WebcamCapture lifetime
         -> selected camera + format + native privacy/error state
         -> canonical monotonic/QPC timestamp
         -> bounded latest-frame GPU handoff (replaceable, never FIFO backlog)
    -> existing NativeVideoPipeline 60-fps output cadence
    -> existing D3D11Compositor / same output RTV
         desktop through ArZoom/ArVisual -> click/keyboard -> webcam overlay
    -> existing NV12 conversion + ONE H264/AAC Media Foundation writer
    -> ONE MP4
```

**Hard invariants:**

1. `RecorderSession` is the sole authority for camera requested/actual state, activation, refresh and teardown. A source worker/callback is an owned subcomponent; UI polls the native snapshot rather than inventing status.
2. Camera input timestamps use `MonotonicClock`/QPC in the same physical domain as WGC/audio. Webcam never defines MP4 frame PTS, resumes its own media timeline or modifies AAC.
3. One D3D11 device/adapter is preferred. Never call the recorder's D3D11 immediate context concurrently from a capture callback. Transfer textures with explicit device/adapter compatibility and GPU synchronization. The recorder video worker alone binds camera SRVs and draws.
4. A fixed, small set of retained GPU resources and a replaceable **latest-only** slot. No unbounded queues, repeated full-frame CPU copies, per-output-frame texture creation, lock held while waiting on camera/driver, or blocking callbacks.
5. Camera preview and recording must share a **single actual device open** when concurrent; the preview is a bounded, throttled consumer of native camera truth, not a second managed capture pipeline. Default capture is OFF until the user explicitly enables it.
6. Once camera is toggled OFF or the session stops, release/deactivate hardware; don't keep recording a hidden webcam. A visible, truthful privacy indicator reports when capture is *actually* active.
7. Connection failure, user-denied privacy, busy device, unsupported format, camera unplug, QPC discontinuity or graphics device loss make camera **unavailable**, never a fake stale picture. Existing desktop/audio recording continues when optional camera fails (unless a user-selected fail-start policy later explicitly requires camera).

## 3. Capture API shortlist and acceptance-gated selection

**Candidate A (preferred to probe first):** Media Foundation asynchronous Source Reader from native `IMFActivate`, configured with `IMFDXGIDeviceManager` from the recorder-owned D3D11 device and `MF_SOURCE_READER_D3D_MANAGER`. Negotiate camera formats in an ordered profile (native NV12/GPU-friendly first; other supported formats only after measured GPU conversion). Acquire DXGI-backed samples when the device/driver actually supports them, and use an owned callback/lifecycle barrier before texture handoff.

**Candidate B (fallback to evaluate, not silently ship):** WinRT `MediaCapture`/`MediaFrameReader`, with explicit Windows privacy/device lifecycle and D3D adapter interoperability. Consider it only when Candidate A lacks the supported hardware/access behavior required for accepted camera devices. WinRT integration must not instantiate a second graphics/compositor/mux engine.

**Reject for production hot path:** C# camera capture, DirectShow/CPU full-frame `ReadSample` conversion, MJPEG decode to CPU BGRA every frame, unrestricted callback queues, or a second media writer. Software-only/adapter-incompatible sources fail closed until a separately budgeted, approved path exists.

**The choice is not yet proven.** Before implementing capture or calling a path “zero-copy,” build a Windows-only diagnostic probe on real devices (no need for a second product binary): record device ID hash (never raw symbolic link), consent/permission result, native media types, source-reader output subtype, adapter LUID, whether DXGI texture extraction succeeds, peak retained textures, frame cadence and teardown timing. Benchmark at 720p30 and 1080p30 where supported, plus camera unplug/denied/busy. Select A only with observed GPU sample ownership and reliable recovery; otherwise document why B is selected. No unsupported automatic CPU fallback.

## 4. Native lifecycle contract (proposed)

```text
Off -> Enumerated -> Opening -> Active -> Closing -> Off
                        |         |
                        v         v
                   Unavailable <- Faulted
```

- `Off`: no camera device handle; preview blank; camera request defaults OFF.
- `Opening`: validate generation token once, copy the symbolic link under bridge lock, release the lock before any MF COM/API call; handle stale device refresh separately.
- `Active`: camera source callback delivers timestamped GPU frames to one replaceable, generation-tagged handoff. A callback never renders, encodes, waits for graphics completion or performs filesystem I/O.
- `Unavailable/Faulted`: explicit reason/status, discard old retained frames and choose a bounded retry policy (no spin/reopen loop); optional webcam failure does not destroy the H264+AAC recording.
- `Closing`: disarm callbacks -> stop source -> wait for owned callbacks/leases to complete without holding UI/bridge/render locks -> release MF/D3D resources. Stop while paused and repeated Start/Stop use the identical cleanup path.
- Camera preview before recording may own the single webcam session in the bridge context; transferring control to the recorder must never open the same device twice. Define one explicit owner/transfer policy in P8W1, with a deterministic unit test for simultaneous preview/Start.

**Pause/Resume compatibility (do not implement on old `main`):** Keep camera capture bounded while paused, but hide/expire its last frame on each Resume generation change until a **post-resume** sample is available. Output timestamps remain assigned only by the existing compressed virtual QPC when PR #115 is accepted. Stop while paused releases the camera deterministically. Do not import/merge PR #115 into this camera branch before its new real recording and per-seam approval.

## 5. Compositor, geometry and UI contracts

- Use one output-space normalized face-cam rectangle (size, anchor and margin), independent of ArZoom's desktop-content viewport. Validate finite values, keep clipping inside the final output aspect, and use one deterministic crop mapping for 4:3, 16:9 and portrait cameras.
- A camera texture/SRV and masked rounded-rect alpha should be resolved within the **existing single fullscreen D3D11 draw**, after ArVisual desktop color processing and keyboard; the camera image should not inherit desktop grading/zoom. Add optional mirror, border and shadow with bounded shader math, not a second fullscreen render pass.
- Non-camera mode must remain pixel-identical to the previously accepted compositor; use a disabled shader branch and null camera resource without affecting existing constants. Manage SRV/RTV hazards and texture-generation changes transactionally.
- P8W3 Settings source selector must bind `NativeDeviceItem.Token`, never `CameraDevice` display text or a hard-coded `USB2.0 HD UVC Webcam` label; invalidate stale choices on refresh. The Camera floating toggle and hotkey both dispatch one native command; button state reflects actual `Opening/Active/Unavailable/Off`.
- Privacy indicator is shown only from native actual capture state. The Avalonia preview may consume a throttled native preview surface, but may not initiate independent WGC/MediaCapture or create a second camera timeline.

## 6. Resource and instrumentation budget — targets, NOT measured claims

Measure *incremental* cost against a matched camera-OFF 1080p60/System+Mic baseline. As a provisional design target, webcam should add no more than **2.0 ms p95** to video-compositor GPU time at a supported 720p webcam profile; the limit must be validated on real devices and revised only with cited evidence. No extra output-frame render pass. Maintain a fixed (initial target: <=3) camera frame/texture slots; profile any decoder-owned buffers separately rather than pretending they are under Arssyut control.

Required non-sensitive telemetry: camera state/reason code, requested vs negotiated format, expected/observed capture FPS, last accepted camera QPC, frame age, published/replaced/discarded/stale samples, unsupported/permission/device-lost events, queue/slot high-water, D3D copy/conversion path, compositor CPU/GPU p95 delta, peak private bytes, resource generations and callback-drain duration. **Do not** persist raw symbolic links or webcam pixels in public diagnostics.

## 7. Deterministic / failure / real-hardware gates

| Slice | Gate |
|---|---|
| P8W0 (this change) | Source ownership audit + provisional ADR + device-capability probe contract. **No production camera claim, no source pipeline modification.** |
| P8W1 | MF capability benchmark; device token stale/refresh cases; open/close x100; denied/busy/disconnect and re-open; callback-lifetime race; bounded latest-only handoff; no per-frame heap/CPU full-frame transfer; mock camera source tests. |
| P8W2 | One compositor/output writer; deterministic static source while face-cam moves; 4:3/16:9/portrait crop; mirrored vs unmirrored; camera off bitwise-equivalent desktop frame; lost/recovered GPU surface; zero extra compositor output passes; 1080p60 real MP4. |
| P8W3 | Native-snapshot toggle/hotkey equality, stale selector, preview/record handoff without double-open, truthful privacy on/off and errors; no hard-coded camera name; capture-excluded UI. |
| P8W4 | 30–60 minute soak; 100 Start/Stop cycles; device hot-unplug/re-plug; dual/system/mic-only audio; camera toggle during recording; Pause/Resume with 3–5 deliberate joins and Stop while paused **after #115 hardware acceptance**; H264 frame and AAC continuity/AV sync, resource p95 and peak memory; matched installer/portable SHA and real user MP4 playback review. |

**Stop conditions:** Any new audio loss, A/V drift, encoder regression, black/stale camera frame after Resume, microphone pool exhaustion exacerbation, unbounded growth, source/finalizer deadlock or additional writer requires rollback/investigation before progressing. Windows CI is a compile/regression gate, not evidence of webcam permissions, native GPU texture path, or visual acceptance.

## 8. Integration plan / isolation

1. Merge or accept P8W0 ADR independently from the current accepted `main`. Do not advance `main` engine via this documentation milestone.
2. Preserve Draft PR #115 at its passing source SHA until a **new** Windows dual-audio MP4 + diagnostics with per-Resume indices is analyzed and the user accepts each seam. Do not release RC3/stable.
3. Start P8W1 from the latest green `main` (after ADR), independently of Pause. One `RecorderSession` integration owner maintains the webcam state; avoid two competing branches editing it.
4. Implement P8W1 hardware/capability gate, then P8W2 render only after GPU handoff is proven. Add deterministic and failure tests to the **existing** Windows CI. Mark webcam product-ready only after P8W4 hardware QA.
