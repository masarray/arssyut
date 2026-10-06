# P6UI.6D Authority Audit

Issue: #32 — Port ArZoom Spotlight + cinematic camera motion / Freeze parity

Audit base: `021c8fb7954f1b0cf3310878334aa03a760dd9a2` (`feat/p6ui-avalonia-shell`)

Pinned upstream: `masarray/arzoom-follow-obs@ada8f5269246c64429d7aceb6cc72f81e72120ba`

## Canonical runtime ownership

The active product pipeline already has the required authorities:

```text
PresentationInputWorker / native hotkeys
        |
        v
PresentationController
        |
        v
ArZoomCameraAdapter
        |
        v
PresenterAwareSmartCamera        <-- single semantic camera
        |
        v
PresentationFrameState           <-- bounded output-timeline snapshot
        |
        v
D3D11Compositor::render_retained <-- single retained render authority
        |
        v
NativeVideoPipeline CFR output
```

Region framing consumes the same camera center/zoom through the existing native
Region projection. Avalonia does not own camera or Region geometry.

## Spotlight ownership decision

The three upstream Spotlight units are portable state/geometry helpers:

- `arzoom-spotlight.hpp`: analytic geometry/mask policy only;
- `arzoom-cinematic-spotlight.hpp`: bounded minimum-jerk aperture state;
- `arzoom-spotlight-zoom-resize.hpp`: observes configured/live zoom and emits
  one bounded Spotlight scale.

None of them is a camera. None writes camera intent.

Arssyut will therefore integrate Spotlight as bounded state inside the existing
presentation authority and pass the resulting frame state to the existing
retained compositor. Spotlight may consume canonical pointer/camera output; it
must never create another pointer mapping, viewport planner, or renderer.

## Freeze ownership decision

Pinned upstream Phase 3 defines Freeze by pausing `SmartCamera::step()` and
reading the same camera output unchanged. This is the correct Arssyut contract.

Freeze must therefore gate advancement of `ArZoomCameraAdapter` inside the
existing `PresentationController`. It is not a UI snapshot, copied transform,
second camera, or post-camera interpolation layer. Reset/full-frame remains
higher priority and may resume the existing camera toward 1x.

## Motion-quality decision gate

`SceneKinematicMotion` and `SceneViewportPlanner` are already vendored, but
they must not be chained after `PresenterAwareSmartCamera`.

P6UI.6D-F may only choose one of two outcomes after deterministic trace and real
recording evidence:

1. keep/tune the current `PresenterAwareSmartCamera` authority; or
2. switch the adapter boundary to the existing Scene planner/motion authority.

Running both and reconciling their outputs is prohibited.

## Rejected duplicate-authority designs

- second camera or post-camera easing layer;
- Spotlight-owned camera/focus planner;
- UI-side presentation geometry truth;
- extra keyboard/mouse hook for Freeze or Spotlight;
- second Region coordinate solver;
- second compositor or Spotlight texture/readback/blur pipeline;
- unbounded focus/click history;
- per-frame resource allocation.

## P6UI.6D-A gate

This package intentionally performs no product/render integration.

It only:

1. vendors the three pinned Spotlight headers byte-for-byte;
2. records provenance and ownership rules;
3. ports the three upstream deterministic tests unchanged;
4. registers those tests in the existing CMake/CTest graph.

Renderer/state integration starts only after these gates are green.
