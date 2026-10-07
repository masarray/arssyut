using System;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Media;
using Arssyut.UI.Interop;

namespace Arssyut.UI;

public sealed partial class RecordingCountdownWindow : Window
{
    private readonly PixelRect _targetBounds;

    public RecordingCountdownWindow(
        PixelRect targetBounds)
    {
        _targetBounds = targetBounds;
        InitializeComponent();

        TransparencyLevelHint =
        [
            WindowTransparencyLevel.Transparent,
            WindowTransparencyLevel.AcrylicBlur,
            WindowTransparencyLevel.None
        ];

        Opened +=
            (_, _) =>
            {
                PlaceOnTarget();

                // Countdown is presentation UI only. WGC is already warm while
                // this surface is visible, so exclusion is mandatory.
                WindowsCaptureExclusion.TryApply(
                    this);
            };
    }

    public void ShowNumber(
        int value)
    {
        Background =
            Brush.Parse("#A6000000");
        LeaderRing.IsVisible =
            true;
        HorizontalGuide.IsVisible =
            true;
        VerticalGuide.IsVisible =
            true;
        CountdownText.FontSize =
            150;
        CountdownText.Text =
            value.ToString();
    }

    public void ShowAction()
    {
        // Frame zero must see the normal desktop. Only ACTION remains visible
        // to the user and this HWND is excluded from capture.
        Background =
            Brushes.Transparent;
        LeaderRing.IsVisible =
            false;
        HorizontalGuide.IsVisible =
            false;
        VerticalGuide.IsVisible =
            false;
        CountdownText.FontSize =
            74;
        CountdownText.Text =
            "ACTION!";
    }

    private void PlaceOnTarget()
    {
        var center =
            new PixelPoint(
                _targetBounds.X +
                    Math.Max(
                        0,
                        _targetBounds.Width / 2),
                _targetBounds.Y +
                    Math.Max(
                        0,
                        _targetBounds.Height / 2));

        var screen =
            Screens.ScreenFromPoint(
                center) ??
            Screens.Primary;

        var scale =
            Math.Max(
                0.25,
                screen?.Scaling ??
                    1.0);

        Position =
            new PixelPoint(
                _targetBounds.X,
                _targetBounds.Y);
        Width =
            Math.Max(
                1,
                _targetBounds.Width /
                    scale);
        Height =
            Math.Max(
                1,
                _targetBounds.Height /
                    scale);
    }
}
