# Arssyut Product Concept

## One sentence

**Arssyut is a compact Windows screen recorder that turns a natural desktop demonstration into a polished MP4 in one pass: smooth zoom, visible clicks, meaningful keyboard shortcuts, tasteful color treatment, and synchronized audio — without OBS-level complexity.**

---

## 1. The experience

The application should feel closer to a small camera tool than a video-production suite.

### Before recording

The user chooses:

- what to capture;
- whether system audio/mic are active;
- 30 or 60 FPS;
- presentation polish level.

Then presses **Record**.

### While recording

The user works normally.

Arssyut quietly:

- tracks presentation intent;
- moves/zooms the virtual camera only when useful;
- shows click feedback;
- visualizes meaningful shortcut keys;
- applies restrained color treatment;
- captures system/mic audio;
- keeps the media path hardware accelerated.

### After recording

The user receives one optimized MP4.

No timeline editor is required for the normal path.

---

## 2. Presentation philosophy

The recorder must help the viewer understand, not constantly remind the viewer that an effect engine exists.

### Zoom

Good:
- follows an explanation area;
- helps small UI details;
- settles smoothly;
- holds still while the user is reading/typing;
- returns to context deliberately.

Bad:
- chases every cursor movement;
- zooms in/out after every click;
- overshoots;
- vibrates around small pointer motions.

ArZoom's planner + kinematic motion is the behavioral foundation.

### Click feedback

Good:
- brief;
- clear;
- anchored to content;
- distinct enough to see in tutorials.

Bad:
- giant explosion;
- long particle trail;
- event history that grows.

### Keyboard actions

Good:

```text
[ Ctrl ] [ C ]
[ Ctrl ] [ Shift ] [ S ]
[ Alt ] [ Tab ]
```

Bad:
- reconstructing everything the user types;
- huge text toast;
- permanent keystroke history.

The supplied keycap reference establishes the preferred visual character: physical, compact, friendly keycaps with light/dark variants.

### Color

Good:
- clean whites;
- slightly stronger separation;
- readable dark UI;
- restrained vividness;
- protected highlights.

Bad:
- changing engineering/status colors;
- clipped whites;
- “HDR-looking” oversaturation;
- pumping as the scene changes.

Default is **Clean Screen**. A **Pixel Accurate** option is always available.

---

## 3. Product personality

Arssyut should feel:

- compact;
- professional;
- calm;
- precise;
- modern;
- fast.

Avoid:

- huge cards;
- giant headings;
- excessive gradients;
- animation for decoration;
- configuration walls;
- “streaming studio” visual complexity.

The GUI should use normal desktop information density.

---

## 4. Core controls

Recommended default main-window information architecture:

```text
Arssyut
────────────────────────────────────────
Capture      Display 1            [Change]
Audio        System ✓   Mic: USB ✓
Quality      Balanced · 1080p60
Polish       Zoom ✓  Click ✓  Keys ✓  Clean Screen
────────────────────────────────────────
                      [ Record ]
```

Advanced details open only when requested.

During recording:

```text
● 00:03:42     Mic ✓             [■ Stop]
```

No full control dashboard is necessary.

---

## 5. Smart Zoom concept

Smart Zoom does not mean AI/vision/OCR.

It is a deterministic presentation camera driven by bounded action signals:

- click;
- pointer motion/settlement;
- recent shortcut action;
- manual zoom command;
- current viewport pressure.

The planner decides WHERE.

The kinematic engine decides HOW.

The result should be predictable and testable.

---

## 6. Shortcut visualization concept

### Default recognized category

Display:

- modifier + key shortcuts;
- navigation/system actions;
- function keys;
- recorder commands.

Examples:

```text
Ctrl+C
Ctrl+V
Ctrl+X
Ctrl+A
Ctrl+Z
Ctrl+Shift+Z
Ctrl+S
Ctrl+F
Alt+Tab
Alt+F4
Win+D
Win+Shift+S
F2
F5
F11
Esc
Delete
```

This list is product configuration, not a hard-coded pile of if-statements.

### Chord grouping

If modifier transitions occur within the small canonical chord window, they become one display event.

Repeated auto-repeat keydown events should not spam new keycaps.

### Position

Default:
- lower center or lower-left safe area;
- configurable;
- never obscure the current click/cursor focus by default.

---

## 7. Output concept

Normal user-facing output:

`Arssyut 2026-09-30 10-30-22.mp4`

Behind the scenes:

```text
record safely
-> close/drain
-> remux/finalize
-> verify
-> atomically publish MP4
```

The user does not need to understand the temporary container.

If finalization fails, the app says the recording is recoverable rather than deleting it.

---

## 8. Quality modes

### Efficient

For older/lower-power machines.

- hardware-first;
- conservative analysis cadence;
- presentation effects still available;
- lower output cost.

### Balanced — default

The intended daily mode.

- 60 FPS where supported;
- hardware H.264;
- Smart Zoom;
- click visual;
- shortcut visual;
- Clean Screen grade.

### High Quality

For content production.

- higher encode quality;
- larger file;
- same deterministic effects.

Quality modes do not create separate engines.

---

## 9. Future Studio mode

The architecture should leave room for a future optional mode that writes:

- raw/clean recording;
- bounded/streamed event sidecar;
- camera/key/click metadata.

That could later support non-destructive re-export.

However, the first production release should not delay a reliable direct-MP4 workflow to build an editor.

---

## 10. Product boundary

Arssyut wins by being **smaller in concept** than OBS while borrowing professional realtime discipline from OBS-class software.

It is not:

- “OBS but simpler”;
- a browser app wrapped in a desktop shell;
- a CPU screenshot recorder with effects;
- an editor that happens to have a Record button.

It is a purpose-built native presentation recorder.
