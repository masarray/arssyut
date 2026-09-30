ARSSYUT — EARLY GUI RECORDER BUILD
==================================

This is the actual Arssyut GUI recorder application.

TEST IT LIKE A REAL APP
-----------------------
1. Double-click arssyut.exe
2. Choose a Display or Window
3. Choose 60 FPS or 30 FPS
4. Click Record
5. Use the computer normally for 10–30 seconds
6. Click Stop
7. Wait until status says "Recording saved"
8. Click Open Recording

OUTPUT
------
%USERPROFILE%\Videos\Arssyut\Arssyut-YYYYMMDD-HHMMSS.mp4

DIAGNOSTICS
-----------
A sidecar diagnostics file is written next to the recording:

Arssyut-YYYYMMDD-HHMMSS.mp4.diagnostics.json

Please send BOTH the MP4 and the diagnostics JSON when reporting recording
quality, dropped-frame, lag, encoder, or memory issues.

The diagnostics include:
- capture frames received/replaced/dropped
- rendered/reused/skipped frames
- encoder submitted/backpressure
- capture callback p95
- compositor CPU/GPU p95
- process memory
- output file size
- recorder status/error details

CURRENT SCOPE
-------------
Already real:
- Win32 GUI
- monitor/window source picker
- Windows Graphics Capture
- D3D11 GPU composition
- Media Foundation H.264
- MP4 file output
- 30/60 FPS
- internal diagnostics

Next layers:
- system + microphone audio
- ArZoom smart zoom/click
- keyboard-action visualizer
- ArVisual grading
