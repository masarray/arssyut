<p align="center"><img src="assets/favicon/web-app-manifest-192x192.png?v=c8f49853" alt="Arssyut product logo" width="80" height="80"></p>

**Current release source (13 October 2026):** Windows A/V + native webcam PIP, adjustable masks and logo overlay. Historical RC1/RC2 were older preview builds. See [PR #127](https://github.com/masarray/arssyut/pull/127).

<h1 align="center">Arssyut</h1>
<p align="center"><strong>Enhanced Screen Recorder for Windows</strong></p>
<p align="center">Capture your screen. Stay in the flow.</p>
<p align="center"><a href="https://masarray.github.io/arssyut/">Website</a> · <a href="https://masarray.github.io/arssyut/download/">Downloads</a> · <a href="https://github.com/masarray/arssyut/releases">GitHub Releases</a> · <a href="https://github.com/masarray/arssyut/issues">Support</a></p>

<p align="center"><img alt="Windows x64" src="https://img.shields.io/badge/Windows-x64-303c4b?style=flat-square"> <a href="https://github.com/masarray/arssyut/actions/workflows/ci.yml"><img alt="Windows build status" src="https://github.com/masarray/arssyut/actions/workflows/ci.yml/badge.svg"></a></p>

Arssyut is a focused Windows screen recorder for tutorials, software demos, walkthroughs, and everyday captures. Choose what to record, start capturing, and save an MP4—without a crowded workspace.

> [!IMPORTANT]
> **Hardware recording evidence:** The 13 October 2026 real 1080p60 H.264/AAC MP4 decoded successfully with webcam plus both System Audio and Microphone (2,675 packets each), two Pause/Resume operations, no encoder errors, zero webcam GPU copy failures and one skipped frame. This acceptance covers the tested PC setup, not every device or multi-hour session. Public Installer and Portable must come from the same checked Windows release.

## What can Arssyut do?

| Feature | Current status |
| --- | --- |
| Record an entire display | Available |
| Capture a window or selected region | Available |
| Save and open MP4 recordings | Available |
| Mouse click highlighting and shortcut visualization | Available |
| Zoom/presentation controls | Available, subject to capture mode |
| Remember user preferences | Available |
| System audio and microphone recording | Available, with Windows playback endpoint selection |
| Audio mixer, volume, mute and stereo level meters | Implemented |
| Webcam recording and preview | Available, with live Camera PIP |
| Game capture | Not yet available as a finished feature |

The application you launch is **Arssyut.UI.exe**, the compact Windows GUI. It does not require opening a Command Prompt.

## Download & get started

1. Visit the **[Download page](https://masarray.github.io/arssyut/download/)**, which selects the newest complete matching official Windows release; or inspect [GitHub Releases](https://github.com/masarray/arssyut/releases).
2. Choose **Setup EXE** for the Inno installer, or **Portable EXE** to run one standalone binary without extracting a ZIP. Both buttons always reference the same GitHub Release.
3. Launch Arssyut, select **Display**, **Window** or **Region**, enable **System audio** and/or **Microphone** as needed, and press **Start**.
4. Click **Stop** to finalize the MP4, then choose **Open** or **Folder** to review it.

The Download page selects the **newest complete** published audio-accepted Windows release pair dynamically (Setup EXE and portable single EXE from the same tag). Acceptance is explicitly indicated on a dedicated line `ARSSYUT_AUDIO_HARDWARE_ACCEPTED` in the official release notes after hardware signoff; SHA-256 and green CI alone do not prove audio fidelity. If GitHub API metadata is unavailable, the page falls back to GitHub Releases without inventing a downloadable binary.

### Audio and webcam

**System audio and microphone recording are implemented.** The Windows audio-enabled recorder supports source toggles, recorded gain/mute controls, stereo input meters, and device selection. The earlier video-only CI artifact is only a regression/reference build; **do not distribute it as the audio-capable public application**.

**Webcam recording and live preview are implemented.** Camera PIP supports Circle, Rounded Rectangle, two diagonal masks, Mask Width with Square preset, and GPU-composited PNG logo overlay.

## Questions and support

**Does it work on Windows 10 and 11?** Arssyut targets Windows x64. Hardware capabilities and Windows capture support can affect device compatibility.

**Do I need to install it?** No. The published Windows **portable .exe** can launch directly. The **Setup .exe** installs Start-menu shortcuts instead.

**Where are bugs reported?** Open a [GitHub Issue](https://github.com/masarray/arssyut/issues) and include your Windows version, capture mode, reproduction steps, and relevant diagnostics.

## For contributors and technical readers

Arssyut combines an Avalonia Windows interface with a native C++ capture/processing engine. Development emphasizes bounded real-time work, GPU-based presentation, explicit A/V clock ownership, and regression testing.

| Resource | Description |
| --- | --- |
| [Architecture](docs/ARCHITECTURE.md) | Capture, graphics, input, audio and encoding design |
| [Engineering roadmap](docs/ROADMAP.md) | Planned stages and acceptance gates |
| [Development handoff](docs/CURRENT_HANDOFF.md) | Current technical status and constraints |
| [Build / packaging](packaging/windows/README.md) | Windows portable and installer candidate workflows |
| [Third-party notices](docs/P6UI_THIRD_PARTY_NOTICES.md) | Dependency attribution and notices |

To run the native regression suite on a compatible Windows development machine:

<pre><code>cmake --preset windows-x64
cmake --build --preset windows-release --parallel
ctest --preset windows-release</code></pre>

The video-only CI package remains a regression reference, **not** the public audio-enabled application. Keep future installer and portable release assets sourced from the same verified audio-enabled binary.

### Branding

The official brand identity is the updated [assets/favicon/](assets/favicon) set (source commit `c8f49853`). The landing page, Downloads, GitHub README, Avalonia window/app logo, native Windows EXEs, Inno Setup, Start menu shortcuts and uninstall entry all consume this same branded icon set. Previously published v0.1.0 binaries retain their original embedded icon; a new version must be built to refresh executable resources.

### License and redistribution

Arssyut is licensed under **GPL-2.0-or-later** (see [LICENSE](LICENSE)). Windows distributions include third-party notices, including FFmpeg. Redistribution must respect the license terms of all included dependencies.
