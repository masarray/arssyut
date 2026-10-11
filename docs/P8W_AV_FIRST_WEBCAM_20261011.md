# P8W — A/V-first Windows webcam recovery (2026-10-11)

## Canonical baseline — NO experimental webcam as parent

Branch `feat/120-av-first-webcam-doubleclick-stability` begins at **`main@72bef9a1b8f6dcaddad3a1693b6f9dedacfdda9a`**. That point already contains accepted dual A/V integration and microphone mute, plus a documentation-only P8W0 webcam ADR. The immutable real dual-audio acceptance checkpoint is **`checkpoint/av-dual-audio-accepted-20261011@12e1fa98e33735cade2ec5bdca61a76861d8bf23`**. Mic-mute main merge **`1d5ed67bcb47466c6cd210145d5f8fd80e90e526`** follows the A/V checkpoint. Do not regress audio to a video-only PR #119 artifact or import Draft Pause PR #115.

## Reported failure — evidence and non-claims

A user tested webcam preview in later Draft PR #119 after the first camera-source and compositor CI-green milestones. The UI placeholder remained blank and selector was not functioning in one artifact. In a later artifact, launching the program was reported to freeze and force close. A real user desktop-only MP4 plus diagnostics showed **529 encoded H264 desktop frames in 8.82s with no encoder backpressure** but no visible face-cam and no camera capture telemetry. This does **not** establish an exact exception address/root cause for the app crash; don't invent one. Previous webcam-only test artifacts missed the accepted internal pinned-FFmpeg A/V product lane.

## Scope of this narrow recovery

- Parent the feature from the accepted **latest main A/V** code, not the unstable Draft #119 branch; transplant only reviewed optional webcam **input** / **one-pass NV12 composition** / native GUI preview parts. Keep accepted `RecorderSession` WASAPI, canonical mixer, H264/AAC writer and portable installer logic as unchanged as possible.
- Internal GUI artifact embeds the camera opt-in, using the already accepted `InternalAudioPreview` packaging pattern. `Arssyut.UI.exe` must start by **double-click**, without PowerShell, command line, env var, or extra camera probe executable. Public/default builds remain camera OFF until real acceptance.
- No camera opened at application launch. Native device enumeration runs asynchronously after the first visible frame so driver/COM latency cannot block Avalonia's UI loop. Camera opens only after the user explicitly selects a real generation-scoped device and turns webcam ON.
- Serialize preview stop/reopen, device switch, recording transition and app Closing through one managed operation barrier; all driver-start/stop/COM enumeration calls use a worker task rather than executing on the UI dispatcher. Preview is shut down before the recorder obtains the camera; this is a **sequential handoff**, not an assertion of zero-restart capture. The product still needs accepted native same-owner handoff and mid-recording toggle/hotkey before public release.
- Reuse GPU sample ownership (3-slot bounded latest-wins), one NativeVideoPipeline clock, one D3D11 compositor pass, one H264/AAC MP4 writer and tiny 192x108 low-frequency GPU preview readback. No extra audio/video writer or CPU readback of full webcam frames.

## CI and physical acceptance

Only the existing `[audio]` pinned-FFmpeg product job produces the requested GUI artifact, with `-p:InternalAudioPreview=true -p:InternalWebcamPreview=true`, retaining the existing native Windows release and deterministic CTests. Internal package README must explicitly instruct double-click, webcam preview before recording, audio+webcam and output MP4 playback. Standalone webcam probe is NOT an acceptance artifact.

The Windows runner can check build, shader, start/stop smoke with no camera and synthetic WARP GPU samples; it **cannot** prove USB camera permissions, driver startup, actual thumbnail pixels or A/V MP4 playback. Real Windows user gate in order: (1) GUI opens normally and remains responsive, (2) webcam ComboBox shows real connected device and switches selection, (3) Camera ON displays moving thumbnail before Record, (4) record Display+System+Mic+Camera ~15 seconds, Stop and play one readable MP4 with actual camera pixels and audio, (5) Camera OFF produces desktop-only output, (6) physical busy/unplug and long-run resource checks. Stop or roll back on force-close/freeze/audio regression. Keep PR Draft and main pristine until accepted. Do not ask for more repeated GPU probe JSON.
