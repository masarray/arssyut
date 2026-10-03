# P5B — Asynchronous ArVisual Scene Analysis

## Goal

P5B adds scene-adaptive control to the P5A standalone ArVisual grade without
placing GPU readback latency on the 60-fps render path.

The statistical behavior is ported from the same immutable upstream baseline as
P5A:

- repository: `masarray/arvisual-obs`
- commit: `d0a3f405447446e88dc56f4a50535a17257fcccf`
- source behavior: `src/arvisual-filter.cpp`
- license: GPL-2.0-or-later

## Pipeline

```text
fresh retained WGC source
        |
        +--> normal CFR presentation/grade path --------------------+
        |                                                           |
        +--> at most 5 Hz                                           |
             64 x 36 linear downsample                              |
                  |                                                  |
                  v                                                  |
             staging slot 0 / 1                                     |
                  |                                                  |
             D3D11 EVENT query                                      |
                  |                                                  |
             async Flush1 submit (5 Hz; no wait)                     |
                  |                                                  |
             DONOTFLUSH poll on later output frame                   |
                  |                                                  |
             only when query says READY                             |
                  v                                                  |
             map 64 x 36 BGRA                                       |
                  |                                                  |
             fixed histograms + EMA                                 |
                  |                                                  |
             adaptive constants ------------------------------------+
```

The final grade still executes at canonical output cadence. Analysis is a
producer of bounded constants only.

## Nonblocking contract

P5B uses two retained staging textures and two retained `D3D11_QUERY_EVENT`
objects.

A staging surface is never mapped until its corresponding query reports ready
through `GetData(..., D3D11_ASYNC_GETDATA_DONOTFLUSH)`.

A headless recorder has no swap-chain `Present` call to guarantee that a
partially filled D3D11 command buffer is submitted promptly. At each analysis
submission, P5B therefore uses `ID3D11DeviceContext3::Flush1` when available
(`Flush` only as a legacy fallback). This call submits queued work
asynchronously; it does not wait for the GPU to finish.

If both slots are still pending when another low-cadence sample becomes due,
the sample is skipped. The recorder does not:

- spin until a query becomes ready;
- use a blocking completion wait;
- map a pending staging texture;
- allocate a third/unbounded staging surface;
- delay an output slot waiting for scene statistics.

The previous adaptive state remains valid until a new sample completes.

## Sampling cadence

Analysis submissions are capped at 5 Hz (200 ms).

Submission is attempted only when WGC delivers a fresh source frame. A static
desktop therefore does not repeatedly downsample the same retained image.

The analysis target is fixed at 64 x 36 BGRA (2304 pixels), matching the pinned
upstream sampling footprint.

## Scene statistics

Transparent pixels and near-black letterbox/background pixels are excluded from
exposure statistics.

P5B retains the upstream measurements:

- p10, median, p90 and p98 luma;
- mean saturation;
- p90 saturation;
- shadow fraction;
- near-clip fraction;
- vivid fraction;
- hot-vivid fraction;
- neutral fraction;
- colored fraction.

Statistics use fixed 256-bin luma and saturation histograms. No per-sample heap
allocation is required.

## Temporal behavior

The upstream 0.65-second exponential moving average remains the adaptation
authority.

Because Arssyut intentionally samples at a lower cadence than the OBS render
loop, EMA alpha is computed from the elapsed time between completed scene
samples rather than from output-frame count. This preserves time-domain behavior
without requiring 60 readbacks per second.

## Adaptive outputs

The model updates exactly the P5A adaptive inputs:

- smart exposure;
- smart pop;
- smart highlight pressure;
- smart shadow pressure;
- smart creative strength;
- smart chroma limit;
- smart neutral clean;
- smart hero-color separation.

The asymmetry of upstream Smart Auto remains intentional: dangerous/highly
saturated scenes can reduce the creative dose strongly, while muted scenes only
receive a small positive lift.

### P5C.2 screen-capture calibration layer

Matched real recordings on 2026-10-03 exposed one topology where literal
upstream adaptive mapping was not suitable for a desktop recorder: a
browser/document can be overwhelmingly neutral and near-white by design.

In the calibration triad, both Clean Screen and Vivid Presentation ended on a
scene with roughly 98.5% neutral pixels and almost no saturation, yet the
literal mapping produced `highlight=1`, `exposure=-0.025`, and a muted-scene
`pop=1.025`. The following colorful scene then inherited that state through
the 0.65 s EMA, producing visible brightness/chroma settling.

P5C.2 therefore adds one Arssyut-specific classifier:

- bright median luma;
- overwhelming neutral-pixel fraction;
- very low scene saturation.

Only when all three gates agree is the scene treated as bright neutral UI.

For that topology only:

- negative exposure is strongly attenuated;
- neutral luminance/near-clip pressure is strongly attenuated;
- hot-vivid pressure remains fully authoritative;
- muted-scene positive pop lift is suppressed;
- clean-white support remains active.

This does not change the sampled statistics, the 5 Hz async backend, the
0.65-second EMA, or hot-vivid/dark-scene protection. Diagnostics expose the
classifier confidence as `visual_adaptive_white_ui`.

## Failure behavior

Scene analysis is optional infrastructure.

If analyzer resources cannot be created, or a readback/map fails:

- recording continues;
- P5A grade remains active;
- neutral/default adaptive values or the last valid adaptive state are used;
- no black frame or recorder failure is caused solely by Smart Auto analysis.

## Telemetry

Recorder diagnostics expose:

- `arvisual_smart_auto`;
- `visual_analysis_available`;
- `visual_analysis_submitted`;
- `visual_analysis_completed`;
- `visual_analysis_busy_skips`;
- `visual_analysis_map_failures`;
- `visual_adaptive_white_ui` (P5C.2 neutral-white UI classifier evidence).

For a healthy dynamic recording, completed analysis should advance while
map-failure remains zero. Busy skips are safe backpressure, not a recorder
failure.

## Automated gates

Portable scene-model tests cover:

- neutral-dominant scene classification and clean-white response;
- bright neutral browser/document classification without false clipping
  pressure;
- hot-vivid highlight/color-risk reduction;
- dark-scene shadow pressure;
- time-based EMA behavior;
- transparent and near-black exclusion.

Windows/WARP integration tests cover:

- retained analyzer resource creation;
- exactly two pending staging slots;
- third due submission skips rather than waits;
- read-later completion after asynchronous headless command submission;
- zero staging map failures;
- adaptive grade differs materially from static P5A on a hot-vivid scene;
- no compositor resource-generation growth during analysis.

## Direct validation

Record ArVisual-enabled 1080p60 material that transitions between:

1. neutral white browser/document;
2. dark IDE;
3. muted product/demo scene;
4. saturated colorful page or animation;
5. bright vivid highlights.

Validate:

- no visible pumping or flicker;
- whites remain neutral;
- saturated/highlight scenes calm down rather than clip;
- dark scenes do not jump exposure aggressively;
- transitions settle smoothly rather than snapping;
- `visual_analysis_completed` increases;
- `visual_analysis_map_failures == 0`;
- compositor/encoder cadence remains within the existing performance budget.
