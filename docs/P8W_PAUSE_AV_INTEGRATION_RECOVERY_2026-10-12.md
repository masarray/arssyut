# P8W integrated recovery — Pause/Resume + microphone mute over accepted webcam/A-V/F1

**Status 2026-10-12: implementation candidate, not real-hardware accepted.**
Base: `checkpoint/p8w-f1-audio-stop-drain-ci-green-20261012@db32d49f7ae4ed1cefd606f1b831bad30e774fc7` on Draft PR #120. Feature was rebuilt here from the actual Pause-seam source in Draft PR #115 `e80a0d412f8b1c11a1e3145590f145986e01cb58` and the field-tested, still unaccepted 13-cycle checkpoint `dadfe2ecac80011db3110140e4de404a259f4daa`; **do not wholesale merge #115 and erase F1**. No changes to main.

## Recovery matrix: STOP making CI green equal product ready

| Capability | Present in this candidate? | Acceptance still required |
|---|---|---|
| A/V source and accepted microphone/system mixer | preserved | Real sound and 48 kHz AAC MP4 |
| F1 `StageFull(8)` stop fix | preserved | Real Stop = Ready, zero staging overflows |
| GPU face-cam recorded MP4 | preserved | Physical PIP 15s and A/V parity |
| Native Pause/Resume | **ported from PR #115** with same virtual QPC; connected bridge, GUI and hotkey | Multi-seam A/V MP4 waveform/video check, no pause-gap media, no mic drops; previous 13-cycle build dropped 8 mic packets + 1 video frame |
| Mic Mute | existing native mixer path retained; controller enabled during Paused and Recording, requested state applied on Resume | Verify mic actually silent while system audio continues, twice in same MP4; no mixer DSP regression |
| GUI continuously live face-cam preview while recording | **not yet fixed** | F2 one physical MF source, GPU ownership handoff and tiny preview image source |
| Settings Camera placement/masks | **not yet fixed** | F3 profile/bridge/shader and pixel acceptance |
| Logo Overlay Settings | **not yet fixed** | F4 cached GPU image layer |

### Architectural seam decision

The recorder worker is the only owner of Paused state. `RecordingPauseClock` subtracts completed and in-progress paused QPC ticks from **both** video and canonical AAC timestamps; the same one MF writer/MP4 remains open. WASAPI and WGC capture providers remain active while paused, but the existing bounded packet queues are consumed/discarded; source resamplers are reanchored to physical Resume and excluded time. Post-resume stale input events and audio are not replayed. GUI and hotkeys send native `TogglePause`; native bridge state enum **appends 8** without renumbering existing values; Stop also works from Paused. No web app or additional writer.

Integration with F1: for ordinary Stop, retain the new producer quiesce → one-lease/source interleaved drain → safe close/submit → exact-physical-Stop trim. For **Stop while Paused**, discard queued paused packets and source staging, quiesce producer, skip SRC resampler-tail flush from excluded time, close only through the existing compressed media Stop, then release queues; same writer, no synthetic source frames. Native Mic Mute intent may be requested while Paused and applied after Resume by the existing recorder worker. No second clock, no new audio pool, no bigger staging.

### Gate before any merge/release

1. Existing native Windows Release/CTest including Pause Clock, Audio Canonical Program, Source Mapper, Ingress, F1 StageFull regression, bridge idle state and WASAPI quiesce.
2. Existing pinned-FFmpeg **AUDIO-ENABLED** GUI package, actual startup smoke. `Arssyut.UI.exe` by **double-click**, no environment commands or PowerShell.
3. On real PC, record Display+System+Mic+Camera, Pause 3–5s and Resume twice, toggle Mic Mute while recording and also while paused, then Stop; submit MP4 and diagnostics with `pause_count`, excluded ticks, 48 kHz AAC waveform, dropped mic/loopback packets, encoder drops, `audio_staging_overflows=0`, `status_code=0`.
4. Verify Stop while Paused makes playable 1 MP4 with no silent extension and Camera OFF remains accepted baseline. Do not treat synthetic CI as physical QA. Compare to original 13-cycle checkpoint honestly: eight mic pool exhaustions were observed there and must not be hidden.
5. Keep continuous webcam preview and Camera Settings visible in the matrix as **unsolved**, not silently passed due to Pause being restored.

Related: [ADR-011](adr/ADR-011-webcam-source-continuity-and-output-overlays.md), [F1–F5 plan](P8W_F1_F5_EXECUTION_PLAN_2026-10-12.md), [Pause PR #115](https://github.com/masarray/arssyut/pull/115), [A/V-first PR #120](https://github.com/masarray/arssyut/pull/120).
