ARSSYUT — NATIVE SCREEN RECORDER (EARLY VERTICAL SLICE)
========================================================

This is the actual Arssyut GUI recorder application, not the P1 probe.

How to test:
1. Double-click arssyut.exe
2. Choose a Display or Window source
3. Choose 60 FPS or 30 FPS
4. Click Record
5. Use the computer normally for 10–30 seconds
6. Click Stop
7. Wait until status says "Recording saved"
8. Click Open Recording

Output:
  %USERPROFILE%\Videos\Arssyut\Arssyut-YYYYMMDD-HHMMSS.mp4

Diagnostics:
  The app writes a sidecar file next to the MP4:
  Arssyut-YYYYMMDD-HHMMSS.mp4.diagnostics.json

Please send BOTH the MP4 and diagnostics JSON when reporting recording quality
or performance problems. The diagnostics include capture/encode frame counters,
coalescing/backpressure, capture/compositor latency, memory usage, and encoder
submission statistics.

Current vertical-slice scope:
- real Win32 GUI
- display/window selection
- Windows Graphics Capture
- D3D11 GPU composition
- Media Foundation H.264 MP4 output
- 30/60 FPS
- internal diagnostics

Not yet included in this slice:
- system/microphone audio
- ArZoom smart zoom/click overlay
- keyboard overlay
- ArVisual color grading

Those features are added on top of this now-real recorder path.
