# Arssyut Delivery Strategy and Milestones

This roadmap is deliberately gate-based. Each milestone must prove the foundation it owns before later presentation features are stacked on top.

The project must move forward without the recurring pattern of “prototype -> rewrite -> regression”.

---

## Strategy

Build in this order:

```text
P0 Foundation
-> P1 Capture + Clock + Recoverable Media
-> P2 Audio + Encoder + MP4
-> P3 ArZoom Camera + Click
-> P4 Keyboard Actions
-> P5 ArVisual Color
-> P6 Compact Product UI
-> P7 Hardening / Soak / Recovery
-> P8 Release Candidate
```

Why this order:

- beautiful overlays are irrelevant if the recorder loses frames;
- camera tuning is meaningless before frame timing is stable;
- color grading must not be blamed for encoder/capture problems;
- UI should bind to a mature session state machine, not invent it.

---

## P0 — Foundation / production skeleton

### Deliverables

- CMake project and Visual Studio build.
- Core Result/status primitives.
- Canonical monotonic clock.
- Session state machine.
- Bounded diagnostics counters.
- SPSC/fixed-capacity queue utilities where justified.
- D3D11 device owner.
- Unit-test harness.
- CI build.
- dependency/license manifest mechanism.
- source-provenance document for ArZoom/ArVisual transplant.

### Gates

- release x64 build succeeds;
- start/stop empty session state is deterministic;
- queue saturation tests exist;
- no detached threads;
- no circular ownership in session coordinator.

### Explicit non-goal

Do not add a fake GDI screenshot recorder as a shortcut.

---

## P1 — Native capture and recoverable video skeleton

### Deliverables

- Windows Graphics Capture monitor source.
- Window source.
- region crop through GPU.
- free-threaded/bounded frame pool.
- latest-frame coalescing handoff.
- output frame scheduler at 30/60 FPS.
- basic D3D11 copy/scale compositor.
- temporary recoverable video-session container.
- source resize handling.
- captured-source closure handling.

### Validation

- 1080p30 30-minute recording skeleton;
- 1080p60 30-minute recording skeleton;
- repeated start/stop x100;
- source resize during recording;
- no frame queue growth;
- no per-frame texture creation;
- memory snapshot after warm-up remains stable.

### Performance evidence

Record:

- capture callback p50/p95/p99;
- frames received/used/replaced;
- compositor CPU/GPU timing;
- working/private bytes over time.

---

## P2 — Audio, hardware encode, mux, optimized MP4

This milestone makes Arssyut a real recorder before adding polish.

### P2A — Encoder technology spike

Compare at least:

1. FFmpeg/libav* D3D11 hardware path;
2. Windows Media Foundation hardware path.

Measure:

- NVENC/QSV/AMF coverage available on test machines;
- D3D11 surface handoff cost;
- CPU utilization;
- initialization reliability;
- output compatibility;
- dependency size;
- error diagnostics;
- remux/recovery behavior.

Select one primary production media stack with a documented decision record.

### P2B — Video encoder

- H.264 hardware-first.
- canonical quality profile mapping.
- fixed output cadence.
- bounded encoder submission.
- explicit software fallback policy.

### P2C — Audio

- WASAPI loopback.
- microphone capture.
- 48-kHz canonical mix.
- AAC.
- bounded audio rings.
- measured clock drift.

### P2D — crash-safe finalization

- temp recording close/drain;
- MP4 remux without re-encoding;
- verify media;
- atomic promotion;
- retain temp on failure;
- startup recovery scan.

### Gates

- one-hour 1080p60 system-audio recording;
- one-hour mic + system recording;
- A/V sync measured;
- output seeks correctly;
- interrupted session recovery tested;
- disk-full case does not delete recoverable media;
- final MP4 verified with media inspection tooling.

---

## P3 — ArZoom camera and click engine

Only begin after raw recording is stable.

### P3R — parity recovery after first real recording

The first P3 integration proved the feature path but did not preserve the
accepted per-source OBS ArZoom behavior closely enough. P3R therefore replaces
partial/adapted ownership with an immutable upstream snapshot plus a thin
Arssyut intent adapter.

P3R invariants:

- vendor ArZoom portable camera/click headers unchanged from the pinned commit;
- per-source recorder uses `PresenterAwareSmartCamera` with scene context off;
- Smart follow + Cinematic motion + 28% safe zone + (0.50, 0.45) anchor;
- product Auto Zoom emits intent only; ArZoom owns all camera motion;
- latest WGC source is retained while camera/presentation re-renders on every
  CFR output slot;
- premium click shader behavior is ported from the same pinned ArZoom effect;
- parity tests compare Arssyut adapter output directly with upstream output;
- P3R is not accepted until a real 1080p60 recording visually matches the
  expected ArZoom gimbal/zoom character.

See `docs/P3R_ARZOOM_PARITY.md`.


### P3A — portable camera transplant

Extract/adapt from ArZoom:

- math;
- viewport planning;
- kinematic motion;
- timing;
- bounds;
- mapping.

No OBS types in the portable core.

### P3B — mapping

Define and test:

```text
desktop -> capture source -> crop -> normalized content -> camera -> output
```

Test:

- monitor at negative coordinates;
- scaled Windows display;
- region crop;
- window resize;
- 16:9 and non-16:9 sources.

### P3C — camera modes

- Off;
- Smart;
- Manual toggle/hold;
- zoom +/-;
- reset/full frame.

### P3D — click visuals

Port bounded click slots and content-anchored projection.

### Gates

- deterministic camera simulations at multiple update rates;
- no click/zoom coordinate divergence;
- no camera jitter from tiny pointer motion;
- no new per-frame allocation;
- 1080p60 recording performance remains within budget.

---

## P4 — Keyboard action visualizer

### P4R — semantic/keycap recovery

The initial shortcut path is replaced by a structured semantic chord model and
content-sized physical-keycap renderer. P4R removes formatted shortcut strings
from runtime state, keeps ordinary typing private, and retains GDI/GPU
resources.

### P4R.1 — direct-validation polish

A real 1080p60 recording on 2026-10-02 demonstrated that Win+R could execute
without producing an overlay, modifier keycaps should use one white visual
language, and the P3R dual-ring click skin was less satisfying than the earlier
single-ring direction.

P4R.1 therefore:
- keeps Raw Input primary and adds a narrow Windows-key low-level hook
  supplement with semantic dedupe;
- makes every keycap white/light and renders the Windows modifier as a logo;
- changes click feedback to one larger adaptive ring with smooth fade;
- adds diagnostics for hook availability and presentation-input overflow.

See `docs/P4R_KEYBOARD_VISUALIZER.md`.

### P4R.2 — Cursor & Click Motion Polish

P4R.2 promotes the cursor into the presentation compositor so click feedback
and pointer impact can behave as one coherent visual system without sacrificing
pointer truthfulness.

P4R.2 therefore:
- disables native WGC cursor capture while presentation features are active;
- retains the current Windows cursor shape in a bounded eight-slot cache;
- projects the custom cursor through the same ArZoom camera transform;
- scales strictly around the Windows hotspot;
- adds a ~230 ms bounded ballistic click response
  (1.00 -> 1.17 -> 0.95 -> 1.025 -> 1.00);
- adds a subtle motion-speed response capped around +5%;
- evolves the single click ring into a brighter 11 -> 62 px emissive pulse
  with an expanding glow falloff and ~0.68-0.72 s lifetime;
- keeps steady-state rendering allocation-free and output-cadence driven.

See `docs/P4R2_CURSOR_CLICK_MOTION.md`.

### Deliverables

- dedicated Raw Input worker;
- narrow Win-key low-level-hook supplement with semantic dedupe;
- canonical pressed-key state;
- chord reducer;
- privacy filter;
- bounded click event ring;
- retained GPU keycap renderer with one white physical-key language;
- retained hotspot-anchored Windows cursor compositor;
- bounded cursor-shape cache with no steady-state resource churn.

### Default display behavior

Show meaningful actions such as:

- Ctrl+C/V/X/Z/A/S;
- Ctrl+Shift variants;
- Alt+Tab;
- Win shortcuts;
- function/system keys;
- recorder control hotkeys.

Hide ordinary typed content.

### Gates

- chord ordering deterministic;
- left/right modifier normalization tested;
- repeat suppression tested;
- ordinary password-like typing does not generate transcript metadata;
- event-to-visible latency measured;
- keycap resource cache bounded.

---

## P5 — ArVisual standalone color engine

### P5A — shader/behavior transplant

Port only the portable grading behavior.

Implemented scope:
- one-pass D3D11 adaptation of pinned ArVisual v0.5.9 grading math;
- neutral/white cleanup and cast reduction;
- bounded tone shaping and highlight shoulder;
- headroom-based vibrance and hue-zone object separation;
- skin classification/protection and bounded beauty/healthy-tone behavior;
- luma-only anti-halo clarity;
- gloss/depth shaping and luminance-preserving gamut safety;
- adaptive parameters wired at neutral values pending P5B;
- Pixel Accurate default with an opt-in validation toggle;
- grading before Arssyut click/cursor/keycap presentation layers.

See `docs/P5A_ARVISUAL_STANDALONE.md`.

### P5B — asynchronous analysis

Implemented scope:
- fixed 64x36 GPU downsample surface;
- 5 Hz maximum analysis cadence on fresh WGC frames only;
- exactly two retained staging textures and EVENT queries;
- DONOTFLUSH readiness polling with map-only-after-ready behavior;
- safe busy-skip when both staging slots are occupied;
- pinned ArVisual luma/saturation percentile statistics;
- 0.65 s time-domain EMA;
- adaptive exposure/pop/highlight/shadow/strength/chroma/clean/separation;
- soft fallback to P5A static grade if analysis is unavailable;
- recorder diagnostics for analysis availability and backpressure.

See `docs/P5B_ARVISUAL_ASYNC_ANALYSIS.md`.

### P5C — product modes

- Pixel Accurate;
- Clean Screen;
- Vivid Presentation.

### Gates

Visual test scenes:

- white browser/document;
- dark IDE;
- colorful web page;
- skin/webcam content if later webcam is present;
- clipped highlights;
- saturated animation/game.

Requirements:

- no visible pumping/flicker;
- white/neutral stability;
- no highlight blowout;
- no black frames;
- no material recording smoothness regression.

---

## P6 — Compact product UI/UX

The engine state machine already exists before this milestone.

### Surfaces

- compact main recorder;
- source picker;
- region selector;
- minimal recording control;
- settings;
- result/finalizing surface;
- recovery prompt.

### Design constraints

- no oversized typography;
- no bulky card dashboard;
- dense but readable spacing;
- clear hierarchy;
- one prominent Record/Stop action;
- advanced settings progressively disclosed;
- dark/light support where practical.

### Gates

- all visible session status derives from authoritative engine state;
- UI can close/reopen settings without mutating runtime truth;
- recording overlay exclusion tested;
- keyboard-only accessibility for essential actions.

---

## P7 — hardening

### Fault matrix

Test:

- graphics device removal;
- monitor disconnect;
- captured app closes;
- source resizes rapidly;
- audio device unplug;
- encoder cannot initialize;
- encoder stalls;
- disk slow;
- disk full;
- output path permission failure;
- application forced termination;
- system sleep/resume if supported;
- 100 start/stop cycles.

### Soak matrix

- 2h 1080p60;
- 2h 1080p30;
- representative 4K30;
- 4K60 on explicitly capable hardware.

Track:

- memory;
- handles;
- COM/resource counts where observable;
- queue maxima;
- frame replacement/drop;
- audio drift;
- output validity.

### Gate

No unresolved P0/P1 issue.

---

## P8 — release candidate

### Build/release

- portable package first is acceptable and keeps installation simple;
- optional per-user installer can be added without admin rights;
- no Program Files requirement unless a later packaging decision needs it;
- semantic versioning;
- release checksums;
- reproducible/pinned dependencies where practical;
- automated Windows CI artifact.

### Release acceptance

- fresh-machine launch;
- record monitor;
- record window;
- record region;
- click visual;
- smart zoom;
- shortcut visual;
- system audio;
- microphone;
- grade;
- MP4 finalization;
- recovery;
- long recording;
- uninstall/portable cleanup behavior.

---

## Architecture decision records

Create `docs/adr/` before implementation decisions that are expensive to reverse.

Initial ADRs expected:

- ADR-001: media stack — FFmpeg/libav vs Media Foundation.
- ADR-002: UI shell technology.
- ADR-003: recoverable temp container.
- ADR-004: hardware encoder capability/selection policy.
- ADR-005: WGC vs fallback capture policy.
- ADR-006: Raw Input and hook fallback policy.
- ADR-007: ArZoom/ArVisual source sharing/vendoring strategy.

An ADR records evidence, alternatives, decision, and consequences. It is not a ceremonial document for obvious small choices.

---

## Performance scorecard

Every performance-sensitive milestone updates one scorecard with the same metrics.

Recommended fields:

| Metric | 1080p30 | 1080p60 | 4K30 | 4K60 |
|---|---:|---:|---:|---:|
| capture callback p95 | | | | |
| video pipeline CPU p95 | | | | |
| compositor GPU p95 | | | | |
| encode submit p95 | | | | |
| frame replaced/drop % | | | | |
| max video queue depth | | | | |
| max audio queue depth | | | | |
| A/V drift after 1h | | | | |
| memory after warmup | | | | |
| memory after soak | | | | |

Never replace measured values with adjectives such as “fast” or “light”.

---

## Milestone completion report format

When stopping after a milestone, report:

### Status
What is actually complete.

### Architecture
Which owner/state model was established or changed.

### Validation
Exact tests/build/recordings performed.

### Performance
Measured values, not assumptions.

### Known limitations
Only genuine remaining issues.

### Next
The next milestone and why it is now safe to start.

---

## Final delivery rule

Do not skip directly to the attractive features.

The shortest path to a polished recorder is to make the capture/time/encode/lifecycle foundation correct once, then add ArZoom, keyboard visualization, and ArVisual without destabilizing it.
