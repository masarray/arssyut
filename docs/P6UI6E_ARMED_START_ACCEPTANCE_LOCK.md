# P6UI.6E — Deterministic Armed Start Acceptance Lock

**Runtime candidate:** `c428b525fb549866483b8b189e62cc1ba9435a76`  
**Canonical CI:** #330 / run `37577851046` — Native + Avalonia green  
**Product artifact:** `arssyut-p6ui-avalonia-windows-x64` / artifact `11463199673`

## Product contract

The recording start sequence is deliberately split into preparation and media start:

```text
Start button / F9
      |
      v
Preparing
  encoder open
  WGC started
  compositor ready
  presentation input warm
  first real WGC frame available
      |
      v
Armed
      |
      +--> full-target dim layer (capture excluded)
      |
      +--> 3
      +--> 2
      +--> 1
      |
      v
ACTION!
  dim layer clears
  recorder_commit_start()
      |
      v
Recording
  reset_timeline(now)
  started_at = now
  frame zero follows the commit
```

This is not a cosmetic delay in front of `RecorderSession::start()`.
Native readiness owns the transition to Armed, and the UI countdown begins only
after that state is observable.

## Visual lock

The accepted visual direction is intentionally cardless:

- full selected capture target is dimmed during pre-roll;
- large centered movie-leader number;
- subtle circular leader ring and center guides;
- no dialog/card/chrome around the number;
- `ACTION!` removes the dim layer before start commit and remains briefly as a
  capture-excluded cue;
- countdown window is topmost and uses the same target/Region geometry already
  published by the native overlay authority.

Do not replace this with a modal dialog, toast card or recorder-owned compositor
effect.

## Deterministic start guarantees

1. Encoder, capture and presentation input are initialized before Armed.
2. Armed is not published until at least one real WGC frame exists in the
   bounded latest-frame handoff.
3. No video frame is submitted to the encoder while the session is Armed.
4. Presenter input collected during pre-roll is discarded at commit so a
   Zoom/Reset/Freeze press during countdown cannot become the first recorded
   semantic action.
5. The native media clock is created only after `recorder_commit_start()`.
6. User-visible elapsed recording duration ends when the render/write loop
   ends; MP4 finalization time is not included.
7. Stop/F9 while Preparing or Armed cancels pre-roll instead of creating a
   misleading empty recording.
8. Countdown/controller UI are capture-excluded HWNDs; no countdown graphics
   are part of the encoded output.

## Diagnostic evidence

New recordings expose:

- `prepare_latency_ms` — Start/F9 request to native Armed readiness;
- `armed_wait_ms` — time intentionally spent in Armed/countdown before commit;
- `commit_to_first_frame_us` — media-clock start to first successful encoder
  submission;
- `capture_preroll_received` — real WGC frames received before commit.

These values are the primary evidence for deterministic start. Do not infer
Start/F9 latency from total recording duration.

## Real Windows acceptance matrix

The exact CI #330 artifact must be tested before P6UI.6E is visually accepted.

1. Display recording from main Start button.
2. Display recording from global F9 while another app owns focus.
3. Verify dim layer appears while native is Preparing, before `3`.
4. Verify movie-style `3 -> 2 -> 1 -> ACTION!` has no card.
5. Verify the desktop is visually normal at `ACTION!`.
6. Verify resulting MP4 begins at the intended post-countdown moment and never
   contains countdown UI.
7. Compare visible `ACTION!` timing with
   `commit_to_first_frame_us`; first encoded frame should follow commit without
   a second preparation delay.
8. Press F9 during Preparing and during `3/2/1`; cancellation must return to
   Ready without a saved empty MP4.
9. Repeat on Window capture.
10. Repeat on Region; countdown bounds must use the canonical native Region.
11. Repeat on a secondary/negative-origin monitor.
12. Repeat at 30 fps and 60 fps.
13. With Spotlight/Zoom/Freeze hotkeys configured, press them during countdown
    and confirm no pre-roll command leaks into frame zero.
14. Confirm the floating recording controller appears only after native reaches
    Recording and remains absent from MP4.
15. Repeat several Start/Cancel/Start/Stop cycles and inspect memory/resource
    counters for growth.

CI success proves build/regression integrity, not real visual timing. Keep the
milestone open until encoded-video evidence validates this matrix.
