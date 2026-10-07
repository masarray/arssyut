# P6UI.6D-J — Spotlight / Freeze Real-Video Acceptance Lock

**Active branch:** `feat/p6ui-avalonia-shell`  
**Issue:** #32  
**Authority rule:** existing native PresentationController + SmartCamera + retained D3D11 compositor only  
**Purpose:** close P6UI.6D with repeatable real-Windows evidence without reopening accepted camera/render/input authority.

This document is an acceptance procedure, not a claim that real-video acceptance has already passed.

## 1. Automated implementation lock

Before real-video acceptance, the exact candidate head must satisfy:

- pinned ArZoom Spotlight source-contract gates;
- cinematic Zoom -> Spotlight choreography gates;
- Freeze exact-hold / reset / zoom-off gates;
- SmartCamera motion trace at 30/60/120/144 fps;
- no second camera/planner in the recorder;
- native baseline guard;
- bridge ABI/configuration tests;
- Settings persistence/migration regression;
- Spotlight disabled-state exact pass-through;
- Spotlight retained single-pass/resource-stability regression;
- Region negative-origin/camera-viewport regression;
- Spotlight OFF/ON structural performance smoke at 1080p and 4K.

CI timing from WARP is diagnostic only. It cannot replace hardware GPU/video acceptance.

## 2. Locked product behavior

| Product intent | Locked implementation |
| --- | --- |
| Smart Zoom camera | existing PresenterAwareSmartCamera, scene_context=false |
| Camera motion style | Smart follow + Cinematic |
| Spotlight focus | existing PresentationController Spotlight state |
| Zoom -> Spotlight choreography | existing upstream-derived cinematic choreography |
| Spotlight rendering | analytic math in existing retained D3D11 presentation shader |
| Freeze Camera | pauses existing camera/overview stepping at exact rendered state |
| Pointer coordinates | existing native presentation_screen_rect normalization |
| Region crop / viewport | native Region geometry |
| Global Freeze hotkey | existing bridge RegisterHotKey authority |
| Spotlight Settings | one start-time ABI v9 configuration path |
| Spotlight default | Off for existing-user compatibility |

No acceptance defect may be fixed by adding a second camera, second Region solver,
second keyboard hook, post-camera smoothing tail, snapshot compositor, extra
Spotlight texture/blur pass, or UI-owned presentation geometry.

## 3. Real Windows evidence matrix

Run against the exact packaged artifact produced by the final green CI head.

| # | Scenario | Required evidence | Result |
| --- | --- | --- | --- |
| 1 | Display recording, Spotlight Off | MP4 + exact pass-through observation | pending |
| 2 | Display recording, Spotlight On | MP4; focus bright, outside gently dimmed | pending |
| 3 | Toggle/Hold Zoom -> linked Spotlight | MP4; camera frames first, Spotlight closes smoothly | pending |
| 4 | Remote pointer move while zoomed | MP4; fast catch-up, smooth settle, no wobble | pending |
| 5 | Local pointer explanation/orbit | MP4; camera remains calm / no micro-tracking | pending |
| 6 | Freeze while camera is moving | MP4; exact current shot locks without snap | pending |
| 7 | Pointer motion while frozen | MP4; viewport remains pixel-stable | pending |
| 8 | Unfreeze after remote pointer movement | MP4; same camera resumes continuously | pending |
| 9 | Zoom +/- while frozen | MP4; frozen shot stays fixed; new target applies after resume | pending |
| 10 | Reset / Full Frame while frozen | MP4; Freeze clears and camera returns smoothly | pending |
| 11 | Region recording + Spotlight | MP4; focus and camera viewport remain inside selected Region | pending |
| 12 | Region + Freeze | MP4; identical Region presentation viewport remains locked | pending |
| 13 | Negative-origin secondary monitor | MP4 + screenshot of monitor arrangement | pending |
| 14 | Window capture | MP4; Spotlight/camera mapping remains correct | pending |
| 15 | 30 fps | MP4 + diagnostics | pending |
| 16 | 60 fps | MP4 + diagnostics | pending |
| 17 | 1080p60 Spotlight Off vs On | diagnostics + playback; no material regression | pending |
| 18 | 4K Spotlight Off vs On | diagnostics + playback; no material regression | pending |
| 19 | Spotlight Focus Size presets | three short clips or one comparative capture | pending |
| 20 | Spotlight Motion presets | Smooth/Balanced/Snappy behavior remains bounded | pending |
| 21 | Settings restart persistence | close/reopen app; chosen Spotlight settings restored | pending |
| 22 | Legacy settings migration | existing hotkeys/zoom preserved; Spotlight starts Off | pending |

## 4. Visual acceptance criteria

Spotlight must read as presentation focus, not a theatrical black vignette:

- focus area retains natural source brightness;
- outside dim is gentle and feathered;
- no hard halo or visible mask edge;
- no first-frame dim flash before focus aperture is established;
- linked Zoom -> Spotlight sequence feels framing-first rather than simultaneous;
- camera does not chase small pointer movements;
- distant pointer movement is caught without feeling delayed;
- settle decelerates naturally and reaches a drift-free hold;
- Freeze is visually exact and release does not jump.

## 5. Performance acceptance criteria

For the real hardware run, compare the same source/settings with Spotlight Off and
On. Record at least:

- output resolution / fps;
- rendered/submitted/backpressure counters;
- CPU submit latency diagnostics when available;
- GPU execution latency diagnostics when available;
- dropped/duplicated frame observations;
- encoder/result success.

A small shader-cost increase is acceptable. A new steady-state allocation,
resource-generation churn, readback, additional render pass, or material frame
cadence regression is not.

## 6. Closure rule

P6UI.6D may be called **implementation-complete** after the final deterministic
CI gate is green.

P6UI.6D may be called **accepted / locked** only after the applicable real-video
matrix above passes on the exact candidate artifact.

If real evidence exposes a defect, fix the smallest owning layer first:
configuration -> PresentationController intent/choreography -> existing
SmartCamera -> existing compositor. Do not replace a green lower-level authority
without a reproduced failure that proves it is the cause.
