# Arssyut

Arssyut is a Windows-first, lightweight screen recorder designed for polished tutorials, software demonstrations, engineering walkthroughs, and product presentations.

The product direction is intentionally narrower than OBS: one-click recording with a production-grade native capture pipeline, smooth camera motion, click feedback, shortcut visualization, tasteful color enhancement, synchronized audio, and an optimized MP4 result.

## Product principles

- **Lightweight by architecture** — native capture/compositing/encoding path; no CPU full-frame processing in steady state.
- **One camera authority** — ArZoom camera/planner concepts are transplanted as a single deterministic zoom/follow engine, not duplicated across UI and renderer.
- **GPU-first presentation** — zoom, click visuals, cursor, keycap overlay, and color treatment remain on the GPU path.
- **Bounded realtime work** — fixed-capacity queues, latest-wins coalescing for replaceable state, no session-length growth, no unbounded event history.
- **Smooth before flashy** — time-based motion, jerk-limited transitions, stable frame pacing, no zoom jitter.
- **Privacy-aware input visualization** — shortcut/chord visualization by default; ordinary typed text is not persisted.
- **Crash-safe recording** — temporary recoverable recording container, then lossless/remux-only finalization to optimized MP4.
- **Evidence-based optimization** — performance changes require measurements and regression protection.

## Upstream engines

Arssyut will reuse the proven behavior of:

- [ArZoom for OBS](https://github.com/masarray/arzoom-follow-obs) for camera planning, kinematic motion, click feedback, cursor mapping, and presentation semantics.
- [ArVisual for OBS](https://github.com/masarray/arvisual-obs) for bounded color enhancement, neutral/highlight protection, scene adaptation, and GPU grading behavior.

The OBS glue is **not** copied blindly. Portable algorithms are separated from OBS-specific state and adapted to the standalone D3D11 recording pipeline with explicit provenance and parity tests.

## Planned architecture

```text
Windows Graphics Capture / DXGI fallback
              |
              v
       Latest-frame slot
              |
Input -> canonical event reducer -> Camera/Overlay state
              |
              v
      D3D11 compositor
      |  zoom / pan
      |  click visual
      |  keycap overlay
      |  ArVisual grade
      v
       RGB -> NV12
              |
              v
     hardware H.264 encoder
              |
Audio --------+--------> bounded mux writer
                       |
                       v
              recoverable temp media
                       |
                       v
                optimized MP4
```

## Foundation documents

- `AGENTS.md` — non-naive production realtime engineering contract.
- `docs/CONCEPT.md` — product/UX concept and presentation behavior.
- `docs/PRD.md` — product requirements and acceptance criteria.
- `docs/ARCHITECTURE.md` — capture/render/input/audio/encoding architecture and invariants.
- `docs/ROADMAP.md` — gated implementation strategy and validation milestones.
- `docs/RESEARCH.md` — ArZoom/ArVisual audit plus Microsoft/GitHub/GitLab research.

## Build the P0 foundation

Windows x64:

```powershell
cmake --preset windows-x64
cmake --build --preset windows-release --parallel
ctest --preset windows-release
```

P0 builds the production core primitives and deterministic tests. It does not
include a temporary screenshot recorder.

## P0 foundation

Implemented on the production path:

- canonical 100-ns monotonic clock backed by QPC on Windows;
- compact `Status/Result` failure contracts;
- fixed-capacity SPSC event ring;
- lock-free latest-wins primitive for small replaceable state;
- bounded diagnostic counters;
- canonical recording session state machine;
- RAII D3D11 device/immediate-context ownership;
- deterministic core and D3D11 WARP CI tests;
- source provenance and third-party dependency ledgers;
- Windows x64 GitHub CI.

See `docs/P0_FOUNDATION.md` for ownership and acceptance gates.

## Status

**P0 accepted. P1 implementation is in validation.**

P1 adds the real Windows Graphics Capture path, a fixed three-slot latest-frame
handoff, canonical 30/60 FPS scheduling, GPU-only crop/scale compositing,
non-blocking CPU/GPU latency instrumentation, and the recoverable session
manifest boundary.

CI also publishes `arssyut_p1_probe.exe` for direct desktop validation. See
`docs/P1_NATIVE_CAPTURE.md`.

P2 (audio/encoder/MP4) begins only after the P1 capture path is accepted on a
real interactive Windows desktop.
