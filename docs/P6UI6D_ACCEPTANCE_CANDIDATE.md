# P6UI.6D-L — Real-Video Acceptance Candidate

**Issue:** #32  
**Active product branch:** `feat/p6ui-avalonia-shell`  
**Candidate commit:** `fb3eeb99dbb1040946394da5fa35ec5fa90aaf1d`  
**Canonical CI:** #324 / run `37552892062`  
**CI result:** both `Native Windows x64 Release` and `P6UI Avalonia Windows x64` green

This file freezes the exact package that must be used for P6UI.6D real-video acceptance. Do not test an older artifact and transfer the result to this candidate.

## 1. Candidate artifacts

### Avalonia product artifact

- Name: `arssyut-p6ui-avalonia-windows-x64`
- Artifact ID: `11454310007`
- Size: `45,801,932` bytes
- SHA-256: `ee39fe7387bce3b72ab55667a24b63d04981afc8b993a282a1d2f5858ddad76e`
- Workflow run: `37552892062`
- Head SHA: `fb3eeb99dbb1040946394da5fa35ec5fa90aaf1d`

### Native recorder artifact

- Name: `arssyut-recorder-windows-x64`
- Artifact ID: `11454125863`
- Size: `285,724` bytes
- SHA-256: `ad3d3a9343669446c8d7afd67b1d1fd7d3d301a70e033d1208c06d7a7df4ff78`
- Workflow run: `37552892062`
- Head SHA: `fb3eeb99dbb1040946394da5fa35ec5fa90aaf1d`

The Avalonia artifact is the primary product-acceptance package. The native artifact is retained for engine-level reproduction only.

## 2. What changed before this candidate

This candidate includes the accepted P6UI.6D progression and the final deterministic corrections from PR #47:

- Zoom-first cinematic Spotlight choreography;
- minimum-jerk Spotlight close/open;
- delayed dim choreography;
- smooth mid-transition reversal;
- live Zoom +/- Spotlight resize through the pinned upstream resize helper;
- Freeze Camera exact-shot hold and resume;
- beginner-facing Spotlight settings and persistence;
- standalone Spotlight when `Spotlight with Zoom` is Off;
- standalone mode uses the existing canonical Cursor focus path;
- Compact / Balanced / Wide now change the actual retained-renderer aperture scale;
- Region / negative-origin deterministic gates;
- Spotlight OFF/ON retained-path tests at 1080p and 4K;
- no second camera, Region solver, input hook, compositor, or render pass.

## 3. Candidate architecture lock

The real-video acceptance run must not trigger any implementation change merely to make a clip look different unless a reproduced defect proves the owning layer is wrong.

Locked authorities:

- camera: existing `PresenterAwareSmartCamera`;
- camera profile: Smart follow + Cinematic;
- Spotlight choreography: vendored `CinematicSpotlightState`;
- Zoom +/- Spotlight resize: vendored `SpotlightZoomResizeState`;
- Freeze: pause existing camera/overview stepping in `PresentationController`;
- pointer mapping: native `presentation_screen_rect` normalization;
- Region: existing native Region geometry;
- rendering: existing retained D3D11 presentation pass;
- encoding: existing native Media Foundation path.

Do not add post-camera easing, a second focus planner, a UI-side transform, a Spotlight texture/blur pass, or a second recorder input loop.

## 4. Required acceptance recordings

Use `docs/P6UI6D_REAL_VIDEO_ACCEPTANCE_LOCK.md` as the full matrix. The minimum first-pass bundle for this candidate is:

1. Spotlight Off baseline at 1080p60.
2. Zoom ON → camera begins framing → Spotlight closes.
3. Zoom OFF → dim restores immediately → Spotlight opens beyond frame → exact pass-through.
4. Rapid Zoom ON/OFF reversal during Spotlight transition.
5. Zoom In / Zoom Out while already zoomed; no activation replay or snap.
6. Freeze mid-camera movement, move pointer far away, then unfreeze.
7. Compact / Balanced / Wide Focus Size comparison.
8. Standalone Spotlight (`Spotlight with Zoom = Off`) with pointer movement while camera remains full-frame.
9. Region + Spotlight + Freeze.
10. Negative-origin secondary monitor if available.
11. 4K Spotlight Off vs On performance comparison.

## 5. Diagnostics to capture for OFF vs ON comparisons

Recorder diagnostics already expose the needed evidence; no new telemetry milestone is required before testing.

Record at least:

- capture frames received;
- capture frames replaced / busy drops;
- video frames rendered;
- video frames reused;
- video frames skipped;
- encoder frames submitted;
- encoder backpressure events;
- capture p95 latency;
- compositor CPU p95 latency;
- compositor GPU p95 latency;
- private memory / peak private memory;
- compositor resource generation;
- resulting MP4 success / playback status.

Compare the same source, resolution, FPS, visual mode and duration with Spotlight Off and On. WARP CI proves structural stability; these real-machine values prove product cost.

## 6. Acceptance interpretation

A passing result should show:

- camera begins visibly before the linked Spotlight closes;
- aperture closes calmly with no dark first-frame flash;
- focus area stays natural brightness;
- outside dim stays soft rather than black;
- Zoom OFF opens continuously instead of disappearing abruptly;
- rapid reversals start from the current visible aperture state;
- local pointer work does not make Smart Spotlight chase independently;
- Freeze is exact and unfreeze is continuous;
- standalone Spotlight does not wake or move the camera;
- Focus Size presets are visibly distinct;
- no material cadence/backpressure regression from Spotlight On;
- no resource-generation churn attributable to Spotlight.

## 7. Closure rule

CI #324 makes `fb3eeb99...` the deterministic implementation candidate, not the final visual-acceptance result.

Issue #32 may be called **accepted / locked** only after the applicable real-video rows are reviewed against this exact artifact. If a defect is found, preserve this candidate as the reproduction baseline and fix only the smallest owning layer.