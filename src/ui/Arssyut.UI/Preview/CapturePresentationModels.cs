using Avalonia;

namespace Arssyut.UI.Preview;

public enum PreviewCaptureMode
{
    Display,
    Window,
    Region,
    Game
}

public sealed record PreviewSourceItem(
    string Id,
    PreviewCaptureMode Mode,
    string Title,
    string Subtitle,
    PixelRect Bounds,
    bool IsPrimary = false,
    ulong NativeToken = 0);
