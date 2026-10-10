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

GitHub `CI / P6UI Avalonia Windows x64` builds the self-contained, single-file
`Arssyut.UI.exe`. Unzip the artifact and double-click it; it is the actual
Avalonia Windows product. The official app icon is compiled into its PE headers.

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
