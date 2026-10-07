# P6UI.6A — ArZoom Presenter Zoom Hotkeys

**Pinned behavior authority:** `masarray/arzoom-follow-obs@ada8f5269246c64429d7aceb6cc72f81e72120ba`

## Scope

P6UI.6A transplants the latch/step presenter controls that can be represented
safely through the existing Windows global-hotkey bridge:

- Toggle Zoom;
- Zoom In;
- Zoom Out;
- Reset / Full Frame.

ArZoom's configured zoom step is 0.25x and its supported presenter range is
1.10x through 4.00x.

## Ownership

No camera is added.

```text
Windows RegisterHotKey
        |
        v
bridge ABI v5 hidden HWND
        |
        v
direct repeat-safe presenter dispatch
        |
        v
bounded RecorderSession presenter mailbox
        |
        v
existing PresentationController
        |
        v
existing ArZoomCameraAdapter
        |
        v
pinned PresenterAwareSmartCamera
```

Manual Toggle Zoom is OR-composed with existing click/motion Smart Zoom intent.
Zoom In/Out change only configured framing. Reset clears active manual and
automatic zoom intent, then the accepted cinematic camera returns to full frame.
The configured zoom amount is retained for the next activation, matching ArZoom
presenter behavior.

## Resource policy

The recorder does not start presentation input solely for unused presenter
controls. A dedicated start flag is set only when at least one presenter zoom
hotkey is assigned.

Cross-thread commands are fixed-size atomics:
- toggle request parity;
- signed zoom-step accumulator;
- reset request marker.

There is no growing queue, extra camera worker or UI-side camera state.

## Settings

Presenter zoom shortcuts are user-assigned and default to **unassigned**.
Arssyut does not invent defaults that are not defined by the pinned ArZoom
product behavior.

The same Ctrl/Shift/Alt/Win chord grammar and Windows conflict validation used
by Start/Stop applies to presenter shortcuts. Unlike Start/Stop, presenter zoom
hotkeys are dispatched directly by the native bridge HWND so repeated Zoom
In/Out taps cannot be collapsed by the Avalonia polling interval.

## Deferred P6UI.6B

Hold Zoom and Overview Peek require key-down plus key-up delivery. The existing
`PresentationInputWorker` already tracks those transitions. P6UI.6B extends
that authority rather than adding a second low-level keyboard hook.

Freeze Camera / Toggle Smart Follow may be considered there only if they can be
mapped without creating another camera state owner.
