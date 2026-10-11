# P8W2B — real camera selector, GPU preview, verified MP4 diagnostics

Status: engineering implementation candidate. This is NOT a green hardware acceptance before checking the resulting MP4 and thumbnail in the user's Windows GUI.

## Evidence and actual root cause

User supplied one screenshot, the real desktop MP4 `Arssyut-20261011-190742-pixel-accurate.mp4` and the matching diagnostics. MP4: H.264 1920x1080 60fps, 529 frames / 8.81665 s, 0 skipped, 0 encoder backpressure, desktop scene visible across extracted 1/3/5/7-second frames and **no webcam overlay**. Diagnostics are desktop-only and contain **no webcam-source/compositor counters**. This does not prove GPU device absence; the previous native camera hardware probe had already captured 138 DXGI-backed frames in five seconds.

Code audit root causes: camera ToggleSwitch declared in `MainWindow.axaml` without `Click="CameraToggle_OnClick"`, so it could look red/ON without setting `_session.CameraEnabled` or submitting native `START_CAMERA`. `CameraDeviceComboMain` was populated by MFEnumDeviceSources but had no `SelectionChanged` handler and was disabled again by `ApplyProductCapabilitySurface`. `CameraPreviewImage` was explicitly hidden, had no frame source or binding, and showed only a static camera icon. This is missing product wiring, not a reason to endlessly probe the camera hardware.

## Reference-first architecture

Official OBS camera docs: https://obsproject.com/kb/video-capture-sources . OBS DirectShow source: https://github.com/obsproject/obs-studio/blob/master/plugins/win-dshow/win-dshow.cpp ; review `EnumVideoDevices`, `DeviceSelectionChanged`, `ActivateClicked`, `OnVideoData`, `Deactivate` and `obs_source_output_video2`. Relevant patterns: real device list, explicit source activation, asynchronous frame delivery, release source on deactivation and clear stale frames. No OBS GPL source code is copied. Media Foundation remains the proven GPU camera input backend here; the actual selected camera is generation-checked via native symbolic link, not display name.

- **One physical camera owner:** `WebcamPreviewSession` runs only while Camera is explicitly ON in idle main UI and is forcibly stopped **before** `RecorderSession` opens the selected camera. This prevents device conflicts and camera privacy lamp leaks.
- **Preview:** reuse exact `WebcamCaptureSource` and three-slot `WebcamGpuFrameSlot`, hardware DXGI GPU-only; use `WebcamNv12Surface` and a small direct HLSL NV12->Bgra shader to scale to a fixed **192×108** render target; read back only this 82,944-byte thumbnail **10 fps at most**. No CPU full-HD frame, no independent encoder, no other camera backend. UI updates one reusable Avalonia `WriteableBitmap` only on new frame sequence; placeholder only when OFF/Opening and explicit error when unavailable.
- **Device list:** the visible ComboBox is the actual native list; device-token selection is processed and selection changes restart preview with one owner. Both `CameraToggle` and ComboBox are enabled in `ARSSYUT_WEBCAM_PREVIEW=1` internal builds. Audio and webcam preview flags are independent; public build still Camera OFF.
- **Recording:** `RecorderSession` reopens camera after preview releases device. New numeric diagnostics in `.diagnostics.json`: `webcam_requested`, `webcam_received`, `webcam_published`, `webcam_ticks_startup/streaming`, `webcam_adapter_mismatch`, `webcam_foreign_device`, `webcam_new_gpu_frames`, `webcam_rendered_slots`, `webcam_copy_failures`, `webcam_hresult`. This distinguishes an unwired UI flag, camera open failure and GPU shader/compositor issue without new serial probe tasks.

## CI / tests

Windows-only CTest `arssyut_webcam_preview_contract_tests` verifies no synthetic frame when off, fail-closed empty symbolic link and production preview VS/PS shader compilation even on camera-less CI runners. Existing synthetic NV12 compositor test still exercises GPU overlay and camera-off behavior. Full Windows build and Avalonia contract must be green on the same exact SHA before user testing. All non-webcam WGC/audio/writer source blobs unchanged; historical guard locked to exact updated webcam-specific RecorderSession/bridge/pipeline blobs.

## Real user acceptance

Download the **Avalonia GUI application artifact**, not the probe. Extract entirely, launch in PowerShell:
```powershell
$env:ARSSYUT_WEBCAM_PREVIEW = '1'
.\Arssyut.UI.exe
```
Select real webcam using **visible Webcam ComboBox**. ON must display moving live image inside GUI within device startup interval. Select Display, Record 15 seconds, Stop, play MP4. Face-cam must appear upper-right on MP4; `.diagnostics.json` must show `webcam_requested=true`, `webcam_received>0`, `webcam_new_gpu_frames>0`, and `webcam_rendered_slots>0`; no adapter mismatch/copy failures. Camera OFF must restore pure desktop, and LED must turn off when closed/after Stop. Do not run `--engine` again. This is video-only QA; eventual mic/system audio requires separate accepted integrated AV build.

## Unaccepted issues

Native camera may return foreign-device same-adapter samples, unsupported NV12 SRVs, hot unplug or camera busy on other apps. UI must surface failures and stop preview; do not fabricate image or claim camera works merely because desktop MP4 is ready. Rec.709 limited colorimetry matches known 720p NV12 test device but may not suit all UVCs. Long-running performance, runtime camera toggle, full A/V, camera device switch under recording, privacy controls and final release remain gated. Draft PR #119 remains unmerged; Pause PR #115 remains isolated.
