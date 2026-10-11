# P8W1A — webcam GPU capability gate (native Windows)

Status: **implementation candidate, not webcam recorder support**  
Baseline: `main@72bef9a1b8f6dcaddad3a1693b6f9dedacfdda9a` (P8W0 ADR merged)  
Tracks: [#116](https://github.com/masarray/arssyut/issues/116), [ADR-010](adr/ADR-010-native-webcam-overlay.md)  
Pause [PR #115](https://github.com/masarray/arssyut/pull/115): Draft; hardware per-Resume acceptance pending.

## Why this slice comes before production capture

`MFEnumDeviceSources` exists, but that only establishes webcam enumeration. The native camera's pixel formats, DXGI sample ownership, adapter compatibility and driver-lifecycle behavior are **hardware-dependent**. Assuming that an MF Source Reader is zero-copy, or letting a CPU-backed camera silently enter the 60-fps compositor, violates `AGENTS.md`.

P8W1A adds a small, native **diagnostic-only** executable:
- Reuses `app::enumerate_cameras()` and opaque camera selection **by numeric index for QA**; never emits symbolic links or friendly names.
- Uses the recorder's `D3D11Device::create(HardwareOnly)` helper and a Media Foundation DXGI Device Manager.
- Enumerates native media types; deterministic policy prefers bounded NV12 1280×720 @30fps; supports probing YUY2/RGB32 only when native, never MJPEG/CPU decode.
- Opens exactly one selected camera with MF asynchronous Source Reader callback. One outstanding sample read; no new application thread; finite timeout (5 s startup, 2.5 s subsequently) and 3–60 samples.
- Checks each obtained sample for `IMFDXGIBuffer`, texture device and actual adapter LUID **without** calling `Lock`, `Map`, `ConvertToContiguousBuffer` or performing texture CPU readback.
- Records only numeric media mode, counts, monotonic sample times and HRESULT. Does not persist frame data. A `gpu_candidate` result indicates source-reader feasibility on that specific camera, **not** complete capture/compositor acceptance.
- Runs device-independent candidate-selection tests inside existing CTest, native CLI `--help` in the existing Windows build, and uploads one tiny diagnostic EXE artifact. No extra video-only product build or product media-code changes.

## Manual GPU/device QA (must run on actual Windows webcam)

Download `arssyut-P8W1-WEBCAM-GPU-PROBE-win-x64` from the exact passing PR CI run. In PowerShell inside its extracted directory:

```powershell
.\arssyut_p8w1_webcam_gpu_probe.exe --list
.\arssyut_p8w1_webcam_gpu_probe.exe --capture 0 12
```

If more than one camera exists, repeat `--capture 1 12` etc. Test the intended USB/built-in webcam while connected and with Windows Camera permission enabled. Repeat with Camera access denied, camera occupied by another app, and after unplug/replug. The tool opens a camera **only** with explicit `--capture`; `--list` merely enumerates.

**Exit codes:** 0 = list successful or captured GPU-backed samples on the recorder GPU adapter with strictly nondecreasing timestamps and no missing samples; 2 = argument/tool failure; 3 = camera index missing; 4 = unsupported hardware path/driver/permission/format/sample (needs investigation). A clean GPU candidate result must be repeated with the actual target device/profile and later soak/recovery tests. A source that supplies system-memory buffers **does not pass** this GPU gate even if its camera app looks fine.

Copy the JSON stdout to the issue comment; it deliberately contains **no** raw webcam IDs, images or audio. Do **not** upload webcam video to GitHub.

## Acceptance and next implementation owner

1. Exact SHA Windows CI passes: CTest including new policy tests, no GUI/encoder/audio regressions; available artifact provenance.
2. Real hardware reports a negotiated native camera mode and all accepted samples DXGI-backed on the expected adapter. If there are no compatible modes/frames, document device/driver and evaluate WinRT MediaCapture GPU interoperability; **do not add a silent CPU fallback**.
3. After GPU feasibility, P8W1B may implement the owned `RecorderSession` camera source with **fixed small latest-wins texture/sample leases**, QPC timestamps, lifecycle/drain contracts, privacy/device-loss counters and deterministic mock tests. No `RecorderConfig` camera enable flags, UI enablement or MP4 webcam claims before real backend validation.
4. P8W2 GPU single-pass output-space overlay, P8W3 controls/preview, P8W4 real Windows acceptance follow ADR-010. Webcam never reimplements ArZoom SmartCamera viewport, DSP, AAC or the native MF writer.

**Hard stop:** Camera open hangs beyond the diagnostic sample timeout, CPU surface only, adapter LUID mismatch, device unplug deadlock, crash, resource growth or an audio regression means this path is not accepted. P8W1A is a capability gate, not evidence of a shipped camera feature.

## P8W1A diagnostic refinement — real Windows webcam feedback, 2026-10-11

A Windows user confirmed one webcam with native NV12 1280×720 @30 Hz on the recorder's D3D11 adapter. An initial 12-read run reported **11/11 delivered frames DXGI-backed, 0 CPU frames, monotonically increasing timestamps**, plus one null sample; initial `status=review` and `empty_samples=1`. This is a **strong positive GPU compatibility result**, not yet zero-gap capture acceptance.

Per [Microsoft IMFSourceReaderCallback::OnReadSample](https://learn.microsoft.com/en-us/windows/win32/api/mfreadwrite/nf-mfreadwrite-imfsourcereadercallback-onreadsample), a null `pSample` can accompany `MF_SOURCE_READERF_STREAMTICK`, indicating a **real gap in source time**, or `MF_SOURCE_READERF_ENDOFSTREAM`. The first probe omitted stream flags from diagnostics, so it **cannot retrospectively classify** that single null callback. Do not assume it was a stream tick, a lost frame, or a hardware problem.

Updated tool classifies and counts Source Reader callbacks, adding `completed_reads`, `stream_ticks`, `unexpected_empty`, `stream_changes`, `reader_errors`, `end_of_stream`, `last_flags` and `gpu_compatible` alongside existing numeric output.

- `gpu_candidate`, exit 0: at least three actual samples, all on the matching GPU adapter, no gaps, no unexpected nulls/type changes/read errors, monotonically ordered.
- `gpu_path_with_stream_gaps`, exit 4: GPU sample feasibility confirmed, but source reader reported one or more stream ticks. A tick is an actual time gap; it does **not** prove full delivery quality.
- `review`, exit 4: flags inconsistent, CPU-backed sample, renegotiation, EOS, read error, unexpected null, or insufficient GPU samples. Conservative by design.
- Probe reports callback counts, not measured frame loss, real-time jitter, CPU utilization or camera-overlay readiness. `last_flags` is the numeric last callback mask and is not a personal device ID.

New Windows-only CTest exercises every documented flag, combined flags, null samples, unknown bits and priority ordering. This change touches **only** the diagnostic executable, test and documentation—not RecorderSession, GUI, writer, or audio.

**Next hardware run:** download the new probe artifact from the **new exact-head green PR #119 CI**, not the prior `056cec3` artifact. Run `--capture 0 30` three times and share each JSON line. Repeated stream ticks motivate longer capture and source timing tests before production ownership. Continue testing no-camera/busy and privacy failure modes before P8W1B.
