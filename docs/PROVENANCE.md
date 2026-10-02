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

## Implemented transplant — P3R ArZoom parity recovery

| Field | Value |
|---|---|
| Local files | `third_party/arzoom/include/arzoom-math.hpp`, `arzoom-smart-zone-camera.hpp`, `arzoom-scene-motion-synthesizer.hpp`, `arzoom-scene-viewport-planner.hpp`, `arzoom-camera.hpp`, `arzoom-click-visual.hpp` |
| Upstream repository | `masarray/arzoom-follow-obs` |
| Upstream commit | `ada8f5269246c64429d7aceb6cc72f81e72120ba` |
| Upstream files | matching `src/*.hpp` files listed above |
| License | GPL-2.0-or-later |
| Adaptation | **None in vendored files.** Arssyut integration is isolated in `src/presentation/arzoom_camera_adapter.hpp` and recorder/compositor glue. The earlier namespace-adapted copies under `src/presentation/arzoom` were removed during P3R. |
| Camera parity | Per-source recorder path uses upstream `PresenterAwareSmartCamera` with scene context disabled, Smart follow, Cinematic motion, 28% safe zone and anchor (0.50, 0.45), matching accepted OBS per-source behavior. |
| Cadence parity | Latest source frame is retained; camera/presentation is re-composited for every output CFR slot rather than only when WGC publishes a new frame. |
| Click projection | Content anchoring remains on the same ArZoom camera transform. After direct visual validation on 2026-10-02, click lifetime/appearance intentionally became an Arssyut product skin (bounded local slots + single analytic ring); this no longer claims visual parity with `arzoom.effect`. |
| Parity test | `tests/test_arzoom_parity.cpp` compares adapter output frame-for-frame against the pinned upstream engine and checks Cinematic selection/frame-rate stability. Windows compositor tests prove camera transforms advance while the same source texture is retained. |

## Implemented transplant — P5A ArVisual standalone grade

| Field | Value |
|---|---|
| Local files | `src/visual/arvisual_grade.hpp`, ArVisual section inside `src/platform/windows/graphics/d3d11_compositor.cpp` |
| Upstream repository | `masarray/arvisual-obs` |
| Upstream commit | `d0a3f405447446e88dc56f4a50535a17257fcccf` |
| Upstream files | `data/effects/arvisual.effect`, defaults/behavior reference from `src/arvisual-filter.cpp` |
| License | GPL-2.0-or-later |
| Adaptation | OBS effect uniforms are mapped to Arssyut's retained D3D11 constant buffer; OBS filter/property/texrender/stagesurface and scene-readback plumbing are excluded. The shader runs on the retained desktop sample before Arssyut presentation overlays. Adaptive inputs are neutral until P5B. |
| Pixel-faithful path | `enabled=false` bypasses the grade and remains the recorder default. |
| Parity/safety test | Windows D3D11 tests cover real colorful grading, neutral balance, highlight/gamut safety, pixel-identical bypass, and zero resource-generation growth when toggling the grade. |

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
