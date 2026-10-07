# P6UI.6A.2 — Presenter Zoom Configuration

**Baseline:** P6UI.6A.1
`c394e3e2dfb262cf193878353eb9ec7fefc74b62`

## Goal

Remove the last UI-path hardcoded presenter zoom without reopening the accepted
ArZoom camera implementation.

The product owns only a configured zoom amount. Native
`PresentationController -> ArZoomCameraAdapter` remains authoritative for
camera state, Smart Zoom, Toggle/Hold composition, Overview Peek and motion.

## Product presets

The canonical Settings presets are:

- 1.10x
- 1.25x
- 1.50x
- 1.75x
- 2.00x (default)
- 2.50x
- 3.00x
- 4.00x

The list exists once in `SettingsPreviewState`. Settings renders it; it does
not maintain a second numeric list.

Toggle Zoom and Hold Zoom consume the configured value. Zoom In/Out continue
to use the accepted 0.25x native step and 1.10x..4.00x native clamp.

## Bridge

ABI v8 appends:

```text
float presenter_zoom
uint32 reserved1
```

to `ArssyutBridgeStartRequestV1`.

The bridge validates finite 1.10x..4.00x input before source/session work and
assigns the accepted value directly to `RecorderConfig.presentation.zoom`.
The P6UI bridge no longer assigns 2.0x itself.

## Persistence

There is still one file and one writer:

`%LOCALAPPDATA%\Arssyut\settings.json`

Schema v2 stores:
- canonical hotkey modifier/VK bindings;
- presenter zoom.

Schema v1 migration:
- read all existing hotkeys exactly as before;
- assign 2.00x presenter zoom in memory;
- do not rewrite merely because the application started;
- the next accepted persistent change writes schema v2 atomically.

Malformed/unsupported values reject the entire snapshot and use the existing
safe-default/quarantine path.

## Settings navigation alignment

The Preferences rail now uses `Button.settings-nav`:
- button remains full-width;
- content alignment is Left;
- one 14px content inset;
- every Lucide icon starts on the same X coordinate;
- every label starts on the same X coordinate;
- Preferences heading/footer use the same visual inset.

This is Settings-only; generic `Button.nav` remains unchanged.

## Acceptance

Automated:
- default presenter zoom is 2.00x;
- supported preset can be set;
- unsupported value is rejected without mutation;
- Reset restores 2.00x;
- schema v2 persists/restores zoom + hotkeys together;
- schema v1 preserves hotkeys and migrates zoom to 2.00x;
- corrupt settings still fail safe;
- native bridge rejects <1.10x and >4.00x;
- valid presenter zoom proceeds to normal source validation;
- managed/native ABI versions both equal 8;
- no P6UI bridge hardcoded 2.0x assignment remains.

Real Windows acceptance:
- Settings sidebar icons/labels form one clean left rail;
- zoom selector is compact and does not collide with scrollbar;
- chosen zoom persists across restart;
- Toggle Zoom and Hold Zoom open at the selected amount;
- Zoom In/Out still step 0.25x;
- Reset/Overview/Smart Zoom behavior remains identical to P6UI.6C.
