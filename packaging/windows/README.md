# Arssyut Windows packaging

The canonical visual identity is **`assets/favicon/`** on `main`.
Do not maintain a second icon set in the installer, UI, or landing website.

- `favicon.ico`: embedded Windows PE application icon, Avalonia window icon,
  Inno installer executable/uninstaller identity, and Windows shortcuts.
- `android-chrome-192x192.png`: GUI title/product logo and README.
- `android-chrome-512x512.png`: landing product hero logo.
- `favicon-{16,32}x{16,32}.png`, `apple-touch-icon.png` and
  `site.webmanifest`: root `index.html` browser identity.

## Portable build

The audio-enabled release candidate uses one self-contained single-file
`Arssyut.UI.exe` payload (the actual Avalonia GUI), renamed as
`Arssyut-<version>-portable-win-x64.exe` for public download. This is an
**actual standalone EXE, not a ZIP**. The Setup EXE is built from the same
verified audio-enabled GUI payload. The official app icon is embedded in PE.

## Installer candidate (manual, NOT a public release)

Run [Windows Installer Candidate](https://github.com/masarray/arssyut/actions/workflows/windows-installer-candidate.yml)
using **Run workflow** from the desired SHA/branch. It compiles the default
**video-only** native bridge and the same Avalonia portable publish, then
packages that *same* directory with `packaging/windows/Arssyut.iss` using
Inno Setup 6. Output artifacts: `Arssyut.UI.exe` portable folder and
`Arssyut-Setup-*-win-x64.exe`. No automatic release, tag, or deployment.

The installer deliberately installs per-user (no UAC), uses the EXE's
embedded icon for Start/Desktop shortcuts, and the same canonical ICO for
the Setup executable. The internal hardware-audio preview artifact remains
separate and **must not** be presented as the public stable installer.

## Release gate

Automatic CI is not proof of real-device Windows audio acceptance. Before
publishing an audio-enabled release, validate Mic-only, System-only, dual
input stereo, long MP4s, A/V duration sync, Stop/Finalize, repeated
Start/Stop, and accurate persisted mix/fader behavior on real hardware.

## Release candidate packaging

The [Windows Release Candidate EXEs](../../.github/workflows/windows-release-exes.yml) workflow deliberately does not publish a GitHub Release automatically. Run it from `main` with a successful exact-commit audio-enabled CI run ID and a candidate version such as `0.1.0-rc.1`. It verifies source provenance and produces exactly two executable assets using one byte-identical standalone payload:

- `Arssyut-Setup-0.1.0-rc.1-win-x64.exe` — Inno installer.
- `Arssyut-0.1.0-rc.1-portable-win-x64.exe` — direct standalone GUI, no ZIP.

It also produces SHA256SUMS. The preview **v0.1.0-rc.1 was published on 2026-10-10**, with both EXEs and checksums. It remains labeled prerelease, not stable. System audio and microphone are implemented and user-confirmed; **webcam is not implemented**. Before future stable promotion, verify the installed GUI, portable EXE, real MP4 and checksums and capture specific evidence rather than retaining obsolete claims that audio still needs implementation. The download website must only link to assets on actual GitHub Releases.
