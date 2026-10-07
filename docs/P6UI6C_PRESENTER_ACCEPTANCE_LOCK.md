# P6UI.6C — Presenter Controls Acceptance Lock

**Baseline:** P6UI.6B `c2d4dca4a2b0ebc846185c6c86bd861671d677e8`

## Objective

Lock real-world presenter behavior before adding any more ArZoom parity
features.

P6UI.6C does not add a new camera or input system. It hardens and proves the
existing path:

```text
Raw Input held-state
      |
      +-- release-only physical-state fuse
      |
      v
RecorderSession momentary gate
      |
      v
PresentationController
      |
      v
ArZoomCameraAdapter
      |
      v
PresentationFrameState
      |
      +--> compositor
      +--> RecorderSnapshot
              |
              v
       native RecorderOverlay
       camera_viewport_rect()
```

## Focus / stuck-key policy

Raw Input remains the activation authority.

After a Raw Input chord already matches, `GetAsyncKeyState` confirms that the
primary key and exact Ctrl/Shift/Alt/Win modifier set are still physically down.
This confirmation is deliberately one-way:

- it may turn a held state **off**;
- it may never turn an untracked chord **on**.

Therefore focus/desktop transitions fail toward a safe release instead of a
sticky zoom without introducing a second hook or input owner.

## Intent ownership matrix

The camera zoom request is still the OR-composition of independent owners:

- Toggle Zoom latch;
- Hold Zoom physical hold;
- Smart Zoom automation.

Acceptance rules:

- release Hold while Toggle remains on -> stay zoomed;
- Toggle off while Hold remains on -> stay zoomed;
- release Hold while Smart Zoom is still active -> stay zoomed;
- return to full frame only when the final active owner releases/expires.

## Reset while held

Reset / Full Frame wins immediately.

A tiny deterministic `MomentaryReleaseGate` preserves the P6UI.6B rule:
a chord that is physically still down after Reset cannot re-arm on the next
presentation tick. It must be released once and pressed again.

The gate never reads keyboard state itself.

## Region parity

The bridge already drives the native recording boundary from
`RecorderSnapshot.presentation_camera_center_x/y/zoom` through
`camera_viewport_rect()`.

P6UI.6C regression-locks this existing ownership:

- Hold Zoom contracts the viewport inside the selected Region;
- Overview Peek at centered 1x expands to the exact selected Region;
- releasing Overview after pointer movement restores the exact saved Region
  viewport.

No Region-specific presenter math is added.

## Automated gates

The presenter deterministic suite covers:

- Toggle / Reset;
- 0.25x zoom step + 1.10x..4.00x bounds;
- Hold press/release;
- Reset block-until-release;
- Toggle + Hold overlap;
- Hold + Smart Zoom overlap;
- Overview saved-shot restore;
- Overview cancel-to-full-frame when underlying zoom intent ends;
- Region boundary Hold / Overview / saved-shot parity.

## Real Windows acceptance matrix

CI cannot replace these physical input checks:

1. Assign a Hold Zoom chord and record while another application owns focus.
2. Press chord, release primary key first -> zoom releases.
3. Press chord, release modifier first -> zoom releases.
4. Hold chord, alt-tab repeatedly, release while Arssyut is unfocused -> no
   sticky zoom.
5. Toggle Zoom on + Hold Zoom press/release -> Toggle ownership survives.
6. Hold Zoom + trigger Smart Zoom + release Hold -> Smart Zoom survives.
7. Overview Peek while zoomed -> full frame -> move pointer -> release -> exact
   saved shot returns.
8. Hold/Peek + Reset while the chord remains down -> full frame stays until
   release/re-press.
9. Repeat tests in Region mode and verify the red viewport boundary follows the
   same camera and never escapes the selected Region.

Any reproduced failure must be corrected in the existing input/camera/overlay
authority; do not add fallback presenter systems.
