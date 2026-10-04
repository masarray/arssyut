# P6UI.4C-A — Product Reality + Visual Lock

**Parent engineering baseline:** P6UI.4C `91959c43f9d62972d98bfeaea6ec7eea035cba3d`

This correction exists because real GUI screenshots showed a serious product
truthfulness problem: the Avalonia shell could look usable while the native
bridge was unavailable.

## 1. Engine status

The accepted native engine is not removed, replaced or reimplemented.

This correction does not modify the accepted implementation under:

- `src/app`
- `src/core`
- `src/platform`

Display/Window/Region recording, RecorderSession, WGC, Smart Zoom, ArVisual,
cursor/click/keycap presentation, CFR, H.264/MP4 and native RecorderOverlay
remain owned by the existing native C++ path.

## 2. Product-mode rule

Normal product startup has only two truthful states:

1. native engine is available -> expose only capabilities that are genuinely
   bound; or
2. native engine is unavailable -> show that failure and disable Record.

It must never silently start `PreviewRecorderSession`.

Preview simulation remains available only through explicit deterministic
preview/stress arguments used for UI testing.

## 3. No fake controls

A control may look interactive only if it changes real product/native state.

Current genuinely connected presentation/config intent includes:

- Display / Window / Region source selection;
- Record / Stop;
- 30 / 60 fps;
- Pixel Accurate / Clean Screen / Vivid Presentation;
- Smart Zoom;
- click visualization;
- shortcut visualization;
- output folder;
- saved Open / Folder result actions.

Capabilities without a recording backend are shown as locked status/capability
surfaces rather than fake controls. This currently includes system audio,
microphone recording, camera/PiP, Game capture, Pause/Resume and staged policy choices.

No deterministic fake audio meters or hard-coded demo device choices are shown
in normal product mode.

## 4. Settings visual lock

The Settings workspace follows one right-edge alignment system:

- right-side selectable controls occupy a common 190 px column;
- enabled ComboBoxes stretch to that column;
- fixed values use right-aligned non-interactive value pills;
- descriptive text wraps before the control column;
- internal engineering labels are not visible in the normal product UI.

The aim is a clean engineer/product workspace: quiet hierarchy, consistent
edges, no ambiguous affordance and no overlap such as the previous File naming
description/dropdown collision.

## 5. Single-executable native-engine package

The native bridge is staged during CI, embedded into the managed application as
`Arssyut.Native.arssyut_native_bridge.dll`, and resolved by a deterministic
native-library resolver.

At runtime the resource bytes are:

1. SHA-256 hashed;
2. written to a content-addressed directory under
   `%LOCALAPPDATA%\Arssyut\Native\<hash-prefix>`;
3. hash-verified; and
4. loaded explicitly with `NativeLibrary.Load`.

This keeps the user-facing package as one executable while still loading the
existing native bridge as an actual Windows DLL.

CI must verify:
- the product executable exists;
- no loose `arssyut_native_bridge.dll` escapes into the product artifact;
- no PDB files leak into the product artifact;
- the packaged executable stays alive with `--bridge-required`.

## 6. Real Windows acceptance

The next artifact must be checked for:

- real Display/Window/Region source enumeration;
- Game is visibly unavailable until its native backend exists;
- Record unavailable if engine/source is unavailable;
- no simulator session in normal product use;
- no fake microphone/camera/audio device menus;
- no fake audio meters;
- Frame rate and Visual style controls aligned to the same right edge;
- File naming row has no text/control collision;
- General/Output/Mouse/Advanced fixed values read as status, not clickable
  controls;
- Settings has no `UI PREVIEW`, `P6UI.x preview` or `Visual acceptance`
  product-facing labels;
- the existing P6UI.4C red viewport boundary remains click-through;
- real recording still produces the same native MP4 path.

Only after this product-reality pass should visual polish continue into broader
P6UI.5 transport work.
