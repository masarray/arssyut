using System;
using System.Runtime.InteropServices;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Platform;

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
                    PublishRegionChanged();
            };

        Opened +=
            (_, _) =>
            {
                ApplyNativeOverlayFlags();
                ApplyCaptureExclusion();
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
    private const long WS_EX_TRANSPARENT = 0x00000020L;
    private const long WS_EX_TOOLWINDOW = 0x00000080L;
    private const long WS_EX_NOACTIVATE = 0x08000000L;
    private const uint WDA_EXCLUDEFROMCAPTURE = 0x00000011;

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
