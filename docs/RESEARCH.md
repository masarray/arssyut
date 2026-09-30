# Research Notes — Screen Recorder Architecture

Research date: **2026-09-30**

This document records the evidence used to define Arssyut's initial architecture. It is not a license to copy unrelated code. External projects are studied for proven patterns, failure modes, and design tradeoffs.

---

## 1. Internal source audit

### ArZoom for OBS

Repository:
https://github.com/masarray/arzoom-follow-obs

Observed planning HEAD:
`ada8f5269246c64429d7aceb6cc72f81e72120ba`

Important findings:

#### Fixed-size click history

`src/arzoom-click-visual.hpp` uses four click slots and reuses inactive/oldest slots.

This is exactly the right realtime pattern for Arssyut:

- bounded memory;
- no particle-vector growth;
- overlapping clicks still work;
- deterministic reuse.

Clicks are stored in content coordinates and projected through the current camera transform, so the visual remains attached to the clicked content while camera motion continues.

#### Kinematic motion

`src/arzoom-scene-motion-synthesizer.hpp` separates target selection from motion synthesis.

Important properties:

- explicit position + velocity + acceleration;
- acceleration is jerk-limited;
- fixed bounded integration substeps;
- time-based behavior;
- stable behavior across different frame rates;
- O(1) motion state.

This should be transplanted instead of rebuilding zoom around frame-dependent lerp.

#### Viewport planning

`src/arzoom-scene-viewport-planner.hpp` separates:

- WHERE the camera should frame;
- HOW the camera reaches that target.

This is a critical architecture invariant for Arssyut.

#### Performance philosophy

ArZoom's accepted direction includes:

- no CPU frame readback in presentation hot path;
- one camera authority;
- bounded presentation state;
- no duplicate render graph;
- deterministic safe fallback.

Arssyut should preserve these principles even though OBS itself is removed.

---

## 2. ArVisual for OBS

Repository:
https://github.com/masarray/arvisual-obs

Observed planning HEAD:
`d0a3f405447446e88dc56f4a50535a17257fcccf`

Important findings:

### Small scene analysis surface

ArVisual uses a 64x36 analysis surface.

2304 pixels are enough to derive stable scene statistics without processing the entire frame on CPU.

Statistics include:

- luminance percentiles;
- saturation distribution;
- shadow fraction;
- near-clip fraction;
- vivid/hot-vivid fraction;
- neutral/colored fractions.

### Double staging resources

The plugin uses two staging surfaces, allowing current GPU work and prior analysis readback to be separated.

### Bounded adaptive state

Scene statistics feed bounded adaptive shader parameters rather than a second image-processing pipeline.

### Standalone improvement for Arssyut

Arssyut should keep the same bounded behavioral model but decouple analysis from every video frame.

Recommended:

- analyze at ~6–12 Hz;
- asynchronously read a tiny downsample surface;
- skip analysis rather than wait if data is not ready;
- render the grade at full output FPS using last valid parameters.

This reduces CPU synchronization risk while preserving visual adaptation.

---

## 3. Windows Graphics Capture

Microsoft documentation:
https://learn.microsoft.com/windows/apps/develop/media-authoring-processing/screen-capture

Microsoft documents `Windows.Graphics.Capture` as the API for acquiring frames from a display or application window.

Why it fits Arssyut:

- native Windows capture;
- D3D-backed surfaces;
- monitor/window model;
- modern Windows support;
- avoids CPU screenshot loops.

### Free-threaded frame pool

Documentation:
https://learn.microsoft.com/uwp/api/windows.graphics.capture.direct3d11captureframepool.createfreethreaded

`Direct3D11CaptureFramePool.CreateFreeThreaded` removes the DispatcherQueue dependency and raises `FrameArrived` on the frame pool's internal worker thread.

Architecture consequence:

The callback must remain tiny:

```text
acquire -> stamp -> latest-frame publish -> return
```

Do not turn FrameArrived into the recorder engine.

---

## 4. DXGI Desktop Duplication

Microsoft documentation:
https://learn.microsoft.com/windows-hardware/drivers/display/desktop-duplication-api

Desktop Duplication provides:

- desktop image in GPU memory;
- dirty-region metadata;
- move-region metadata;
- pointer metadata;
- opportunity for GPU processing/encoding.

This confirms the broader design principle that a professional recorder should keep desktop frames on GPU and use metadata/bounded processing instead of CPU screenshot copies.

### Decision

Use WGC first for modern monitor/window capture.

Keep a capture-backend interface so Desktop Duplication can later be added where its desktop-specific behavior is useful.

---

## 5. Hardware encoding

OBS hardware-encoding guidance:
https://obsproject.com/kb/hardware-encoding

OBS recommends hardware encoders for performance because encoding work is offloaded from the CPU to dedicated GPU media hardware.

The common Windows hardware families remain:

- NVIDIA NVENC;
- Intel Quick Sync Video;
- AMD AMF.

### Decision

Arssyut is hardware-first.

Encoder capability is probed before a recording starts. The product profile maps to vendor-specific settings internally.

Do not expose a wall of vendor codec options on the default UI.

---

## 6. WASAPI loopback

Microsoft documentation:
https://learn.microsoft.com/windows/win32/coreaudio/loopback-recording

WASAPI loopback captures the mix played by a rendering endpoint.

### Decision

System audio uses WASAPI loopback.

Microphone uses a separate WASAPI capture source.

Both are normalized into one project audio timebase and bounded buffering model.

---

## 7. Keyboard input strategy

Microsoft low-level keyboard hook documentation:
https://learn.microsoft.com/windows/win32/winmsg/lowlevelkeyboardproc

Important Microsoft guidance:

- low-level hook callbacks must return quickly;
- a timed-out hook can be silently removed;
- Microsoft recommends Raw Input in many cases because it can asynchronously monitor keyboard/mouse data more effectively.

### Decision

Primary:
- Raw Input on a dedicated message worker.

Fallback:
- low-level hook only where required and proven.

The callback never performs:

- chord formatting;
- UI rendering;
- logging;
- allocation-heavy processing.

It emits a tiny canonical event and returns.

---

## 8. FollowCursor — focused zoom-recorder comparison

Repository:
https://github.com/sabbour/followcursor

Observed architecture includes:

- Windows Graphics Capture;
- mouse/click/keyboard tracking;
- smart auto zoom;
- H.264 export;
- NVENC/QSV/AMF detection;
- click/cursor rendering.

This validates the product demand for the same broad experience category.

However its documented architecture uses:

- Python/PySide6;
- lossless AVI pipe;
- OpenCV/NumPy frame manipulation;
- offline cinematic export.

### What Arssyut should learn

Useful product ideas:

- automatic action-aware zoom;
- useful keyboard/click metadata;
- hardware encoder fallback;
- project/metadata separation as a possible future Studio feature.

### What Arssyut should not copy

For our lightweight goal, do not make CPU OpenCV/NumPy processing or lossless frame piping the production realtime path.

Arssyut's differentiator should be native GPU compositing during recording.

---

## 9. Cap — open-source Loom-class product

Repository:
https://github.com/CapSoftware/cap

Cap demonstrates a mature modern screen-recording product with:

- local/studio recording;
- Rust desktop backend;
- separate media crates;
- polished product UX;
- local edit/export architecture.

### Lesson

Separate media/core responsibilities from UI.

Do not embed recorder state in frontend components.

### Deliberate difference

Arssyut does not need Cap's full web/team/cloud stack.

Our first goal is a smaller Windows-native recorder with presentation effects and direct optimized MP4.

---

## 10. ScreenToGif

Project organization:
https://github.com/screentogif-screen-recorder

ScreenToGif demonstrates that tutorial creators value:

- mouse-click highlighting;
- keystroke overlays;
- editing/annotation;
- multiple export formats.

### Lesson

Click and keyboard visualization are core tutorial features, not decoration.

### Deliberate difference

Arssyut should not become a frame-oriented editor before its realtime native video pipeline is complete.

---

## 11. GitLab ecosystem findings

### OpenScreen

https://gitlab.com/Roxanne_Ardary/openscreen

OpenScreen describes a local screen-recording product aimed at polished demos/tutorials with:

- smooth zoom effects;
- annotations;
- backgrounds;
- audio;
- assisted editing.

This reinforces the market direction toward “raw capture + presentation intelligence”.

### RedFFmpegatron

https://gitlab.com/hadoukez/redffmpegatron

This Windows-focused project demonstrates direct screen recording with AMD AMF hardware encoding and system audio.

Its help text documents:

- FFmpeg desktop capture;
- AMF;
- system audio;
- tray control;
- global hotkey.

### GitLab FFmpeg staging mirror

https://gitlab.com/fflabs/ffmpeg

FFmpeg remains relevant as a mature media toolkit providing:

- codecs;
- container muxing;
- filters;
- scaling;
- audio resampling.

### Lesson from GitLab research

The ecosystem repeatedly converges on:

- FFmpeg/media libraries for encoding/muxing;
- hardware codecs;
- native capture;
- explicit audio capture;
- global controls.

The quality difference comes from how well the application manages frame ownership, latency, GPU copies, backpressure, and lifecycle.

---

## 12. Media-stack decision still open

Do **not** prematurely decide “FFmpeg is obviously the answer” or “Media Foundation is lighter”.

Both need a measured P2 spike.

### FFmpeg/libav candidate

Evaluate:

- `libavcodec`;
- `libavformat`;
- D3D11 hardware frames;
- NVENC;
- QSV;
- AMF;
- remux and recovery.

### Media Foundation candidate

Evaluate:

- D3D11 device manager integration;
- hardware H.264 MFT availability;
- output/mux behavior;
- diagnostics;
- cross-vendor consistency.

### Decision criteria

Weight:

1. recording reliability;
2. D3D11 zero/low-copy path;
3. hardware encoder coverage;
4. crash-safe finalization;
5. A/V sync;
6. CPU/GPU cost;
7. output compatibility;
8. dependency footprint;
9. maintainability;
10. licensing/distribution.

---

## 13. Why crash-safe temporary media is required

A conventional MP4 often depends on metadata written/finalized at the end of recording.

For a recorder, an abnormal termination must not unnecessarily make a long recording unusable.

### Product pattern

Use a recoverable recording form first, then remux to the user-facing MP4 after Stop.

This mirrors the broader professional-recorder principle of separating:

- durable capture;
- final delivery container.

No video re-encode should be required during normal finalization.

---

## 14. Why CFR output is preferred initially

Desktop capture arrival can be irregular.

A compatible final video is easier to reason about when the output scheduler owns a deliberate cadence.

For a 60-FPS profile:

- sample the latest valid captured frame at each output tick;
- use latest canonical input/camera state;
- duplicate static content when necessary;
- let H.264 efficiently compress unchanged frames.

This avoids accumulating latency merely to preserve every capture callback.

A future VFR mode can be an explicit feature, not accidental timing behavior.

---

## 15. Why keyboard overlay must be semantic

Recording every key is easy but creates privacy and UX problems.

The requirement is not “show everything typed”; it is “show actions that the viewer cannot otherwise see”.

Therefore the reducer should emit concepts such as:

```text
Ctrl+C
Ctrl+Shift+S
Alt+Tab
F5
Win+D
```

rather than reconstructing words/passwords.

This also radically bounds the rendering/cache problem.

---

## 16. Keyboard visual reference supplied for Arssyut

The supplied reference SVG is 4300x1800 and visually contains:

- white keycap set;
- black keycap set;
- Ctrl/C/V/A/Z;
- Alt;
- Shift;
- Space;
- WASD.

The desired quality is clear: compact physical keys rather than generic toast text.

### Implementation decision

Use the image as a visual reference, not as a giant traced asset in the render loop.

Recreate the keycap style using retained GPU/vector primitives and cached glyphs.

Benefits:

- arbitrary key labels;
- DPI-safe output;
- smaller asset footprint;
- dark/light style;
- no runtime SVG parsing;
- easier animation.

---

## 17. Research-derived architecture principles

The research converges on the following rules:

### Capture
Native API + GPU surface.

### Scheduling
Output timeline owns cadence.

### Backpressure
Latest-wins for replaceable frame/pointer state.

### Semantics
Clicks/chords are timestamped bounded events.

### Motion
One time-based jerk-limited camera authority.

### Visuals
GPU compositor, not CPU frame painting.

### Color
Small decoupled analysis + GPU grade.

### Audio
WASAPI + canonical sync clock.

### Encoding
Hardware-first H.264.

### Storage
Recoverable session media before final MP4.

### Lifecycle
All workers join; all resources have owners.

### Privacy
Visualize actions, not secret text.

---

## 18. Source/licensing note

ArZoom and ArVisual currently declare GPL-2.0-or-later.

If Arssyut copies/adapts source from those repositories, every imported unit must preserve provenance and applicable license obligations.

Before first code transplant:

- settle Arssyut license;
- record upstream commit/file;
- record local modifications;
- preserve copyright/license notices as required;
- audit bundled FFmpeg/media build licensing.

Do not postpone this until release packaging.

---

## 19. Final research conclusion

There is no need to invent a new screen-recorder theory.

The ingredients are well established:

- Microsoft native GPU capture;
- hardware media encoding;
- WASAPI audio;
- bounded input observation;
- GPU compositing;
- crash-safe media handling.

Arssyut's engineering challenge is to combine them without naive queues, CPU frame copies, duplicate camera truth, input privacy problems, or resource leaks.

That is why the repository begins with production contracts before implementation.
