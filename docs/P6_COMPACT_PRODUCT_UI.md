# P6 — Compact Professional Product UI/UX

## Goal

Turn the existing engineering recorder shell into a compact product workflow
without changing recorder, ArZoom, ArVisual, encoder or color-pipeline
authority.

P6 follows four product rules:

1. capture first;
2. one obvious primary action;
3. progressive disclosure instead of a dashboard;
4. every visible session state comes from the recorder engine.

## Idle workspace

The default window is intentionally short and dense.

Primary capture card:
- Capture Source dropdown using the existing monitor/window catalog;
- Refresh;
- 60/30 fps selector;
- one compact rectangular Record button;
- one-line summary of resolution, fps, Visual Style and presentation effects.

Header:
- product name;
- small descriptive subtitle;
- Settings disclosure button.

No audio/microphone placeholder cards are shown because those controls are not
implemented by the current production path.

## Settings disclosure

Settings expands the same window rather than opening a second configuration
authority.

It exposes:
- Smart zoom;
- Click visual;
- Shortcut keys;
- Visual Style: Pixel Accurate / Clean Screen / Vivid Presentation.

Closing Settings only hides the controls. It does not rewrite their values.

The collapsed capture summary remains visible so the operator can see the
current recording configuration without reopening Settings.

## Recording surface

Once RecorderSnapshot enters an active state, the idle shell collapses into a
small borderless top-center bar.

The bar contains only:
- engine-derived status;
- elapsed time;
- one Stop control.

It remains capture-excluded while recording and does not duplicate recorder
state.

The pre-record idle window rectangle is restored after Ready/Failed.

## Result surface

Ready and Failed return to the same compact workspace.

Ready:
- status becomes Saved;
- final performance counters remain inspectable;
- output filename is shown;
- Open video is enabled when the output exists;
- Diagnostics is enabled when its sidecar exists.

Failed:
- no blocking completion popup is required;
- error/status/encoder stage are rendered inline;
- diagnostics remain directly accessible.

The only blocking failure dialogs left are pre-session/actionable conditions
such as no source selected or recorder startup failure.

## Keyboard

F9 is an application accelerator for the primary Record/Stop action. It invokes
the same command handler as the Record button; there is no keyboard-specific
recording authority.

Tab/Space navigation remains available through standard Win32 controls.

## Visual language

- native dark shell;
- thin low-contrast borders;
- one restrained red recording accent;
- no oversized typography;
- no giant circular record button;
- no tile/dashboard grid;
- compact labels and Segoe UI;
- status colors are semantic only: red for recording/failure and green for
  successful completion.

## Locked engine boundary

P6 does not modify:
- P5D.7 BT.709/full-RGB-to-studio-NV12 color contract;
- P5D/P5D.6 text/UI structure constants;
- P5E screen-native/neutral-anchor constants;
- H.264 rate-control policy;
- ArZoom camera behavior;
- click/keycap compositor behavior;
- capture cadence or frame scheduler.

## Acceptance

Automated:
- Windows release build;
- existing deterministic test suite;
- GUI launch smoke;
- package artifact.

Real GUI review:
- collapsed idle hierarchy is readable at 100% scaling;
- source names remain usable for long app/window titles;
- Settings opens/closes without layout overlap;
- current configuration summary updates after setting changes;
- Record is visually primary but not oversized;
- recording bar is compact and readable;
- F9 and pointer Record/Stop behave identically;
- Ready state exposes video/diagnostics cleanly;
- Failed state stays actionable without UI-state divergence;
- recording window remains excluded from capture.

P6 should remain unmerged until this real GUI acceptance is complete.
