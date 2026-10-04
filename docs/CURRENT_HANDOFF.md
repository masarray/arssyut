# Current Handoff — Arssyut P6UI

**Updated:** 2026-10-04  
**Active PR:** #31  
**Active branch:** `feat/p6ui-avalonia-shell`  
**Current engineering milestone:** **P6UI.4B implemented / CI green / real-recording acceptance pending**  
**Known-good head before this handoff document:**  
`c38deafebf9f48a07482de4ab8af91d0f4d3337b`

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

Current PR head CI is green:
- **CI run #267**
- native Windows build/tests: green;
- bridge tests: green;
- legacy native GUI smoke: green;
- Avalonia build/publish: green;
- bridge-required Avalonia launch smoke: green.

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

A CI-green build does not replace this real-recording acceptance.

---

## 7. Exact next engineering milestone

### P6UI.4C — native overlay / Region bridge

**Do this next after addressing any P6UI.4B real-acceptance bug.**

The objective is not to invent a new overlay.

Reuse the P6R native implementation.

Required progression:

```text
Avalonia selected source
        |
        v
existing bridge context
        |
        v
native RecorderOverlay
        |
        +--> Display / Window capture boundary
        |
        +--> PresentationFrameState
        |        |
        |        v
        |    Smart Zoom viewport boundary
        |
        +--> existing native Region editor
                 |
                 v
          canonical Region rect
                 |
                 v
          existing RecorderConfig crop
```

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

Therefore P6UI.4C must focus on:
- show existing native Region boundary from Avalonia;
- move Region;
- resize Region;
- selected rectangle equals encoded crop;
- region works on monitors with negative virtual-screen origins;
- Smart Zoom boundary contracts/moves **inside the selected Region**;
- no desktop input blocking outside Region editor interaction.

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
