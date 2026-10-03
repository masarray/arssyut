using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
using Avalonia;
using Avalonia.Controls;

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
    bool IsPrimary = false);

public static class SourcePreviewCatalog
{
    public static IReadOnlyList<PreviewSourceItem> Enumerate(
        PreviewCaptureMode mode,
        Screens screens,
        IntPtr ownWindow)
    {
        return mode switch
        {
            PreviewCaptureMode.Display =>
                EnumerateDisplays(mode, screens),
            PreviewCaptureMode.Region =>
                EnumerateDisplays(mode, screens),
            PreviewCaptureMode.Window =>
                EnumerateWindows(mode, ownWindow),
            PreviewCaptureMode.Game =>
                EnumerateWindows(mode, ownWindow),
            _ =>
                Array.Empty<PreviewSourceItem>()
        };
    }

    private static IReadOnlyList<PreviewSourceItem> EnumerateDisplays(
        PreviewCaptureMode mode,
        Screens screens)
    {
        var result =
            new List<PreviewSourceItem>();

        var primary =
            screens.Primary;
        var index = 0;

        foreach (var screen in screens.All)
        {
            ++index;
            var bounds =
                screen.Bounds;
            var isPrimary =
                ReferenceEquals(screen, primary) ||
                screen.Equals(primary);

            var title =
                mode == PreviewCaptureMode.Region
                    ? $"Region on Display {index}"
                    : $"Display {index} · {bounds.Width} × {bounds.Height}";

            var subtitleParts =
                new List<string>();

            if (isPrimary)
                subtitleParts.Add("Primary");

            if (!string.IsNullOrWhiteSpace(screen.DisplayName))
                subtitleParts.Add(screen.DisplayName!);

            subtitleParts.Add(
                $"{Math.Round(screen.Scaling * 100):0}% scaling");

            result.Add(
                new PreviewSourceItem(
                    $"display:{index}",
                    mode,
                    title,
                    string.Join(" · ", subtitleParts),
                    bounds,
                    isPrimary));
        }

        return result;
    }

    private static IReadOnlyList<PreviewSourceItem> EnumerateWindows(
        PreviewCaptureMode mode,
        IntPtr ownWindow)
    {
        if (!OperatingSystem.IsWindows())
            return Array.Empty<PreviewSourceItem>();

        var result =
            new List<PreviewSourceItem>();

        EnumWindows(
            (window, _) =>
            {
                if (window == ownWindow ||
                    !IsWindowVisible(window) ||
                    GetWindow(window, GW_OWNER) != IntPtr.Zero)
                    return true;

                var exStyle =
                    GetWindowLongPtr(
                        window,
                        GWL_EXSTYLE).ToInt64();

                if ((exStyle & WS_EX_TOOLWINDOW) != 0)
                    return true;

                if (DwmGetWindowAttribute(
                        window,
                        DWMWA_CLOAKED,
                        out var cloaked,
                        Marshal.SizeOf<int>()) == 0 &&
                    cloaked != 0)
                    return true;

                var length =
                    GetWindowTextLengthW(window);

                if (length <= 0)
                    return true;

                var builder =
                    new StringBuilder(
                        Math.Min(length + 1, 260));

                if (GetWindowTextW(
                        window,
                        builder,
                        builder.Capacity) <= 0)
                    return true;

                var title =
                    builder.ToString().Trim();

                if (string.IsNullOrWhiteSpace(title))
                    return true;

                if (!TryGetWindowBounds(
                        window,
                        out var bounds))
                    return true;

                if (result.Count >= 24)
                    return false;

                var compactTitle =
                    title.Length > 72
                        ? title[..69] + "…"
                        : title;

                var prefix =
                    mode == PreviewCaptureMode.Game
                        ? "Application"
                        : "Window";

                var subtitle =
                    mode == PreviewCaptureMode.Game
                        ? $"{bounds.Width} × {bounds.Height} · game-source candidate"
                        : $"{bounds.Width} × {bounds.Height} · visible top-level window";

                result.Add(
                    new PreviewSourceItem(
                        $"{prefix.ToLowerInvariant()}:{window.ToInt64():X}",
                        mode,
                        compactTitle,
                        subtitle,
                        bounds));

                return true;
            },
            IntPtr.Zero);

        return result;
    }

    private static bool TryGetWindowBounds(
        IntPtr window,
        out PixelRect bounds)
    {
        bounds = default;

        if (DwmGetWindowAttribute(
                window,
                DWMWA_EXTENDED_FRAME_BOUNDS,
                out RECT rect,
                Marshal.SizeOf<RECT>()) != 0)
        {
            if (!GetWindowRect(
                    window,
                    out rect))
                return false;
        }

        var width =
            rect.Right - rect.Left;
        var height =
            rect.Bottom - rect.Top;

        if (width <= 0 || height <= 0)
            return false;

        bounds =
            new PixelRect(
                rect.Left,
                rect.Top,
                width,
                height);

        return true;
    }

    private delegate bool EnumWindowsProc(
        IntPtr hwnd,
        IntPtr lParam);

    [StructLayout(LayoutKind.Sequential)]
    private struct RECT
    {
        public int Left;
        public int Top;
        public int Right;
        public int Bottom;
    }

    private const int GW_OWNER = 4;
    private const int GWL_EXSTYLE = -20;
    private const long WS_EX_TOOLWINDOW = 0x00000080L;
    private const int DWMWA_EXTENDED_FRAME_BOUNDS = 9;
    private const int DWMWA_CLOAKED = 14;

    [DllImport("user32.dll")]
    private static extern bool EnumWindows(
        EnumWindowsProc callback,
        IntPtr lParam);

    [DllImport("user32.dll")]
    private static extern bool IsWindowVisible(
        IntPtr hwnd);

    [DllImport("user32.dll")]
    private static extern IntPtr GetWindow(
        IntPtr hwnd,
        int command);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern int GetWindowTextLengthW(
        IntPtr hwnd);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern int GetWindowTextW(
        IntPtr hwnd,
        StringBuilder text,
        int maxCount);

    [DllImport("user32.dll")]
    private static extern bool GetWindowRect(
        IntPtr hwnd,
        out RECT rect);

    [DllImport("dwmapi.dll")]
    private static extern int DwmGetWindowAttribute(
        IntPtr hwnd,
        int attribute,
        out RECT value,
        int size);

    [DllImport("dwmapi.dll")]
    private static extern int DwmGetWindowAttribute(
        IntPtr hwnd,
        int attribute,
        out int value,
        int size);

    private static IntPtr GetWindowLongPtr(
        IntPtr hwnd,
        int index) =>
        IntPtr.Size == 8
            ? GetWindowLongPtr64(hwnd, index)
            : new IntPtr(
                GetWindowLong32(hwnd, index));

    [DllImport("user32.dll", EntryPoint = "GetWindowLongW")]
    private static extern int GetWindowLong32(
        IntPtr hwnd,
        int index);

    [DllImport("user32.dll", EntryPoint = "GetWindowLongPtrW")]
    private static extern IntPtr GetWindowLongPtr64(
        IntPtr hwnd,
        int index);
}
