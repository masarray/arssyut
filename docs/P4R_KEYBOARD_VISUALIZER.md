# P4R — Keyboard Visualizer Recovery

Status: implementation branch `feat/p4r-keyboard-visualizer`

P4R replaces the first shortcut overlay with a privacy-safe semantic model and
a compact physical-keycap renderer.

## Why recovery was needed

The first P4 path proved global shortcut capture, but two design decisions were
not suitable as the production contract:

1. runtime state stored a formatted string such as `Ctrl + C`, then the
   compositor parsed that string again;
2. the visual was one large dark rounded panel containing gray buttons, rather
   than individual physical keycaps matching the supplied keyboard-button
   design direction.

That representation mixed semantic input with presentation formatting and made
future layout/style work unnecessarily fragile.

## Ownership model

```text
Windows Raw Input
    ↓
Windows virtual-key mapping
    ↓
ShortcutChord
    key: semantic ShortcutKey
    modifiers: canonical Ctrl/Shift/Alt/Win bitmask
    ↓
privacy/display policy
    ↓
KeyboardOverlayFrame
    fixed array: max 5 structured keycaps
    ↓
retained keycap raster texture
    ↓
GPU compositor
```

No formatted keyboard transcript is stored.

## Privacy contract

Default behavior is action visualization, not typing visualization.

Hidden by default:

- unmodified A-Z;
- unmodified 0-9;
- unmodified Space;
- unmodified punctuation.

Visible:

- modified action chords such as Ctrl+C, Ctrl+V, Ctrl+Shift+S, Alt+Tab,
  Win+D and Win+Shift+S;
- F1-F24;
- navigation/edit action keys;
- Print Screen / Pause / Menu;
- supported volume/media action keys.

The visualizer never stores clipboard contents and never reconstructs the text
being typed.

## Canonical chord contract

Modifiers always render in this order:

`Ctrl -> Shift -> Alt -> Win -> action key`

Left/right modifier variants collapse to the same semantic modifier when the
visual meaning is identical.

The maximum overlay is fixed at five keycaps: four modifiers plus one action.

Repeated Raw Input key-down events while a physical key remains pressed are
suppressed by the input worker. Equivalent semantic chords arriving inside the
short duplicate window are coalesced by the presentation controller without
creating a new raster generation.

## Visual contract

The supplied keyboard-buttons SVG is a visual reference, not a runtime asset.
It contains large traced vectors and is intentionally not parsed every frame.

P4R renders compact physical keycaps with retained native resources:

- transparent background; no large enclosing card;
- dark modifier keycaps;
- light action keycap;
- subtle physical depth;
- restrained border;
- compact spacing;
- bottom-center placement;
- content-sized overlay instead of a fixed 36% screen-wide panel;
- bounded adaptive scale for 720p through 4K output.

The fixed D3D11 texture is updated only when shortcut generation changes.
Font, brushes, pens, DIB and GPU texture are created once and reused.

## Input backend

Primary backend remains Raw Input on the dedicated message worker.

A low-level keyboard hook is **not** added speculatively. It is allowed only if
direct Windows validation demonstrates a required system shortcut that Raw
Input cannot observe. Secure-attention/secure-desktop sequences are outside the
normal recorder shortcut contract.

## Animation

Shortcut overlays use bounded time-based presentation state:

- short minimum-jerk fade in;
- stable hold;
- minimum-jerk fade out;
- no animation history proportional to recording duration.

The compositor samples only the current bounded overlay state.

## Automated gates

Core shortcut tests verify:

- ordinary typing is hidden;
- modified printable keys are visible;
- canonical modifier order;
- maximum five-keycap capacity;
- deterministic F/system/media labels;
- semantic chord identity.

Windows keymap tests verify:

- letters;
- F24;
- OEM plus;
- media keys;
- modifier recognition;
- modifier-only input never becomes a primary action.

Windows compositor tests verify:

- structured keycap overlay renders successfully;
- changing shortcut generation does not allocate new compositor frame
  resources.

## Direct visual acceptance

Record 1080p60 and exercise at least:

- Ctrl+C;
- Ctrl+Shift+S;
- Alt+Tab;
- Win+D;
- Win+Shift+S;
- F5 / F12;
- arrow/navigation key;
- repeated Ctrl+C;
- ordinary typing.

Acceptance requires:

- ordinary typing produces no overlay;
- chord order is correct;
- keycaps are compact and legible;
- no large background panel;
- overlay timing is smooth;
- repeated key-down does not flicker;
- no visible recording hitch when the overlay changes.

P5 ArVisual work may proceed only after the P4R build is technically green;
final keycap polish can still be tuned from a real recording without changing
the semantic architecture.
