# Current Handoff — Arssyut P6UI

**Updated:** 2026-10-04  
**Active PR:** #31  
**Active branch:** `feat/p6ui-avalonia-shell`  
**Current engineering milestone:** **P6UI.4C-A product reality + visual lock implemented / CI + real GUI acceptance pending**  
**Canonical baseline entering this correction:**  
`91959c43f9d62972d98bfeaea6ec7eea035cba3d` (P6UI.4C native overlay / Region bridge)

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
