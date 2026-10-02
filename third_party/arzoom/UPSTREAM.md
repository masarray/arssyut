# ArZoom vendored source

This directory contains an **unmodified** header-only snapshot from:

- Repository: `masarray/arzoom-follow-obs`
- Commit: `ada8f5269246c64429d7aceb6cc72f81e72120ba`
- License: GPL-2.0-or-later

Vendored files:

- `src/arzoom-math.hpp`
- `src/arzoom-smart-zone-camera.hpp`
- `src/arzoom-scene-motion-synthesizer.hpp`
- `src/arzoom-scene-viewport-planner.hpp`
- `src/arzoom-camera.hpp`
- `src/arzoom-click-visual.hpp`

The files are copied byte-for-byte into `third_party/arzoom/include`.
Arssyut-specific behavior belongs in `src/presentation/arzoom_camera_adapter.hpp`
and other adapter/integration files, never in the vendored source.

P3R parity contract:

1. Per-source recorder camera uses `PresenterAwareSmartCamera` with scene
   context disabled, matching the accepted per-source OBS ArZoom path.
2. Camera defaults are Smart follow, Cinematic motion, 28% safe zone,
   anchor (0.50, 0.45), and configured zoom 2.0x unless the product profile
   explicitly changes the zoom amount.
3. Arssyut product events only produce camera intent. They do not implement a
   second smoothing/panning engine.
4. The camera is sampled and composited at output cadence, independent of WGC
   source-frame cadence.
5. Camera adapter regression tests compare every output field against the
   pinned upstream engine for the same canonical input sequence.
