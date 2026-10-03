# P5E — Real Visual Acceptance

Date: 2026-10-03

Status: **PASS / LOCKED**

Baseline source:
- P5D.7 authoritative SDR color pipeline;
- P5E screen-native ArVisual implementation from PR #28;
- real 1920x1080 60 fps desktop recordings;
- H.264 High, bitrate-controlled unconstrained VBR, QualityVsSpeed 85;
- full-range RGB compositor input -> explicit BT.709 studio-range NV12/H.264.

## Clean Screen acceptance

Accepted preservation constants:
- text_legibility = 0.56;
- ui_structure = 0.72;
- screen_native = 1.00;
- neutral_surface_anchor = 0.94.

Final scene evidence:
- raw exposure = -0.025;
- raw highlight = 1.000;
- raw shadow = 0.601022;
- applied exposure = -0.00507052;
- applied highlight = 0.244777;
- applied shadow = 0.137030;
- applied strength = 0.974262;
- applied chroma limit = 0.980978;
- applied screen policy = 0.839136.

Realtime evidence:
- analysis submitted/completed = 81/81;
- analysis busy skips = 0;
- analysis map failures = 0;
- capture busy drops = 0;
- encoder backpressure = 0;
- compositor CPU p95 = 0.5 ms;
- compositor GPU p95 = 4.0 ms;
- resource_generation = 2.

Visual acceptance:
- white and near-white UI surfaces remained distinct;
- gray cards/sidebar hierarchy remained visible;
- dark Linear/IDE surfaces retained multiple authored levels;
- thin P5D borders and text remained natural;
- no material exposure pumping was visible across light/dark UI transitions;
- no halo/tint regression was accepted.

## Vivid Presentation acceptance

Accepted preservation constants:
- text_legibility = 0.34;
- ui_structure = 0.38;
- screen_native = 0.72;
- neutral_surface_anchor = 0.62.

Saturated-content scene evidence:
- mean saturation = 0.423191;
- p90 saturation = 0.875139;
- colored fraction = 0.498155;
- vivid fraction = 0.155180;
- hot-vivid fraction = 0.0264297;
- color risk = 0.696924;
- hot risk = 0.221156.

Raw/applied policy evidence:
- raw exposure = -0.025;
- raw highlight = 1.000;
- raw shadow = 0.404536;
- raw strength = 0.603046;
- applied exposure = -0.012688;
- applied highlight = 0.596247;
- applied shadow = 0.211602;
- applied strength = 0.667646;
- applied chroma limit = 0.928278;
- applied screen policy = 0.518400.

Realtime evidence:
- analysis submitted/completed = 46/46;
- analysis busy skips = 0;
- analysis map failures = 0;
- capture busy drops = 0;
- encoder backpressure = 0;
- compositor CPU p95 = 0.5 ms;
- compositor GPU p95 = 4.0 ms;
- resource_generation = 2.

Visual acceptance:
- neutral/white UI remained close to the Clean baseline rather than becoming a
  global stylizing filter;
- dark UI received bounded presentation depth without lifting blacks;
- saturated content triggered color-risk and hot-risk protection;
- no broad clipping/posterization regression was accepted;
- no material whole-screen exposure pumping was visible during
  dark-UI-to-colorful-content transitions.

## Locked conclusion

P5E is accepted as the production visual baseline.

P6 and later work must not opportunistically alter P5D.7 color range,
P5D/P5D.6 preservation values, P5E product-mode values, shader ordering,
screen-policy semantics, or encoder rate-control policy.

A future visual change is allowed only when backed by a reproduced regression or
explicit new product requirement, automated regression coverage, and a new
matched real-recording acceptance set.
