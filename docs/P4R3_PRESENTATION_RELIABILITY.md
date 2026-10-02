# P4R.3 — Presentation Reliability & Visual Polish

## Why this corrective milestone exists

Direct 1920x1080 / 60 FPS validation on 2026-10-03 exposed three presentation
problems that synthetic/unit validation did not:

1. the P4R.2 custom cursor could flicker while idle, became difficult to track
   during fast motion, and did not gain enough visual size during ArZoom;
2. single-letter action keycaps were text-measured and therefore narrower than
   their height, while the Windows key looked closer to a realistic 1u key;
3. the click circle was readable but did not have enough luminous mass,
   thickness, glow or fade-tail to feel premium.

P4R.3 treats the recording itself as the visual authority and simplifies the
cursor path instead of adding more cursor effects.

## P4R.3A — Native Cursor Restore

The Windows/WGC cursor is again the only rendered cursor authority.

Pipeline:

```text
Windows Graphics Capture (native cursor ON)
        |
        v
retained desktop texture, including native cursor
        |
        v
ArZoom camera sampling
        |
        +--> ArVisual grade (when enabled)
        +--> analytic click feedback
        +--> keycap overlay
        |
        v
encoder
```

Raw Input still tracks pointer position and activity, but only for:

- ArZoom intent/targeting;
- click event coordinates;
- motion-tail timing.

P4R.3 removes:

- custom cursor texture/cache resources;
- custom HCURSOR rasterization;
- cursor-shape SRVs;
- hotspot-based custom cursor compositing;
- ballistic cursor scaling;
- velocity-reactive cursor scaling.

Because the native cursor is captured inside the WGC desktop image before the
camera transform, it naturally scales and moves with the same ArZoom sampling
as the demonstrated content.

This is intentionally simpler and more reliable than attempting to emulate the
system cursor.

## P4R.3B — Realistic Keycap Geometry

Key width is no longer derived from label text extent.

The structured semantic keycap frame now carries a physical size class:

| Key class | Width | Height | Keyboard unit |
|---|---:|---:|---:|
| letter/digit/symbol/function/Windows | 82 px | 82 px | 1u |
| Ctrl / Alt and medium action keys | 103 px | 82 px | 1.25u |
| Shift / Enter | 123 px | 82 px | 1.5u |
| Backspace | 164 px | 82 px | 2u |
| Space | 287 px | 82 px | 3.5u |

The Windows key remains a four-pane logo and now acts as the visual reference
for all ordinary 1u keys.

Corner radius is reduced from 16 px to 13 px so square keys read more like
physical keyboard caps and less like pills/cards.

The retained GDI/D3D11 resource contract is unchanged: keycap generation may
update pixels, but it does not create per-frame textures, fonts, brushes or
pens.

## P4R.3C — Emissive Single-Ring V2

P4R.3 keeps the validated single-ring rule:

- one circle per active target;
- no center dot;
- no center fill;
- no visible secondary ring.

The stronger appearance is generated from one analytic ring distance field with
three energy zones:

1. bright core;
2. near bloom;
3. diffuse halo.

At 1080-class output the nominal geometry is:

- radius: approximately 10.5 -> 64 px before adaptive output scaling;
- core half-width: approximately 3.9 -> 2.25 px;
- near bloom spread: approximately 9 -> 16 px;
- diffuse halo spread: approximately 18 -> 30 px;
- left lifetime: 0.88 s;
- right lifetime: 0.90 s;
- middle lifetime: 0.84 s.

The original click palette remains unchanged:

- left: `#32B8FF`;
- right: `#FF5C8A`;
- middle: `#FFC857`.

### Bright-background behavior

Pure additive light cannot become brighter than a white UI surface. P4R.3
therefore uses restrained deeper chromatic support around the same ring on
bright surfaces. This preserves the perception of glow without drawing a
second circle or dark filled center.

### Rapid repeated clicks

A same-kind click within approximately 120 ms and a small normalized target
radius recharges the existing pulse instead of allocating a second concentric
pulse. Repeated clicks still renew energy, but the presentation remains clean.

## Performance contract

P4R.3 removes GPU/GDI cursor resources rather than adding new ones.

The click V2 remains analytic in the existing compositor pass and uses the
existing fixed four-click state. Keycap size classes are metadata only.

No P4R.3 feature may add:

- per-frame heap allocation;
- synchronous GPU readback;
- output-frame waiting;
- cursor texture creation;
- blur render passes;
- unbounded click history.

## Automated gates

Tests cover:

- custom cursor frame/compositor path no longer exists;
- same-target rapid click leaves exactly one active pulse;
- click remains active through the intended long fade and eventually expires;
- click center remains completely unfilled;
- click core remains visible on dark and near-white surfaces;
- diffuse halo remains visible on dark and near-white surfaces;
- modifier/action keycaps carry deterministic physical unit classes;
- long key classes remain wider than 1u;
- existing ArZoom, P4R keyboard privacy, P5A and P5B gates remain active.

## Direct 1080p60 acceptance gate

Use a real recording, not screenshots alone.

### Cursor

1. leave the cursor idle for at least 10 seconds: no flicker/disappearance;
2. move quickly in zig-zag/large sweeps: pointer remains easy to track;
3. hover Arrow, Hand, I-Beam and resize surfaces: native shapes remain correct;
4. trigger 2x Smart Zoom: cursor scales with the captured content naturally;
5. no duplicate cursor appears.

### Keycaps

1. `Ctrl+A`: A is square and matches Windows-logo 1u geometry;
2. `Shift+A`: Shift is wider while A remains square;
3. `Ctrl+Shift+Win+S`: chord remains compact and aligned;
4. long keys retain recognizable keyboard proportions;
5. white/light keycap language and Windows logo remain unchanged.

### Click

1. core is clearly thicker than P4R.2;
2. bloom is obvious on dark UI;
3. ring/glow remains obvious on white browser UI;
4. fade has a smooth afterglow rather than abrupt disappearance;
5. no center dot/fill;
6. no visible double ring;
7. rapid double-click at one location visually remains one re-energized ring.

### Regression/performance

- Win+R overlay still appears;
- ArZoom trajectory/gimbal behavior remains unchanged;
- P5A/P5B visual engine continues to function;
- `presentation_input_dropped == 0` in normal validation;
- compositor/encoder cadence remains within the established 1080p60 budget.

## Direct validation result — accepted

The real recording `Arssyut-20261003-060151.mp4` was reviewed on
2026-10-03 at 1920x1080 / 60 FPS.

Accepted observations:

- native Windows cursor remains stable through the reviewed idle/motion
  sequences;
- cursor visibility is materially better than the retired P4R.2 custom path;
- during Smart Zoom, the native cursor scales naturally with the captured
  desktop source rather than remaining a small independent overlay;
- click V2 was explicitly accepted by direct user validation;
- realistic keycap geometry was explicitly accepted by direct user validation;
- diagnostics reported zero presentation-input drops and no analysis map
  failures.

P4R.3 is therefore the accepted production baseline. Future presentation work
must preserve this behavior unless a new direct recording demonstrates a
specific regression or a deliberately approved replacement.

CI success proves structural and deterministic gates; this direct recording
closes the P4R.3 visual-acceptance gate.
