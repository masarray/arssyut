# AGENTS.md — Arssyut Production Realtime Engineering Contract

This file is the operating contract for AI/code agents and human contributors modifying Arssyut.

Arssyut is a native Windows screen recorder whose product quality depends on capture reliability, smooth frame pacing, deterministic camera motion, low CPU overhead, GPU-path correctness, audio/video sync, privacy-safe input visualization, bounded memory, recoverable recording, and optimized MP4 output.

The project must not begin as a disposable prototype.

---

## 1. Prime directive

**Do not implement an intentionally naive version when the production architecture is already knowable.**

Prefer the smallest coherent production-quality change that advances the accepted architecture.

Priority order:

1. no recording loss, crash, black frame, corrupt output, or A/V desync;
2. smooth capture/compositor/encoder pacing;
3. one coherent camera/input/render state model;
4. bounded realtime CPU/GPU/memory cost;
5. privacy-safe input handling;
6. recoverable recording and correct final MP4;
7. regression compatibility;
8. maintainability;
9. implementation convenience.

A feature is not successful if it looks correct for a 10-second test but leaks memory, grows queues, blocks capture, causes periodic stalls, or fails after a long recording.

---

## 2. Mandatory engineering workflow

For every non-trivial task:

```text
RECONNAISSANCE
-> BASELINE / REPRODUCE
-> ROOT CAUSE OR REQUIREMENT INVARIANTS
-> OWNER / STATE MODEL
-> ARCHITECTURE IMPACT
-> PERFORMANCE BUDGET
-> IMPLEMENT
-> DETERMINISTIC TEST
-> FAILURE TEST
-> SOAK / RESOURCE CHECK
-> WINDOWS RELEASE BUILD
-> REAL RECORDING VALIDATION
```

Before editing:

- identify the subsystem that owns the behavior;
- identify all consumers of that state;
- identify whether the change touches capture, compositor, camera, input, audio, encoder, muxer, finalizer, UI, or lifecycle;
- read the relevant accepted architecture/PRD sections;
- check whether the proposed change creates a second authority or duplicate pipeline.

If three consecutive patches in one subsystem are still treating symptoms, **STOP**. Patch four requires a fresh ownership/root-cause audit.

Never claim a test or measurement was run when it was not.

---

## 3. Core architecture invariants

Unless an explicitly approved architecture task changes them, preserve all of the following:

- one semantic camera/planner authority;
- one canonical input-event model;
- one canonical recording clock;
- one authoritative recording session state machine;
- no CPU full-frame readback in the steady-state video path;
- no per-frame heap churn in the steady-state video path;
- no unbounded queues, histories, logs, caches, or event lists;
- no detached worker threads;
- no blocking UI work inside capture/render/audio callbacks;
- no arbitrary sleep-based race fixes;
- no duplicated color-grading engine;
- no duplicated shortcut/chord state engine;
- no second hidden zoom state inside UI code;
- no direct final MP4 dependency that can make an interrupted recording unrecoverable;
- every hardware resource has one lifetime owner and deterministic teardown.

The intended pipeline is:

```text
capture -> latest-frame handoff -> canonical state sampling
       -> GPU compositor -> pixel-format conversion -> encoder
       -> bounded mux writer -> recoverable session media
       -> remux/finalize -> MP4
```

---

## 4. Canonicalization is mandatory

Arssyut uses canonical representations so equivalent inputs do not create parallel behavior.

Examples:

- all realtime timestamps convert to the project monotonic/QPC timebase;
- screen/capture coordinates are converted through one explicit mapping chain;
- camera positions use one normalized content-space representation;
- left/right modifier keys are normalized to a canonical shortcut representation where product semantics do not need the distinction;
- shortcut ordering is canonical, e.g. `Ctrl+Shift+S`, never whichever key arrived first;
- recording settings normalize to one validated `RecordingProfile`;
- output sizes, rates, color space, audio format, and encoder decisions are resolved once before the recording hot path begins.

Do not let every subsystem interpret raw OS input or configuration independently.

---

## 5. Coalescing and bounded backpressure

Realtime state has two classes:

### Replaceable state

Examples:

- latest captured frame;
- latest cursor position;
- latest window geometry;
- latest UI settings snapshot;
- latest level meter value.

Replaceable state must generally use **latest-wins coalescing**. Do not queue obsolete intermediate states merely because they arrived.

### Semantic events

Examples:

- mouse click;
- key chord;
- recording start/stop;
- source closed;
- encoder error.

Semantic events use a fixed-capacity bounded queue/ring with an explicit overflow policy.

Rules:

- no queue grows with recording duration;
- queue capacity must be justified;
- queue depth must be observable in diagnostics;
- overflow behavior must be deterministic;
- capture must not build seconds of latency to preserve every frame;
- when video production temporarily outruns encoding, prefer controlled frame coalescing/drop at the replaceable-frame boundary over accumulating latency.

Never solve overload by allocating a larger unbounded buffer.

---

## 6. Thread and worker contract

Every worker has one purpose, one owner, one shutdown path, and a documented synchronization boundary.

Expected topology:

- UI/main thread;
- Windows capture callback/worker;
- dedicated input message/raw-input worker;
- system-audio capture worker;
- microphone capture worker when enabled;
- video pipeline worker owning the realtime compositor/encode submission path;
- bounded mux/I/O worker;
- non-realtime finalizer/recovery worker.

Rules:

- no detached threads;
- shutdown uses stop request -> callback disarm -> producer stop -> drain/abort contract -> join -> resource release;
- callbacks must not outlive their owner;
- do not hold a blocking mutex across capture, render, encoder, or audio callbacks;
- cross-thread realtime data should use atomics, immutable snapshots, SPSC rings, fixed slots, or another bounded handoff justified by measurement;
- UI never directly owns or mutates hot-path state.

---

## 7. Capture hot-path rules

Windows Graphics Capture is the primary Windows capture backend. A fallback backend may exist only behind the same capture contract.

Capture callbacks must:

- acquire/reference the frame;
- stamp it using the canonical timebase;
- publish it into a bounded/latest-frame handoff;
- return quickly.

They must **not**:

- encode;
- run color analysis;
- render overlays;
- format diagnostics;
- write files;
- wait on slow workers;
- append frames into a growing queue.

Source resize, monitor topology change, capture closure, device loss, and access failure are explicit states, not exceptions to ignore.

---

## 8. GPU/render contract

The steady-state video path is GPU-first.

Forbidden in normal per-frame operation:

- CPU copy of the full captured frame;
- per-frame shader compilation;
- per-frame texture creation/destruction;
- filesystem access;
- image decoding;
- font creation;
- rebuilding keycap geometry;
- dynamic resource churn that can be prepared ahead of time.

Resources such as textures, render targets, glyph atlases, samplers, and conversion surfaces are retained/pool-managed and rebuilt transactionally only when dimensions/device/configuration change.

Every shader constant/parameter used by a draw path must receive a deterministic finite value before drawing.

Black output, NaNs, stale GPU state, device-removed loops, and incorrect resource reuse are P0/P1 regressions.

---

## 9. ArZoom transplant contract

Arssyut reuses behavior from `masarray/arzoom-follow-obs`, but does not import OBS state ownership.

Portable concepts to preserve include:

- deterministic viewport planning;
- jerk-limited kinematic camera motion;
- time-based integration with bounded substeps;
- hysteresis/dead-zone behavior;
- content-anchored click feedback;
- bounded fixed-size click history;
- cursor/camera mapping semantics;
- single camera authority.

The recorder must not create:

- one live zoom engine plus a second export zoom engine;
- UI-specific camera truth;
- click logic that independently retargets camera unless the planner contract says it may;
- arbitrary frame-rate-dependent lerp constants.

Camera tests must include equivalent pointer trajectories at multiple frame rates and verify materially equivalent motion.

### P3R parity lock

After the first real-recording audit, ArZoom integration has stricter rules:

- `third_party/arzoom/include` is an immutable vendored snapshot of the pinned
  upstream commit; do not edit those files to customize Arssyut;
- Arssyut-specific behavior belongs in thin adapters/intent producers;
- the normal recorder path uses upstream `PresenterAwareSmartCamera` with
  scene context disabled, preserving the accepted per-source SmartCamera
  gimbal behavior;
- the default presentation profile is Smart follow + Cinematic motion, safe
  zone 0.28, anchor (0.50, 0.45);
- WGC source cadence and virtual-camera cadence are independent: retain the
  latest GPU source and re-composite camera/presentation on every scheduled
  output frame;
- never optimize static desktop capture by reusing an already-composited output
  while camera/click/presentation state is moving;
- ArZoom owns camera behavior and content projection. Click appearance may use
  an Arssyut product skin after direct visual validation, but it must remain
  content-anchored to the same camera transform, fixed-capacity, analytic on
  GPU, and allocation-free in the frame hot path;
- CI must retain a frame-by-frame adapter-vs-upstream parity gate.


---

## 10. ArVisual transplant contract

Arssyut reuses color behavior from `masarray/arvisual-obs`, not OBS plugin plumbing.

Preserve:

- neutral/white protection;
- highlight protection;
- bounded scene adaptation;
- skin-specific constraints where applicable;
- luminance/gamut safety;
- anti-halo clarity behavior;
- deterministic safe fallback.

Scene analysis must be decoupled from the 60-fps render requirement. It may run at a lower bounded cadence using asynchronous/downsampled data.

The color engine must never force a synchronous full-frame GPU->CPU readback.

A technical/pixel-faithful recording mode must remain available because aggressive grading is inappropriate for some engineering/UI capture.

### P5A standalone-grade lock

After the first ArVisual transplant:

- Pixel Accurate remains the default. The standalone grade must be explicitly
  enabled by recorder configuration;
- P5A ports only pinned portable shader behavior from
  `masarray/arvisual-obs@d0a3f405447446e88dc56f4a50535a17257fcccf`;
- OBS source/filter ownership, property plumbing, texrender/stagesurface logic,
  and CPU scene analysis do not enter the Arssyut compositor;
- adaptive inputs stay at deterministic neutral values until P5B provides the
  asynchronous analysis authority;
- grading occurs on the captured desktop sample before click and keyboard
  overlays. Since P4R.3 restores native WGC cursor capture, the native cursor
  is part of the captured source and follows the same color/camera path;
- grade ON/OFF and parameter changes are constant-buffer changes only and must
  not allocate textures, render targets or staging resources;
- no synchronous GPU-to-CPU readback is permitted in the render path;
- neutral balance, highlight/gamut safety, bypass pixel accuracy and retained
  resource behavior must remain under automated tests.

### P5B asynchronous-analysis lock

After Smart Auto analysis is introduced:

- scene analysis uses a fixed 64x36 downsample target and exactly two retained
  staging/read-later slots;
- analysis submission cadence is capped at 5 Hz and is attempted only for a
  fresh retained WGC source;
- headless analysis batches are explicitly submitted at the 5 Hz boundary
  using asynchronous ID3D11DeviceContext3::Flush1 when available; this is a
  command submission only, never a GPU completion wait;
- GPU readiness is polled only with D3D11_ASYNC_GETDATA_DONOTFLUSH;
- a staging texture may be mapped only after its EVENT query reports ready;
- if both staging slots are pending, skip the analysis sample. Never spin-wait,
  stall an output frame, or allocate an extra staging surface;
- scene statistics use fixed histograms and the pinned ArVisual 0.65 s EMA;
- EMA is time-based from scene-sample intervals, not output-frame count;
- failed/unavailable analysis is a soft fallback: P5A static grade remains
  active and recording continues;
- the last valid adaptive state remains authoritative until a newer completed
  sample is available;
- Smart Auto telemetry must expose availability, submitted/completed samples,
  busy skips, and map failures;
- no analysis implementation may introduce a synchronous full-frame
  GPU-to-CPU readback.

---

## 11. Input visualization and privacy contract

The default keyboard feature visualizes **actions/shortcuts**, not typed content.

Default behavior:

- show modifier chords such as `Ctrl+C`, `Ctrl+V`, `Ctrl+Z`, `Ctrl+A`, `Alt+Tab`, `Win+D`, function/system keys, and configured recorder actions;
- hide ordinary alphanumeric typing unless the user explicitly enables a broader mode;
- suppress auto-repeat noise unless repeat is semantically useful;
- canonicalize modifiers;
- coalesce duplicate display events within a short defined window;
- store only what is required for visualization.

Raw Input is preferred for global keyboard/mouse observation where practical. Low-level hooks are fallback infrastructure and must return immediately after publishing a compact event.

Do not persist a keylogger-style transcript.

Secrets typed into password fields must not become recoverable from normal session metadata.

### P4R keyboard-visualizer lock

After the first shortcut-overlay audit:

- runtime keyboard state is a fixed structured keycap array, never a formatted
  transcript string that another layer must parse;
- at most four canonical modifiers plus one action key are retained;
- modifier order is always Ctrl, Shift, Alt, Win, action;
- ordinary unmodified printable input is hidden by default;
- modified printable input may be shown as an action chord, but never with
  clipboard/text contents;
- Raw Input remains the primary backend. Direct Windows validation on
  2026-10-02 demonstrated a concrete Win+R gap, so WH_KEYBOARD_LL is allowed
  only as a narrow supplemental source for Windows-key/system chords;
- the low-level hook must publish only compact semantic action events, perform
  no rendering/string/log/file work, and return immediately to CallNextHookEx;
- Raw Input and hook events are deduplicated by the canonical semantic chord
  reducer; do not create a second overlay authority;
- keycap texture, DIB, font, brushes and pens are retained resources;
- shortcut generation changes may update the retained texture, but must not
  create per-frame GPU/GDI resources;
- keycap rendering is semantic-size and transparent; do not reintroduce a
  large fixed background panel around a small shortcut;
- all production keycaps use one light/white physical-key language; modifiers
  must not switch to dark/black surfaces;
- the Windows modifier uses a Windows-logo glyph rather than the text "Win";
- the supplied keyboard-button artwork is the visual design direction, not a
  reason to parse or rasterize its large traced SVG in the realtime path.

### P4R.3 presentation reliability lock

Direct 1080p60 validation on 2026-10-03 superseded the P4R.2 custom-cursor
experiment. The authoritative rules are now:

- native Windows/WGC cursor capture remains enabled even when presentation
  features are active; do not add a second custom cursor compositor;
- Raw Input pointer position remains an intent/targeting source for ArZoom and
  click coordinates only. Cursor pixels themselves come from WGC;
- because the native cursor is part of the retained desktop source, it follows
  the same ArZoom sampling/scale as the captured content;
- do not reintroduce ballistic cursor scaling, cursor trails, cursor texture
  caches, per-frame cursor rasterization, or velocity-reactive cursor sizing;
- ordinary alphanumeric, digit, symbol, function and Windows-logo keycaps use
  one square 1u physical geometry; long keys use explicit keyboard units rather
  than text-measured width;
- Ctrl/Alt use 1.25u, Shift/Enter 1.5u, Backspace 2u and Space 3.5u;
- click feedback remains one analytic ring with no center fill and no visible
  second ring;
- one ring may contain a bright core, near bloom and diffuse halo derived from
  the same ring distance field;
- bright-surface visibility may use restrained chromatic support around that
  same ring because additive light alone cannot exceed white;
- rapid same-target clicks may recharge one active pulse instead of stacking
  concentric geometry;
- real-recording validation outranks synthetic visual assumptions for cursor
  stability, keycap proportions and click energy.


---

## 12. Audio contract

System audio and microphone capture use separate explicit sources and one canonical media timebase.

Requirements:

- bounded audio buffering;
- defined resampling format;
- deterministic mix policy;
- no session-length accumulation;
- observable underflow/overflow/drift counters;
- device disconnect/reconnect is an explicit state;
- long-session A/V drift must be measured.

Do not “fix” drift by periodic arbitrary sleeps or large discontinuous audio drops without a documented sync algorithm.

---

## 13. Encoder and MP4 contract

The default production path prefers a hardware H.264 encoder available on the active graphics adapter.

Encoder selection is resolved before recording and represented canonically.

Rules:

- prefer hardware encoding for the lightweight default;
- never silently switch encoder semantics mid-session unless the architecture explicitly supports safe renegotiation;
- no unbounded frame queue ahead of the encoder;
- output cadence is deliberate and tested;
- software fallback must have an explicit performance policy;
- codec failures return controlled session errors.

Arssyut must not risk an entire recording solely because a normal MP4 final atom was not written.

Production recording therefore uses a recoverable temporary media strategy. Final MP4 generation is remux/finalization without video re-encoding whenever the selected codec/container combination allows it.

The final MP4 must be checked for:

- playable duration;
- expected stream count;
- H.264/AAC metadata;
- monotonic timestamps;
- A/V sync;
- seekability;
- fast-start/web-friendly metadata placement where configured.

---

## 14. Memory/resource lifecycle

Every owned OS/GPU/media object uses RAII or another explicit ownership wrapper.

Required:

- fixed-capacity frame/event/audio pools;
- no history proportional to recording length;
- no leaked COM references;
- no stale capture callbacks;
- no texture/resource leak after repeated start/stop cycles;
- no font/glyph cache growth from arbitrary text;
- bounded diagnostic memory.

Any cache must document:

- key;
- maximum size;
- eviction/reuse policy;
- owner;
- reset condition.

Long-session testing is mandatory for changes touching capture, render, encoding, mux, input history, or audio buffering.

---

## 15. Exception and error-boundary rules

Expected realtime failures do not use exceptions as ordinary control flow.

Use compact explicit status/result contracts for hot paths.

No exception may unwind through:

- capture callback;
- input hook/raw-input callback;
- audio callback;
- render loop;
- encode submission loop.

Framework/filesystem/configuration exceptions may occur at non-realtime boundaries; catch them at the nearest meaningful boundary and convert them to structured state/diagnostics.

Configuration/resource promotion is transactional:

```text
VALIDATE -> PREPARE CANDIDATE -> VERIFY -> COMMIT
                                 \-> retain last-known-good / safe fallback
```

---

## 16. Diagnostics contract

Realtime diagnostics are bounded and non-blocking.

Hot paths may emit:

- stable numeric error code;
- counters;
- small timing samples;
- queue depth;
- dropped/coalesced counts;
- tiny fixed context fields.

Hot paths must not:

- serialize JSON;
- generate stack traces;
- repeatedly format long strings;
- write logs/files synchronously;
- perform telemetry/network I/O.

Human-readable formatting belongs off the realtime path.

Repeated errors are deduplicated/rate-limited.

---

## 17. Performance discipline

Do not claim an optimization without before/after evidence.

Measure as applicable:

- capture callback duration;
- video pipeline CPU time;
- GPU compositor time;
- encode submit latency;
- mux write latency;
- frame coalesce/drop rate;
- audio queue depth;
- A/V drift;
- allocation rate;
- committed/private memory over time;
- GPU resource count;
- input-event-to-visible-overlay latency;
- start/stop/finalize latency.

Optimization order:

1. remove unnecessary work;
2. coalesce replaceable work;
3. retain/reuse resources;
4. reduce copies;
5. move appropriate work to GPU;
6. isolate slow non-realtime work;
7. only then consider additional workers/caches.

A worker thread is not automatically an optimization.

---

## 18. UI discipline

Arssyut is intended to feel handy and compact.

UI is a control surface, not a second engine.

Do not:

- create oversized card-heavy layouts for basic recorder controls;
- poll engine state at high frequency when event/snapshot delivery is available;
- run capture/analysis/encoding logic in view-models;
- rebuild complex UI trees every frame;
- hide important recording failure state behind cosmetic animation.

The recording overlay must be low-overhead, capture-safe, and excluded from capture when required by the selected backend.

---

## 19. Testing gates

Changes must add the smallest meaningful test at the owning layer.

Required categories as applicable:

### Deterministic
- camera trajectory and settling;
- click lifetime/slot reuse;
- chord canonicalization;
- coalescing policy;
- timestamp conversion;
- encoder profile mapping;
- color parameter bounds.

### Failure
- capture source closes;
- source resizes;
- D3D device removal;
- encoder unavailable;
- audio device disconnect;
- disk becomes slow/full;
- temporary file/finalize failure;
- malformed settings;
- queue saturation.

### Resource
- repeated start/stop;
- long 1080p60 recording;
- representative 4K recording;
- no monotonic memory/resource growth after warm-up.

### Output
- ffprobe/media inspection contract;
- duration/timestamps;
- A/V sync;
- seeking;
- recovery/finalization.

### Visual
- click anchor through moving zoom;
- keycap placement;
- camera smoothness;
- color-grade neutral/highlight safety;
- no black/flicker frames.

---

## 20. Change discipline

Do not:

- mix unrelated refactors into a focused change;
- add a second state authority because wiring the current owner is harder;
- add an unbounded queue “temporarily”;
- use per-frame allocation-heavy abstractions in the hot path without evidence;
- hide bugs with arbitrary clamps/delays;
- copy OBS-specific glue into the standalone core;
- add dependencies without evaluating binary size, licensing, security, runtime cost, update burden, and hardware compatibility;
- weaken performance/output tests to make a feature pass.

Source imported from ArZoom/ArVisual must have documented provenance and compatible licensing.

---

## 21. Definition of done

A task is not done because it compiles.

Depending on scope, done means:

```text
DETERMINISTIC TESTS
+ FAILURE TESTS
+ RESOURCE/LIFETIME CHECK
+ PERFORMANCE CHECK
+ WINDOWS RELEASE BUILD
+ REAL RECORDING
+ OUTPUT INSPECTION
+ VISUAL VALIDATION
```

For hot-path changes, include measured evidence.

For output changes, inspect the produced media.

For lifecycle changes, test repeated start/stop and abnormal stop.

---

## 22. Completion report

Every substantial completion report must state:

- what changed;
- why this subsystem owns the change;
- architecture/state decision;
- invariants preserved;
- failure/fallback behavior;
- regression tests added/updated;
- performance/resource evidence;
- exact validation executed;
- known remaining limitation;
- next milestone, if work stops.

---

## Final rule

Think like the engineer responsible for a two-hour customer recording that cannot be recreated.

Keep the pipeline bounded, keep state canonical, coalesce obsolete work, preserve one authority per concept, release every resource deterministically, prefer GPU-native processing, protect privacy, and require evidence before calling the recorder smooth, lightweight, or production-ready.
