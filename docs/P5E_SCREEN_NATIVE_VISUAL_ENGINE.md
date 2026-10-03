# P5E — Screen-Native Visual Engine

## Goal

P5E evolves the existing P5A/P5B/P5C engine from camera-oriented tone
adaptation into screen-native behavior without creating a second shader or
parallel color engine.

The guiding rule is:

> preserve authored UI luminance first, enhance second.

Pixel Accurate remains the exact reference path. Clean Screen becomes the
strongest screen-native mode. Vivid Presentation remains screen-native too, but
keeps more creative color/depth latitude.

## Why P5E exists

Direct validation after P5D.7 confirmed that the SDR color-range pipeline was
finally correct and gray UI hierarchy survived the encoder. The remaining
mismatch was behavioral:

- mixed white browser chrome + dark application content could still produce
  simultaneous camera-style highlight and shadow pressure;
- Smart Auto could interpret authored dark/light UI as exposure error;
- flat neutral surfaces could be reshaped by tone/contrast logic even though
  their gray level was intentionally chosen by the UI designer.

Those are grading-policy problems, not encoder problems.

## One engine, two policies

P5E does not fork ArVisual.

The same analyzer, same retained compositor shader, same constant buffer, and
same P5A creative controls remain in use.

A new bounded product-mode prior selects how adaptive evidence is interpreted:

- camera-style evidence can still describe exposure/highlight/shadow pressure;
- screen-native modes transform that evidence into a safety/risk policy;
- hot-vivid and saturation risk remain authoritative;
- global exposure normalization is strongly suppressed for screen content.

## Screen-topology evidence

The existing fixed 64x36 P5B analysis frame now performs a second tiny pass
over the same 2304 pixels.

No heap allocation, OCR, window detection, app detection or GPU readback is
added.

New scene statistics:

- flat fraction;
- neutral-flat fraction;
- bright-neutral-flat fraction;
- dark-neutral-flat fraction;
- local edge fraction.

A pixel contributes to the flat topology only when its local right/down luma
difference remains within a tight threshold.

The classifier then exposes:

- `screen_ui` — broad screen-content confidence;
- `mixed_ui` — simultaneous bright + dark authored neutral surfaces;
- `color_risk` — saturation/vivid safety pressure;
- `hot_risk` — hot-vivid highlight safety pressure.

The original histogram statistics and 0.65 s EMA remain active.

## Product-mode mapping

### Pixel Accurate

- screen-native prior: 0.00;
- neutral-surface anchor: 0.00;
- grade disabled;
- analysis disabled.

### Clean Screen

- screen-native prior: 1.00;
- neutral-surface anchor: 0.94;
- strongest preservation of authored neutral surfaces;
- Smart Auto acts primarily as a risk limiter.

### Vivid Presentation

- screen-native prior: 0.72;
- neutral-surface anchor: 0.62;
- retains more creative color/depth latitude;
- still suppresses camera-style global exposure behavior relative to the old
  engine.

## Smart Auto screen policy

The analyzer still computes the previous camera-oriented pressure values. This
is intentional: measurement and product policy stay separate.

`apply_adaptive()` blends those measurements toward screen semantics according
to the product mode and screen-topology confidence.

For screen modes:

- exposure pressure trends strongly toward zero;
- neutral/global highlight pressure is strongly reduced;
- shadow normalization is strongly reduced;
- muted-scene positive pop preload is removed;
- creative strength/chroma are restored toward neutral when the scene is
  low-risk UI;
- hot-vivid and true color-risk still reduce pop/strength/chroma and retain
  highlight protection.

Clean/Vivid are screen-recorder modes, so topology confidence strengthens a
baseline screen prior rather than acting as the only on/off switch.

This prevents colorful webpages from falling back to full camera exposure
normalization merely because they contain less neutral gray.

## Neutral-surface luma anchor

P5E adds a late-stage luma anchor inside the existing compositor shader.

The anchor operates only when the source region is:

- locally flat;
- low-chroma / neutral;
- inside the safe SDR luma band;
- not skin.

The source luma remains the authority.

For high-confidence Clean Screen neutral surfaces, the effective allowed
deviation is only a few code values. Vivid Presentation receives a looser
budget.

The anchor runs after creative tone shaping but before P5D.6 separator
preservation.

That ordering is deliberate:

1. creative/color pipeline may operate normally;
2. flat neutral authored surface luma is pulled back within budget;
3. P5D.6 restores shallow 1px border/separator contrast;
4. text remains owned by P5D/P5D.5, not by the flat-surface anchor.

## Constant-buffer/resource contract

P5E reuses the unused components of the existing `arvisual6` float4:

- x = UI structure;
- y = screen-native prior;
- z = neutral-surface anchor;
- w = applied screen-policy weight.

The constant-buffer size does not grow.

P5E adds no:

- shader variant;
- render pass;
- texture;
- staging resource;
- CPU/GPU readback;
- per-frame allocation.

## Diagnostics

P5E records both raw scene evidence and the values actually applied to the
shader.

New scene fields include:

- `visual_scene_flat_frac`;
- `visual_scene_neutral_flat_frac`;
- `visual_scene_bright_neutral_flat_frac`;
- `visual_scene_dark_neutral_flat_frac`;
- `visual_scene_edge_frac`.

New adaptive evidence:

- `visual_adaptive_screen_ui`;
- `visual_adaptive_mixed_ui`;
- `visual_adaptive_color_risk`;
- `visual_adaptive_hot_risk`.

New applied-state fields:

- `visual_applied_smart_exposure`;
- `visual_applied_smart_pop`;
- `visual_applied_smart_highlight`;
- `visual_applied_smart_shadow`;
- `visual_applied_smart_strength`;
- `visual_applied_smart_chroma_limit`;
- `visual_applied_screen_ui`.

This distinction matters because raw camera pressure may remain high while the
screen policy deliberately refuses to normalize authored UI.

## Automated gates

Portable tests cover:

- mixed bright/dark neutral UI topology;
- high screen/mixed-UI confidence;
- raw camera-style pressure remains observable;
- Clean Screen suppresses global exposure/highlight/shadow normalization;
- low-risk UI returns strength/chroma close to neutral;
- positive muted-pop preload is absent;
- hot-vivid risk remains authoritative;
- Pixel Accurate remains outside screen-native behavior.

Windows/WARP tests cover:

- a neutral gray ladder through the real compositor;
- Clean Screen anchored output stays within a tight source-luma budget;
- anchor never moves a flat neutral surface farther from source;
- saturated color is excluded from neutral-surface anchoring;
- no compositor resource-generation growth.

## Real validation

Use the accepted P5D.7 BT.709/range pipeline.

Record Clean Screen and Vivid Presentation over the same sequence:

1. white ChatGPT/GitHub;
2. gray cards/sidebars;
3. dark Linear/IDE;
4. mixed white browser chrome + dark application;
5. colorful webpage/product content;
6. saturated animation/media.

Acceptance priorities:

- gray surfaces retain authored hierarchy;
- dark surfaces retain authored hierarchy;
- no visible exposure pumping between light/dark UI;
- white browser chrome does not dim merely because p98 is high;
- dark app content does not lift merely because shadow fraction is high;
- saturated media still receives risk limiting;
- P5D text/border improvements remain intact;
- no new halo, tint or resource/performance regression.

## Accepted production baseline — 2026-10-03

P5E completed real visual acceptance with separate Clean Screen and Vivid
Presentation 1080p60 recordings after the P5D.7 color-pipeline correction.

Locked product-mode preservation values:

| Mode | text_legibility | ui_structure | screen_native | neutral_surface_anchor |
|---|---:|---:|---:|---:|
| Pixel Accurate | 0.00 | 0.00 | 0.00 | 0.00 |
| Clean Screen | 0.56 | 0.72 | 1.00 | 0.94 |
| Vivid Presentation | 0.34 | 0.38 | 0.72 | 0.62 |

Accepted invariants:
- Pixel Accurate stays the reference bypass;
- Clean Screen preserves authored neutral luminance first;
- Vivid Presentation keeps more creative latitude but remains screen-native;
- raw camera-style exposure/highlight/shadow measurements remain observable
  diagnostics rather than mandatory whole-screen normalization;
- saturated-content color/hot risk stays authoritative;
- neutral-surface anchor ordering remains after creative tone shaping and before
  P5D.6 UI-structure preservation;
- P5D.7 BT.709/range and P5D.6 bitrate-controlled VBR contracts are part of the
  accepted visual baseline.

The measured acceptance evidence is recorded in
docs/P5E_REAL_VISUAL_ACCEPTANCE.md.

Any future change to these values or semantics requires a reproduced reason,
automated regression gates and another matched real-recording validation set.
