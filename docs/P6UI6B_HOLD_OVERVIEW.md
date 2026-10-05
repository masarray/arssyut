# P6UI.6B — Hold Zoom / Overview Peek

**Pinned behavior authority:** `masarray/arzoom-follow-obs@ada8f5269246c64429d7aceb6cc72f81e72120ba`

## Scope

P6UI.6B adds the two ArZoom presenter controls that require both key-down and
key-up semantics:

- **Hold Zoom** — zoom is requested only while the assigned chord is physically
  held;
- **Overview Peek** — while already zoomed, hold to glide to centered full
  frame; release to restore the exact saved shot.

## Input ownership

No new keyboard hook is added.

```text
Windows Raw Input
      |
      v
existing PresentationInputWorker
atomic pressed_[256]
      |
      v
RecorderSession presentation cadence
      |
      +-- Hold Zoom chord state
      +-- Overview Peek chord state
      |
      v
existing PresentationController
      |
      v
existing ArZoomCameraAdapter
```

Momentary bindings are transported through bridge ABI v6 at recording start.
They are intentionally not registered with `RegisterHotKey` because that API
does not expose release events.

Chord state uses exact modifier matching. Releasing the primary key or any
required modifier releases the action on the next presentation tick.

## Hold Zoom

ArZoom semantics are preserved:

```text
zoom requested =
    Toggle Zoom latched
    OR Hold Zoom held
    OR Smart Zoom active
```

Hold Zoom therefore never owns or clears another zoom intent.

## Overview Peek

The pinned upstream `OverviewPeekController` is vendored unchanged into the
ArZoom include surface.

On press while zoomed:
1. save current screen-space shot transform;
2. minimum-jerk transition to center 0.5/0.5 at 1x;
3. hold full frame while the chord remains pressed;
4. pause normal camera stepping so cursor movement cannot retarget the saved
   shot.

On release:
1. minimum-jerk transition back to the saved transform;
2. resume the existing camera only after the saved shot has been restored.

If all underlying zoom intent disappears while Peek is held, the upstream
cancel-to-overview path wins and the camera ends at full frame.

## Reset / stuck-key safety

Reset / Full Frame clears Hold and Peek intent. RecorderSession then marks both
momentary bindings **blocked until release**. A still-physically-held key cannot
re-arm zoom on the next frame; it must be released and pressed again.

This matches upstream ArZoom's global release behavior without adding state to
Avalonia.

## Validation

Deterministic tests cover:
- Hold key-down zoom and key-up full-frame return;
- Overview centered 1x hold;
- pointer movement while Peek is active;
- exact saved-shot restoration on release;
- cancel-to-full-frame if Hold Zoom ends while Peek remains pressed;
- existing Toggle/step/reset tests remain active.

Real acceptance remains required for focus changes, modifier release order and
Region boundary synchronization.
