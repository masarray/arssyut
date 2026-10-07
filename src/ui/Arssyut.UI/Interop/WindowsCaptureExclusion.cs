using System;
using System.Runtime.InteropServices;
using Avalonia.Controls;

namespace Arssyut.UI.Interop;

/// <summary>
/// Presentation-only Win32 policy for recorder UI surfaces. This does not own
/// capture; it only asks Windows not to include the given top-level UI HWND in
/// screen capture.
/// </summary>
internal static class WindowsCaptureExclusion
{
    private const uint WdaMonitor = 0x00000001;
    private const uint WdaExcludeFromCapture = 0x00000011;

    public static bool TryApply(Window window)
    {
        if (!OperatingSystem.IsWindows())
            return false;

        var handle =
            window.TryGetPlatformHandle()?.Handle ??
            IntPtr.Zero;

        if (handle == IntPtr.Zero)
            return false;

        // WDA_EXCLUDEFROMCAPTURE is the preferred Windows 10 2004+ behavior.
        // WDA_MONITOR is a bounded compatibility fallback: if exclusion is not
        // supported, the controller content is still protected rather than
        // silently appearing in the recording.
        return SetWindowDisplayAffinity(
                   handle,
                   WdaExcludeFromCapture) ||
               SetWindowDisplayAffinity(
                   handle,
                   WdaMonitor);
    }

    [DllImport(
        "user32.dll",
        SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool SetWindowDisplayAffinity(
        IntPtr window,
        uint affinity);
}
