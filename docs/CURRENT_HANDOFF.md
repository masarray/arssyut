# Current Handoff — Arssyut P6UI

**Updated:** 2026-10-05  
**Active PR:** #31  
**Active branch:** `feat/p6ui-avalonia-shell`  
**Current engineering milestone:** **P6UI.6A.1 Hotkey Product Hardening implemented / CI + real Windows acceptance pending**  
**Canonical baseline entering this milestone:**  
`5e9c858e2a51cda2594e823792f83a61c3fe601a` (P6UI.6C Presenter Controls Acceptance Lock)

This file is the first document a new ChatGPT thread or engineer must read
before continuing P6UI work.

Do not reconstruct project state from old chat messages. The repository is the
source of truth.

---

## 1. Where the project actually is

The project is no longer at P6UI.3.

P6UI.4A and P6UI.4B have already been implemented.

### P6UI.4A — read-only native bridge

Completed:
- versioned `arssyut_native_bridge.dll`;
- one opaque native application-lifetime context;
- native source snapshots from the existing P6R source catalog;
- native microphone/camera snapshots from the existing device catalog;
- native RecorderSession snapshot projection;
- generation-scoped opaque source/device tokens;
- managed `NativeBridgeClient`;
- duplicate managed Win32 source enumeration removed;
- duplicate Avalonia capture-boundary window removed from runtime;
- packaged Avalonia launch smoke requires the bridge.

### P6UI.4B — command bridge

Completed:
- ABI v2 native Record/Stop command surface;
- normal Avalonia Display/Window Record uses the existing native
  `RecorderSession`;
- Stop is nonblocking and native Stopping/Finalizing state remains
  authoritative;
- real output and diagnostics paths return to Avalonia;
- Open / Folder actions use the real native result path;
- selected native source is resolved only from its opaque generation token;
- frame rate, visual product mode, Smart Zoom, click visualization, shortcut
  visualization and output-folder intent are canonicalized into native start
  configuration;
- floating controller follows the native session lifecycle;
- stale source, busy session and start failures are explicit;
- unsupported media inputs are rejected instead of silently omitted.

The P6UI.4B checkpoint is CI-green through **CI run #269**. P6UI.4B-A adds
capture-exclusion and Settings-truthfulness guards on top of that checkpoint.
The latest PR-head CI, not this historical run number, is authoritative for the
new acceptance-lock commit.

---

## 2. Locked authorities — DO NOT REWRITE

Read `docs/ENGINE_BASELINE_LEDGER.md` for detailed commit provenance.

The ownership rules are:

| Concept | Authority |
| --- | --- |
| Capture / WGC | native C++ |
| CFR / media clock | native C++ |
| H.264 / MF / MP4 | native C++ |
| ArZoom camera | native C++ |
| Native cursor | WGC |
| Click/keycap compositor | native C++ |
| ArVisual | native C++ |
| Source catalog | native C++ |
| RecorderSession | native C++ |
| Capture boundary | native P6R `RecorderOverlay` |
| Smart Zoom boundary | native PresentationFrameState / viewport geometry |
| Region rect/crop | native Region geometry + RecorderConfig crop |
| Main product shell | Avalonia |
| Settings/product interaction | Avalonia |
| Inter/Lucide/material/motion | Avalonia |

**Never add a second source catalog, Region rect, capture boundary, camera
viewport solver, media clock, recorder session, cursor compositor or encode
path in managed code.**

---

## 3. Baselines and recovery checkpoints

### Accepted engine baseline

`main@5c3684b6ce0dcd1037eb35c60efce17294bb6bae`

P5E real-visual accepted engine baseline.

### Frozen P6R native functional reference

Branch:

`milestone/p6r-native-functional`

Commit:

`b11451bd640a072d81dbd1024b4c641a76c9daac`

Use this when inspecting:
- RecorderOverlay;
- Display/Window capture boundary;
- Smart Zoom boundary contraction/movement;
- Region editor;
- Region crop mapping;
- negative-origin multi-monitor geometry.

### P6UI.3 visual checkpoint

Branch:

`milestone/p6ui3-settings-controller-baseline`

Commit:

`0e3dedb0dfe47dbfcb128e43d31d095041310915`

Use only as a visual recovery checkpoint for:
- Settings;
- Inter/Lucide treatment;
- floating controller presentation.

Do not restore its preview engine authorities over P6UI.4.

### P6UI.4B checkpoint

A milestone branch is created from the P6UI.4B command-bridge state so future
visual/overlay work always has a safe recovery point:

`milestone/p6ui4b-command-bridge-baseline`

---

## 4. Important history — why this handoff exists

This project has already paid the cost of replacing accepted authorities with
simplified duplicates:

- P3 required P3R recovery;
- P4R.2 custom cursor was removed by P4R.3 in favor of native WGC cursor;
- P6UI temporarily recreated source enumeration/boundary in Avalonia, causing a
  real multi-monitor input-blocking regression.

The repair was P6UI.4A:
- managed source enumeration removed;
- Avalonia capture-boundary runtime removed;
- source identity returned to native opaque tokens.

Do not reintroduce those deleted duplicate authorities.

---

## 5. Current product behavior

### Display / Window

These are now native-command paths.

The Avalonia source picker receives native snapshots, retains opaque native
tokens and starts the bridge-owned native RecorderSession.

### Region

**Not yet enabled in normal Avalonia recording.**

This is intentional.

Region already exists natively and must be reintroduced through the native
overlay/Region bridge. Do not create another Avalonia Region editor.

### Game

Explicitly unsupported until a dedicated native backend exists.

Do not alias Game to Window capture.

### System audio / microphone / camera

Presentation/device discovery exists, but recording backends are not yet bound.

If the user enables an unavailable stream, recording must fail clearly rather
than pretend the stream was captured.

---

## 6. Real acceptance still required for P6UI.4B

Before declaring P6UI.4B accepted, test on a real Windows machine:

1. Record Display 1.
2. Record Display 2 on a multi-monitor system.
3. Record a Window source.
4. Verify Avalonia remains responsive through:
   `Record -> Stop -> Stopping -> Finalizing -> Saved`.
5. Verify produced MP4 opens.
6. Verify Open and Folder actions use the actual output path.
7. Repeat start/stop several times.
8. Test 30 fps and 60 fps.
9. Test Pixel Accurate, Clean Screen and Vivid Presentation.
10. Confirm Smart Zoom/click/shortcut configuration reaches native recording.
11. Turn on an unbound audio/mic/camera stream and confirm Record is blocked
    with an explicit unsupported status.
12. During Display recording, verify the floating Avalonia controller is never
    present in the encoded MP4. The controller HWND must report
    `WDA_EXCLUDEFROMCAPTURE` (or `WDA_MONITOR` only on compatibility
    fallback).

Automated acceptance-hardening details and the manual evidence matrix live in
`docs/P6UI4B_ACCEPTANCE_LOCK.md`.

A CI-green build does not replace this real-recording acceptance.

### P6UI.4C-A — product reality + visual lock

The user's real Windows screenshots exposed a presentation/package defect, not
an engine rollback:

- the Avalonia shell reported the bridge unavailable and silently kept a
  simulator-capable product surface alive;
- Record and multiple Settings controls looked usable even when their advertised
  backend did not exist;
- Audio/Camera pages showed deterministic preview meters and hard-coded devices;
- internal preview/milestone labels leaked into product presentation;
- right-side Settings controls did not share one clean alignment column and
  long description text could collide with controls.

The correction is intentionally presentation/package-only:

- normal product mode no longer falls back to `PreviewRecorderSession`;
- if the native engine cannot load, Record is unavailable and the UI says so
  explicitly;
- audio/microphone/camera and other unbound choices are status/capability
  surfaces, not fake controls;
- Settings ComboBoxes and right-side controls share a 190 px aligned column
  with wrapping descriptive text;
- internal preview labels are removed from normal presentation;
- the native bridge is embedded in the single executable and extracted/loaded
  deterministically from a versioned SHA-256 cache;
- CI verifies that no external bridge DLL is required and then launches the
  exact packaged executable with `--bridge-required`.

No accepted implementation under `src/app`, `src/core` or `src/platform`
is changed by this correction.

See `docs/P6UI4C_PRODUCT_REALITY_VISUAL_LOCK.md`.

---

### P6UI.4C-B / P6UI.5A / P6UI.5B batched implementation

- Region tail noise is corrected only in native layered-window repaint:
  `SWP_NOCOPYBITS` during editable resize plus full client redraw on size.
- Bridge ABI v4 adds Windows global hotkey registration on the existing hidden
  bridge HWND. F9 is one context-aware Start/Stop action and does not depend on
  Avalonia focus.
- Settings validates multi-key Ctrl/Shift/Alt/Win chords against Windows;
  duplicate/reserved registrations surface as conflicts.
- Pause/Resume, Mute/Unmute microphone and Show/Hide webcam shortcut grammar is
  visible but capability-gated until those native backends exist.
- Main recorder is recomposed as Capture | Audio | Webcam | Record with capture
  mode dropdown, native source picker, honest pending media state and circular
  primary Record action.
- Workflow concurrency cancels superseded PR runs. Implementation is assembled
  into one Git tree before one branch update, avoiding intermediate CI queues.

Only `src/app/recorder_overlay.cpp` crosses the protected native baseline, as
the narrow reproduced Region repaint correction.

### P6UI.5C — visual polish / tactile-control lock

Real screenshot review after P6UI.5B exposed presentation gaps rather than
engine defects:

- the main F9 badge did not read as a physical keycap;
- Capture mode still looked like a generic ComboBox rather than the primary
  mode selector;
- Audio/Microphone/Webcam context blocks were visually passive;
- detected media device context was not visible in the main workspace;
- Settings hotkey values still read as left-aligned form fields.

P6UI.5C corrects those points without pretending pending media backends exist:

- Capture uses four tactile Lucide toggle buttons with exactly one active mode;
- main and Settings hotkeys use one centered keycap visual grammar;
- Audio and Microphone rows get Lucide identity, a visible capability-gated
  on/off toggle and a compact device selector surface;
- Webcam gets a capability-gated toggle and detected-device selector;
- microphone/camera names come only from the existing native device snapshot;
- System audio truthfully shows only `Default playback device` because no
  playback-device catalog exists in the bridge yet;
- pending audio/microphone/webcam controls remain disabled until the real
  recording backends can change encoded output.

This milestone is presentation-only. P6UI.4C-B Region repaint, P6UI.5A global
hotkey, native RecorderSession and all capture/media authorities remain
unchanged.

### P6UI.6A — ArZoom presenter zoom hotkeys

The user accepted the P6UI.5C GUI direction for continued functional progress
and explicitly requested zoom hotkeys like ArZoom.

Authority was verified against the pinned upstream source already used by P3R:
`masarray/arzoom-follow-obs@ada8f5269246c64429d7aceb6cc72f81e72120ba`.

P6UI.6A implements the safe latch/step subset:
- Toggle Zoom;
- Zoom In (+0.25x);
- Zoom Out (-0.25x);
- Reset / Full Frame.

Behavior remains owned by the existing `PresentationController ->
ArZoomCameraAdapter` path:
- manual latch is OR-composed with existing Smart Zoom intent;
- configured zoom remains bounded to ArZoom 1.10x..4.00x;
- Reset clears active manual/automatic zoom and returns smoothly to 1x while
  preserving configured zoom amount;
- RecorderSession receives commands through a bounded atomic mailbox;
- bridge ABI v5 exposes global presenter actions through the same hidden HWND;
- repeat-sensitive Zoom In/Out commands dispatch directly from WM_HOTKEY into
  the bounded native mailbox, avoiding UI-timer event coalescing;
- Settings exposes assignable presenter keycaps with no invented defaults;
- the presentation input worker is enabled for this feature only when at least
  one presenter hotkey is assigned.

Hold Zoom and Overview Peek are intentionally deferred to P6UI.6B because they
need press/release semantics. P6UI.6B must extend the existing raw-input worker
rather than create another keyboard hook.

P6UI.5D multi-DPI/focus polish remains a hardening task but no longer blocks
functional progression after the accepted real screenshot direction.

### P6UI.6B — Hold Zoom / Overview Peek

P6UI.6B completes the press/release presenter subset from the pinned ArZoom
authority without adding another keyboard subsystem.

Implementation:
- Hold Zoom and Overview Peek bindings are passed through bridge ABI v6 as
  recording-start configuration;
- bindings are not registered with `RegisterHotKey`; RecorderSession samples
  the existing `PresentationInputWorker` Raw Input pressed-state table on the
  existing presentation cadence;
- exact modifier matching means releasing Ctrl/Shift/Alt/Win immediately
  releases the momentary action even if the primary key is still down;
- Hold Zoom is OR-composed with Toggle Zoom and Smart Zoom;
- the exact upstream `OverviewPeekController` is vendored into the existing
  ArZoom include surface and reuses the same camera transform math;
- Overview Peek saves the shot, glides to centered full frame, freezes camera
  retargeting while visible, and restores the saved shot on release;
- if zoom intent ends during Peek, cancel-to-overview ends at full frame;
- Reset / Full Frame blocks still-held Hold/Peek chords until physical release,
  matching upstream non-sticky behavior;
- deterministic tests lock Hold press/release, saved-shot Peek restore, and Peek
  cancellation when zoom intent disappears.

No capture, Region, compositor, encoder, timing or second-camera authority is
introduced.

### P6UI.6C — Presenter Controls Acceptance Lock

P6UI.6C hardens the P6UI.6A/6B presenter controls without creating another
input or camera authority.

Implemented lock:
- Raw Input remains the only Hold/Peek activation source;
- `GetAsyncKeyState` is used only as a stale-release fuse after Raw Input has
  already matched the chord, so focus/desktop transitions fail safe toward
  release rather than sticky zoom;
- Reset's block-until-release behavior is formalized as a deterministic
  `MomentaryReleaseGate` and regression-tested;
- Toggle Zoom + Hold Zoom overlap is locked: releasing/toggling one owner does
  not clear the other;
- Hold Zoom + Smart Zoom overlap is locked: releasing Hold preserves the active
  Smart Zoom window;
- Overview Peek saved-shot restore remains locked under pointer movement;
- presenter camera state is fed through the existing Region
  `camera_viewport_rect()` path, with deterministic tests for contracted Hold
  viewport, exact 1x Overview full frame and exact saved viewport restoration;
- no bridge ABI change, capture change, Region geometry rewrite, compositor
  change, media timing change, encoder change or second camera/input path.

Real Windows acceptance remains required for:
1. Hold/Peek while another application owns focus;
2. release primary key vs modifier in different orders;
3. alt-tab during a held chord;
4. Reset while Hold/Peek remains physically down;
5. Region boundary following Hold/Peek during a real recording.

**Next after real 6C acceptance:** P6UI.6D Presenter Advanced Controls decision
/ parity work for Freeze Camera and Toggle Smart Follow, but only through the
same accepted camera authority. If those controls are not release-critical,
skip feature expansion and proceed to P6UI.7A native audio/microphone.



### P6UI.6A.1 — Hotkey Product Hardening

This milestone intentionally closes the earlier hotkey-product debt after the
P6UI.6C presenter camera/input checkpoint. P6UI.6C remains frozen; this work
does not reopen camera, Region or Raw Input ownership.

Implemented:
- one canonical managed `HotkeyChord` identity: modifier mask + Windows VK;
- comprehensive OEM punctuation mapping including grave/backtick, brackets,
  slash/backslash, semicolon/quote, comma/period, minus/equal;
- Numpad digits/arithmetic, navigation/editing keys and F1-F24;
- canonical display labels and alias normalization;
- generic duplicate detection by chord identity rather than string spelling;
- bridge ABI v7 temporary hotkey conflict probe using the existing hidden HWND;
- Settings probes Windows conflicts before mutating/persisting any native-facing
  shortcut, including Hold Zoom / Overview Peek;
- MainWindow registration and momentary recorder-start config consume canonical
  chords directly; no string reparse;
- versioned hotkey snapshot persistence at
  `%LOCALAPPDATA%\Arssyut\settings.json`;
- atomic write-through temp + replace, all-or-nothing restore, corrupt-file
  quarantine and safe-default fallback;
- preview/stress CLI modes do not touch user persistence;
- one ScrollViewer content gutter prevents the scrollbar from colliding with
  keycap CTA controls;
- automated acceptance matrix covers OEM, Numpad, F-key and navigation chords,
  persistence round-trip and corrupt-file recovery.

P6UI.6C presenter controls remain the accepted camera/input checkpoint.

**Exact next milestone after real hotkey acceptance:** P6UI.6A.2 Presenter Zoom
Configuration — persist/expose the configured Toggle Zoom amount and remove the
remaining native hardcoded 2.0x start value without changing camera authority.

## 7. Current / exact next engineering milestone

### P6UI.4C — native overlay / Region bridge

The objective remains: do not invent a new overlay.

P6UI.4C now implements the P6R reuse path:

```text
Avalonia selected source
        |
        v
bridge ABI v3
        |
        v
hidden native message owner
        |
        v
existing P6R RecorderOverlay
        |
        +--> Display / Window click-through boundary
        |
        +--> RecorderSnapshot camera state
        |        |
        |        v
        |    Smart Zoom viewport boundary
        |
        +--> existing native Region editor
                 |
                 v
          canonical native Region rect
                 |
                 v
          existing map_region_to_crop()
                 |
                 v
          RecorderConfig.crop
```

Automated regression now exercises the actual `ArssyutCaptureBoundary` HWND
with `WM_NCHITTEST`, including the previous red-border input-blocking failure
class.

The next work inside P6UI.4C is **CI completion + real Windows acceptance +
narrow correction of any reproduced Region correctness defect**. Do not replace
the existing overlay/Region implementation.

Constraints:
- no Avalonia capture-boundary window;
- no C# Region crop math;
- no second capture path;
- no rebuilding HWND/HMONITOR from labels;
- source token resolution stays inside native bridge;
- Smart Zoom visible boundary must use the same native camera values as the
  compositor;
- custom Region fix starts from the existing P6R Region path, because that path
  already has geometry/crop/negative-origin tests.

### Region acceptance target

The user previously observed that native Display/Window boundary + Smart Zoom
was already substantially correct.

The genuine remaining Region gap is real custom sizing/moving acceptance.

P6UI.4C implementation now covers the bridge path. Real acceptance must focus
on:
- verify Display/Window border never blocks click, drag, hover or scroll;
- move Region from the native pill;
- resize Region from every edge/corner;
- selected rectangle equals encoded crop;
- Region works on monitors with negative virtual-screen origins;
- Smart Zoom boundary contracts/moves **inside the selected Region**;
- Region interior remains click-through;
- Clean Screen / Vivid Presentation scene analysis is verified against the same
  canonical Region crop.

Do not restart Region from scratch.

---

## 8. UI direction remains forward-moving

Engine lock does **not** mean visual work must slow down.

Avalonia may continue improving aggressively:
- main-window composition;
- spacing;
- typography;
- real Lucide icons;
- source picker presentation;
- hover / selected / pressed state layers;
- Mica/Acrylic material;
- Settings organization;
- compact floating controller;
- accessibility;
- DPI quality.

The design-system rules remain in:
- `docs/P6UI_AVALONIA_DESIGN_SYSTEM.md`
- `docs/adr/ADR-002-ui-shell-technology.md`

Do not make engine changes simply to simplify a UI implementation.

---

## 9. Files a new thread must read

Read in this order:

1. **`docs/CURRENT_HANDOFF.md`** — current state and exact next step.
2. **`docs/ENGINE_BASELINE_LEDGER.md`** — historical subsystem authorities.
3. **`docs/ROADMAP.md`** — milestone gates.
4. **`docs/adr/ADR-008-native-ui-bridge.md`** — bridge ownership/lifecycle.
5. **`AGENTS.md`** — coding, architecture and testing contracts.

For overlay/Region work also inspect the frozen P6R implementations before
writing code:
- `src/app/recorder_overlay.*`
- `src/app/region_geometry.*`
- `src/app/windows_main.cpp` only as historical wiring/reference;
- `src/app/recorder_session.*`
- existing Region/viewport tests.

---

## 10. New-thread operating rule

If another thread receives a generic request such as **"continue progress"**:

1. fetch PR #31 and current branch head;
2. read this file;
3. inspect latest CI;
4. preserve every already-accepted authority;
5. fix current real-acceptance bugs first;
6. otherwise continue at the **Exact next engineering milestone** above;
7. never move back to an earlier prototype implementation just because it is
   easier to wire.

If repository state has advanced beyond this handoff, update this file first
and continue from the newer repository state.

---

## 11. Merge rule

PR #31 is still the active integration PR.

Do not merge merely because CI is green.

Merge only after the current real-GUI/recording acceptance gate is satisfied
for the milestone being closed.

The repository, tests and acceptance evidence—not chat memory—decide what is
complete.
