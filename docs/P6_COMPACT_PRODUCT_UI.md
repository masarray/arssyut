# P6 — Recorder Workspace UI/UX

> Superseded by the P6R redesign after direct GUI testing on 2026-10-03.

The first P6 implementation treated the recorder as a compact configuration
form. Direct testing showed that this was not a sufficient product model for a
desktop screen recorder and exposed an interaction regression around
Record/Stop.

The authoritative P6 design and implementation contract is now:

- `docs/P6R_RECORDER_UX_RESEARCH.md`;
- the P6R section in `docs/ROADMAP.md`;
- the P6R recorder workspace contract in `AGENTS.md`.

## Required product grammar

P6R treats these as first-class recorder concepts:

- Capture Mode: Display / Window / Region / Game;
- visible capture/viewport boundary;
- compact recording toolbar;
- Record/Stop icon;
- Pause icon;
- System Audio;
- Microphone + device selection;
- Camera + device selection;
- dedicated Settings window;
- Output / Sound / Camera / Mouse & Keystroke / Hotkeys categories.

## Runtime ownership

The GUI may present upcoming recorder capabilities before their backend
milestones are complete because P6R is intentionally UX-first, but it must never
pretend an unsupported stream or capture mode was recorded.

Display and Window remain the currently authoritative capture paths.

Region, Game, audio, microphone, webcam composition and pause/resume are tracked
as explicit follow-on backend milestones in the P6R roadmap.

The capture boundary and recording toolbar are separate overlay windows. The
main recorder HWND must not be morphed into the recording toolbar.

The UI timer must not block waiting for the recorder worker. Ready/Failed
transitions are accepted only after the worker publishes completion.

## Visual acceptance

P6R remains unmerged until real Windows testing confirms:

- main recorder is understandable without explanation;
- capture mode and target are obvious;
- boundary follows selected Display/Window;
- boundary contracts/moves with Smart Zoom using the compositor camera state;
- Record -> recording -> Stop -> Finalizing -> Ready stays responsive;
- Settings is usable and categorized correctly;
- real microphone/camera device names appear when devices exist;
- unsupported capabilities are visibly pending rather than silently faked.
