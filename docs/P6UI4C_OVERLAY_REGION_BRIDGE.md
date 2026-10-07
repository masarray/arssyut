# P6UI.4C — Native Overlay / Region Bridge

**Authority rule:** bridge the accepted P6R implementation; do not redesign it.

## Runtime chain

```text
Avalonia source selection
        |
        v
bridge ABI v3
        |
        +--> hidden native message owner
        |        +--> Region move/resize completion
        |        +--> 33 ms active-recording boundary sync
        |
        +--> existing RecorderOverlay
        |        +--> Display / Window boundary
        |        +--> editable idle Region
        |        +--> click-through recording viewport
        |
        +--> existing region_geometry
        |        +--> clamp_region_rect
        |        +--> map_region_to_crop
        |        +--> camera_viewport_rect
        |
        +--> existing RecorderSession
                 +--> RecorderSnapshot camera center/zoom
                 +--> canonical Region crop
```

## Input semantics lock

Display and Window boundaries are presentation only. They must not intercept
mouse input:

- native extended style includes `WS_EX_TRANSPARENT`;
- `WM_NCHITTEST` returns `HTTRANSPARENT`.

Idle Region intentionally differs only where editing is required:

- edge/corner => native resize hit target;
- top move pill => `HTCAPTION`;
- Region interior => `HTTRANSPARENT`.

While Region is recording, editability is disabled and the whole visible
viewport boundary is click-through.

The native bridge regression test verifies these semantics by sending real
`WM_NCHITTEST` messages to the actual `ArssyutCaptureBoundary` HWND.

## Region authority

Region is stored in native virtual-screen coordinates. The bridge never sends
geometry to C# for interpretation.

A Region recording uses the selected monitor's existing WGC source. The
selected rectangle maps once through `map_region_to_crop()` into
`RecorderConfig.crop`; output size and presentation normalization use that same
mapping.

## Smart Zoom boundary

During active recording, the bridge observes the authoritative
`RecorderSnapshot.presentation_camera_*` values and calls the existing
`camera_viewport_rect()`. The visible red viewport therefore follows the same
camera state as the compositor rather than reconstructing Smart Zoom in the UI.

## Automated acceptance

CI must prove:

- ABI v3 loads;
- source/device/command tests remain green;
- Display boundary carries `WS_EX_TRANSPARENT`;
- Display center hit-test returns `HTTRANSPARENT`;
- Region interior hit-test returns `HTTRANSPARENT`;
- Region edge remains a resize hit target;
- synthetic Region move publishes a new canonical native rectangle;
- Region -> Display -> Region preserves geometry on the same monitor;
- locked P5/P6R implementation files remain unchanged.

## Real Windows acceptance still required

- Display boundary never blocks click, drag, hover or scroll on the underlying
  application;
- Window boundary never blocks underlying interaction;
- Region move/resize works from every edge and corner;
- Region interior remains click-through;
- negative-origin monitor geometry;
- encoded Region dimensions/pixels equal the selected rectangle;
- Smart Zoom viewport remains inside Region and visibly tracks camera movement;
- Region recording completes to a playable MP4;
- Region -> Display -> Region remains coherent across repeated switching.

## Known correctness gate

Before P6UI.4C can be called fully accepted for Clean Screen / Vivid
Presentation Region recording, scene-analysis sampling must be verified against
the canonical Region crop. The bridge must not hide or work around any
engine-side mismatch.
