# P8W2A — One-pass native NV12 face-cam overlay in a real Arssyut MP4

**Implementation candidate:** Draft PR #119, isolated from PR #115 Pause and historical public releases. Do not merge to main or advertise hardware quality acceptance before the recorded MP4 has been played and tested on the user's PC.

## Grounded architecture / borrowed ideas, not borrowed source

Reviewed OBS Studio's Windows DirectShow source for input activation, explicit device choice, configured resolution/frame rate, async unbuffered input, and deactivation semantics: https://github.com/obsproject/obs-studio/blob/master/plugins/win-dshow/win-dshow.cpp . OBS source is GPL; **no OBS source code was copied**. The selected Windows Media Foundation + D3D11 backend was retained because the user's real hardware has demonstrated working GPU camera frames.

The accepted Arssyut pipeline remains WGC latest GPU desktop -> existing NativeVideoPipeline CFR scheduler -> existing D3D11Compositor -> existing H264/AAC Media Foundation writer -> **one** MP4. An additional optional WebcamCaptureSource owned in the same RecorderSession publishes a **bounded 3-frame GPU latest-wins slot**. On each already-due output frame, if there is a new webcam sample, the compositor copies its NV12 subresource **once per camera frame** into a small retained D3D11 NV12 texture; it holds separate R8 luma / R8G8 chroma shader views. The *same* output shader draw composites the camera in output-normalized upper-right coordinates, after desktop grade/ArZoom/click/keyboard. No CPU camera-frame copy, additional fullscreen output pass, detached scheduler, second video writer, or audio contract changes.

The shader uses limited-range Rec.709 YUV->RGB suitable for HD 720p NV12; output-space rounded rectangle / restrained border, aspect-preserving size. A future colorimetry milestone must negotiate/report BT.601/709, video-vs-full range for other cameras rather than assuming all webcam formats are identical. Current first hardware gate is exact user's 1280x720@30 NV12 GPU webcam. The desktop-only shader path remains disabled by default and must be pixel-identical.

**Failure safety:** Source always starts OFF and only opens for an explicit camera-enabled recording. Bridge validates its exact generation-scoped native MF symbolic-link token before passing a copied ID to RecorderSession. Non-GPU, other-device (even same adapter LUID), unsupported non-NV12, device-lost, or stale captures never trigger a CPU fallback. Camera frame expires after 500ms without a valid new frame and is removed from output; desktop recording continues. The privacy lamp must switch off after Stop. Mid-recording camera toggle and preview remain intentionally disabled; camera is configured before Start only in the first hardware QA. The camera source is *not* instantiated if its UI flag is off.

## INTERNAL QA ONLY — no public camera promise

Download **arssyut-VIDEO-ONLY-avalonia-win-x64** from P8W2A exact-head Windows CI (not the standalone webcam probe, and not the previously tested audio-enabled EXE). Unzip, then launch product GUI \`Arssyut.UI.exe\` with one explicit opt-in:

\`\`\`powershell
$env:ARSSYUT_WEBCAM_PREVIEW = "1"
.\Arssyut.UI.exe
\`\`\`

In the Arssyut main window choose the one real USB webcam in the native selector, turn **Camera ON**, select Display, and Record approximately 15 seconds. Stop, **play the MP4** and confirm webcam pixels are in the upper right, no black image, no duplicate camera open, normal desktop effects/zoom and one readable MP4. Repeat once Camera OFF and compare desktop framing. This initial CI artifact is video-only; do not assume Mic/System audio is included or accepted in this special camera QA build. For combined audio, use a later pinned-FFmpeg product-on CI lane after visual webcam acceptance.

**Real-world known data (older engine):** one genuine NV12 1280x720@30 camera, 138 GPU frames received in 5.012s steady streaming; 27.27 fps observed, 1 startup stream tick, zero streaming ticks, 0 invalid/adapter mismatches/timeouts, no queue busy drops, at most one occupied mailbox sample. A source FPS below 30 fps nominal can be normal for certain USB camera/lighting/driver combinations; Arssyut output 60fps still reuses newest camera GPU surface while rendering 60fps without generating bogus camera frames.

## CI acceptance

- Single existing Windows CI lane builds \`Arssyut.UI.exe\` and native recorder, runs all CTest and GUI smoke.
- Added \`arssyut_webcam_compositor_tests\`: creates one production shader compositor on WARP, verifies a default desktop-only render, and (where driver supports NV12) tests a synthetic GPU NV12 camera composited into the upper-right and removed with \`clear_webcam\`. CPU staging readback occurs **only in test**, never product.
- Historical WGC/audio/MP4 writer remain unmodified. Existing P6UI freeze guard updated with exact reviewed hashes for dedicated webcam compositor/native pipe sources, not broadened to unrelated engine.
- This is a **hardware-preview** milestone, not RC-ready; requires real MP4 visual and long-run stress, foreign-device/error, 30-60-minute memory/frame pacing and audio parity before release.

## Follow-on

P8W2B: hardware MP4/visual QA fixes based on real media, checked error-state handling, camera cleanup. P8W3: proper UI native camera snapshot, device switch/privacy indicator, hotkeys/start-stop. P8W4: integrated P7 audio + accepted Pause per seam, real long-run soak, release hardening. No repeated probe loops; next evidence must be **an actual MP4**.
