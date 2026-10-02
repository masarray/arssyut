# P5A — ArVisual Standalone Grade

## Goal

P5A ports only the portable grading behavior from the pinned ArVisual OBS
implementation into Arssyut's native D3D11 compositor.

Upstream authority:

- repository: `masarray/arvisual-obs`
- commit: `d0a3f405447446e88dc56f4a50535a17257fcccf`
- shader: `data/effects/arvisual.effect`
- behavior/default reference: `src/arvisual-filter.cpp`
- license: GPL-2.0-or-later

OBS source/filter ownership, property callbacks, texrender/stagesurface objects,
and scene-readback code are intentionally not transplanted.

## Pipeline placement

P5A is composed in the existing single D3D11 pixel pass:

```text
retained WGC desktop texture
        |
        v
ArZoom camera sampling
        |
        v
ArVisual standalone grade   <-- P5A
        |
        +--> click glow
        +--> keyboard keycaps
        |
        v
encoder
```

This ordering is deliberate. Desktop content can be enhanced, while click and
keycap colors remain authored presentation colors rather than being re-graded.
After P4R.3, the native Windows cursor is part of the captured WGC source and
therefore follows the same camera/color path as desktop content.

## Default behavior

`ArVisualGradeSettings::enabled` defaults to `false`.

Therefore the normal recorder remains Pixel Accurate until the user explicitly
enables the validation toggle. The disabled path returns the source sample
without applying ArVisual math.

P5C will own final product-mode mapping. The P5A toggle exists only so the
transplanted engine can be directly validated before product modes are defined.

## Portable behavior retained

The shader adaptation preserves the pinned v0.5.9 behavior for:

- neutral/white detection and cast cleanup;
- bounded midtone and upper-midtone shaping;
- diffuse-white and highlight protection;
- luminance-preserving gamut fitting;
- headroom-based vibrance instead of unbounded saturation;
- red/orange/yellow/green/cyan/blue/magenta object-zone separation;
- skin hue/luma/saturation classification;
- bounded skin smoothing, lift and healthy-tone movement;
- symmetric neighborhood sampling for clarity;
- luma-only anti-halo clarity;
- object depth/gloss shaping;
- final shoulder and gamut safety.

The original default creative controls are retained:

- master 1.00;
- enhance 0.78;
- color pop 0.86;
- clean white 0.72;
- clarity 0.68;
- skin protect 1.00;
- skin beauty 0.72;
- healthy tone 0.62;
- toy gloss 0.48;
- depth pop 0.76;
- highlight guard 0.94;
- performance 1.00.

## P5A / P5B boundary

The upstream OBS plugin already separates shader parameters from scene-derived
adaptive inputs. P5A uses that boundary directly.

Until P5B exists, adaptive inputs are deterministic neutral values:

- smart exposure 0.0;
- smart pop 1.0;
- smart highlight 0.0;
- smart shadow 0.0;
- smart strength 1.0;
- smart chroma limit 0.985;
- smart clean 0.0;
- smart separation 0.0.

P5A performs no GPU-to-CPU readback.

P5B will be allowed to update these inputs only through a low-cadence,
asynchronous/downsampled analysis path. The 60-fps render path must never wait
for scene statistics.

## Resource contract

Grade ON/OFF and tuning are constant-buffer changes only.

P5A creates no new per-frame:

- texture;
- render target;
- staging surface;
- shader resource view;
- CPU image buffer.

The grade shares the already-retained source texture and output render target.

## Automated gates

Windows compositor tests verify:

1. colorful midtones receive a real grade when enabled;
2. neutral gray remains chromatically neutral;
3. highlight/gamut protection does not manufacture digital clipping;
4. disabled mode returns pixel-identical output;
5. grade ON/OFF does not increase compositor resource generation.

Existing P3R/P4R retained-source, click and keycap tests remain active; native
cursor stability is validated through real WGC recordings.

## Direct validation

After CI acceptance, record matched 1080p60 samples with ArVisual OFF and ON.

Recommended scenes:

1. white browser/document;
2. dark IDE;
3. colorful product/web page;
4. skin/webcam content if available;
5. saturated animation/game;
6. bright colored highlights.

For P5A, evaluate static grading quality and safety only. Scene adaptation and
pumping/flicker behavior are P5B acceptance concerns.
