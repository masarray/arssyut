# P5D — Screen Text Fidelity

## Problem

Direct P5C recordings showed that small desktop text can look perceptually
thinner in the encoded MP4/player than it does on the live Windows desktop.

The full path matters:

```text
Windows application text rasterization
        |
        v
WGC BGRA desktop source
        |
        v
Arssyut crop / ArZoom sampling
        |
        v
ArVisual product mode
        |
        v
BGRA -> NV12 4:2:0
        |
        v
H.264
        |
        v
player/browser rescale
```

Small anti-aliased strokes may contain only one or two high-value pixels. Any
resampling, chroma subsampling and final player downscale can make those strokes
read lighter or thinner even when the original font itself has not changed.

P5D improves **perceived stroke survival**. It does not try to detect text
semantically and does not morphologically bold arbitrary dark shapes.

## External design references

P5D is an original Arssyut implementation. No source code is copied from these
references.

- Microsoft ClearType documentation explains that ClearType uses RGB
  sub-pixels to increase horizontal text resolution.
- Microsoft DirectWrite/DirectComposition documentation describes sub-pixel
  text rendering and the cases where intermediate/scaled surfaces can fall
  back toward grayscale behavior.
- AMD FidelityFX Contrast Adaptive Sharpening documents the useful general
  principle that adaptive sharpening should sharpen already-sharp regions less
  and recover softer detail more selectively.

The local implementation uses those principles only as design guidance.

## P5D.1 — Text-fidelity gates

Windows/WARP tests use deterministic high-contrast neutral edge fixtures.

They prove:

- Pixel Accurate remains pixel-identical at 1:1;
- turning text legibility on increases neutral edge luma contrast;
- flat neutral regions do not move materially;
- the same bounded enhancement still works when a 2x larger source is
  minified to the output size;
- changing text-legibility strength allocates no compositor resources.

These synthetic gates are intentionally simpler than real fonts. They protect
the mathematical behavior while direct recordings remain the visual authority.

## P5D.2 — Luma-only screen-content reinforcement

P5D reuses the symmetric neighborhood already sampled by the P5A clarity path.
No additional blur texture, render pass, staging surface or CPU readback is
created.

The existing source-detail signal is classified with:

- micro-edge magnitude;
- source saturation;
- skin exclusion;
- available luma headroom;
- effective source-to-output scale.

The adjustment changes target **luminance only** through the existing
luminance-preserving gamut path.

Consequences:

- dark text on a light UI can become slightly darker;
- light text on a dark IDE can become slightly brighter;
- neutral gray/black/white text receives the strongest support;
- saturated syntax/icon edges receive a deliberately reduced dose;
- skin receives almost no dose;
- flat regions receive no dose;
- the maximum local luma delta is tightly bounded.

This is not a one-pixel dilation, morphological bold, or global unsharp mask.

## Scale resilience

The shader derives the effective number of source pixels represented by one
output pixel from:

- crop extent;
- source texel size;
- output dimensions;
- active ArZoom camera zoom.

When the desktop is being minified, for example a 4K source recorded to 1080p,
text-legibility strength receives only a small bounded scale-resilience boost.

Zooming in does not receive that boost.

P5D does not replace the stable D3D11 linear sampler with nearest-neighbor
sampling. Pixelated/shimmering camera motion would be a worse regression.

## Product-mode mapping

| Mode | Text legibility |
|---|---:|
| Pixel Accurate | 0.00 |
| Clean Screen | 0.56 |
| Vivid Presentation | 0.34 |

Pixel Accurate remains the truth/reference path and must never receive screen
text enhancement.

Clean Screen is intentionally strongest because browser/document/IDE
legibility is its primary product purpose.

Vivid Presentation keeps a lower dose so its presentation/color character does
not turn into visibly sharpened UI.

## P5D.3 — Encoder screen-quality policy

The production media path remains:

```text
BGRA -> D3D11 Video Processor -> NV12 -> H.264
```

NV12 4:2:0 remains the compatibility-first default.

P5D.6 keeps High Profile but corrects the rate-control policy after real
recordings showed that quality-VBR could collapse almost-static desktop
captures far below the requested 18 Mbps budget.

Microsoft documents two important semantics:

- quality-VBR uses `AVEncCommonQuality` and does not use the mean bitrate as
  the controlling target;
- unconstrained VBR attempts to achieve the target bitrate carried by
  `MF_MT_AVG_BITRATE` / `AVEncCommonMeanBitRate`.

The production policy is therefore:

- prefer H.264 High Profile;
- fall back to Main if the active encoder rejects High during stream
  negotiation;
- prefer **unconstrained bitrate-controlled VBR**;
- set both `MF_MT_AVG_BITRATE` and `AVEncCommonMeanBitRate` to the requested
  recording budget;
- request `AVEncCommonQualityVsSpeed=85` as a high-complexity preference;
- if an MFT rejects QualityVsSpeed, retry VBR + mean bitrate without it;
- if explicit VBR attributes are rejected entirely, retry normal Sink Writer
  negotiation rather than failing recorder startup;
- 1080p60 budget remains 18 Mbps and 1080p30 remains 12 Mbps;
- diagnostics record negotiated profile, rate-control mode, bitrate-VBR
  acceptance, QualityVsSpeed acceptance/value, NV12, and 4:2:0.

Rate-control preferences are never allowed to turn into a recording startup
failure.

## Why 4:4:4 is not the default

4:4:4 can preserve colored one-pixel desktop detail better, but it changes the
hardware encoder/decoder compatibility envelope and does not fit the current
stable NV12 Media Foundation path.

A future archival/maximum-fidelity mode may evaluate 4:4:4 separately.

P5D must not silently replace the default production format with a
less-compatible format.

## Realtime/resource contract

P5D may not add:

- a second full-frame sharpening pass;
- per-frame textures;
- blur pyramids;
- synchronous GPU readback;
- CPU text recognition;
- OCR;
- glyph detection;
- frame history proportional to recording length.

Text enhancement uses existing P5A neighborhood samples and one existing
constant-buffer slot.

## Real visual validation

Use the same desktop scene in all three modes.

Recommended scene:

1. white browser at 100% zoom with 9-14 px UI/body text;
2. dark IDE with gray and colored syntax;
3. file explorer/settings UI with thin labels;
4. 4K monitor -> 1080p recording if available;
5. Smart Zoom in/out on text-heavy content;
6. playback at:
   - 100% / 1:1;
   - embedded/player fit-to-window;
   - fullscreen.

Compare:

- vertical stems such as `i l I H M`;
- small counters in `e a 8 0`;
- gray secondary text;
- colored syntax;
- white text on dark background;
- black text on white background.

Reject if:

- halos become visible;
- glyph holes/counters close;
- thin icons acquire dark outlines;
- photos/skin look sharpened;
- colored syntax develops excessive chroma fringe;
- UI shimmers during camera motion.

Accept only if text is easier to read after normal player scaling while the
desktop still looks natural.

## P5D.5 — bright-background text calibration

The first complete P5D triad was reviewed on 2026-10-03 using matched Pixel
Accurate, Clean Screen and Vivid Presentation recordings.

Diagnostics confirmed that the intended production path was active:

- H.264 High Profile;
- quality-VBR accepted at quality 86;
- NV12 4:2:0;
- Pixel Accurate text legibility 0.00;
- Clean Screen text legibility 0.56;
- Vivid Presentation text legibility 0.34;
- no encoder backpressure or presentation-input drops in the matched Clean/Vivid
  samples.

Frame-matched review showed an important asymmetry:

- dark-UI / light-text regions gained a clear and natural readability benefit;
- black/gray text on white browser UI improved at native 1080p, but much of
  that benefit disappeared when the video was fit-to-player/downscaled.

The correct response is not to raise global text-legibility strength, because
dark UI is already at the desired sharpness.

P5D.5 therefore adds a directional classifier inside the existing P5D shader.

Additional reinforcement is allowed only when local detail is negative, the
immediate neighborhood is bright and neutral, and the existing P5D edge and
neutral-text gates are active.

For that topology only, text gain can rise smoothly from 1.00x to at most
1.45x and the negative luma cap may widen from -0.014 to at most -0.018. The
cap, not the multiplier alone, remains the final safety authority.

Positive-detail text, including white/light text on dark IDE backgrounds,
continues to use the original P5D path with no P5D.5 multiplier.

P5D.5 still changes luma only, uses the existing neighborhood samples, adds no
render pass or resources, and leaves Pixel Accurate unchanged.

Windows/WARP regression fixtures explicitly compare neutral dark-on-bright and
light-on-dark strokes. Both must retain reinforcement while dark-on-bright gets
the larger calibrated dose.

## P5D.6 — low-contrast UI structure preservation

Direct comparison against the original browser/GitHub/Linear UI showed a
second fidelity class that text sharpening must not own: subtle neutral 1px
card borders and separators.

Typical examples are a light-gray line on a near-white card or a slightly
lighter separator inside a dark neutral panel. These structures can be visually
important while carrying much less contrast than text strokes.

P5D.6 adds a separate `ui_structure` product-mode control:

| Mode | UI structure |
|---|---:|
| Pixel Accurate | 0.00 |
| Clean Screen | 0.72 |
| Vivid Presentation | 0.38 |

The shader reuses the same retained source neighborhood already sampled by
P5D. No extra pass or texture fetch group is added.

The classifier requires:

- shallow local detail only;
- source and neighborhood both neutral/low-chroma;
- a bright context for a darker line, or a dark context for a lighter line;
- strong-edge exclusion so text/icon edges do not receive a second sharpen
  path;
- skin exclusion.

The preservation is applied **after final tone shaping** so earlier contrast,
clean-white or highlight logic cannot flatten the separator again.

Hard local luma limits remain deliberately tiny:

- negative structure delta >= -0.010;
- positive structure delta <= +0.007.

This preserves visibility without turning subtle cards into outlined boxes.

Windows/WARP gates cover:

- gray 1px border on bright neutral UI;
- gray 1px separator on dark neutral UI;
- unchanged flat background;
- strong-edge exclusion;
- zero compositor resource-generation growth.
