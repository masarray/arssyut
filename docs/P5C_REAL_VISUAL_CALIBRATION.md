# P5C — Real Visual Calibration Protocol

## Purpose

P5C product modes are structurally complete. This milestone turns visual
calibration into an evidence workflow rather than subjective one-off tuning.

The calibration target is not "make every frame more colorful." It is to prove
that the three public modes form a useful and safe hierarchy:

```text
Pixel Accurate
    exact source authority

Clean Screen
    cleaner / safer / more legible

Vivid Presentation
    stronger presentation energy
    while still bounded by Smart Auto
```

No mode should be tuned from a single screenshot or from synthetic shader tests
alone.

## Matched triad

For each calibration scene, record the same sequence three times:

1. Pixel Accurate
2. Clean Screen
3. Vivid Presentation

The recorder now puts the mode name in the MP4 filename, for example:

- `Arssyut-...-pixel-accurate.mp4`
- `Arssyut-...-clean-screen.mp4`
- `Arssyut-...-vivid-presentation.mp4`

Each MP4 must be accompanied by its `.diagnostics.json`.

## Isolation rules

For color calibration, presentation effects should be disabled:

- Smart Zoom OFF;
- Clicks OFF;
- Keys OFF.

This prevents camera motion, click emissive light and keycap white surfaces from
biasing color judgments.

Keep identical across all three recordings:

- source display/window;
- 1920x1080 output;
- 60 FPS;
- OS display profile;
- browser/IDE theme;
- brightness/HDR state;
- application content;
- scroll path and scene order;
- recording duration as closely as practical.

Do not tune between the three captures.

## Recommended calibration sequence

A useful 30-60 second sequence should include, in the same order:

1. **White browser/document**
   - large white/near-white areas;
   - black text;
   - light gray UI chrome;
   - a small amount of colored UI.

2. **Dark IDE**
   - near-black background;
   - neutral gray panels;
   - syntax colors;
   - fine text and thin lines.

3. **Colorful product/web page**
   - saturated icons/buttons;
   - gradients;
   - mixed warm/cool colors;
   - medium-luma imagery.

4. **Highlight-risk scene**
   - bright colored buttons/logos;
   - white UI next to saturated color;
   - content close to digital clipping.

5. **Skin/webcam scene** when available
   - neutral indoor lighting preferred;
   - avoid intentionally stylized camera filters.

6. **Saturated animation/game** when available
   - vivid reds/blues/magentas;
   - rapid scene changes;
   - bright highlights.

## Diagnostics evidence

P5C calibration diagnostics now include three evidence layers.

### 1. Public mode / base grade

Examples:

- `arvisual_mode`;
- `arvisual_base_master`;
- `arvisual_base_enhance`;
- `arvisual_base_color_pop`;
- `arvisual_base_clean_white`;
- `arvisual_base_clarity`;
- `arvisual_base_skin_protect`;
- `arvisual_base_skin_beauty`;
- `arvisual_base_healthy_tone`;
- `arvisual_base_toy_gloss`;
- `arvisual_base_depth_pop`;
- `arvisual_base_highlight_guard`;
- `arvisual_base_performance`.

This proves exactly which preset mapping was used.

### 2. Scene statistics

The final completed P5B analysis state includes:

- p10 / median / p90 / p98 luma;
- mean / p90 saturation;
- shadow fraction;
- near-clip fraction;
- vivid fraction;
- hot-vivid fraction;
- neutral fraction;
- colored fraction.

These fields help distinguish "the preset is too strong" from "the scene was
genuinely high-risk."

### 3. Adaptive safety state

The final applied Smart Auto state includes:

- exposure;
- pop;
- highlight pressure;
- shadow pressure;
- creative strength;
- chroma limit;
- clean-neutral support;
- separation.

Pixel Accurate should remain unprimed because it does not schedule P5B analysis.

## Visual acceptance matrix

### Pixel Accurate

Must:

- remain the reference source;
- preserve white point and saturation exactly;
- schedule no Smart Auto analysis;
- show no mode-induced pumping.

### Clean Screen

Must:

- preserve neutral whites;
- improve perceived UI cleanliness without whitening colored content;
- keep dark IDE backgrounds stable;
- not look desaturated or "gray";
- not create visible halos around text;
- not materially alter skin tone;
- remain visibly more conservative than Vivid.

Reject/tune if:

- white browser becomes blue/yellow;
- gray UI becomes chromatic;
- syntax colors lose identity;
- skin looks processed;
- dark scenes lift aggressively;
- it becomes indistinguishable from Pixel Accurate.

### Vivid Presentation

Must:

- provide clearly stronger presentation energy than Clean;
- add useful color/depth without neon clipping;
- keep whites neutral;
- retain highlight detail;
- avoid red/magenta skin shift;
- remain stable during scene cuts because P5B adapts gradually.

Reject/tune if:

- bright colors clip or flatten;
- white surfaces tint;
- skin becomes orange/red/magenta;
- dark UI becomes crushed;
- rapid cuts cause visible pumping;
- Vivid is only "more saturated" without useful depth/clarity.

## Performance acceptance

For each mode:

- `capture_busy_drops == 0` under normal desktop recording;
- `presentation_input_dropped == 0` when presentation controls are disabled;
- no black frames;
- no material compositor/encoder pacing regression.

For Clean/Vivid:

- `visual_analysis_available == true`;
- completed samples advance;
- `visual_analysis_map_failures == 0`.

Pixel Accurate:

- grade OFF;
- Smart Auto OFF;
- scene analysis should remain unprimed.

## Tuning discipline

Do not change multiple conceptual dimensions blindly.

When a matched triad demonstrates a problem, tune the narrowest relevant
parameter group:

- white/neutral issue -> clean-white / neutral support;
- highlight clipping -> highlight guard / Smart Auto highlight behavior;
- excess saturation -> color pop / chroma limit;
- flatness -> enhance / depth pop;
- texture harshness -> clarity;
- skin issue -> skin protect / beauty / healthy tone;
- synthetic shine -> toy gloss.

After any mapping change, repeat the same matched triad.

A calibration change is accepted only when it improves the target problem
without causing a regression in the other scene classes.

## Current status

P5C engine and mode mappings are implemented and CI-gated.

This calibration-support milestone does **not** claim final visual tuning by
itself. Final P5C visual acceptance requires matched real recordings for all
three modes.
