# Source Provenance and License Ledger

This ledger is mandatory for source transplanted into Arssyut.

Arssyut is licensed under **GPL-2.0-or-later** so portable source derived
from the current ArZoom and ArVisual repositories can remain license-compatible.

## Current P0 status

No ArZoom or ArVisual implementation source has been copied into the P0 core.
P0 establishes only original Arssyut infrastructure: result/status primitives,
bounded concurrency helpers, diagnostics, canonical clock, session state, D3D11
ownership, tests, and build/CI files.

## Approved upstream baselines for future transplant

### ArZoom

- Repository: `masarray/arzoom-follow-obs`
- Planning baseline: `ada8f5269246c64429d7aceb6cc72f81e72120ba`
- License: GPL-2.0-or-later
- Candidate portable units:
  - `src/arzoom-math.hpp`
  - `src/arzoom-click-visual.hpp`
  - `src/arzoom-scene-motion-synthesizer.hpp`
  - `src/arzoom-scene-viewport-planner.hpp`
  - presentation cursor/mapping units as specifically audited later

### ArVisual

- Repository: `masarray/arvisual-obs`
- Planning baseline: `d0a3f405447446e88dc56f4a50535a17257fcccf`
- License: GPL-2.0-or-later
- Candidate portable behavior:
  - scene-statistics math from `src/arvisual-filter.cpp`
  - bounded adaptive parameters
  - grading shader behavior and visual-safety constraints

## Required entry for every future transplant

Before merging copied/adapted implementation source, add:

| Field | Required value |
|---|---|
| Local file | Arssyut destination |
| Upstream repository | Exact repository |
| Upstream commit | Exact immutable SHA |
| Upstream file(s) | Exact source path(s) |
| License | SPDX identifier |
| Adaptation | What changed and why |
| Parity test | Test proving intended behavior survived the transplant |

Do not use "copied from latest" as provenance. Pin an immutable commit.
