# ARSSYUT — canonical engineering handoff, 2026-10-11 (Pause → Webcam)

> **CURRENT SOURCE OF TRUTH FOR THE NEXT THREAD.** Re-read GitHub refs and CI when you resume; this is a point-in-time verified snapshot, not permission to reset main. Earlier \`docs/CURRENT_HANDOFF.md\` preserves pre-audio P7 planning, and \`docs/RELEASE_HANDOFF_2026-10-10.md\` describes obsolete RC1-era packaging. Neither may override this document.

Repository: https://github.com/masarray/arssyut  
Agent contract: [AGENTS.md](../AGENTS.md)  
Product PRD: [PRD.md](PRD.md)  
Integrated media acceptance: [AV_RELEASE_ACCEPTANCE_2026-10-11.md](AV_RELEASE_ACCEPTANCE_2026-10-11.md)  
Pause-specific QA document (on PR #115): [PAUSE_RELIABILITY_MILESTONE_2026-10-11.md](https://github.com/masarray/arssyut/blob/feat/115-native-av-pause-resume/docs/PAUSE_RELIABILITY_MILESTONE_2026-10-11.md)

## 1. Immutable facts: ref, CI and acceptance

| Item | Verified snapshot |
|---|---|
| Stable development \`main\` | **\`1d5ed67bcb47466c6cd210145d5f8fd80e90e526\`** — PR #114 real-Windows-accepted live Mic Mute integrated |
| Active, **unmerged Draft** | [PR #115](https://github.com/masarray/arssyut/pull/115): **\`e80a0d412f8b1c11a1e3145590f145986e01cb58\`**, branch \`feat/115-native-av-pause-resume\` |
| Latest exact-head Windows CI | [#38119158377](https://github.com/masarray/arssyut/actions/runs/38119158377): success; Native Windows tests and integrated audio-enabled GUI built, UI smoke succeeded |
| PR #115 protected original hardware checkpoint | \`checkpoint/pr115-pause-13-cycles-hardware-20261011\` → **\`dadfe2ecac80011db3110140e4de404a259f4daa\`** |
| PR #115 protected **latest instrumentation + CI** | \`checkpoint/pr115-pause-seam-audit-ci-green-20261011\` → **\`e80a0d412f8b1c11a1e3145590f145986e01cb58\`** |
| Clean prior A/V baseline | \`checkpoint/av-dual-audio-accepted-20261011\` → \`12e1fa98e33735cade2ec5bdca61a76861d8bf23\` (PR #110) |
| PR #115 latest integrated GUI artifact | [CI run #38119158377](https://github.com/masarray/arssyut/actions/runs/38119158377), artifact **\`arssyut-AUDIO-ENABLED-TEST-win-x64\`**, ID **\`11694524180\`**; download from GitHub Actions, not a public release |
| Historical public releases | \`v0.1.0-rc.1\` and \`v0.1.0-rc.2\` exist but **failed real-world audio quality**; do not base a new release on these EXEs |
| Next publication | no RC3/stable released; use explicit manual release workflow, exact successful \`main\` integrated-A/V CI and separately accepted installer+portable binary |

**Important:** The acceptance of the original 13-cycle Pause test at \`dadfe2e\` DOES NOT imply acceptance of each splice of the *newer* \`e80a0d4\` binary. The latter has additional diagnostics and QA only but requires its own real-media acceptance. Avoid confusing “CI green” with “hardware acceptance”.

## 2. What the user actually tested

User confirmed good sound in real Windows System Audio-only and simultaneous System Audio+Microphone recordings before Pause work. Native live microphone mute (global shortcut \`Ctrl+F9\`, floating controller, 5ms source-only gain fade) was also user-tested and accepted; merged via [PR #114](https://github.com/masarray/arssyut/pull/114) to \`main\`.

User tested original Pause/Resume candidate at \`dadfe2e\`, submitted private file \`Arssyut-20261011-124202-pixel-accurate.mp4\` with \`.diagnostics.json\`, and judged it reasonably good. Diagnostics (real hardware):

| Observation | Value |
|---|---:|
| File output ready; status code | ready / 0 |
| Pause events | **13** |
| Excluded Pause duration | **232.7152073 s** |
| Recorded / encoded media timeline | **229.957662 s** |
| Total physical session | ~**462.6728693 s** |
| Submitted audio PCM | **11,037,967 frames / 48k**, ~229.957646s |
| Submitted video frames | **13,796 / 60fps**, ~229.933333s |
| Overall encoded A/V endpoint delta (by counters) | ~**24.3 ms** |
| System Audio packets | **45,041/45,041**, 0 dropped |
| Microphone packets | **46,458 captured / 46,450 published**, **8 dropped** (8 pool exhaustions) |
| AAC program dropped, stale/duplicate PCM, staging gap/overflow | **0 each** |
| Encoder backpressure | **0** |
| Video skipped | **1 frame** |
| Peak private process memory | **294,006,784 bytes** |

This is a meaningful **global A/V/clock/lifecycle checkpoint**. It is **not** zero packet loss and is **not** proof every one of the 13 Pause→Resume splices has clean video frames and click-free waveform. Private MP4 is NOT committed to GitHub. Earlier diagnostics do not contain the individual seam positions, so their exact points cannot be inferred retrospectively from \`pause_count\`.

## 3. Latest Pause milestone already implemented (PR #115, do not redo)

The user-tested original native Pause/Resume includes:
- **One** \`RecorderSession\` recording authority; same source for floating Pause/Play button and \`F10\` hotkey; \`Paused\` state is real native state, not a UI timer simulation.
- **One** compressed virtual monotonic QPC clock for H264 scheduler, PCM source mapping and AAC writer; stopped time does not belong to MP4.
- One existing MP4 writer kept open across Pause/Resume, one continuous 48k stereo 1024-frame AAC program; WASAPI capture remains bounded and is drained during Pause; resampler/source ingress reanchors on Resume. Stop while paused finalizes the one file.
- No new detached realtime worker, unbounded queue, parallel writer, additional PCM source clock or reimplementation of the accepted GPU pipeline.

Subsequent instrumentation and QA merged **into PR branch only**, not into main:
- Single fixed-size \`std::array<int64_t, 64>\` in RecorderSession records each successful Resume **output-media** QPC, with overflow counter; fields: \`pause_resume_events\`, \`pause_resume_media_ticks_100ns\`, \`pause_resume_seam_overflow\`. Memory usage is constant, no per-frame heap allocation.
- [Offline checker](https://github.com/masarray/arssyut/blob/feat/115-native-av-pause-resume/tools/acceptance/check_pause_recording.py), no product-runtime changes: decode H264/AAC through existing base checker, compare packet PTS around every recorded Resume, check excluded-time/track duration, relevant diagnostics and source drops.
- \`PASS\` (exit 0), \`REVIEW\` (exit 3, **not** release approval), \`FAIL\` (exit 1); CLI/tool failure exit 2. Old 13-cycle example is \`REVIEW\` because 8 microphone packet losses, 1 skipped video and no seam index.
- Deterministic Python contract tests, protection of static 64-entry seam storage, CI QA early (fast checks before costly native compile), exact-head green CI run #38119158377.
- No changes to approved WASAPI DSP, source drift/mapper, AAC encode, H264 writer, D3D11 compositor or GUI behavior relative to original Pause candidate: re-audit the PR diff if HEAD moves.

## 4. First task for the next thread — close Pause without regressions

1. Fresh \`main\`, \`feat/115-native-av-pause-resume\`, PR #115 metadata/diff and exact-head CI. **Never reset or force-push main**, never merge draft PR blindly.
2. Get new real Windows recording **from the latest \`e80a0d4\` CI artifact**, System Audio+Mic (and ideally System only) with 3–5 deliberate Pause/Resume transitions while speaking and playing music; plus test Stop while paused. The old 13-cycle file lacks per-seam telemetry.
3. Run from PR #115 branch on a PC with Python 3 + FFmpeg/FFprobe:
   \`\`\`powershell
   python tools/acceptance/check_pause_recording.py recording.mp4 recording.mp4.diagnostics.json --output pause-acceptance.json
   \`\`\`
   Verify new \`pause_resume_media_ticks_100ns\` indices, packet PTS around EACH join, H264/AAC complete decode, A/V offset, zero staging errors and remaining Mic packet drops. Listen/watch around **each** join. The current helper audits packet PTS/5-second AAC windows; **manual detailed per-join waveform and frame inspection remains a real acceptance gate**. Never say it was measured unless you actually opened and analyzed the file.
4. If PASS plus user visual/audio approval and no unexplained dropouts, checkpoint current exact HEAD, ready PR #115, merge only matching passing SHA, repeat integrated A/V CI on merge commit and compare exact Git blobs. If REVIEW, investigate the specific source/format without modifying accepted DSP speculatively. FAIL blocks merge.
5. Test mic-only, dual source, mute during recording, fast Pause/Resume, Stop while paused, successive recordings (each Start/Stop creates a separate MP4), long A/V sync and resource bounds. Keep \`main\` untouched until gate is met.
6. New preview release uses \`.github/workflows/windows-release-exes.yml\` **manual dispatch** from successful exact-main integrated audio-enabled CI; Setup Inno + portable PE must package the same executable hash. Public Download gating requires actual hardware signoff (release note \`ARSSYUT_AUDIO_HARDWARE_ACCEPTED\` on its own line), not green CI.

**CI efficiency:** run cheap Python/Node contracts before native compilation, reuse the single H264+AAC Windows product build for UI/Settings; do not run a redundant video-only *product* build on audio PRs. Do not create dozens of CI commits; plan source + regression + exact guard updates coherently.

## 5. Camera clarified: native Smart Zoom exists; native **Webcam is still missing**

There are two different “camera” features:
- **ArZoom Smart Camera**: deterministic viewport motion, zoom/freeze/presenter semantics already implemented in native core. Read \`docs/P3R_ARZOOM_PARITY.md\`; do not replace with a new managed camera planner or second compositor.
- **Webcam / face-cam overlay**: NOT implemented as product. The floating \`CameraButton\` remains disabled: “Camera compositor backend is not wired yet.” README and Download must not advertise recorded webcam. Native device enumeration alone is not webcam capture or live compositing.

[Issue #116 — P8W Native Webcam Overlay](https://github.com/masarray/arssyut/issues/116) is the newly-created roadmap. It scopes native camera device capture with guarded Windows permissions, GPU-first/latest-frame bounded webcam handoff, one D3D11 compositor and canonical QPC, webcam crop/position/size/mirror, live preview that reads native truth, click Camera toggle during recording, Pause semantics, disconnect/reconnect, UI handling and source/audio/video soak.

**Order:** P8W0 current backend/device/compositor audit + capture ADR → P8W1 native capture/lifecycle/telemetry → P8W2 GPU overlay and native compose → P8W3 Avalonia preview and Camera control → P8W4 real Windows hardware acceptance + release readiness. Source and QA must stay independent of unsafe Pause changes while its PR is unaccepted; choose one explicit integration owner.

Also pending: Region/game acceptance, 60-minute A/V sync, 100x Start/Stop and setup/portable hardware validation are not silently “done.” Issues #71 (long A/V soak), #113 (Pause/Mic control) remain relevant. Prioritize actual product blockers over speculative DSP tuning.

## 6. Hotspots and authoritative files

- \`AGENTS.md\`: **mandatory engineering contract** (bounded buffers, no duplicated state/clock, GPU retained render, no per-frame allocations)
- \`src/app/recorder_session.cpp/.hpp\`: source of truth for session state, pause, media clock, metadata/diagnostics; only one integration owner
- \`src/app/recording_pause_clock.hpp\` **on PR #115**
- \`src/app/audio_product_runtime.hpp\`, \`src/core/audio/audio_packet_ingress.hpp\`, \`src/core/audio/audio_canonical_program_assembler.hpp\`: 48k program, source QPC mapping, continuity; do not randomly change
- \`src/platform/windows/media/mf_h264_mp4_writer.cpp\`: Media Foundation one-writer authority
- \`src/ui/Arssyut.UI/MainWindow.axaml.cs\`, \`RecordingControllerWindow.axaml.cs\`: thin UI over native state; \`F10\` Pause and \`Ctrl+F9\` Mic Mute must call same native bridge
- \`src/bridge/native_bridge.h/.cpp\`, \`src/ui/Arssyut.UI/Interop/NativeBridgeClient.cs\`: ABI/interop + hotkeys + device snapshots
- \`third_party/arzoom/include/\`, \`src/presentation/arzoom_camera_adapter.hpp\`: Smart Zoom camera path, NOT face-cam
- \`assets/favicon/\`: official product icon family for Windows, Inno installer, README and GitHub Pages; never restore missing old assets
- \`tools/acceptance/check_av_recording.py\` and PR #115 \`tools/acceptance/check_pause_recording.py\` (and its QA tests)
- \`.github/workflows/ci.yml\`, \`.github/workflows/windows-release-exes.yml\`: exact-head audio-enabled CI and manual opt-in release.

## 7. Work and reporting discipline for the next thread

1. Announce latest main SHA, PR #115 head, green CI, and the accepted checkpoint; check refs again, **don't trust static numbers when HEAD moves**.
2. State **done / pending / blocked**, never invent a GitHub artifact, MP4 analysis, camera functionality or public release.
3. A task is completed only after source diff and unit/failure tests, matching CI, real Windows QA for hardware media, checkpoint and safe merge. Avoid “optimizing” already clean WASAPI/H264 just because there is spare time.
4. Keep human/user data and recordings private; store only diagnostics summaries and cryptographic file identifiers in public repo.
5. Next work after Pause acceptance: open P8W0 webcam discovery/lifecycle architecture lane from current green \`main\`, not the 2026-10-07 P7 planning baseline. User expects a substantial product, not another empty prototype.
