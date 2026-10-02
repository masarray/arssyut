# P4R.2 — Cursor & Click Motion Polish

## Purpose

P4R.2 turns pointer feedback into a first-class presentation layer while
preserving the P3R ArZoom camera contract and the P4R.1 keyboard/input fixes.

The milestone has two visible goals:

1. keep the click as one clean ring but give it an emissive, expanding energy
   falloff that remains legible on bright and dark surfaces;
2. make the Windows cursor respond to a click with a short ballistic scale
   impact without moving its true hotspot away from the pixel being indicated.

## Pipeline

When presentation features are active:

```text
Windows Graphics Capture (native cursor OFF)
        |
        v
retained desktop texture
        |
        v
ArZoom camera projection
        |
        +--> analytic single-ring click + emissive falloff
        |
        +--> retained Windows cursor texture
                 |
                 +--> projected by the same camera
                 +--> scale anchored at Windows hotspot
        |
        +--> retained keyboard keycap overlay
        |
        v
Media Foundation encoder
```

When all presentation features are disabled, WGC keeps its normal native cursor
capture behavior and the custom presentation cursor is not used.

## Cursor observation and cache

The input worker samples:

- screen position;
- `HCURSOR` identity;
- cursor visibility;
- last pointer activity.

The compositor owns a fixed eight-slot cursor-shape cache. All eight GPU
textures/SRVs, the GDI DIB, and the CPU scratch buffer are created once.
A shape is rasterized only when an `HCURSOR` is not already present in the
cache. Repeated frames and repeated use of the same shape only bind retained
resources.

The cache canvas is 256 x 256 BGRA. Shape width, height and Windows hotspot are
stored independently from the canvas dimensions.

## Hotspot truthfulness

The cursor position carried by `PresentationFrameState` is the Windows hotspot
position in content coordinates.

For each output frame:

1. project the content hotspot through the same ArZoom center/zoom transform;
2. convert the projected hotspot to output pixels;
3. scale cursor dimensions and hotspot by the same presentation scale;
4. derive top-left from `projected_hotspot - scaled_hotspot`.

Therefore ballistic scaling changes the apparent cursor mass while the pointing
pixel itself remains stationary.

Do not replace this with center-based bitmap scaling or position-follow lag.

## Ballistic click response

A click starts one bounded 230 ms scale envelope:

| Time | Scale |
| ---: | ---: |
| 0 ms | 1.00 |
| 40 ms | 1.17 |
| 105 ms | 0.95 |
| 180 ms | 1.025 |
| 230 ms | 1.00 |

Segments use minimum-jerk interpolation so there are no hard velocity
discontinuities at the control points.

Pointer velocity contributes a separate subtle scale response. It is capped at
approximately +5% and is reduced during the click-impact envelope so impact and
motion energy do not stack into an exaggerated animation.

No positional easing is applied to the cursor.

## Click presentation skin

P4R.2 retains exactly one analytic ring and no center fill.

At 1080-class output the intended geometry is approximately:

- initial radius: 11 px;
- final radius: 62 px;
- core half-width: 2.25 -> 1.0 px;
- outer emissive spread: 5 -> 18 px;
- left-click lifetime: 0.70 s;
- right-click lifetime: 0.72 s;
- middle-click lifetime: 0.68 s.

The existing P4R.1 palette is retained:

- left: electric sky `#32B8FF`;
- right: vivid rose `#FF5C8A`;
- middle: warm amber `#FFC857`.

Glow is derived from distance to the same ring. It must not visually resolve
into a second circle.

## Performance contract

P4R.2 does not permit:

- per-output-frame GDI cursor rasterization;
- per-frame cursor texture/SRV creation;
- GPU output readback;
- unbounded cursor history;
- cursor trails;
- a second presentation clock.

Desktop, camera, click, cursor and keyboard remain recomposited at canonical CFR
output cadence even when WGC reuses the retained desktop source.

## Automated gates

Windows tests cover:

- ballistic punch at ~40 ms;
- recoil at ~105 ms;
- bounded click lifetime;
- custom system cursor rendering;
- retained cursor-cache reuse without compositor resource-generation growth;
- existing retained-source camera cadence;
- existing single-ring/no-center-dot behavior;
- existing keyboard/keycap rendering.

## Direct validation checklist

After CI is green, record a real 1920 x 1080 / 60 FPS sample and verify:

1. no duplicate native + custom cursor;
2. Arrow, Hand, I-Beam and Resize shapes change correctly;
3. cursor hotspot remains on the actual click target while scaling;
4. the 1.17 punch is noticeable but not cartoon-like;
5. recoil is perceptible without making the cursor look unstable;
6. fast mouse movement adds only subtle energy and no positional lag;
7. left/right/middle rings still read as one ring;
8. glow is visible on both white browser surfaces and dark UI;
9. Win+R and white keycaps remain unchanged from P4R.1;
10. diagnostics remain within the established 1080p60 budget.

Real-recording visual acceptance remains a direct-validation gate; unit/CI
success alone does not claim aesthetic completion.
