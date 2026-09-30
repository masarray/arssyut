# Product Requirements Document — Arssyut

Status: **Foundation / Phase 0**  
Target platform: **Windows 10/11 x64 first**  
Product class: **lightweight native screen recorder for polished demonstrations**

---

## 1. Product vision

Arssyut records a screen/window/region and produces a presentation-ready video without requiring the user to learn OBS scenes, timeline editing, manual zoom keyframes, or color grading.

The core promise is:

> Press Record, demonstrate naturally, press Stop, receive a smooth and attractive MP4 with restrained camera motion, visible clicks, useful keyboard-shortcut overlays, synchronized audio, and low system overhead.

The application is intentionally not a general broadcasting suite.

---

## 2. Primary use cases

1. Software tutorials.
2. Engineering-tool demonstrations.
3. Product and UI walkthroughs.
4. Bug reproduction videos.
5. Training/classroom recordings.
6. YouTube screen-recorded explanations.
7. Internal documentation and async demos.

---

## 3. Product goals

### G1 — Handy

A normal recording requires only:

1. choose source;
2. choose audio;
3. Record;
4. Stop;
5. open/share MP4.

Advanced settings must not block this path.

### G2 — Lightweight

Steady-state recording must avoid CPU full-frame processing and favor D3D11 + hardware video encoding.

### G3 — Smooth

Camera motion, click feedback, overlays, frame pacing, and audio sync must remain stable during long recordings.

### G4 — Presentation-ready

Default output should look polished without materially misrepresenting UI colors or making zoom behavior distracting.

### G5 — Recoverable

An application crash, encoder failure, or power interruption should not unnecessarily destroy all media already recorded.

### G6 — Private by default

Keyboard visualization must show useful actions without turning normal recording into a keylogger.

---

## 4. Non-goals for the first production release

The first production release will not attempt to become:

- an OBS replacement;
- a nonlinear timeline editor;
- a livestreaming platform;
- a cloud video-hosting service;
- an AI OCR/vision tracker;
- a webcam studio with complex scene graphs;
- a cross-platform abstraction before the Windows pipeline is mature.

Future work may add some of these capabilities only after the native recording foundation is stable.

---

## 5. Experience model

### Main compact window

The primary surface contains:

- source selector;
- microphone selector;
- system-audio toggle;
- quality/FPS preset;
- click visual toggle;
- smart zoom toggle;
- keyboard shortcut visual toggle;
- color treatment selector;
- Record button.

Avoid large card grids and oversized typography.

### During recording

Use a small floating control:

- elapsed time;
- pause/resume if supported by the active session contract;
- microphone status;
- stop button.

The overlay must be excludable from capture where supported.

### After recording

Show:

- output file name/path;
- duration;
- open file;
- open folder;
- copy file/path;
- record another.

Finalization state must be explicit.

---

## 6. Capture requirements

### P0

Support:

- full monitor;
- application window;
- rectangular region on one monitor;
- 30 FPS;
- 60 FPS;
- SDR recording.

Windows Graphics Capture is the primary backend.

A backend abstraction must allow a future DXGI Desktop Duplication fallback without changing the product state model.

### Capture behavior

- handle source resize;
- handle captured window closure;
- handle monitor topology changes safely;
- preserve negative desktop coordinates;
- do not capture stale frames indefinitely after source loss;
- do not queue captured frames without bound.

For region capture, capture the owning display/window through the native backend and crop on the GPU.

---

## 7. Camera / zoom requirements

The camera engine is derived from ArZoom behavior.

### Core behavior

- one camera authority;
- time-based motion;
- jerk-limited movement;
- bounded substep integration;
- hysteresis and dead zones;
- no frame-rate-dependent arbitrary lerp;
- no abrupt pan when the cursor makes a small movement.

### User modes

#### Off
Full-frame capture only.

#### Smart
Default presentation mode. The camera may zoom and frame the current action area using click/activity intent, with restrained motion.

#### Manual
Global recorder shortcuts can toggle/hold/adjust zoom at the pointer/action position.

### Intent priority

Recommended precedence:

```text
manual camera command
> explicit click/action anchor
> smart follow/activity intent
> stable hold/full-frame state
```

Click visualization must not silently become an independent second camera planner.

### Acceptance

Equivalent pointer/action trajectories at 30/60/120/144-Hz simulation must yield materially equivalent timing and landing behavior.

---

## 8. Click visualization requirements

Derived from ArZoom's bounded click-state design.

Requirements:

- left/right/middle distinction;
- compact animated pulse/ring;
- GPU rendering;
- content-space anchoring;
- pulse remains visually attached to clicked content while camera zoom/pan moves;
- overlapping clicks remain bounded;
- no particle list that grows over time.

Default fixed event history should remain small, e.g. four simultaneous active effects unless testing proves another bound is required.

---

## 9. Keyboard shortcut visualization

### Purpose

Show keyboard actions that otherwise leave no visible trace in the captured UI.

Examples:

- Ctrl+C
- Ctrl+V
- Ctrl+X
- Ctrl+Z
- Ctrl+Shift+Z
- Ctrl+A
- Ctrl+S
- Alt+Tab
- Win+D
- function/system keys
- configured recorder hotkeys

### Default privacy mode

By default:

- ordinary typed letters/numbers are not displayed;
- ordinary typed text is not persisted;
- modifier chords are canonicalized;
- auto-repeat noise is suppressed;
- duplicate shortcut events may be coalesced within a defined short window.

### Visual language

Use the supplied keyboard reference as direction:

- compact physical-keycap look;
- light and dark variants;
- individual keycaps in one horizontal chord group;
- high readability at 1080p;
- subtle entrance/exit animation;
- no oversized lower-third panel.

The supplied traced SVG is a design reference, not a required runtime asset. Runtime rendering should use retained vector geometry/SDF + cached glyphs.

---

## 10. Color treatment requirements

ArVisual behavior is transplanted into the standalone recorder.

### Modes

#### Pixel Accurate
No creative grade beyond required color-space conversion.

#### Clean Screen — default
Restrained enhancement for UI/tutorial recording:
- subtle contrast/exposure assistance;
- neutral/white protection;
- highlight protection;
- conservative saturation;
- no obvious color cast.

#### Vivid Presentation
Stronger but still bounded enhancement.

Future presets may expose ArVisual-derived product/beauty looks where relevant.

### Engine rules

- grading occurs on GPU;
- adaptive analysis is bounded and may run at a lower cadence than output FPS;
- no synchronous full-frame CPU readback;
- failed analysis retains previous valid adaptation or neutral state;
- color treatment must never be required for recorder stability.

---

## 11. Audio requirements

### P0

- system audio via WASAPI loopback;
- microphone input;
- independent enable/disable;
- 48 kHz canonical project format;
- AAC-LC final output;
- basic per-source gain;
- bounded buffers;
- long-session synchronization.

### Acceptance

Measure A/V drift over long recordings and expose diagnostic counters for:

- capture underflow/overflow;
- resample correction;
- audio discontinuity;
- device disconnect.

---

## 12. Video encoding requirements

### Default output

- MP4;
- H.264/AVC;
- 4:2:0 compatible output;
- 30 or 60 FPS;
- AAC audio;
- seekable;
- editor/browser friendly.

### Encoder policy

Prefer hardware encoding on the active/appropriate GPU:

- NVIDIA NVENC when appropriate;
- Intel Quick Sync when appropriate;
- AMD AMF when appropriate;
- platform/media fallback where validated;
- software encoder only under an explicit fallback policy.

Encoder selection must be resolved before recording.

### Frame-pacing policy

The output timeline is canonical and deliberate.

A 60-FPS profile should emit a stable 60-FPS timeline using the most recent valid capture state rather than accumulating capture latency.

If the producer temporarily outruns the pipeline:

- coalesce obsolete replaceable frames;
- preserve semantic events;
- never create an ever-growing frame backlog.

---

## 13. Crash-safe media requirement

Arssyut must not depend on a normal final MP4 close operation for survival of an entire session.

Preferred product behavior:

1. record into a recoverable temporary container/session file;
2. close cleanly on Stop;
3. remux/finalize to MP4 without video re-encoding;
4. place MP4 metadata for fast seek/start where configured;
5. atomically promote the completed MP4;
6. remove temporary media only after verified success.

On abnormal launch after a prior crash, Arssyut should be able to detect and offer recovery/finalization of supported temporary sessions.

---

## 14. Session state machine

One authoritative state machine:

```text
Idle
 -> Preparing
 -> Countdown
 -> Recording
 <-> Paused          (only when pause contract is implemented)
 -> Stopping
 -> Finalizing
 -> Ready

Any active state
 -> RecoverableError / Failed
 -> Idle or Recovery flow
```

UI reflects this engine state. UI must not invent its own recording truth.

---

## 15. Performance requirements

Performance must be measured on named reference machines; do not present device-independent marketing numbers as guarantees.

### Architecture-level hard requirements

- zero full-frame CPU copy in normal steady-state video path;
- bounded frame slots;
- bounded input events;
- bounded audio queues;
- no per-frame shader compilation/resource creation;
- no per-frame filesystem/log work;
- no memory structure proportional to session length.

### Initial validation targets

For a representative mainstream Windows PC:

#### 1080p60
- stable recording with hardware H.264;
- no sustained frame-queue growth;
- click/key overlay response visually within a few frames;
- no progressive memory growth after warm-up;
- no periodic multi-hundred-ms stalls introduced by Arssyut.

#### 4K60
- supported only when preflight shows hardware path is capable;
- app must degrade/warn rather than silently create severe lag.

### Long-session target

After warm-up, a two-hour 1080p60 test must not show monotonic memory/resource growth attributable to the recorder.

---

## 16. Quality profiles

Expose simple product profiles rather than vendor-specific encoder jargon.

### Efficient
- 1080p30 or current resolution;
- lower hardware-encoder quality target;
- minimal color analysis cadence.

### Balanced — default
- source/native resolution up to supported limits;
- 60 FPS when selected;
- quality-biased hardware encoder;
- Clean Screen grade;
- Smart Zoom enabled by user preference.

### High Quality
- higher hardware encoder quality/bitrate target;
- larger output files;
- full presentation effects.

Vendor-specific knobs remain internal and map from the canonical profile.

---

## 17. Failure behavior

The user must receive a specific actionable state for:

- capture permission failure;
- source closed;
- encoder initialization failure;
- hardware encoder overload;
- audio device loss;
- graphics device removal;
- disk full;
- disk write stall;
- temporary-session creation failure;
- finalization failure.

Do not silently “finish” a recording whose muxer or disk failed.

If a recoverable temporary recording exists, never delete it because final MP4 creation failed.

---

## 18. Diagnostics

Local diagnostics should be available for support without becoming a realtime burden.

Capture:

- frames received;
- frames used;
- frames coalesced/dropped;
- render timing;
- encode timing;
- mux latency;
- queue depths;
- audio drift;
- input-event counts;
- GPU/device resets;
- memory snapshots.

No network telemetry is required for the production foundation.

---

## 19. Security and privacy

- no cloud dependency for recording;
- no typed-text transcript by default;
- no secret storage of key events;
- output path validation;
- safe temporary-file permissions;
- pinned/verifiable third-party media binaries if bundled;
- dependency/license manifest;
- no remote code download in the recording hot path.

---

## 20. Upstream reuse requirements

### ArZoom source baseline

Repository:
`masarray/arzoom-follow-obs`

Planning baseline observed:
`ada8f5269246c64429d7aceb6cc72f81e72120ba`

Reusable concepts include:

- click visual fixed slots;
- content-to-output projection;
- kinematic motion synthesizer;
- viewport planner;
- presentation cursor semantics;
- render-safety principles.

### ArVisual source baseline

Repository:
`masarray/arvisual-obs`

Planning baseline observed:
`d0a3f405447446e88dc56f4a50535a17257fcccf`

Reusable concepts include:

- scene statistics;
- adaptive grade parameters;
- neutral/highlight protection;
- gamut/luma safety;
- clarity constraints;
- performance tiers.

Every transplanted source unit must record provenance and licensing.

---

## 21. Release acceptance criteria

A release candidate is not accepted until all applicable gates pass:

1. release x64 build;
2. clean install/portable launch;
3. 1080p30 recording;
4. 1080p60 recording;
5. hardware encoder matrix available on test machines;
6. system audio;
7. microphone audio;
8. click visual;
9. keyboard chord visual;
10. smart/manual zoom;
11. Clean Screen grade;
12. source resize;
13. repeated start/stop;
14. abnormal stop/recovery;
15. disk-full/finalize failure handling;
16. long-session resource test;
17. output metadata inspection;
18. visual review for smoothness/flicker/black frame;
19. no known P0/P1 regressions.

---

## 22. Product success definition

Arssyut succeeds when a user can record a software explanation on an ordinary Windows machine and receive a video that:

- feels smoother and more intentional than a raw desktop capture;
- clearly communicates clicks and important keyboard shortcuts;
- remains easy on CPU because video work stays GPU/hardware accelerated;
- does not require an editor for basic polish;
- remains reliable for long sessions;
- produces a compatible optimized MP4;
- does not expose typed private content merely to provide keyboard visuals.
