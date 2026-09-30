# P1 GitHub Soak Automation

P1 uses two different GitHub Actions validation modes because a hosted Windows runner is not equivalent to a user's interactive Windows desktop.

## Hosted regression mode

Use Actions > P1 Soak > Run workflow > hosted-regression.

This runs on windows-latest and validates Release build, CTest, repeated WARP/D3D11 resource ownership, repeated frame-slot/compositor churn, and recovery-file lifecycle. It is useful automation, but it is not evidence of real Windows Graphics Capture performance.

## Interactive WGC mode

Use Actions > P1 Soak > Run workflow > interactive-wgc.

The job requires a self-hosted runner with labels: self-hosted, Windows, X64, arssyut-interactive.

The workflow refuses to run in Session 0 and verifies that Explorer exists in the same session.

For capture validation, sign in normally to Windows and launch the GitHub runner from that interactive desktop using run.cmd. Do not use Windows-service runner mode for the WGC soak.

## Recommended P1 acceptance run

First run: mode=interactive-wgc, duration_minutes=30, fps=60, lifecycle_cycles=100.

Then repeat with fps=30.

The workflow uploads p1-wgc-soak.log, p1-wgc-lifecycle.log, and the exact arssyut_p1_probe.exe used by the run.

## Evidence captured

The probe reports capture frames received, unread frames coalesced, busy-slot drops, rendered/reused/unavailable output slots, scheduler skipped intervals, compositor resource generation, private-memory start/end/max/delta, WGC callback p50/p95/p99, compositor CPU p50/p95/p99, and compositor GPU p50/p95/p99.

## Self-hosted runner setup

In the repository open Settings > Actions > Runners > New self-hosted runner, choose Windows x64, and follow GitHub's generated registration commands.

For P1 capture validation:

1. Sign in normally to the Windows test machine.
2. Keep the desktop session active and unlocked.
3. Register the runner.
4. Add custom label arssyut-interactive.
5. Launch the runner with run.cmd from that desktop session.
6. Leave that terminal open while the soak workflow runs.

A dedicated test PC is preferable because display disconnects, sleep/lock, RDP/session transitions, GPU resets, and topology changes can affect capture results.
