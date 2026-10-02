# P3R — ArZoom Parity Recovery

Status: implementation branch `feat/p3r-arzoom-parity`

P3R exists because the first Arssyut presentation integration reused some
ArZoom source but did not preserve the same runtime behavior as the accepted
per-source OBS ArZoom path.

## Root causes found from the first real recording

1. Arssyut instantiated `SceneViewportPlanner` directly.
   Per-source OBS ArZoom uses `PresenterAwareSmartCamera` with scene context
   disabled, which delegates to the accepted legacy `SmartCamera` gimbal.
2. Arssyut hard-coded `Balanced` motion while the intended recorder profile
   is `Cinematic`.
3. Camera/presentation composition only ran when WGC supplied a new desktop
   frame. A 60-fps MP4 could therefore contain many duplicated already-rendered
   camera frames even though camera state had advanced.
4. Click state came from ArZoom, but the appearance shader was an Arssyut
   approximation rather than the pinned ArZoom shader choreography.

P3R corrects ownership instead of stacking smoothing constants on top.

## Upstream authority

Vendored source:

`masarray/arzoom-follow-obs@ada8f5269246c64429d7aceb6cc72f81e72120ba`

The portable headers under `third_party/arzoom/include` are unmodified.

Arssyut-specific mapping is isolated in:

`src/presentation/arzoom_camera_adapter.hpp`

The adapter configures:

- per-source camera mode: legacy SmartCamera through PresenterAwareSmartCamera;
- follow policy: Smart;
- motion style: Cinematic;
- safe zone: 0.28;
- anchor: (0.50, 0.45);
- configured zoom: product profile, currently 2.0x.

Product behavior such as click-triggered Auto Zoom is an **intent producer**.
It may request zoom/emphasis, but it does not own smoothing, pan, gimbal,
activation, coast, settle, or return motion.

## Output-cadence invariant

The video pipeline now treats source cadence and virtual-camera cadence as
different concepts.

Before:

```text
new WGC frame
 -> source copy
 -> camera composite
 -> output

no WGC frame
 -> reuse previous already-composited output
```

P3R:

```text
new WGC frame
 -> update retained source texture

every CFR output slot
 -> sample current presentation state
 -> render retained source through current ArZoom camera
 -> click/presentation pass
 -> encoder
```

This means a 60-fps recording can animate the virtual camera at 60 fps even
when the captured desktop changes less frequently.

No extra full-frame CPU copy is introduced. The retained source is a D3D11
texture and no per-output-frame texture is created.

## Click projection and product skin

P3R camera parity remains pinned to upstream ArZoom. Click positions still use
the same content coordinates and camera center/zoom projection, so click
feedback cannot drift away from the object being demonstrated.

After direct visual validation on 2026-10-02, the upstream dual-ring appearance
was intentionally replaced by the Arssyut product skin:

- one analytic ring;
- larger grow radius for presentation visibility;
- smooth ease-out expansion;
- longer bounded fade;
- adaptive 1080p/4K pixel scale;
- Left: electric sky blue;
- Right: vivid rose;
- Middle: warm amber;
- no center dot and no second ring.

P4R.3 direct validation on 2026-10-03 strengthens that same single-ring skin
with a thicker core, near bloom, diffuse halo, bright-surface chromatic support,
and a longer ~0.84-0.90 s fade tail. Rapid same-target clicks recharge one
pulse rather than stacking concentric geometry.

The click state remains fixed-capacity and session-length independent. This
appearance change does **not** alter ArZoom camera/gimbal parity.


## Automated gates

### Adapter parity

`tests/test_arzoom_parity.cpp`

For a deterministic pointer/zoom/emphasis trace, every camera output field from
the Arssyut adapter is compared against the pinned upstream per-source engine:

- center;
- zoom;
- velocity;
- acceleration;
- state;
- intent confidence;
- urgency.

The suite also guards that the product profile is Cinematic rather than
Balanced and exercises 30/60/120-fps stability.

### Upstream Smart Zone gate

`tests/upstream/arzoom_phase1_gate.cpp`

The pinned upstream Smart Zone/Smooth Idle gate is carried into Arssyut CI so
zoom trajectories, local explanation stability, coast-to-idle behavior,
relocation launch softness, retarget smoothness and frame-rate consistency
remain protected.

### Retained-source camera gate

Windows D3D11 tests update the source texture once, then render it twice with
different camera transforms. The second output must visibly change while the
compositor resource generation stays constant.

This specifically prevents regression back to "camera only moves when WGC
publishes a frame".

## P3R acceptance

Automated acceptance requires:

- Release build;
- all CTest suites;
- upstream parity tests;
- shader compilation through compositor initialization;
- retained-source camera regression;
- no existing Media Foundation/recorder regression;
- GUI launch smoke.

Direct acceptance additionally requires one real 1080p60 recording with:

1. click-trigger zoom-in;
2. local pointer explanation movement;
3. long relocation left/right;
4. direction reversal;
5. idle hold;
6. zoom-out/full-frame return;
7. left/right/middle click visual check.

The visual comparison target is the pinned per-source OBS ArZoom behavior, not
the superseded P3 Arssyut implementation.

P4R keycap/SVG/system-shortcut work begins only after P3R camera/click behavior
is accepted on the user's real recording.
