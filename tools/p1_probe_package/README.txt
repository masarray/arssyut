ARSSYUT P1 NATIVE CAPTURE VALIDATION PACKAGE
=============================================

This package is NOT the final Arssyut screen recorder application.
It is a validation package for the P1 native capture engine.

For the easiest test:
  1. Double-click Run-Smoke-Test.cmd
  2. Wait 10 seconds
  3. The window will stay open and show PASS/FAIL
  4. Send p1-smoke-test.log if you want the result analyzed

For soak validation:
  - Run-30min-60fps.cmd
  - Run-30min-30fps.cmd

What it tests:
  - Windows Graphics Capture receives frames
  - bounded latest-frame handoff
  - canonical output scheduler
  - D3D11 GPU crop/scale compositor
  - frame replacement/drop counters
  - private-memory delta
  - capture callback latency
  - compositor CPU submit latency
  - compositor GPU execution latency

Important:
  - keep the Windows session unlocked
  - keep the monitor active
  - do not allow the PC to sleep
  - avoid switching to/from RDP during the test
  - the final user-facing GUI is a later milestone

The raw arssyut_p1_probe.exe is intentionally a console validation tool.
Use the .cmd files above instead of double-clicking the EXE directly.
