# Pause/Resume — 13-cycle real-Windows reliability milestone

Test evidence: user-supplied 2026-10-11 12:42 MP4 and diagnostics. The media remains private; this report contains only aggregate counters. The verified initial implementation lives at commit `dadfe2ecac80011db3110140e4de404a259f4daa`, protected by branch `checkpoint/pr115-pause-13-cycles-hardware-20261011`. PR #115 remains Draft.

| Diagnostic observation | Value |
|---|---:|
| Final recording state / native error | ready / 0 |
| Pauses / excluded duration | 13 / 232.7152073 seconds |
| Encoded media duration (diagnostics clock) | 229.957662 seconds |
| Approximate physical duration | 462.6728693 seconds |
| System Audio captured / published / dropped | 45041 / 45041 / 0 |
| Mic captured / published / dropped | 46458 / 46450 / 8 |
| Mic fixed-pool exhaustions | 8 |
| Stale / duplicate canonical PCM frames | 0 / 0 |
| Audio staging gaps / overflows | 0 / 0 |
| AAC program blocks dropped | 0 |
| Video encoder submitted / skipped | 13796 / 1 |
| H264 encoder backpressure | 0 |
| Memory peak (process private bytes) | 294006784 |

At 60 FPS, 13796 encoded video frames represent about 229.933 seconds. The submitted 11037967 audio PCM frames at 48 kHz represent about 229.958 seconds: about 24 ms difference by counters. This does **not** establish that every individual Pause/Resume seam in the decoded MP4 is flawless. Eight Mic packet drops (~0.0172% of captured Mic packets) are actual losses; they must not be described as zero loss.

## Fixed-memory trace in the next QA build

New diagnostics now include `pause_resume_events`, `pause_resume_media_ticks_100ns` and `pause_resume_seam_overflow`, measured in the **included** media timeline. The existing recorder worker stores up to 64 completed Resume events in a fixed `std::array`, with no new audio/video workers, no heap activity per transition, and no change to encoder output or canonical clock. The previous 13-pause recording predates this instrumentation and therefore cannot retrospectively reveal precise join times.

## Repeatable media audit

Run on a real Windows MP4 from the updated PR #115 QA build (Python 3 plus ffprobe/ffmpeg on PATH):

```powershell
python tools/acceptance/check_pause_recording.py recording.mp4 recording.mp4.diagnostics.json --output pause-acceptance.json
```

The script runs the existing integrated video/audio quality gate once, additionally checks PTS continuity near every recorded Resume join, validates media duration against the compressed Pause clock and categorizes results. `PASS`/exit 0 requires strict quality conditions; `REVIEW`/exit 3 explicitly needs listening or further evidence (including this older 13-cycle sample with eight mic drops or absent seam indices); `FAIL`/exit 1 means a hard media/clock/source defect. Tool errors use exit 2. Review is **not** release acceptance.

The media analyzer is offline only. The Python QA tests use mocked media observations and run in the existing CI lane. No extra video-only product build or new real-time audio DSP is involved.

## Release policy

No RC3/stable tag or public download may be approved solely from green CI or the `ready` status. Verify each seam in a fresh actual Windows sample, listen to audio around Resume, test Stop while paused and Mic+System Audio, inspect packet losses, and ensure the exact release binaries originate from successful integrated-A/V main CI. Preserve the previously accepted clean audio baseline until hardware evidence confirms this next candidate.
