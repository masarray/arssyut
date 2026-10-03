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

P5D changes encoder policy rather than replacing the media architecture:

- prefer H.264 High Profile;
- fall back to Main if the active encoder rejects High during stream
  negotiation;
- prefer quality-based VBR with quality 86;
- if the active hardware MFT rejects quality parameters, retry the same input
  type with the previous/default encoder negotiation;
- 1080p60 fallback bitrate budget increases from 12 Mbps to 18 Mbps;
- 1080p30 fallback bitrate budget increases from 8 Mbps to 12 Mbps;
- diagnostics record requested bitrate, actual negotiated profile,
  quality-VBR acceptance, requested quality, NV12, and 4:2:0.

Quality preferences are never allowed to turn into a recording startup failure.

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
