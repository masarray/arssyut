# F1 implementation candidate — Audio Stop/Finalize StageFull(8)

Date: 2026-10-12. Owner: [P8W F1–F5 execution plan](P8W_F1_F5_EXECUTION_PLAN_2026-10-12.md), [Draft PR #120](https://github.com/masarray/arssyut/pull/120).
Status: **SOURCE + DETERMINISTIC TEST CANDIDATE; Windows CI and real A/V hardware acceptance must be recorded separately.**

## Evidence before editing

The user's 15.38s real 1080p60 H264 + AAC MP4 (with live GPU face-cam in output) was playable with 923 video frames and 718 audio samples, yet returned `status_code=4`, `status_detail=8`, `audio_staging_overflows=1`. Core `PacketIngressStatus::StageFull` enumerates to 8. It is a genuine lost audio-stage offer; do not mark that recording Saved merely because the MP4 plays.

Old `ProductAudioRuntime::finish()` consumed up to 32 rounds × two WASAPI packets per active source before closing even one 1024-frame program interval. With a fixed 16384-frame per-source staging ring, a 10ms-packet backlog can overflow after 35 source offers. Closing a future program block prematurely to fix it is **not acceptable**: that substitutes missing-source silence for still-queued microphone packets. `WasapiCaptureSource::stop()` joins then retires its handoff, so stopping before draining the fixed queue would discard actual accepted captured data.

## Native ownership decision

1. `WasapiCaptureSource::quiesce()` uses the same existing producer stop+join lifecycle but **retains its published packet handoff**. Its wrapper methods delegate to the same source; `stop()` continues to retire the pool as before. This is a Stop control-plane function, never called by WASAPI callback or Avalonia UI.
2. The recorder samples physical Stop once in `RecorderSession`. `ProductAudioRuntime::finish()` quiesces microphone/system, then on the ONE existing writer-owner thread alternates a single native lease from each source with bounded SRC tail progress, 1024-frame program block closure, and writer submission.
3. An early block may close only if **every selected, not-yet-fully-drained source** has an already-emitted canonical output frontier at/past that block end. Once a source's handoff is empty and SRC drain emits no more, it is explicitly complete; only then may a quiet endpoint be represented by silence. Both frontiers use original `CanonicalPacketIngress` output-frame cursors. No new clock.
4. Bounded work: 512 fixed stop-drain iterations maximum, 1 lease/source/iteration and 4 program submits/iteration; 512 final closed blocks maximum, followed by one physically trimmed partial AAC interval. Lack of progress/overflow/writer failure fails closed; never silently retries SRC-consumed samples, reallocates buffers, or pads with synthetic full-length audio.
5. `stop_sources()` finally releases handoff and pool after no leases remain. Stop error paths also retire after reporting **first** failure. Existing 48k stereo mixer, gain/mute, libswresample configuration, WGC and H264/AAC writer unchanged.
6. First error telemetry: `audio_failure_phase` (0 none, 1 running, 2 stop_drain, 3 src_flush, 4 program_close, 5 writer_submit, 6 mf_finalize), `audio_failure_source` (1 microphone, 2 system, 3 program/writer), native HRESULT, packet status, source-stage current/peak frames, and pending program blocks. Existing status 4/8 remains stable. The UI displays "Audio buffer overflow..." instead of opaque `Native error 4 · 0x8`.

## Regression coverage / acceptance

- **Old-failure reproduction:** portable `arssyut_audio_canonical_program_assembler_tests` offers 60 × 480-frame stereo PCM packets per source to the unchanged 16384-frame stages. Old batch-first policy must reproduce exactly one StageFull. New frontier-aware interleaving must preserve absolute 1024-frame AAC block order, both-source audio content, no staging overflow, no program drops, bounded high-water and only one partial Stop block.
- **Source lifetime:** existing Windows `arssyut_wasapi_loopback_tests` uses a fake injected WASAPI source. A frame published before quiesce must remain poppable exactly once after the worker joins, followed by ordinary retirement via Stop. This tests the **real** native control-plane contract, not just a mock stop state.
- **CI:** exact-head Windows native release with all CTest and existing pinned-FFmpeg **AUDIO-ENABLED** `Arssyut.UI.exe` product lane. No new workflow, no duplicate video-only application and no extra CI job.
- **Hardware required before F1 accepted:** user double-clicks the *exact matching* A/V artifact (no PowerShell), records System+Mic+webcam 15–30s, Stop returns Ready, audio `StageFull=0`, zero `audio_dropped_program_blocks`, audio+video playable, original sound quality and Mic Mute retained. Repeat System-only and Mic-only, quick Stop, and longer recording. A green Windows runner cannot prove physical sound quality.
- **Do not merge:** main A/V stable, PR #115 Pause draft, PR #119 webcam experiment draft remain untouched. F2 begins only after F1 exact-head CI and real Windows A/V review; this milestone is **not** webcam preview/settings/logo work.
