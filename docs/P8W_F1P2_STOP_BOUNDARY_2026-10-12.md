# P8W F1.2 — Stop-bounded canonical PCM admission

Status: **source candidate, hardware acceptance pending** (2026-10-12).
Parent: [Draft PR #122](https://github.com/masarray/arssyut/pull/122) at `352713e5a2aac0d05f1e4dcf4484a9d1f7a0ddd9`. This work uses a distinct stacked branch; no `main` merge.

## Evidence and scope

The user-supplied real Pause → Resume → Stop recording has `PlatformFailure(4)/StageFull(8)`, first error `stop_drain` on **microphone**, and a 16,084-frame stage peak against a fixed approximately 16,384-frame capacity. The prior F1 interleaved source drain had passed CI but failed real hardware. This is direct evidence of overflow, **not** yet proof of a single underlying cause.

Source audit: `RecorderSession` already passes a compressed media Stop timestamp to `ProductAudioRuntime::finish()`. But `CanonicalPacketIngress::process_packet()` and `drain_one()` admitted all native SRC output to the staging ring, including samples with canonical frame positions beyond that Stop. Meanwhile the program writer will not close media past Stop. Such a backlog can fill staging while never being eligible for output.

## Implementation

- Derive a **single exclusive canonical 48 kHz Stop boundary** from the *already compressed* `RecordingPauseClock` media Stop and original recorder media zero, with integer rational rounding. The original exact Stop still governs AAC trimming.
- Thread the bound through the existing `finish() → pump_available() → process_lease() → process_packet()` path and the SRC `drain_one()` tail. The normal running `service()` remains untouched, as do source timing, resampler, mixer, hardware format, buffer capacity, system audio, and encoded writer.
- A packet spanning Stop contributes only its pre-Stop canonical prefix; a wholly post-Stop sample contributes no staging PCM. The SRC still consumes the packet and advances its cursor exactly once; the WASAPI pool lease is still released on all normal paths. No blind packet replay, buffer growth, or error suppression.
- Preserve the existing first-error telemetry and fail-closed semantics.

## Regression

`tests/test_audio_packet_ingress.cpp` adds a hardware-independent Pause → Resume → Stop fixture. It generates a 1024-frame microphone packet spanning a 600-frame remaining Stop interval, then 40 more post-Stop packets and SRC tail. It asserts the admitted pre-Stop PCM count, packet/SRC cursor advancement, zero staging overflow, correct final AAC block content, and reproduces the previous StageFull with an intentionally unbounded comparison fixture. This test does **not** emulate a real Windows WASAPI driver.

## What remains explicitly unaccepted

1. Run full Windows native/CTest, portable QA, pinned-FFmpeg audio-enabled GUI startup, and capture exact workflow SHA.
2. On real hardware run **System + Mic + webcam**, Pause/Resume, Mic Mute, then Stop. Require `status_code=0`, `audio_staging_overflows=0`, `audio_dropped_program_blocks=0`, correct AAC waveform, no A/V desync, and correct excluded pause duration. Repeat Stop while Paused.
3. **F2B is not implemented here**: the camera is still re-opened between preview and recorder, and continuous GUI thumbnail during recording is not solved. Follow ADR-011 with one source/device owner, GPU-consumer handoff, and a small preview publish path. Keep PR Draft / main untouched.

This is a targeted P0 correction and deterministic test milestone, not a release-ready or hardware-accepted webcam build.
