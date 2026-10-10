<p align="center"><img src="assets/favicon/android-chrome-192x192.png" alt="Arssyut product logo" width="80" height="80"></p>
<h1 align="center">Arssyut</h1>
<p align="center"><strong>Enhanced Screen Recorder for Windows</strong></p>
<p align="center">Capture your screen. Stay in the flow.</p>
<p align="center"><a href="https://masarray.github.io/arssyut/">Website</a> · <a href="https://masarray.github.io/arssyut/download/">Downloads</a> · <a href="https://github.com/masarray/arssyut/releases">GitHub Releases</a> · <a href="https://github.com/masarray/arssyut/issues">Support</a></p>

<p align="center"><img alt="Windows x64" src="https://img.shields.io/badge/Windows-x64-303c4b?style=flat-square"> <a href="https://github.com/masarray/arssyut/actions/workflows/ci.yml"><img alt="Windows build status" src="https://github.com/masarray/arssyut/actions/workflows/ci.yml/badge.svg"></a></p>

Arssyut is a focused Windows screen recorder for tutorials, software demos, walkthroughs, and everyday captures. Choose what to record, start capturing, and save an MP4—without a crowded workspace.

> [!IMPORTANT]
> **Release status:** Arssyut is preparing a public **video-only preview** with separate Windows Setup EXE and portable single-file EXE. Check [GitHub Releases](https://github.com/masarray/arssyut/releases) for availability. **Audio-enabled recording is not part of the public preview**: hardware acceptance remains outstanding.

## What can Arssyut do?

| Feature | Current status |
| --- | --- |
| Record an entire display | Available in the Windows video recorder |
| Capture a window or selected region | Available |
| Save and open MP4 recordings | Available |
| Mouse click highlighting and shortcut visualization | Available |
| Zoom/presentation controls | Available, subject to capture mode |
| Remember user preferences | Available |
| System audio and microphone recording | Internal audio-enabled testing build only |
| Game capture and webcam recording | Not yet available as finished features |

The application you launch is **Arssyut.UI.exe**, the compact Windows GUI. It does not require opening a Command Prompt.

## Download & get started

1. Visit the **[download page](https://masarray.github.io/arssyut/download/)** or [GitHub Releases](https://github.com/masarray/arssyut/releases). Only releases published there should be considered public downloads.
2. Choose a **Setup EXE** for Start menu integration or a **Portable EXE** for a single-file application with no installation or ZIP.
3. Open installed **Arssyut.UI.exe**, or double-click the portable downloaded `.exe`, then select **Display**, **Window**, or **Region**, and click **Start**.
4. Click **Stop** to finalize your MP4, then choose **Open** or **Folder** to review it.

**No public download showing?** The release might not yet be published or GitHub might be temporarily unavailable. The website checks official GitHub Releases for the latest published version each visit, and never substitutes internal Actions artifacts.

### About audio

The native system-audio and microphone pipeline is under acceptance testing for stereo capture, recorded volume/mute, synchronization, long sessions, and repeated Start/Stop. Audio testing uses a **separate build** named **arssyut-AUDIO-ENABLED-TEST-win-x64** in [GitHub Actions](https://github.com/masarray/arssyut/actions). These builds may require a GitHub login and are not stable public downloads.

If you open **arssyut-VIDEO-ONLY-avalonia-win-x64**, its audio toggles are intentionally unavailable. This is a different build, **not** a microphone configuration problem.

## Questions and support

**Does it work on Windows 10 and 11?** Arssyut targets Windows x64. Hardware capabilities and Windows capture support can affect device compatibility.

**Do I need to install it?** No. The portable EXE runs directly; the Setup EXE adds a Windows Start menu shortcut.

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

The default video-only reference build and experimental audio-enabled package have **different native capabilities** and must not be substituted for each other.

### Branding

The official product identity lives in [assets/favicon/](assets/favicon). The Windows executable, installer icon, website favicon, and product image reuse that source.

### License and redistribution

This repository currently does not contain a root software license file. Public access to source does not by itself grant reuse or redistribution rights. Third-party components have separate licenses and notices. A software license should be explicitly selected before positioning the repository as an open-source release.
