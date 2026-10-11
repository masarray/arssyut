# Arssyut integrated A/V — real-Windows acceptance, 11 October 2026

**Protected engine source:** merged PR #110, commit 12e1fa98e33735cade2ec5bdca61a76861d8bf23 (main). User-tested audio source is checkpoint 17e23d50163052857b8de0c32efaf71a1766e196; later PR changes were CI/testing only. Branch checkpoint/av-dual-audio-accepted-20261011 protects merged source.

| Real hardware | Broken 08:17 | System-only 08:59 | Dual system+mic 09:18 |
|---|---:|---:|---:|
| Recording duration | 24.4s | 29.95s | 52.096s |
| Stale canonical audio frames | 240127 | 0 | 0 |
| Lost system/mic packets | 0 / n/a | 0 / n/a | 0 / 0 |
| Empty mixed output blocks | — | 184 during silence | 0 |
| Lost A/V program blocks | 0 | 0 | 0 |
| Skipped video / encoder backpressure | 2 / 0 | 0 / 0 | 0 / 0 |
| Decoded AAC 1024-frame boundary ratio | 4–8x | 0.94–1.06x | 0.88–1.04x |

The newest actual Windows recording contains 3124 H264 1080p60 video frames and 2442 AAC stereo 48kHz packets, with no video/audio PTS gap and ~29ms total track-end difference. 4460/4460 System Audio and 5395/5395 microphone packets were published; no pool/ring overflow, PCM stale frames, duplicates, clipping or encoder backpressure. Peak private memory ~303.6MB. User subjectively considers dual audio good. The 350 missing_source_intervals did not cause a single empty mixed-program block and are not automatically lost AAC packets.

Exact reference SHA256 for private evidence (MP4 files are **not** in GitHub):

- Dual mic+system 09:18: 6d10e3f975245a5365f6b61ec1390a6fc99fd4fe7b38a5d77de22726b459f8aa
- System-only 08:59: 70ddd02d3705ae17b4e5ae5743572981f81196ecb4d668c6fbe314969e978d4d
- Previously broken 08:17: 04f566dc42da13d5d50920d14fad4538461dbac461b9b1449589dc5c784a8503

Offline release gate command (Python 3, FFmpeg and FFprobe on PATH):
    python tools/acceptance/check_av_recording.py recording.mp4 recording.mp4.diagnostics.json --output av-acceptance.json

A PASS is independent technical media acceptance, not a replacement for human listening, Mic-only acceptance, repeated Start/Stop, microphone mute/fader/level, longer soak and real Setup/Portable Windows install/launch/uninstall.

## Release and architecture lock

- One canonical QPC/48kHz stereo program; never reset resampler on untrusted timestamp or flag source-only changes as global AAC discontinuity.
- Fixed-size WASAPI queues and bounded settlement; no new realtime threads/heap churn or speculative video/audio refactoring.
- Preserve polished GPU video, UI, meters and Open/Folder actions; webcam NOT implemented.
- Identical verified audio-enabled executable in Inno Setup and Portable single EXE. CI must test integrated A/V product; a video-only CI build is not a public product.
- PR #110 exact-head Windows CI PASS: https://github.com/masarray/arssyut/actions/runs/38104292421. Some GPU/encoder real-time tests may SKIP on CI, so green is NOT real hardware quality acceptance.
- Previous v0.1.0-rc.1 and rc.2 are historical unaccepted releases. Prepare new version (e.g. rc.3) by explicit manual exact-main-CI workflow dispatch only. Do not reuse old tags/assets.
- Public download eligibility requires standalone marker ARSSYUT_AUDIO_HARDWARE_ACCEPTED in GitHub Release notes, added only after final hardware signoff; no marker is authorized yet. Stable remains pending.
