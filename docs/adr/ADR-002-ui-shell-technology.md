# ADR-002 — UI shell technology

Status: Accepted for P6UI

Date: 2026-10-04

## Context

The native P6R prototype proved recorder interaction and capture behavior, but
direct GUI acceptance repeatedly rejected the Win32/GDI presentation quality.

The product requires:
- compact modern typography;
- vector icons;
- consistent tokens and reusable control themes;
- custom chrome;
- Mica/Acrylic/transparent floating surfaces;
- fast visual iteration without moving recorder logic into the view layer.

## Options

### Continue Win32/GDI

Advantages:
- no bridge to the C++ recorder;
- smallest conceptual runtime delta.

Rejected because premium typography, scalable vector control chrome, reusable
stateful styling and animation all require large amounts of bespoke work. The
prototype already demonstrated control-style leakage and inconsistent visual
quality.

### Qt Quick/QML

Advantages:
- strong GPU UI and animation;
- direct C++ integration.

Not selected because it introduces a second large native UI stack and build
ecosystem when Arssyut already has a stable C++ engine and only needs a modern
presentation shell.

### Avalonia

Advantages:
- XAML resource dictionaries and ControlThemes;
- modern Skia text/vector rendering;
- official embedded Inter package;
- real Lucide icon package;
- Windows custom chrome and transparency levels;
- fast separation between UI simulation and native binding.

Selected.

## Decision

The final recorder presentation shell is C# / Avalonia.

The native C++ recorder remains authoritative for:
- capture;
- Region geometry;
- frame timing;
- ArZoom;
- ArVisual;
- encoder/mux;
- diagnostics;
- recovery.

P6UI initially runs as a separate preview executable. A stable native bridge is
introduced only after visual/interaction acceptance.

## Consequences

Positive:
- design can mature before native API binding;
- UI state can be simulated deterministically;
- native hot paths remain untouched;
- visual regression is isolated from media regression.

Costs:
- one managed UI runtime is added;
- packaging becomes larger;
- a narrow C ABI/interop surface must be maintained;
- native overlays may remain separate until equivalent Avalonia behavior is
  proven capture-safe and flicker-free.

## Dependency policy

Pinned for P6UI.1:
- Avalonia 12.1.3;
- Avalonia.Fonts.Inter 12.1.3;
- Lucide.Avalonia 0.2.23.

Dependencies are reviewed for licensing and update impact before version bumps.
