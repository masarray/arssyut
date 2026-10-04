using System;
using System.Runtime.InteropServices;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Platform;
using Avalonia.Threading;

namespace Arssyut.UI;

public sealed partial class CaptureBoundaryWindow : Window
{
    private bool _editable;
    private bool _applyingBounds;
    private double _scale = 1.0;

    public event EventHandler? RegionChanged;

    public CaptureBoundaryWindow()
    {
        InitializeComponent();

        PositionChanged +=
            (_, _) => PublishRegionChanged();

        PropertyChanged +=
            (_, e) =>
            {
                if (e.Property == ClientSizeProperty)
                {
                    PublishRegionChanged();

                    if (IsVisible)
                        ApplyNativeWindowRegion();
                }
            };

        Opened +=
            (_, _) =>
            {
                ApplyNativeOverlayFlags();
                ApplyCaptureExclusion();
                ApplyNativeWindowRegion();
            };
    }

    public PixelRect CurrentPixelRect =>
        new(
            Position.X,
            Position.Y,
            Math.Max(1, (int)Math.Round(ClientSize.Width * _scale)),
            Math.Max(1, (int)Math.Round(ClientSize.Height * _scale)));

    public void ShowForRect(
        PixelRect rect,
        double scaling,
        bool editable)
    {
        _applyingBounds = true;
        _editable = editable;
        _scale = scaling > 0.0 ? scaling : 1.0;

        CanResize = editable;
        IsHitTestVisible = editable;
        RegionChrome.IsVisible = editable;

        Position =
            new PixelPoint(
                rect.X,
                rect.Y);

        Width =
            Math.Max(
                MinWidth,
                rect.Width / _scale);
        Height =
            Math.Max(
                MinHeight,
                rect.Height / _scale);

        RegionSizeText.Text =
            $"{rect.Width} × {rect.Height}";

        if (!IsVisible)
            Show();

        ApplyNativeOverlayFlags();
        ApplyCaptureExclusion();
        ApplyNativeWindowRegion();

        // Avalonia may complete the native resize one dispatcher turn after Show().
        // Re-apply the frame-only region using the final pixel client size.
        Dispatcher.UIThread.Post(
            ApplyNativeWindowRegion,
            DispatcherPriority.Background);

        _applyingBounds = false;
    }

    public void HideBoundary() =>
        Hide();

    private void MovePill_OnPointerPressed(
        object? sender,
        PointerPressedEventArgs e)
    {
        if (!_editable ||
            !e.GetCurrentPoint(this).
                Properties.
                IsLeftButtonPressed)
            return;

        BeginMoveDrag(e);
    }

    private void ResizeHandle_OnPointerPressed(
        object? sender,
        PointerPressedEventArgs e)
    {
        if (!_editable ||
            sender is not Control control ||
            !Enum.TryParse<WindowEdge>(
                control.Tag?.ToString(),
                out var edge))
            return;

        if (e.GetCurrentPoint(this).
                Properties.
                IsLeftButtonPressed)
        {
            BeginResizeDrag(
                edge,
                e);
        }
    }

    private void PublishRegionChanged()
    {
        if (_applyingBounds ||
            !_editable)
            return;

        var rect =
            CurrentPixelRect;

        RegionSizeText.Text =
            $"{rect.Width} × {rect.Height}";

        RegionChanged?.Invoke(
            this,
            EventArgs.Empty);
    }

    private void ApplyCaptureExclusion()
    {
        if (!OperatingSystem.IsWindows())
            return;

        var handle =
            TryGetPlatformHandle()?.Handle ??
            IntPtr.Zero;

        if (handle != IntPtr.Zero)
        {
            SetWindowDisplayAffinity(
                handle,
                WDA_EXCLUDEFROMCAPTURE);
        }
    }

    private void ApplyNativeWindowRegion()
    {
        if (!OperatingSystem.IsWindows())
            return;

        var handle =
            TryGetPlatformHandle()?.Handle ??
            IntPtr.Zero;

        if (handle == IntPtr.Zero)
            return;

        if (_editable)
        {
            // Region editing needs the complete HWND so its move/resize surfaces
            // remain interactive.
            SetWindowRgn(
                handle,
                IntPtr.Zero,
                true);
            return;
        }

        // A transparent full-screen HWND still participates in native hit testing.
        // Do not rely on WS_EX_TRANSPARENT for a monitor-sized overlay: physically
        // remove the interior from the HWND region so Windows routes input directly
        // to the captured desktop/application underneath.
        var width =
            Math.Max(
                1,
                (int)Math.Round(
                    ClientSize.Width *
                    _scale));
        var height =
            Math.Max(
                1,
                (int)Math.Round(
                    ClientSize.Height *
                    _scale));

        const int framePixels = 6;

        var outer =
            CreateRectRgn(
                0,
                0,
                width,
                height);
        var inner =
            CreateRectRgn(
                Math.Min(
                    framePixels,
                    width),
                Math.Min(
                    framePixels,
                    height),
                Math.Max(
                    framePixels,
                    width - framePixels),
                Math.Max(
                    framePixels,
                    height - framePixels));

        if (outer == IntPtr.Zero ||
            inner == IntPtr.Zero)
        {
            if (outer != IntPtr.Zero)
                DeleteObject(outer);
            if (inner != IntPtr.Zero)
                DeleteObject(inner);
            return;
        }

        CombineRgn(
            outer,
            outer,
            inner,
            RGN_DIFF);

        DeleteObject(
            inner);

        // On success Windows owns outer and will delete it later.
        if (SetWindowRgn(
                handle,
                outer,
                true) == 0)
        {
            DeleteObject(
                outer);
        }
    }

    private void ApplyNativeOverlayFlags()
    {
        if (!OperatingSystem.IsWindows())
            return;

        var handle =
            TryGetPlatformHandle()?.Handle ??
            IntPtr.Zero;

        if (handle == IntPtr.Zero)
            return;

        var style =
            GetWindowLongPtr(
                handle,
                GWL_EXSTYLE).ToInt64();

        style |=
            WS_EX_TOOLWINDOW |
            WS_EX_NOACTIVATE;

        if (_editable)
            style &= ~WS_EX_TRANSPARENT;
        else
            style |= WS_EX_TRANSPARENT;

        SetWindowLongPtr(
            handle,
            GWL_EXSTYLE,
            new IntPtr(style));
    }

    private const int GWL_EXSTYLE = -20;
    private const int RGN_DIFF = 4;
    private const long WS_EX_TRANSPARENT = 0x00000020L;
    private const long WS_EX_TOOLWINDOW = 0x00000080L;
    private const long WS_EX_NOACTIVATE = 0x08000000L;
    private const uint WDA_EXCLUDEFROMCAPTURE = 0x00000011;

    [DllImport("gdi32.dll")]
    private static extern IntPtr CreateRectRgn(
        int left,
        int top,
        int right,
        int bottom);

    [DllImport("gdi32.dll")]
    private static extern int CombineRgn(
        IntPtr destination,
        IntPtr source1,
        IntPtr source2,
        int combineMode);

    [DllImport("gdi32.dll")]
    private static extern bool DeleteObject(
        IntPtr objectHandle);

    [DllImport("user32.dll")]
    private static extern int SetWindowRgn(
        IntPtr hwnd,
        IntPtr region,
        bool redraw);

    [DllImport("user32.dll")]
    private static extern bool SetWindowDisplayAffinity(
        IntPtr hwnd,
        uint affinity);

    private static IntPtr GetWindowLongPtr(
        IntPtr hwnd,
        int index) =>
        IntPtr.Size == 8
            ? GetWindowLongPtr64(hwnd, index)
            : new IntPtr(
                GetWindowLong32(hwnd, index));

    private static IntPtr SetWindowLongPtr(
        IntPtr hwnd,
        int index,
        IntPtr value) =>
        IntPtr.Size == 8
            ? SetWindowLongPtr64(hwnd, index, value)
            : new IntPtr(
                SetWindowLong32(
                    hwnd,
                    index,
                    value.ToInt32()));

    [DllImport("user32.dll", EntryPoint = "GetWindowLongW")]
    private static extern int GetWindowLong32(
        IntPtr hwnd,
        int index);

    [DllImport("user32.dll", EntryPoint = "GetWindowLongPtrW")]
    private static extern IntPtr GetWindowLongPtr64(
        IntPtr hwnd,
        int index);

    [DllImport("user32.dll", EntryPoint = "SetWindowLongW")]
    private static extern int SetWindowLong32(
        IntPtr hwnd,
        int index,
        int value);

    [DllImport("user32.dll", EntryPoint = "SetWindowLongPtrW")]
    private static extern IntPtr SetWindowLongPtr64(
        IntPtr hwnd,
        int index,
        IntPtr value);
}
