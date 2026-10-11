# P8W1B — native webcam GPU capture source and bounded frame handoff

**Status:** implementation / CI candidate. Webcam overlay in `Arssyut.UI.exe` is **not** yet enabled; PR #115 Pause/Resume remains a separate unmerged Draft. Prior field evidence from real Windows: 1 webcam, native NV12 1280×720 @30 fps, 11/11 delivered samples `IMFDXGIBuffer` on the recorder GPU, 0 CPU fallback, 1 null callback (cause still unclassified on old probe).

## Reference-first implementation

Sources: Microsoft [Capture with Source Reader](https://learn.microsoft.com/en-us/windows/win32/medfound/processing-media-data-with-the-source-reader), [async callback contract](https://learn.microsoft.com/en-us/windows/win32/api/mfreadwrite/nn-mfreadwrite-imfsourcereadercallback), [D3D Source Reader manager](https://learn.microsoft.com/en-us/windows/win32/medfound/mf-source-reader-d3d-manager), [Flush](https://learn.microsoft.com/en-us/windows/win32/api/mfreadwrite/nf-mfreadwrite-imfsourcereader-flush). Reuses P8W1A's native format candidate policy and recorder D3D11 GPU device; no detached second writer and no product source fallback to CPU.

**Ownership:** `WebcamCaptureSource` provides one opt-in camera source with a single MTA owner thread and Media Foundation Source Reader configured with an async callback and `IMFDXGIDeviceManager`. The callback merely publishes a completed `IMFSample` to the owning worker's single pending completion. No D3D11 immediate-context access or GPU processing in MF callback. The worker validates DXGI texture, recorder adapter LUID and native dimensions, stamps the sample using canonical `MonotonicClock`/QPC and publishes it into `WebcamGpuFrameSlot`.

**Bounded latest-frame semantics:** exactly three slots, each independently `Free/Writing/Ready/Reading`. Overwrites only obsolete unread samples and never invalidates a consumer lease. The slot retains both `IMFSample` and `ID3D11Texture2D` until release; producer coalesces instead of queuing obsolete frames. Consumer (future existing `NativeVideoPipeline`) reads only the newest slot; no per-frame heap buffer, CPU frame conversion, second 60-fps scheduler or encoder. Fixed count excludes Media Foundation/driver-owned surface pools.

**Lifecycle:** `start(device, symbolic_link, slot)` is nonblocking and transitions `Off→Opening→Active/Unavailable`. The bridge must resolve the symbolic ID from its already generation-checked token; never read a hard-coded display name. `stop()` requests shutdown, joins the owned worker and invalidates unread retained surfaces. `stop()` is not permitted on video/audio callback or UI hot paths; a malfunctioning camera driver can delay MF device shutdown, so bounded physical-driver cleanup is an unresolved hardware acceptance gate. Keep the source/slot/D3D lifetime ordered: consumers must release leases before the owning resources are destroyed; all capture fields are non-sensitive numeric counters.

**Fail-closed:** Source open/permission/format failures, adapter incompatibility, dimensions drift or async callback timeout terminate that optional camera source and mark `Unavailable` without touching the one desktop recording. A `STREAMTICK` increments a gap counter and does not produce a phantom frame. No CPU readback, MJPEG decode fallback, forced camera reopen, or stale camera image after stop.

## Regression and real device checks

`arssyut_webcam_capture_engine_tests` runs in the existing Windows CI. Synthetic Media Foundation samples backed by WARP D3D textures test 3-slot capacity, leases across replacement/invalidation, busy drop, deterministic replay, concurrent 15k publications and idle/argument lifecycle, with **no camera access** on hosted runners.

To test actual **engine** rather than the old DXGI discovery probe, download P8W1B diagnostic artifact from its passing exact-head GitHub Actions and run:

```powershell
.\arssyut_p8w1_webcam_gpu_probe.exe --engine 0 5
```

The numeric device index 0 is selected after `--list`; `5` means five seconds. Repeat three times and compare `status`, `received`, `published`, `replaced`, `busy_drops`, `stream_ticks`, `invalid_samples`, `adapter_mismatch`, `callback_timeouts`, `consumed`, `consumer_nonmonotonic`, `peak_occupied_slots` and `hr`. The tool opens the camera explicitly, consumes the **real** engine's bounded GPU leases, then closes it. The tool does not store/print raw video or source IDs.

Exit 0 `engine_gpu_active`: received >=3 with no gaps/adaptor failures and active at completion. Exit 4 `engine_gpu_with_gaps` means GPU frames were valid but source reported gaps; `review` means insufficient/invalid frames or lifecycle failure. These codes are candidate engineering gates, **not** MP4 webcam acceptance. Test busy/denied camera and unplug/replug separately; never run soak tests on CI shared runners.

## P8W2 and P8W3 still outstanding

Wire single `RecorderSession` source ownership through the versioned native bridge only once `--engine` is accepted on physical hardware. A separate P8W2 integrates NV12 texture into the **existing** D3D11 single-pass compositor with output-space cropping, privacy gating, and disabled-mode pixel parity. P8W3 enables camera device choice and shared preview only when native status is true; keep the final executable name **`Arssyut.UI.exe`**. Finally P8W4 validates real 1080p60 MP4+audio, no new microphone losses, hot-unplug, 30–60 minute capture, and Pause seams after PR #115 hardware acceptance.

**No claim of complete webcam UI/recording yet:** this milestone is the native GPU input engine and end-to-end hardware harness, not overlay pixels in MP4.

## P8W1C — source-cadence and GPU ownership hardening (2026-10-11)

### Verified real Windows evidence (three independent 5-second commands)

| Real run | GPU frames | Published | Replaced unread | Busy drops | Startup/steady ticks | Invalid samples | Adapter mismatch | Timeout | Consumer nonmonotonic |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 95 | 95 | 0 | 0 | **1 unclassified tick** | 0 | 0 | 0 | 0 |
| 2 | 71 | 71 | 1 | 0 | **1 unclassified tick** | 0 | 0 | 0 | 0 |
| 3 | 58 | 58 | 0 | 0 | **1 unclassified tick** | 0 | 0 | 0 | 0 |

All three captured NV12, 1280×720, 30/1. Frames were GPU backed; no evidence of CPU fallback or loss from our bounded mailbox. The original harness duration was measured from the **request to open**, not the first available frame. Therefore dividing 95/71/58 by five seconds cannot establish steady-state FPS, and the repeated single stream tick cannot be assigned to startup or steady-state from old counters. **Do not claim the camera performs below 30 fps or that the single tick is harmless without better timing evidence.**

### Reference-first engineering decisions (no borrowed code)

- [Microsoft Source Reader samples / native format selection](https://learn.microsoft.com/en-us/windows/win32/medfound/processing-media-data-with-the-source-reader): source timing and callback flags must be authoritative; opening time is not streaming time.
- [Microsoft MFCaptureD3D sample](https://learn.microsoft.com/en-us/samples/microsoft/windows-classic-samples/mfcaptured3d-sample/): capture, sample ownership and rendering are distinct responsibilities.
- [OBS Windows capture source](https://github.com/obsproject/obs-studio/blob/master/plugins/win-dshow/win-dshow.cpp): design reference for explicit activation, device-format logging, and replaceable unbuffered video; OBS uses DirectShow and has its own licenses, **so no OBS code was copied** and Arssyut retains its proven Media Foundation GPU path.
- D3D11 **adapter identity is not resource/device identity**. A texture created on another ID3D11Device cannot be bound directly to the recorder device merely because adapter LUIDs match. P8W1C fails closed and counts same-adapter/foreign-device samples for a later explicit GPU sharing path instead of claiming compositing readiness.

### Changes made to the same actual source engine

1. \`WebcamCaptureSource\` remains **Opening** until the first validated and published GPU frame. Separate \`source_open_ms\` and \`first_frame_ms\` from capture throughput. The previously reported raw 5-second frame totals are preserved as historical evidence.
2. Start-state \`STREAMTICK\` increments \`stream_ticks_startup\`; ticks **after** the first frame increment \`stream_ticks_streaming\` and fail a clean steady-stream status. Both remain in the total tick counter. A startup tick is not retroactively a good frame; it simply belongs to a different capture phase.
3. Inter-frame QPC gaps larger than 100 ms counted separately (\`frame_intervals_over_100ms\` and \`max_frame_interval_ms\`). Steady FPS is computed from **N−1 sample intervals and canonical 100-ns ticks**, not requested capture frame rate or process wall time. The calculation is tested against fixed 30fps and edge cases.
4. The same original native diagnostic executable's \`--engine 0 5\` mode now records five seconds **after** first valid camera frame (initial open bounded to eight seconds). It reports \`wall_ms\`, \`steady_ms\`, \`device_open_ms\`, \`first_frame_ms\`, \`observed_fps_x100\`, startup/streaming ticks and frame intervals. Its camera source, mailbox, D3D manager and worker are the actual P8W1B engine; no second product engine or duplicate worker.
5. All additional runtime data is bounded counters and timestamp atomics. No full-frame CPU copy, per-frame heap payload, new codec, new capture API fallback or extra CI lane.

### Hardware gate / next owner

After exact-head CI passes, run the **new** diagnostic artifact only once per desired scenario:

\`\`\`powershell
.\arssyut_p8w1_webcam_gpu_probe.exe --engine 0 5
\`\`\`

Compare measured \`observed_fps_x100\` against native 30/1 (3000 means 30.00 fps) and \`steady_ms\` near 5000; inspect \`stream_ticks_startup\` versus \`stream_ticks_streaming\`, frame interval maxima and device-open latency. The acceptance question is **steady-state delivery and safe device/texture ownership**, not a fabricated source “pass” obtained by ignoring events. After this gate, implement P8W2 output-space NV12 camera compositing in Arssyut's one existing encoder/output path, followed by P8W3 GUI bridge/UI (keeping \`Arssyut.UI.exe\` as the product), and P8W4 MP4/AAC soak/regression.

**Do not merge Draft PR #119 into \`main\` solely because Windows CI is green; do not merge unaccepted Pause PR #115.**
