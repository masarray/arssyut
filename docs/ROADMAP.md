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

### P4R.3 — Presentation Reliability & Visual Polish

Real 1080p60 validation on 2026-10-03 superseded the P4R.2 custom cursor
experiment. P4R.3 prioritizes pointer reliability and moves visual impact back
into click/keycap presentation:

- restores native Windows/WGC cursor capture as the only cursor authority;
- removes custom cursor raster/cache/shader and ballistic scale behavior;
- keeps pointer observation only for ArZoom targeting and click coordinates;
- makes 1u action keys square and assigns explicit realistic keyboard widths to
  modifiers and long keys;
- thickens the single click-ring core, expands near bloom and diffuse halo, and
  lengthens the fade tail;
- adds bright-surface chromatic support without introducing a second ring;
- retriggers one same-target pulse for rapid repeated clicks instead of
  stacking concentric circles.

See `docs/P4R3_PRESENTATION_RELIABILITY.md`.

### Deliverables

- dedicated Raw Input worker;
- narrow Win-key low-level-hook supplement with semantic dedupe;
- canonical pressed-key state;
- chord reducer;
- privacy filter;
- bounded click event ring;
- retained GPU keycap renderer with one white physical-key language and
  semantic 1u/1.25u/1.5u/2u/3.5u geometry;
- native WGC cursor authority that naturally follows ArZoom sampling;
- analytic single-ring click core/bloom/halo with bounded retained state.

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
- asynchronous 5 Hz Flush1 command submission for the headless D3D11 path;
- DONOTFLUSH readiness polling with map-only-after-ready behavior;
- safe busy-skip when both staging slots are occupied;
- pinned ArVisual luma/saturation percentile statistics;
- 0.65 s time-domain EMA;
- adaptive exposure/pop/highlight/shadow/strength/chroma/clean/separation;
- soft fallback to P5A static grade if analysis is unavailable;
- recorder diagnostics for analysis availability and backpressure.

See `docs/P5B_ARVISUAL_ASYNC_ANALYSIS.md`.

### P5C — product modes

Implemented scope:
- replaces the temporary ArVisual checkbox with one explicit product-mode
  selector;
- Pixel Accurate is the default and is a true grade + Smart Auto bypass;
- Clean Screen uses the existing P5A/P5B engine with conservative creative
  dose, stronger clean-white behavior and stronger highlight safety;
- Vivid Presentation preserves the pinned P5A v0.5.9 creative defaults and
  relies on P5B Smart Auto to reduce risk on saturated/highlight-heavy scenes;
- recorder configuration canonicalizes all internal grading parameters from
  the selected product mode so undocumented hybrid presets cannot drift into
  production;
- diagnostics persist the stable mode name;
- mode changes are constant-buffer state only and do not create compositor
  resources.

See `docs/P5C_PRODUCT_MODES.md`.

### P5C.1 — real visual calibration support

Implemented support:
- output filenames include the selected product-mode slug;
- diagnostics persist the exact base grade mapping;
- diagnostics persist the latest completed P5B scene statistics;
- diagnostics persist the latest adaptive safety state;
- Windows/WARP tests verify exported scene/adaptive evidence comes from a
  completed analyzer state;
- matched-recording protocol isolates color by disabling presentation effects.

Visual calibration itself remains evidence-gated and is not declared complete
until matched Pixel Accurate / Clean Screen / Vivid Presentation recordings
have been reviewed.

See `docs/P5C_REAL_VISUAL_CALIBRATION.md`.

### P5C.2 — neutral-white Smart Auto calibration

First matched Pixel Accurate / Clean Screen / Vivid Presentation recordings
were reviewed on 2026-10-03.

Evidence:
- Pixel Accurate correctly bypassed grade and analysis;
- Clean/Vivid analysis completed with zero busy skips/map failures;
- the matched final browser/document scene was ~98.5% neutral with ~0.3%
  saturation, but literal P5B mapping produced max highlight pressure,
  minimum exposure and a positive muted-pop preload;
- frame-matched comparison showed mild white-screen dimming and visible
  brightness/chroma settling after white-to-color transitions.

Correction:
- add a bright + overwhelmingly neutral + low-chroma desktop-UI classifier;
- attenuate negative exposure and neutral clipping pressure only for that
  topology;
- suppress muted-scene pop preload on white UI;
- retain hot-vivid, dark-scene, async cadence and EMA behavior unchanged;
- export `visual_adaptive_white_ui` for the next matched triad.

Preset values for Clean Screen and Vivid Presentation remain unchanged in this
pass.

### P5D — screen text fidelity

P5D addresses a separate real-recording finding: small desktop text can look
perceptually thinner after capture, resampling, NV12 4:2:0 encoding and player
scaling.

Implemented scope:
- adds an explicit bounded text-legibility parameter to the existing visual
  product-mode state;
- Pixel Accurate remains 0.00 and pixel-faithful;
- Clean Screen uses 0.56;
- Vivid Presentation uses 0.34;
- reuses the existing P5A neighborhood/detail samples rather than adding a
  second sharpen pass;
- reinforces luminance micro-edges only, with strong neutral-text preference,
  reduced saturated-edge dose and near-total skin exclusion;
- adds a small scale-aware boost only when the source is being minified;
- keeps the stable linear sampler rather than introducing nearest-neighbor
  shimmer;
- prefers H.264 High Profile with safe Main fallback;
- P5D.6 replaces quality-VBR with bitrate-controlled unconstrained VBR so the
  18 Mbps / 12 Mbps recording budgets remain authoritative;
- requests QualityVsSpeed 85 as a high-complexity preference with tiered
  fallback;
- keeps NV12 4:2:0 as the compatibility-first production path;
- diagnostics expose text legibility, UI-structure preservation, negotiated
  H.264 profile and rate-control mode.

See `docs/P5D_SCREEN_TEXT_FIDELITY.md`.

### P5D.5 — bright-background text calibration

The first complete P5D Pixel/Clean/Vivid triad was reviewed on 2026-10-03.

Evidence:
- High Profile + quality-VBR 86 were actually negotiated;
- Pixel Accurate remained text-legibility bypass;
- Clean/Vivid remained realtime healthy with zero encoder backpressure;
- dark UI and light-on-dark text showed a clear natural readability gain;
- black/gray text on white browser UI improved at native 1080p but lost much of
  that perceived gain after player-fit downscaling.

Correction:
- do not raise global Clean/Vivid text-legibility values;
- classify only negative-detail strokes inside bright neutral neighborhoods;
- allow up to 1.45x directional gain for that topology while retaining the
  same hard negative-luma cap;
- widen only the negative local luma cap from -0.014 toward -0.018;
- keep positive-detail / light-on-dark text on the original P5D response;
- retain all neutral/chroma/skin/micro-edge gates and the no-resource-churn
  contract.

### P5D.6 — low-contrast UI structure + bitrate-controlled encoder

Original UI screenshots compared against the encoded Clean Screen recording
showed that several neutral 1px card borders/separators lost enough contrast to
nearly disappear. This is a separate fidelity problem from text stroke weight.

Implemented correction:
- add an explicit `ui_structure` preservation control;
- Pixel Accurate remains 0.00, Clean Screen uses 0.72, Vivid Presentation 0.38;
- classify only shallow neutral/low-chroma detail;
- darker structure is reinforced only on bright neutral context;
- lighter structure is reinforced only on dark neutral context;
- strong edges/text/icons are excluded from this path;
- apply structure preservation after final tone shaping;
- hard local luma caps remain -0.010 / +0.007;
- no new render pass, texture, readback or resource churn.

Encoder correction:
- retire quality-VBR as the preferred screen-recording mode;
- prefer H.264 unconstrained VBR with requested mean bitrate;
- set both media-type average bitrate and codec mean-bitrate attributes;
- request QualityVsSpeed 85, with VBR-only and default-negotiation fallbacks;
- CI must prove bitrate-VBR negotiation on the Windows runner.

### P5D.7 — color pipeline authority

Real P5D.6 validation showed that 1px neutral borders returned while larger
gray UI surfaces could still collapse toward white/black. Inspection identified
an ambiguous RGB->NV12/H.264 range/colorimetry contract rather than a reason to
retune ArVisual.

Implemented correction:
- declare compositor input as full-range SDR RGB / BT.709;
- declare NV12 output as studio-range BT.709 YCbCr;
- prefer `ID3D11VideoContext1` explicit DXGI color spaces;
- retain an explicit legacy D3D11 color-space fallback rather than driver
  defaults;
- tag both NV12 input and H.264 output media types with BT.709
  primaries/transfer/matrix and 16-235 nominal range;
- diagnostics persist the active color-pipeline authority and range contract;
- upgrade the Media Foundation integration gate to a real gray-ladder
  BGRA->NV12->H.264->decode round trip;
- require near-white and near-black levels to remain distinct.

No Clean/Vivid grading parameters change in P5D.7. Screen-native ArVisual
retuning begins only after the corrected color pipeline is validated in a real
recording.

See `docs/P5D7_COLOR_PIPELINE_AUTHORITY.md`.

### P5E — screen-native visual engine

**Status: COMPLETE / REAL-VISUAL ACCEPTED / LOCKED — 2026-10-03**

P5D.7 real validation confirmed the SDR range/colorimetry pipeline was fixed:
gray surface hierarchy returned and P5D text/border preservation remained
healthy. The remaining issue was policy: mixed light/dark UI could still drive
camera-style exposure, highlight and shadow normalization.

Implemented scope:
- keep one ArVisual shader/analysis engine; do not fork a separate screen
  renderer;
- add flat/neutral/bright-neutral/dark-neutral/edge topology evidence from the
  same fixed 64x36 P5B sample with no heap allocation;
- classify broad screen UI and mixed light/dark UI without OCR/app detection;
- keep raw camera-style pressure measurable, but reinterpret it through a
  product-mode screen prior;
- Clean Screen uses screen-native=1.00 and neutral-surface anchor=0.94;
- Vivid Presentation uses screen-native=0.72 and neutral-surface anchor=0.62;
- suppress global exposure/shadow/neutral-highlight normalization on screen
  content while retaining hot-vivid/color-risk protection;
- remove positive muted-pop preload from applied screen policy;
- add a late flat-neutral luma anchor before P5D.6 separator preservation;
- reuse the existing `arvisual6` constant-buffer vector; no new GPU resources
  or render pass;
- diagnostics separate raw analyzer pressure from applied Smart Auto values.

See `docs/P5E_SCREEN_NATIVE_VISUAL_ENGINE.md`.

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

Real acceptance completed on 2026-10-03:
- Clean Screen passed white/near-white hierarchy, dark hierarchy, P5D
  text/border retention and light/dark transition stability;
- Vivid Presentation passed saturated-content color-risk/hot-risk validation
  while keeping neutral UI stable;
- both recordings retained the authoritative P5D.7 BT.709 studio-range
  pipeline and healthy realtime diagnostics;
- P5E parameters and ordering are now an accepted baseline. Later milestones
  must not retune them without a reproduced regression, regression tests and
  another matched real-recording validation set.

See docs/P5E_REAL_VISUAL_ACCEPTANCE.md for the locked evidence.

---

## P6 — Recorder workspace UI/UX

**Status: P6R REDESIGN IN PROGRESS / REAL GUI ACCEPTANCE PENDING**

The first compact-form P6 pass was rejected in direct GUI review because it
still treated Arssyut as an engineering form rather than a complete recorder
workspace.

P6R is now grounded in established recorder patterns from Camtasia, Bandicam,
ScreenPal, AnyRec and OBS. See `docs/P6R_RECORDER_UX_RESEARCH.md`.

### P6R.1 — recorder workspace + interaction reliability

Implemented on the feature branch:
- Capture Mode is first-class: Display / Window / Region / Game;
- Display/Window use the existing real source catalog;
- Region/Game are visible product intents but recording is blocked until their
  dedicated backends land, so the UI never lies;
- real Windows microphone endpoint enumeration;
- real Media Foundation camera-device enumeration;
- primary System Audio / Mic / Camera controls;
- dedicated categorized Settings window:
  General / Recording / Output / Sound / Camera / Mouse & Keystroke / Hotkeys;
- separate topmost recording toolbar with Pause/Mic/Camera/Stop icon surfaces;
- separate click-through capture-boundary overlay;
- RecorderSnapshot publishes the same camera center/zoom used by the compositor
  so the boundary shrinks/moves with Smart Zoom without becoming a second
  camera authority;
- main HWND is no longer restyled into a popup while recording;
- UI completion path no longer joins the worker thread;
- F9 is a global Record/Stop hotkey while the process is alive.

### P6R.2 — custom Region backend

**Status: IMPLEMENTED ON FEATURE BRANCH / CI + REAL GUI ACCEPTANCE PENDING**

Implemented:
- Region boundary is now an editor, not decorative chrome;
- top pill provides native move behavior;
- visible edges/corners provide native resize behavior with a 320 × 180 minimum;
- idle Region placement is event-driven so the 33 ms UI timer cannot fight the
  native move/resize loop;
- Region is stored in virtual-screen coordinates, preserving negative monitor
  origins and per-monitor-DPI-safe desktop geometry;
- selected Region maps once into the existing monitor WGC source as a canonical
  `CropRect`; there is no second capture path;
- output dimensions follow the selected crop and are trimmed to even NV12/H.264
  dimensions only when needed;
- Smart Zoom/click/keystroke normalization uses the same Region screen rectangle
  that feeds the compositor crop;
- when recording, the visible boundary still contracts/moves from the
  authoritative PresentationFrameState camera values.

Acceptance still required:
- resize/move from all handles on a real multi-monitor desktop;
- negative-origin monitor test;
- output frame dimensions and pixel content match the selected Region;
- Smart Zoom viewport remains inside the selected Region;
- repeated Region -> Display -> Region switching preserves coherent state.

### P6R.3 — audio backend

Required before Sound controls can record:
- WASAPI system-audio loopback;
- microphone endpoint capture using the P6R-selected device;
- independent enable/mute/gain;
- canonical 48 kHz project clock;
- A/V synchronization and drift diagnostics;
- AAC mux integration.

### P6R.4 — webcam backend

Required before Camera can record:
- Media Foundation webcam source using the P6R-selected device;
- retained GPU texture/compositor path;
- move/resize Picture-in-Picture;
- optional shape/border configuration;
- no CPU full-frame composition.

### P6R.5 — pause/resume

Required before Pause is enabled:
- explicit Paused engine state;
- media-time continuity with no wall-clock gap encoded;
- recorder toolbar and hotkey support;
- final MP4 duration/audio sync regression tests.

### P6R.6 — game capture

Required before Game can record:
- ADR for dedicated Windows game capture strategy;
- efficient DirectX/OpenGL game path where technically supportable;
- safe Window/WGC fallback;
- process/window identity and source-loss behavior.

### P6 visual acceptance

Before merging:
- main recorder reads as a recorder, not a settings dashboard;
- capture mode is obvious at first glance;
- capture boundary is visible and matches Display/Window source;
- Smart Zoom visibly contracts/moves the boundary with the actual camera;
- toolbar remains responsive from Record through Stop/Finalizing;
- no blank/frozen main-window transition;
- Settings categories are coherent and compact;
- real microphone/camera names populate when devices exist;
- unsupported Region/Game/audio/camera/pause actions never silently claim
  success;
- P5D.7/P5E visual/encoder baseline remains unchanged.



### P6UI — final Avalonia product shell

**Status: P6UI.0 + P6UI.1 IMPLEMENTED ON FEATURE BRANCH / VISUAL ACCEPTANCE PENDING**

Direct visual acceptance established that the P6R Win32/GDI shell is a
functional prototype, not the final product UI.

The P6R native baseline is frozen at branch:
`milestone/p6r-native-functional`.

The final presentation shell lives under:
`src/ui/Arssyut.UI`.

#### P6UI.0 — baseline split

Implemented:
- freeze P6R native functional baseline before UI migration;
- preserve existing C++ capture/Region/ArZoom/ArVisual/encoder authority;
- prohibit visual feature growth in the legacy Win32 shell;
- keep the legacy shell buildable until native binding acceptance is complete.

#### P6UI.1 — Avalonia design-system shell

Implemented:
- .NET 10 / Avalonia 12.1.3 preview project;
- embedded Inter via `Avalonia.Fonts.Inter`;
- real `Lucide.Avalonia` icon renderer;
- semantic color/spacing/radius/control-height/typography tokens;
- compact custom recorder chrome;
- Mica -> Acrylic -> opaque fallback preference;
- modern main recorder information architecture;
- modern categorized Settings shell;
- UI-only capture-mode / Record / Stop / Pause simulator;
- Windows CI build, self-contained publish and launch smoke;
- third-party notice packaging.

Visual acceptance required before native binding:
- real screenshots at 100/125/150/200% DPI;
- no native white/gray control leakage;
- Inter remains crisp at compact sizes;
- Lucide icons preserve correct rounded strokes and optical alignment;
- primary Record hierarchy is obvious without oversized controls;
- Settings remains compact and readable;
- translucent shell remains readable when Mica/Acrylic is unavailable.

#### P6UI.2 — interaction prototype completion

**Status: IMPLEMENTED ON FEATURE BRANCH / REAL VISUAL ACCEPTANCE PENDING**

Implemented:
- microphone and camera selectors are compact device modules with flyout
  pickers instead of permanent wide selectors;
- device flyouts include realistic alternate devices plus deliberately long
  labels to exercise truncation and layout resilience;
- input enable state is part of the shared UI-preview session rather than a
  second transport state;
- the idle footer shows one primary Record action only;
- after Stop, the footer becomes contextual Saved state with Open and Folder
  result actions instead of permanently occupying recorder space;
- recording launches a separate topmost floating controller prototype with
  elapsed time, Mic, Camera, Pause/Resume and Stop;
- the main recorder hides while the floating controller is active and returns
  to the Saved state when the preview session stops;
- F9 is the local preview Record/Stop shortcut and F10 is Pause/Resume while
  the floating controller is focused;
- Settings closes with Escape and primary recorder controls carry explicit
  automation names/help text;
- hover/pressed/focus-visible states now use one restrained transition grammar
  and recording-red focus indication;
- a deterministic PreviewRecorderSession state machine gates
  Ready -> Recording -> Paused -> Recording -> Saved -> Ready behavior;
- CI runs both a normal launch smoke and an interaction stress smoke using
  `--stress-long-names --controller-preview`;
- the stress scenario constrains the main window to its minimum width while
  loading long display/microphone/camera names.

Acceptance still required:
- real screenshot review at 100%, 125%, 150% and 200% Windows scaling;
- confirm Inter/Lucide remain crisp at high DPI;
- confirm device flyouts never clip off-screen on common desktop layouts;
- confirm floating controller placement and density feel correct on real
  multi-monitor Windows;
- confirm keyboard focus order and focus-visible rings remain calm rather than
  visually noisy.

#### P6UI.3 — Settings acceptance

**Status: IMPLEMENTED ON FEATURE BRANCH / REAL VISUAL ACCEPTANCE PENDING**

Implemented:
- every Settings category now uses the same compact setting-row grammar:
  label + short description + one right-aligned control or preview surface;
- Output uses the Avalonia storage-provider folder picker and updates only
  SettingsPreviewState; it does not write native recorder configuration;
- output filename policy, container summary and post-recording behavior are
  presented without exposing unsupported native mutations;
- Audio presents system playback and microphone device selectors plus
  deterministic animated level meters so spacing, labels and meter density can
  be accepted before live WASAPI binding;
- Camera presents realistic device options and an interactive 4-corner
  picture-in-picture placement preview using one canonical CameraPlacement
  preview state;
- Hotkeys has interactive capture for Record/Stop, Pause/Resume and microphone
  toggle, Escape-to-cancel and duplicate-shortcut conflict feedback;
- Advanced explicitly exposes native-engine ownership and a preview-only Reset;
- SettingsPreviewState owns preview folder/hotkey/camera/device values so
  code-behind remains a UI simulator rather than native configuration truth;
- Settings can be launched standalone with `--settings-preview`, while
  `--settings-stress` loads long folder/device labels for layout testing;
- deterministic interaction tests cover folder selection state, hotkey
  conflicts, camera placement and Reset;
- Windows CI launches the standalone Settings stress scenario in addition to
  the main recorder and floating-controller smokes.

Acceptance still required:
- real screenshots of all categories at 100%, 125%, 150% and 200% scaling;
- confirm long device/folder names truncate cleanly rather than expanding rows;
- confirm folder picker placement/focus feels native but preserves Arssyut
  visual continuity on return;
- confirm camera preview remains useful without making Settings card-heavy;
- confirm hotkey capture and focus-visible feedback are understandable by
  keyboard-only users;
- confirm Audio meter motion is subtle enough for Settings and not visually
  distracting.

### History baseline audit — mandatory forward-progress gate

The repository history has been audited and recorded in
`docs/ENGINE_BASELINE_LEDGER.md`.

From this point forward:
- P5E and earlier accepted engine authorities remain locked;
- P6R native overlay / Smart Zoom / Region geometry are reused rather than
  recreated in Avalonia;
- Region is treated as an existing native implementation requiring focused
  real-acceptance correction, not a greenfield feature;
- the Avalonia source/boundary preview is temporary scaffolding and must not
  grow new engine responsibilities;
- CI compares P6UI changes against the frozen P6R commit
  `b11451bd640a072d81dbd1024b4c641a76c9daac` and rejects native drift during
  presentation-only milestones.

The next engine-facing work is therefore bridge-first, not rewrite-first.

#### P6UI.4 — native bridge

**P6UI.4A status: IMPLEMENTED / CI + REAL GUI ACCEPTANCE PENDING**

P6UI.4A read-only bridge:
- versioned `arssyut_native_bridge.dll` C ABI;
- one opaque bridge context for the Avalonia application lifetime;
- native source snapshot from the existing P6R source catalog;
- native microphone/camera snapshots from the existing device catalog;
- read-only `RecorderSession::snapshot()` projection;
- generation-scoped opaque source/device tokens; C# never reconstructs native
  HWND/HMONITOR/device identifiers;
- duplicate Avalonia Win32 source enumeration removed;
- duplicate Avalonia capture-boundary window removed from the runtime;
- packaged Avalonia launch smoke requires a compatible bridge DLL;
- native bridge ABI has its own Windows regression executable.

**P6UI.4B status: IMPLEMENTED / CI + REAL RECORDING ACCEPTANCE PENDING**

P6UI.4B command bridge:
- bridge ABI v2 resolves the selected generation-scoped source token only
  inside the native context;
- normal Avalonia Product Record for Display/Window now starts the existing
  native `RecorderSession`; `PreviewRecorderSession` remains only for
  explicit UI stress/design scenarios;
- Stop publishes the native stop request and the floating controller remains
  responsive through Stopping/Finalizing until the native worker publishes its
  terminal state;
- frame rate, Pixel Accurate/Clean Screen/Vivid Presentation, Smart Zoom,
  click highlight, shortcut visualization and output-folder choice map into
  the native start configuration;
- successful native recordings expose real output/diagnostics paths and the
  main UI Open/Folder actions operate on the real result;
- stale source generations, busy sessions and start failures surface explicit
  UI states;
- Region is intentionally rejected until P6UI.4C binds the already-existing
  native Region editor/crop path;
- Game remains explicitly unsupported until its backend exists;
- system audio, microphone and camera are explicitly rejected while their real
  backends are not connected, so the UI never claims those streams were
  recorded;
- native bridge tests cover stale-token rejection, unsupported media input and
  idle Stop semantics;
- the locked native engine tree remains unchanged; all new command adaptation
  lives in `src/bridge`.

**P6UI.4B-A status: IMPLEMENTED / CI-HARDENED / REAL WINDOWS ACCEPTANCE PENDING**

Acceptance lock:
- the Avalonia floating recording controller applies
  `WDA_EXCLUDEFROMCAPTURE`, with `WDA_MONITOR` only as a compatibility
  fallback;
- CI launches the real controller HWND and verifies its display affinity;
- Settings options without a bound product authority are visibly locked or
  explicitly identified as staged instead of behaving like silent no-ops;
- no P5/P6R capture, compositor, camera, encoder, Region or media-clock
  implementation is changed;
- real multi-monitor recording remains a manual gate documented in
  `docs/P6UI4B_ACCEPTANCE_LOCK.md`.

Real acceptance required:
- record Display 1 and Display 2 on a real multi-monitor machine;
- record a Window source;
- verify Record -> Stop -> Finalizing -> Saved never freezes the Avalonia UI;
- verify Open and Folder use the produced MP4 path;
- repeat native start/stop several times;
- validate 30/60 fps and all three visual modes;
- confirm Smart Zoom/click/shortcut settings reach the native recorder;
- confirm enabling an unbound audio/mic/camera input blocks recording with a
  clear status rather than silently omitting it;
- confirm the floating Avalonia controller itself never appears in Display or
  overlapping Region output.

**P6UI.4C status: IMPLEMENTED / CI + REAL GUI ACCEPTANCE PENDING**

P6UI.4C native overlay / Region bridge:
- reuses the existing P6R `RecorderOverlay`, `region_geometry` and
  `RecorderSession`;
- bridge ABI v3 owns only a hidden message HWND for native Region edit/timer
  delivery; no Avalonia capture-boundary window exists;
- selected Display/Window source -> native click-through capture boundary;
- idle Region -> existing native move/resize editor with click-through interior;
- Region edit -> canonical virtual-screen rectangle ->
  `map_region_to_crop()` -> existing monitor WGC crop;
- RecorderSnapshot camera center/zoom ->
  `camera_viewport_rect()` -> Smart Zoom boundary;
- Region -> Display -> Region preserves the native Region rectangle on the same
  monitor;
- bridge regression sends real `WM_NCHITTEST` to `ArssyutCaptureBoundary`
  and locks Display click-through plus Region edge/interior semantics;
- no C# crop/geometry/camera implementation.

See `docs/adr/ADR-008-native-ui-bridge.md`.

**P6UI.4C-A status: IMPLEMENTED / CI + REAL GUI ACCEPTANCE PENDING**

Product-reality / visual lock:
- normal product mode never silently substitutes the UI simulator for a missing
  native bridge;
- Record is disabled when the native engine/source authority is unavailable;
- staged audio/microphone/camera/Game/Pause and fixed policy values are not presented
  as fake active controls;
- Settings right-side controls use one aligned column and descriptive copy wraps
  before that column;
- preview-only badges, fake meters and demonstration device choices are removed
  from normal product presentation;
- Windows artifact is one product executable with the bridge embedded as a
  managed resource and loaded from a content-addressed local cache;
- CI rejects an external loose bridge DLL and launches the packaged executable
  with `--bridge-required`;
- P5/P6R engine implementation remains untouched.

**P6UI.4C-B status: IMPLEMENTED / CI + REAL REGION RESIZE ACCEPTANCE PENDING**

- layered Region client bitmap is not preserved during editable resize;
- complete transparent-key repaint follows every new client size;
- Region geometry/crop/capture authority is unchanged.

**P6UI.5A status: IMPLEMENTED / CI + REAL GLOBAL-HOTKEY ACCEPTANCE PENDING**

- bridge ABI v4 owns `RegisterHotKey` on the existing hidden HWND;
- F9 Start/Stop works outside Avalonia focus with `MOD_NOREPEAT`;
- Ctrl/Shift/Alt/Win multi-key grammar and conflict feedback;
- Pause/Mic/Webcam shortcut rows remain backend-gated.

**P6UI.5B status: IMPLEMENTED / CI + REAL VISUAL ACCEPTANCE PENDING**

- main workspace is Capture | Audio | Webcam | Record;
- Capture uses one mode dropdown plus existing native source picker;
- circular Record is the primary action with centered hotkey badge;
- pending media remains truthful status, not fake controls.

**P6UI.5C status: IMPLEMENTED / CI + REAL VISUAL ACCEPTANCE PENDING**

Visual polish / tactile-control lock:
- four-way tactile Lucide capture-mode selector with one active state;
- unified keycap grammar for main F9 and Settings shortcut fields;
- centered shortcut text and compact tactile keycap proportions;
- Audio/Microphone/Webcam final-form control structure with disabled truthful
  toggles until native media backends exist;
- read-only detected microphone/camera device projection from the native bridge;
- truthful Default playback device surface until an output-device catalog exists;
- no engine/capture/media authority changes.

**P6UI.5D — Visual Acceptance + DPI/Interaction Lock: HARDENING OPEN / NON-BLOCKING**
- real P6UI.5C screenshot direction accepted for functional progression;
- retain 100% / 125% / 150% DPI, hover/focus, keyboard navigation and long-name
  checks as a later hardening gate;
- only token/spacing/focus corrections belong here.

**P6UI.6A status: IMPLEMENTED / CI + REAL RECORDING ACCEPTANCE PENDING**

ArZoom presenter zoom hotkeys:
- behavior pinned to `masarray/arzoom-follow-obs@ada8f5269246c64429d7aceb6cc72f81e72120ba`;
- Toggle Zoom, Zoom In, Zoom Out and Reset / Full Frame use the existing ArZoom
  camera authority;
- Zoom In/Out use 0.25x steps and clamp to 1.10x..4.00x;
- Reset returns to full frame while preserving configured zoom amount;
- bridge ABI v5 extends the existing global-hotkey HWND instead of adding a
  keyboard subsystem;
- RecorderSession uses a bounded atomic presenter mailbox;
- presenter input worker is gated off when no presenter zoom hotkey is assigned;
- Settings presenter shortcuts begin unassigned rather than inventing defaults.

**P6UI.6B status: IMPLEMENTED / CI + REAL RECORDING ACCEPTANCE PENDING**

Press/release presenter controls:
- Hold Zoom and Overview Peek use the existing PresentationInputWorker Raw Input
  pressed-state table; no second keyboard hook or worker;
- bridge ABI v6 carries exact momentary chord bindings at recorder start;
- Hold Zoom composes with Toggle/Smart Zoom and releases only its own intent;
- Overview Peek uses the pinned upstream saved-shot minimum-jerk controller;
- pointer movement during Peek cannot retarget the stored shot;
- Reset blocks still-held momentary chords until physical release;
- deterministic tests cover Hold press/release, Peek restore and cancel-to-full-frame.

**P6UI.6C status: IMPLEMENTED / CI + REAL WINDOWS ACCEPTANCE PENDING**

Presenter Controls Acceptance Lock:
- Raw Input remains the only momentary activation authority;
- `GetAsyncKeyState` is a release-only stale-state fuse and cannot activate a
  chord;
- deterministic Reset block-until-release gate prevents sticky re-arm;
- Toggle/Hold and Hold/Smart Zoom ownership matrices are regression-locked;
- Overview saved-shot return remains locked under pointer motion;
- Region boundary uses the same camera state and is regression-locked for Hold,
  1x Overview and exact saved viewport restoration;
- no second keyboard hook, camera, Region solver or bridge ABI expansion.

**Next milestone after real 6C acceptance: P6UI.6D — Presenter Advanced Controls Decision**
- evaluate Freeze Camera and Toggle Smart Follow against the pinned ArZoom
  authority and current single-camera architecture;
- implement only if they are release-critical and can remain bounded;
- otherwise explicitly defer them and proceed to P6UI.7A native
  system-audio/microphone completion.

**P6UI.6A.1 status: IMPLEMENTED / CI + REAL HOTKEY ACCEPTANCE PENDING**

Hotkey Product Hardening (intentional post-6C debt closure):
- canonical modifier-mask + Windows-VK `HotkeyChord` runtime identity;
- full Windows OEM punctuation family, Numpad, F1-F24 and navigation/editing
  keys;
- Settings capture, duplicate detection, persistence, global registration and
  momentary start config share that one chord model;
- bridge ABI v7 adds a temporary conflict probe on the existing hidden HWND;
- rejected/conflicting assignments never mutate persistent state;
- versioned `%LOCALAPPDATA%\Arssyut\settings.json` snapshot with write-through
  temp + replace, all-or-nothing load and corrupt-file quarantine;
- persistent state is disabled in preview/stress CLI modes;
- Settings scrollbar owns a permanent content gutter;
- regression matrix includes Ctrl+`, Ctrl+Shift+`, Ctrl+=, Ctrl+-, Alt+[,
  Ctrl+Shift+F9, Win+Alt+F12 and Numpad+.

**P6UI.6A.2 status: IMPLEMENTED / CI + REAL WINDOWS ACCEPTANCE PENDING**

Presenter Zoom Configuration:
- one canonical product preset set: 1.10x, 1.25x, 1.50x, 1.75x, 2.00x,
  2.50x, 3.00x and 4.00x;
- Toggle/Hold use the configured presenter zoom at recording start;
- Zoom In/Out remain native 0.25x runtime steps;
- bridge ABI v8 carries validated `presenter_zoom` and removes the bridge
  hardcoded 2.0x assignment;
- one product settings schema v2 persists canonical hotkeys + presenter zoom;
- schema v1 hotkey-only settings migrate losslessly with default 2.00x zoom;
- Settings Preferences sidebar uses a fixed settings-only left alignment rail;
- P6UI.6C camera/Region/Raw Input authorities remain unchanged.

**Next after real P6UI.6A.2 acceptance: P6UI.6D — Presenter Advanced Controls
Decision**, then P6UI.7A if Freeze Camera / Toggle Smart Follow are deferred.

#### P6UI.5 — real floating controller + transport

- keep authoritative recorder state projected from native snapshots;
- preserve the P6UI.4B-A capture-excluded controller guarantee;
- add the real global Record/Stop transport registration without creating a
  second recorder state machine;
- no capture-cadence repaint work;
- verify no flicker during real recording.

#### P6UI.6 — Region acceptance lock

- bind existing native Region editor/crop state through P6UI.4C;
- preserve one Region authority;
- keep Smart Zoom viewport/boundary synchronization;
- validate move/resize, negative-origin monitors and encoded crop parity;
- no C# crop implementation.

#### P6UI.7 — feature completion

Complete still-missing product backends before any legacy retirement:
- **P6UI.7A:** WASAPI system audio + microphone + AAC mux + A/V drift evidence;
- **P6UI.7B:** Media Foundation webcam + retained GPU PiP compositor path;
- **P6UI.7C:** explicit native Pause/Resume state with media-time continuity;
- **P6UI.7D:** dedicated Game backend, or an explicit product deferral if it is
  not release-critical.

#### P6UI.8 — premium product polish lock

- DPI/accessibility/keyboard/focus/motion acceptance;
- semantic resource cleanup and removal of staged/no-op presentation;
- empty/error/saved/recovery states;
- architecture cleanup only after authority parity is already proven.

#### P6UI.9 — legacy shell retirement

Retire the legacy presentation shell only after:
- real Display/Window/Region recording passes through Avalonia;
- audio/microphone/camera release scope is satisfied;
- Pause/Resume and Game are implemented or explicitly deferred by product
  decision;
- result/failure states pass;
- Settings/native bridge passes;
- fallback/recovery behavior is preserved.

See:
- `docs/P6UI_AVALONIA_DESIGN_SYSTEM.md`
- `docs/adr/ADR-002-ui-shell-technology.md`


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
