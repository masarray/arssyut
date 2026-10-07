# P6UI.6E — Deterministic Armed Start Acceptance Lock

**Canonical E candidate:** `1e17a26d679a3064d2cc29da78720a4e3c43415b`  
**Canonical CI:** #332 / run `37585130506` — Native + Avalonia green  
**Product artifact:** `arssyut-p6ui-avalonia-windows-x64` / artifact `11465933250`  
**SHA-256:** `616c0cc0ee4a065098c17ab113246696c551bbe2d6e771551c318e02667a2df4`

## Product contract

The recording start sequence deliberately overlaps visual countdown time with
native preparation:

```text
Start button / F9
      |
      +--------------------------+
      |                          |
      v                          v
visible countdown           native Preparing
3 (0..1 s)                  encoder open
2 (1..2 s)                  WGC started
1 (2..3 s+)                 compositor/input warm
      |                     first real WGC frame
      |                          |
      |                       Armed
      |                          |
      +----------- BOTH ---------+
                  |
                  v
         recorder_commit_start()
                  |
                  v
              Recording
         reset_timeline(now)
         started_at = now
                  |
                  v
               ACTION!
          dim layer clears
         frame zero follows
```

The countdown is not a guessed replacement for native readiness. It is a
presentation timeline that begins immediately after native start is accepted.
`recorder_commit_start()` is gated by both:

1. at least three seconds of monotonic visual countdown have elapsed; and
2. native RecorderSession is actually `Armed`.

If native becomes Armed early, it waits for the countdown. If native preparation
takes longer than three seconds, the UI holds the visible `1` until Armed and
then commits immediately. There is never a speculative start.

## Visual lock

- first visible countdown frame already contains `3`; no blank/dark-only
  pre-roll frame is intentional;
- full selected capture target is dimmed during pre-roll;
- large centered cardless movie-leader number;
- subtle circular leader ring and center guides;
- no dialog/card/chrome around the number;
- the dim layer remains visible to the user while native crosses the commit gate;
- `ACTION!` is revealed only after native reports `Recording`, then remains briefly as a capture-excluded cue;
- countdown uses canonical native target/Region geometry.

## Optimization / ownership lock

This change must remain cheap:

- no new native thread;
- no second recorder state machine;
- no new capture queue or pre-roll video buffer;
- no compositor/GPU pass for countdown;
- no readiness prediction/history model;
- no extra camera/input authority.

The existing UI snapshot cadence observes `Preparing/Armed`, while one
`Stopwatch` supplies the visual countdown. Native remains the sole authority
for WGC, encoder, readiness and media timestamp zero.

## Deterministic start guarantees

1. Native start enters `Preparing` and returns without blocking UI countdown.
2. Encoder, capture and presentation input initialize concurrently with
   `3 -> 2 -> 1`.
3. Native does not publish Armed until a real WGC frame exists in the bounded
   latest-frame handoff.
4. No frame is submitted to the encoder while Armed.
5. Start commit cannot occur before the three-second visual minimum or while native is still Preparing.
6. The dim layer cannot clear until native state is `Recording`; normal desktop visibility therefore never leads media start.
7. Presenter input collected during pre-roll is discarded at commit.
8. The media clock is created only after `recorder_commit_start()`.
9. Stop/F9 during Preparing or Armed cancels before frame zero.
10. Countdown/controller HWNDs stay capture-excluded.
11. Recording duration excludes MP4 finalization time.

## Diagnostic evidence

Recordings expose:

- `prepare_latency_ms` — Start/F9 request to native Armed readiness;
- `armed_wait_ms` — time from Armed until commit;
- `commit_to_first_frame_us` — media-clock start to first successful encoder
  submission;
- `capture_preroll_received` — WGC frames received before commit.

With overlap enabled, `prepare_latency_ms` is expected to occur inside the
visible countdown rather than before it.

## Real Windows acceptance matrix

1. Press Start: `3` must appear immediately with the dim layer; there must be
   no multi-second dark-only pause.
2. Repeat from global F9 while another app owns focus.
3. Normal preparation under three seconds should produce approximately one
   three-second start experience, not preparation time plus three seconds.
4. Verify cardless `3 -> 2 -> 1 -> ACTION!`.
5. Verify desktop returns visually normal only when `ACTION!` appears, after native Recording is authoritative.
6. Verify MP4 contains none of the countdown UI.
7. Compare `prepare_latency_ms`, `armed_wait_ms` and
   `commit_to_first_frame_us` with the perceived start timing.
8. Artificially/incidentally slow preparation beyond three seconds must hold
   `1`, never commit early, then transition directly to ACTION when Armed.
9. F9 during Preparing/countdown cancels cleanly without an empty MP4.
10. Repeat on Window and Region.
11. Repeat on secondary/negative-origin monitor.
12. Repeat at 30 fps and 60 fps.
13. Verify pre-roll Zoom/Reset/Freeze input cannot leak into frame zero.
14. Repeat several Start/Cancel/Start/Stop cycles and inspect memory/resource
    counters.

CI success validates deterministic policy/build integrity. Real encoded-video
evidence is still required for visual acceptance.
