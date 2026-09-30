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

## Status

**Foundation / Phase 0.** Architecture and contracts are established before implementation so the codebase does not begin as a prototype that must later be rewritten.
