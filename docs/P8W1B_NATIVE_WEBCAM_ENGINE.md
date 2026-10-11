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
