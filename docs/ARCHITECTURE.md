# Arssyut Architecture — Native Realtime Recording Pipeline

Status: **Phase 0 architecture contract**  
Platform focus: **Windows 10/11 x64**  
Primary graphics API: **D3D11**  
Primary capture API: **Windows.Graphics.Capture**

---

## 1. Why this architecture

Arssyut is not built as a chain of screenshots piped through CPU image processing.

That approach is easy to prototype but conflicts with the product goals:

- high CPU use;
- memory copies;
- frame latency;
- poor 4K scaling;
- stutter when UI/GC/encoding work overlaps;
- difficulty reusing ArZoom/ArVisual efficiently.

The production pipeline therefore keeps the video path on GPU for as long as possible and treats every realtime handoff as bounded.

---

## 2. High-level topology

```text
                                      +--------------------+
                                      | Compact UI / Tray  |
                                      +---------+----------+
                                                |
                                   immutable config/session commands
                                                |
                                                v
+----------------------+              +---------------------+
| Windows Capture      |--latest----->| Recording Session   |
| WGC / future DXGI    |    frame     | Coordinator         |
+----------------------+              +---------+-----------+
                                                |
+----------------------+                        | canonical snapshot
| Raw Input worker     |--events/latest-------->|
| mouse / keyboard     |                        v
+----------------------+              +---------------------+
                                      | Video Pipeline      |
+----------------------+              | worker              |
| WASAPI loopback      |--audio------>| - camera planner    |
+----------------------+              | - D3D11 compositor  |
                                      | - overlays          |
+----------------------+              | - ArVisual grade    |
| WASAPI microphone    |--audio------>| - RGB -> NV12       |
+----------------------+              | - encode submit     |
                                      +---------+-----------+
                                                |
                                           encoded packets
                                                |
                                                v
                                      +---------------------+
                                      | Bounded mux writer  |
                                      +---------+-----------+
                                                |
                                                v
                                      recoverable temp media
                                                |
                                                v
                                      +---------------------+
                                      | Finalizer / remux   |
                                      +---------+-----------+
                                                |
                                                v
                                           final .mp4
```

---

## 3. Repository layout target

Proposed layout:

```text
/
├─ AGENTS.md
├─ CMakeLists.txt
├─ cmake/
├─ docs/
├─ src/
│  ├─ app/                 # bootstrap, session coordinator
│  ├─ ui/                  # thin native UI shell
│  ├─ core/
│  │  ├─ time/
│  │  ├─ result/
│  │  ├─ diagnostics/
│  │  ├─ camera/
│  │  ├─ input/
│  │  ├─ color/
│  │  ├─ audio/
│  │  └─ media/
│  ├─ platform/windows/
│  │  ├─ capture/
│  │  ├─ input/
│  │  ├─ audio/
│  │  ├─ graphics/
│  │  └─ shell/
│  ├─ render/
│  │  ├─ compositor/
│  │  ├─ overlays/
│  │  ├─ shaders/
│  │  └─ color/
│  ├─ encode/
│  ├─ mux/
│  └─ recovery/
├─ tests/
│  ├─ unit/
│  ├─ simulation/
│  ├─ media/
│  └─ soak/
├─ third_party/
└─ tools/
```

OBS-specific types must not leak into `src/core`.

---

## 4. Canonical timebase

Every realtime subsystem must convert into one monotonic project clock.

Recommended Windows source:

- QueryPerformanceCounter / QueryPerformanceFrequency;
- store project timestamps in integer ticks or canonical 100-ns units;
- no wall-clock time for synchronization.

Wall-clock time is allowed only for:

- filenames;
- UI display;
- logs/metadata that are not used for media synchronization.

### Timestamped primitives

```cpp
struct TimePoint {
    int64_t ticks;
};

struct FrameStamp {
    uint64_t sequence;
    TimePoint captured_at;
};

struct ActionStamp {
    TimePoint happened_at;
};
```

Actual types may differ, but all producers must enter the same time domain before being consumed together.

---

## 5. Windows capture backend

### Primary: Windows Graphics Capture

Use:

- `Windows.Graphics.Capture`;
- D3D11 device-backed frame pool;
- free-threaded frame pool where appropriate;
- a small fixed buffer count;
- capture monitor/window natively.

The frame-arrived callback does minimal work.

### Callback contract

```text
FrameArrived:
    try acquire newest frame
    validate dimensions/device
    stamp frame
    publish/replace latest-frame slot
    signal pipeline
    return
```

Never run:

- camera planning;
- color analysis;
- overlay rendering;
- encoding;
- disk writes;
- verbose logging

inside FrameArrived.

### Latest-frame slot

Captured frames are replaceable state.

Use a fixed handoff that lets a new frame replace an obsolete frame not yet consumed.

If three capture frames arrive while the pipeline can only process one, process the most relevant/latest frame rather than building latency.

### Resize

When source dimensions change:

1. detect;
2. mark reconfigure request;
3. coalesce repeated resize events;
4. prepare replacement frame/render resources;
5. commit coherently;
6. release retired resources only when no longer referenced.

---

## 6. Future DXGI Desktop Duplication backend

Desktop Duplication remains a fallback/advanced backend candidate because it exposes desktop surfaces plus dirty/move/pointer metadata.

It must implement the same `ICaptureSource`-style contract as WGC.

No product feature may depend directly on WGC-specific state outside the platform backend.

---

## 7. D3D11 device ownership

Use one primary D3D11 adapter/device aligned with the captured/output monitor and selected encoder where practical.

Goals:

- avoid cross-adapter copies;
- avoid CPU staging;
- share textures internally;
- keep one clear immediate-context owner.

### Immediate context

The realtime video pipeline worker should own normal immediate-context render use.

Do not let arbitrary UI/capture threads issue D3D11 immediate-context work.

Background resource preparation may use deferred contexts only if measurement shows benefit and lifetime/synchronization are explicit.

---

## 8. Video pipeline cadence

The output profile owns the media cadence.

For 60 FPS:

- target frame period ≈ 16.667 ms;
- each output frame samples the latest valid capture/input state;
- semantic events with timestamps are applied in order;
- stale captured frames may be replaced;
- output PTS remains monotonic.

This separates capture arrival jitter from output media cadence.

### Why

A desktop may not produce changes at exactly 60 Hz. The recorder still needs a predictable compatible output timeline.

The encoder is allowed to compress duplicate/static frames efficiently.

---

## 9. Camera engine transplant

ArZoom is the behavioral baseline, not an OBS dependency.

### Reuse candidates

From ArZoom:

- vector/math utilities;
- fixed-size `ClickVisualState`;
- content/output projection model;
- `SceneKinematicMotion`;
- stable motion limits;
- viewport planning/hysteresis concepts;
- presentation cursor semantics.

### New standalone boundary

```text
CanonicalInputState
        |
        v
CameraIntentReducer
        |
        v
ViewportPlanner  (WHERE)
        |
        v
KinematicMotion  (HOW)
        |
        v
CameraTransform
```

One transform is handed to every visual consumer:

- source crop/zoom;
- cursor;
- click pulse;
- spotlight if added later.

This prevents small mapping disagreements between effects.

### Camera transform

Conceptually:

```text
content coordinate
-> source crop/region mapping
-> camera normalized space
-> output frame space
```

No component may invent its own offset based on observed screenshots.

---

## 10. Smart zoom behavior

The recorder should not continuously chase raw pointer coordinates.

Instead use an intent reducer.

Possible inputs:

- click;
- pointer settlement;
- pointer velocity;
- manual zoom command;
- keyboard-action burst;
- current camera hold state.

The planner decides when intent is strong enough to wake/retarget.

This preserves ArZoom's core principle: start following earlier through state/pressure, not by using violent camera acceleration later.

### Future auto-zoom event clustering

If activity clustering is added, it must remain bounded:

- fixed recent time window;
- fixed maximum events;
- no whole-session analysis in realtime;
- no growing trajectory history.

---

## 11. Input subsystem

### Primary input path

Use Raw Input on a dedicated hidden/message window where practical.

Reasons:

- asynchronous input observation;
- no heavy low-level hook callback;
- suitable for keyboard/mouse device events;
- keeps work out of application UI.

### Low-level hook fallback

Use `WH_KEYBOARD_LL` / mouse hook only for cases not covered correctly by Raw Input.

Hook callbacks:

- normalize the minimal event;
- write to bounded SPSC/event channel;
- call next hook/return immediately.

No chord assembly in the hook.

### Pointer state

Pointer position is replaceable state.

Store latest:

- desktop coordinates;
- mapped source coordinate when valid;
- buttons/modifier snapshot;
- timestamp.

### Clicks

Clicks are semantic events.

Publish:

- button;
- canonical timestamp;
- desktop/source-normalized location.

---

## 12. Keyboard chord reducer

The keyboard visualizer is a semantic reducer, not a key logger.

### Pipeline

```text
raw key transition
 -> normalize scan/key/modifier
 -> maintain tiny pressed-state bitmap
 -> detect displayable action/chord
 -> canonical chord
 -> dedupe/coalesce
 -> OverlayEvent ring
```

### Canonicalization

Examples:

```text
LeftCtrl + C   -> Ctrl+C
RightCtrl + C  -> Ctrl+C
Shift + Ctrl + S -> Ctrl+Shift+S
```

### Privacy

Default event generation ignores plain normal typing.

No string reconstruction.

No clipboard contents.

No focused-field text extraction.

### Keycap rendering

Do not decode SVGs per frame.

Use:

- retained rounded keycap geometry;
- GPU-instanced quads or vector path cache;
- glyph atlas/DirectWrite-backed texture cache;
- fixed maximum visible chord count.

---

## 13. Click visual rendering

Preserve ArZoom's fixed-size click history.

A click event stores content-space position.

Every output frame:

```text
click.content_position
 -> current CameraTransform
 -> output position
 -> GPU pulse parameters
```

Therefore the pulse follows the clicked content during camera movement.

No CPU sprite rasterization.

---

## 14. ArVisual color engine transplant

ArVisual currently proves several useful concepts:

- very small analysis image;
- histogram/statistics;
- bounded EMA scene adaptation;
- GPU effect stage;
- safe fallback.

### Standalone implementation

Use two decoupled stages:

#### Stage A — analysis
- downsample source/output to e.g. 64x36;
- run at bounded cadence such as 6–12 Hz, not necessarily every video frame;
- use double-buffered asynchronous staging or future compute reduction;
- never block video render waiting for readback;
- if result is not ready, skip and retain last valid statistics.

#### Stage B — grade
- runs every output frame on GPU;
- consumes immutable validated grade parameters.

### Why not copy OBS code directly

OBS graphics resource APIs, property state, and filter callbacks are implementation glue.

The visual behavior is portable; the ownership model is not.

---

## 15. Compositor pass plan

Target a small fixed pass graph.

Example:

```text
Captured D3D11 texture
        |
        v
Pass 1: camera crop/scale + base color grade
        |
        v
Pass 2: overlay batch
        |  cursor
        |  click pulses
        |  keyboard keycaps
        v
RGB output texture
        |
        v
D3D11 Video Processor / validated conversion
        |
        v
NV12 encoder surface
```

Where shader/runtime constraints allow, some work may be fused.

Do not increase GPU pass count for cosmetic separation unless measured cost is acceptable.

---

## 16. Text/glyph strategy

Keyboard overlay requires text but must not create fonts or rasterize glyphs every frame.

Preferred strategy:

- resolve UI font once;
- glyph cache/atlas;
- limited character set for key labels;
- bounded atlas size;
- deterministic fallback glyph;
- rebuild only on DPI/theme/font-size change.

Do not cache arbitrary typed strings because normal typing is not a default feature.

---

## 17. Audio capture architecture

### Sources

- system output via WASAPI loopback;
- microphone via WASAPI capture.

Each runs on an explicit worker/event-driven client.

### Canonical format

Internally normalize to:

- 48 kHz;
- float32 or another chosen canonical mix format;
- explicit channel layout.

### Audio clocking

Capture timestamps are mapped to the canonical project clock.

A bounded resampler/drift-correction stage accounts for independent device clocks.

### Mixing

```text
system audio ----                  -> gain -> mix -> limiter/safety -> AAC encoder
microphone ------/
```

Do not store the entire session's raw PCM in memory.

---

## 18. Encoder abstraction

Define a canonical interface that hides vendor differences.

Conceptually:

```cpp
struct VideoEncodeConfig {
    Size size;
    Rational fps;
    QualityPreset quality;
    ColorFormat format;
    int keyframe_interval;
};

class IVideoEncoder {
public:
    virtual Result prepare(const VideoEncodeConfig&) = 0;
    virtual SubmitResult submit(GpuFrame, TimePoint pts) = 0;
    virtual Result drain() = 0;
};
```

Actual interface may differ.

### Hardware preference

Detect validated candidates before recording.

Prefer an encoder on the same useful adapter.

Possible implementations may be provided by FFmpeg/libavcodec, Media Foundation, or vendor paths, but the product layer must never know vendor command-line details.

### No mid-session surprise

Encoder selection is frozen for a session unless a future explicit renegotiation contract is designed.

---

## 19. Media library strategy

Two viable implementation families exist:

### A. FFmpeg libraries

Pros:
- mature codec/container ecosystem;
- NVENC/QSV/AMF integration;
- robust mux/remux;
- strong diagnostics.

Cons:
- binary/dependency size;
- licensing/build complexity;
- D3D11 zero-copy integration requires careful hwcontext design.

### B. Windows Media Foundation + native muxing

Pros:
- Windows-native;
- smaller external dependency surface.

Cons:
- hardware behavior varies by installed MFTs;
- container/finalization/recovery behavior needs more custom engineering;
- less uniform vendor control.

### Phase-1 decision gate

Run a spike comparing:

- D3D11 texture handoff;
- encoder initialization reliability;
- hardware coverage;
- CPU overhead;
- binary footprint;
- crash-safe mux/finalization;
- output compatibility.

Do not commit the entire architecture to a library merely because a hello-world encode succeeds.

The rest of this architecture remains library-agnostic.

---

## 20. Crash-safe mux/finalization

Preferred logical contract:

```text
recording:
  session.part.<recoverable-container>

clean stop:
  drain encoders
  close temp mux
  verify temp media
  remux -> output.tmp.mp4
  verify MP4
  atomic rename -> output.mp4
  delete temp only after success
```

### Recovery

At startup:

1. inspect prior unfinished session manifests;
2. never auto-delete recoverable media;
3. offer Recover / Keep / Discard;
4. recovery runs outside realtime recording thread.

A session manifest should be updated transactionally and contain only the metadata required for recovery.

---

## 21. Mux writer

Encoded packets are not freely droppable like raw capture frames.

Use a bounded packet queue and explicit sustained-disk-stall behavior.

If disk cannot keep up:

- surface warning/diagnostic early;
- apply bounded backpressure to video pipeline;
- coalesce future replaceable input frames before encoding;
- if storage remains unable to sustain output, transition to controlled recording failure while preserving already recoverable media.

Do not continue allocating packets until memory exhaustion.

---

## 22. Configuration model

UI settings are normalized before entering the hot path.

Example:

```text
UserSettings
   -> validate
   -> capability probe
   -> resolve
   -> RecordingProfile
```

`RecordingProfile` should include resolved:

- source;
- crop;
- output size;
- FPS;
- camera mode;
- overlay styles;
- grade mode;
- audio sources;
- encoder;
- encoder quality;
- destination;
- recovery strategy.

Recording threads consume the immutable resolved profile.

---

## 23. Session lifecycle

The session coordinator owns all workers/resources.

### Start

```text
validate profile
-> probe destination/capabilities
-> create temp session
-> create D3D resources
-> prepare capture
-> prepare audio
-> prepare encoder
-> prepare mux
-> arm input
-> countdown
-> start canonical media clock
-> Recording
```

If preparation fails, unwind in reverse and remain idle.

### Stop

```text
stop accepting new semantic commands
-> stop capture/input producers
-> complete current video/audio boundary
-> drain encoders
-> close mux
-> release realtime workers/resources
-> Finalizing
-> remux/verify/promote MP4
-> Ready
```

### Destroy

Never destroy session-owned state while a callback may still reference it.

---

## 24. Diagnostics architecture

Use a small lock-free/bounded event/counter channel.

Examples:

```text
capture_frames_received
capture_frames_replaced
video_frames_rendered
video_frames_skipped
encoder_submit_us
mux_queue_depth
audio_underflows
audio_overflows
audio_drift_us
input_events_dropped
device_resets
```

A low-priority consumer may format snapshots for UI/logs.

If the diagnostic consumer stops, recording continues.

---

## 25. Memory model

### Fixed pools

Use fixed/retained:

- WGC frame pool;
- compositor target textures;
- encoder input surfaces;
- input event ring;
- click slots;
- chord overlay slots;
- audio ring;
- encoded packet queue.

### Forbidden growth

No:

- vector of all mouse points;
- vector of every frame;
- list of all clicks for the full recording in live mode;
- unlimited log lines in memory;
- cache keyed by arbitrary window/title/text content.

If future Studio mode needs full edit metadata, stream it incrementally to a sidecar file rather than retaining it all in RAM.

---

## 26. Device loss and reconfiguration

Graphics device removal is not a loop of blind retries.

Transition:

```text
detect device removed
-> stop GPU submissions
-> capture/mux current recoverable state
-> attempt bounded reinitialization if session contract permits
or
-> controlled RecoverableError
```

The first production release may choose controlled stop rather than risky live device migration.

Correct failure is better than corrupt output.

---

## 27. Color-space scope

P0: SDR.

Expected default desktop path:

```text
captured BGRA/RGBA
-> known linear/gamma handling
-> presentation grade
-> Rec.709-compatible YUV conversion
-> NV12
-> H.264
```

Do not pretend HDR is supported by simply accepting a 10-bit display.

HDR requires an explicit later contract for:

- capture format;
- transfer function;
- tone mapping;
- 10-bit output;
- encoder/container metadata.

---

## 28. Performance measurement points

Instrument boundaries with low-overhead timers:

```text
T0 capture event
T1 latest-frame publish
T2 compositor start
T3 compositor end
T4 encoder submit return
T5 encoded packet received
T6 mux write complete
```

For input:

```text
I0 raw input timestamp
I1 canonical event
I2 frame including overlay
```

This allows diagnosing whether lag is capture, render, encode, disk, or input visualization.

---

## 29. Test architecture

### Pure core tests

No GPU required:

- chord reducer;
- click slots;
- camera planner;
- motion integrator;
- coordinate mapping;
- queue/coalescing policies;
- config normalization;
- media timestamp math.

### GPU tests

- shader compile/ABI;
- known-frame render goldens;
- color bounds;
- camera crop;
- click anchoring;
- keycap overlay;
- resource recreate.

### Integration

- WGC source;
- encoder candidate;
- WASAPI loopback;
- mic;
- mux/finalize;
- recovery.

### Soak

Automated/semiautomated long session with periodic metric snapshots.

---

## 30. Upstream source provenance

Planning snapshots:

### ArZoom

- repo: `masarray/arzoom-follow-obs`
- observed HEAD: `ada8f5269246c64429d7aceb6cc72f81e72120ba`
- key source candidates:
  - `src/arzoom-click-visual.hpp`
  - `src/arzoom-math.hpp`
  - `src/arzoom-scene-motion-synthesizer.hpp`
  - `src/arzoom-scene-viewport-planner.hpp`
  - related presentation cursor/mapping files

### ArVisual

- repo: `masarray/arvisual-obs`
- observed HEAD: `d0a3f405447446e88dc56f4a50535a17257fcccf`
- key source candidates:
  - `src/arvisual-filter.cpp`
  - grading shader/effect under plugin data

Before transplanting code, create a provenance document identifying:

- upstream file;
- upstream commit;
- copied/adapted sections;
- licensing;
- standalone changes;
- parity tests.

---

## Final architecture rule

There is no “temporary simple recorder path” and “later production path”.

The first implementation may be small, but it must already use the same bounded capture -> canonical state -> GPU compositor -> encoder -> recoverable mux architecture intended for production.
