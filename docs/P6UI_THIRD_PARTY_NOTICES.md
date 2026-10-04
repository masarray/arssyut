# P6UI third-party notices

This UI preview uses the following third-party packages/assets.

## Avalonia UI

Packages:
- Avalonia.Desktop 12.1.3
- Avalonia.Themes.Fluent 12.1.3
- Avalonia.Fonts.Inter 12.1.3

Project: https://avaloniaui.net/
License: MIT

## Inter

Inter is embedded through the official Avalonia.Fonts.Inter package.

Project: https://rsms.me/inter/
License: SIL Open Font License 1.1

Arssyut does not redistribute loose font files in the source tree; the font is
consumed through the package and embedded into the published application.

## Lucide.Avalonia

Package: Lucide.Avalonia 0.2.23
Project: https://github.com/dme-compunet/Lucide.Avalonia
License: MIT

## Lucide icons

Project: https://lucide.dev/
License: ISC

The P6UI shell uses the real Lucide.Avalonia renderer. The old hand-drawn GDI
Lucide approximation remains only in the frozen native prototype and is not the
target presentation system.
