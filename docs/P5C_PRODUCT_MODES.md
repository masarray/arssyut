# P5C — ArVisual Product Modes

## Goal

P5C turns the P5A/P5B color engine into a small product-facing control surface
without creating a second grading implementation.

The recorder exposes exactly three modes:

1. Pixel Accurate
2. Clean Screen
3. Vivid Presentation

The selected mode is the public authority. At session start Arssyut rebuilds the
internal `ArVisualGradeSettings` from that mode, so stale UI state or caller
code cannot create undocumented hybrid presets.

## Pixel Accurate

Purpose:

- engineering capture;
- UI regression evidence;
- documentation where source colors must remain unchanged;
- troubleshooting where any creative processing would be misleading.

Behavior:

- `enabled = false`;
- `smart_auto = false`;
- no P5B scene-analysis work is scheduled;
- compositor output remains on the existing pixel-faithful path.

Pixel Accurate remains the default mode.

## Clean Screen

Purpose:

- tutorials;
- browser/document recording;
- IDE and engineering UI;
- product walkthroughs where readability and clean whites matter more than
  cinematic color.

P5C mapping:

| Parameter | Value |
|---|---:|
| master | 0.84 |
| enhance | 0.52 |
| color pop | 0.28 |
| clean white | 0.90 |
| clarity | 0.46 |
| skin protect | 1.00 |
| skin beauty | 0.18 |
| healthy tone | 0.12 |
| toy gloss | 0.08 |
| depth pop | 0.30 |
| highlight guard | 0.98 |
| performance | 1.00 |
| text legibility | 0.56 |
| UI structure | 0.72 |
| screen native | 1.00 |
| neutral surface anchor | 0.94 |
| Smart Auto | ON |

Design intent:

- stronger neutral/white cleanup than Vivid;
- stronger highlight safety;
- modest clarity;
- deliberately restrained saturation;
- very low beauty/gloss/depth effects;
- P5B remains active so risky highlights/saturation can still reduce the grade
  further.

Clean Screen must not become a second shader. It is only a conservative
parameter mapping into the existing P5A/P5B engine.

## Vivid Presentation

Purpose:

- polished product demos;
- colorful tutorial content;
- presentation-first capture where the source should feel more dimensional and
  lively while retaining Smart Auto safety.

P5C keeps the pinned P5A v0.5.9 defaults:

| Parameter | Value |
|---|---:|
| master | 1.00 |
| enhance | 0.78 |
| color pop | 0.86 |
| clean white | 0.72 |
| clarity | 0.68 |
| skin protect | 1.00 |
| skin beauty | 0.72 |
| healthy tone | 0.62 |
| toy gloss | 0.48 |
| depth pop | 0.76 |
| highlight guard | 0.94 |
| performance | 1.00 |
| text legibility | 0.34 |
| UI structure | 0.38 |
| screen native | 0.72 |
| neutral surface anchor | 0.62 |
| Smart Auto | ON |

P5E changes how P5B evidence is applied to screen modes. Camera-style global
exposure/shadow/neutral-highlight normalization is suppressed according to the
screen-native prior, while hot-vivid and saturation risk may still lower pop,
strength and chroma ceiling. Vivid therefore means a stronger creative
baseline, not an unconditional saturation boost.

## UI contract

The temporary P5A `ArVisual` checkbox is removed.

The idle recorder UI contains one dropdown:

- `Pixel Accurate`
- `Clean Screen`
- `Vivid Presentation`

The selector is disabled while recording, like the existing source/FPS and
presentation controls.

No advanced individual ArVisual sliders are exposed in P5C.

## Diagnostics

Each recording stores:

```json
"arvisual_mode": "pixel_accurate"
```

or:

```json
"arvisual_mode": "clean_screen"
```

or:

```json
"arvisual_mode": "vivid_presentation"
```

The existing `arvisual_enabled`, `arvisual_smart_auto` and P5B analysis
counters remain present for low-level diagnostics.

## Resource and realtime contract

Switching modes changes configuration/constant-buffer values only.

P5C must not add:

- new shader variants per mode;
- new render passes;
- textures or render targets per mode;
- staging/readback paths;
- per-frame heap allocation;
- alternate Smart Auto implementations.

Pixel Accurate must also avoid scheduling scene analysis because both grade and
Smart Auto are disabled.

## Automated gates

Portable tests verify:

- Pixel Accurate is a true grade + Smart Auto bypass;
- Clean and Vivid both use Smart Auto;
- Clean has stronger white/highlight safety than Vivid;
- Clean has lower creative dose than Vivid;
- Vivid retains the pinned P5A creative defaults;
- diagnostic mode names remain stable.

Windows/WARP compositor tests verify:

- Pixel Accurate output is pixel-identical;
- Clean Screen produces a real grade;
- Vivid Presentation produces a real grade;
- Clean and Vivid produce distinct output;
- changing modes does not increase compositor resource generation.

## Direct validation

P5C should be compared on the same 1080p60 source material.

Recommended matched scenes:

1. white browser/document;
2. dark IDE;
3. colorful web/product page;
4. desktop with saturated icons/wallpaper;
5. bright highlight regions;
6. skin/webcam material when available.

Expected visual hierarchy:

```text
Pixel Accurate
    = exact source

Clean Screen
    = source, but cleaner/safer/more legible

Vivid Presentation
    = stronger depth/color/presentation energy,
      still bounded by Smart Auto
```

Acceptance requires that Clean Screen never look like a washed-out Vivid mode,
and Vivid never destroy white/highlight/skin safety.
