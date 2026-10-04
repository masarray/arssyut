# P6UI — Arssyut Avalonia Design System

## Purpose

P6UI replaces the Win32/GDI presentation shell without replacing the recorder
engine.

The native P6R branch proved capture modes, Region crop geometry, source/device
enumeration, nonblocking completion and recording-boundary behavior. Its UI is
therefore treated as a functional prototype, not the final product surface.

The final product shell is Avalonia.

## Architecture

```text
Arssyut.UI
C# / Avalonia / XAML
Inter / Lucide / design tokens
        |
        | stable native bridge (P6UI.4)
        v
arssyut native core
C++20 / WGC / D3D11 / Media Foundation / ArZoom / ArVisual
```

No capture, compositor, encoder or media clock is reimplemented in C#.

## Visual language

Arssyut intentionally uses a narrow product palette:

- graphite canvas and surfaces;
- white/neutral typography;
- one recording red semantic accent;
- green only for success/ready state;
- no cyan secondary brand accent;
- no giant card dashboard;
- no oversized typography.

The visual hierarchy is quiet and compact. Borders are hairlines, spacing uses a
4 px base scale and controls normally stay between 28 and 40 DIP high.

## Tokens

Foundational resources live under `src/ui/Arssyut.UI/Design/`.

### Spacing

```text
4 / 8 / 12 / 16 / 20 / 24
```

### Heights

```text
XS 28
SM 32
MD 36
Record 40
```

### Radius

```text
6 / 8 / 10 / 12
```

### Icon sizes

```text
16 / 18 / 20
```

## Typography

Inter is embedded through `Avalonia.Fonts.Inter`.

Scale:

- micro/caption: 11;
- body: 12;
- control: 12 Medium;
- section: 13 SemiBold;
- product/window title: 16 SemiBold.

Numeric recorder state should use tabular numerals (`+tnum`) where movement
would otherwise cause layout jitter.

Loose font files must not be added to the repository.

## Icons

P6UI uses `Lucide.Avalonia` directly.

Rules:
- do not redraw Lucide with GDI primitives;
- preserve Lucide's rounded stroke grammar;
- normal icon sizes are 16–20 DIP;
- active/recording color comes primarily from the control/container;
- avoid arbitrary filled glyph substitutions.

## Main recorder information architecture

The main window has four levels only:

1. custom product chrome;
2. capture mode + source context;
3. system audio / microphone / camera;
4. quiet config summary + primary Record action.

Deep configuration belongs in Settings.

Pause is contextual and appears only while recording.

Output/result actions appear only after a session produces an output.

## Settings

Settings is a separate compact window with category navigation:

- General
- Recording
- Output
- Audio
- Camera
- Mouse & Keystroke
- Hotkeys
- Advanced

A settings row contains:
- label;
- optional short description;
- one control aligned to the right.

Settings must never fall back to native white ComboBox/ListBox chrome.

## Material

System Mica is requested first on supported Windows versions, with Acrylic and
opaque fallback.

Glass is a material, not a decoration strategy:
- main window: restrained translucent shell;
- recording controller: stronger floating acrylic treatment;
- settings content: mostly opaque for readability.

## Migration rule

The legacy Win32 shell remains buildable until the Avalonia shell is bound to
the native recorder and passes real-recording acceptance.

Do not delete the legacy shell early and do not add new visual features to it.
Bug fixes required to preserve the frozen native baseline remain allowed.

## Acceptance

P6UI visual acceptance requires real screenshots at 100%, 125%, 150% and 200%
DPI.

Check:
- Inter rendering;
- real Lucide stroke quality;
- consistent compact spacing;
- no native gray/white control leakage;
- no text clipping;
- no oversized cards or headings;
- source/device names remain readable;
- recorder primary action remains obvious;
- Settings categories and rows remain dense but calm.
