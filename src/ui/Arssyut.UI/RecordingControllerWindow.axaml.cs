using System;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Media;
using Avalonia.Threading;
using Arssyut.UI.Preview;
using Lucide.Avalonia;

namespace Arssyut.UI;

public sealed partial class RecordingControllerWindow : Window
{
    private readonly PreviewRecorderSession _session;
    private readonly DispatcherTimer _timer;
    private bool _closingFromStop;

    public event EventHandler? StopRequested;

    public RecordingControllerWindow(
        PreviewRecorderSession session)
    {
        _session = session;

        InitializeComponent();

        TransparencyLevelHint =
        [
            WindowTransparencyLevel.AcrylicBlur,
            WindowTransparencyLevel.Mica,
            WindowTransparencyLevel.None
        ];

        _timer = new DispatcherTimer
        {
            Interval = TimeSpan.FromMilliseconds(250)
        };
        _timer.Tick += (_, _) => RefreshState();

        Opened += (_, _) =>
        {
            PlaceNearTopCenter();
            _timer.Start();
            RefreshState();
        };

        Closed += (_, _) =>
        {
            _timer.Stop();

            if (!_closingFromStop &&
                _session.Phase is PreviewRecordingPhase.Recording or PreviewRecordingPhase.Paused)
            {
                _session.Stop();
                StopRequested?.Invoke(this, EventArgs.Empty);
            }
        };
    }

    private void PlaceNearTopCenter()
    {
        var screen = Screens.Primary;
        if (screen is null)
            return;

        var scale = screen.Scaling;
        var widthPixels =
            (int)Math.Round(Width * scale);

        Position = new PixelPoint(
            screen.WorkingArea.X +
                Math.Max(
                    0,
                    (screen.WorkingArea.Width - widthPixels) / 2),
            screen.WorkingArea.Y +
                (int)Math.Round(18 * scale));
    }

    private void Surface_OnPointerPressed(
        object? sender,
        PointerPressedEventArgs e)
    {
        if (e.GetCurrentPoint(this).Properties.IsLeftButtonPressed)
            BeginMoveDrag(e);
    }

    private void Window_OnKeyDown(
        object? sender,
        KeyEventArgs e)
    {
        if (e.Key == Key.F9)
        {
            RequestStop();
            e.Handled = true;
        }
        else if (e.Key == Key.F10)
        {
            _session.TogglePause();
            RefreshState();
            e.Handled = true;
        }
    }

    private void Mic_OnClick(
        object? sender,
        RoutedEventArgs e)
    {
        _session.MicrophoneEnabled =
            !_session.MicrophoneEnabled;
        RefreshState();
    }

    private void Camera_OnClick(
        object? sender,
        RoutedEventArgs e)
    {
        _session.CameraEnabled =
            !_session.CameraEnabled;
        RefreshState();
    }

    private void Pause_OnClick(
        object? sender,
        RoutedEventArgs e)
    {
        _session.TogglePause();
        RefreshState();
    }

    private void Stop_OnClick(
        object? sender,
        RoutedEventArgs e) =>
        RequestStop();

    private void RequestStop()
    {
        _session.Stop();
        RefreshState();

        _closingFromStop = true;
        StopRequested?.Invoke(this, EventArgs.Empty);
        Close();
    }

    private void RefreshState()
    {
        var paused =
            _session.Phase ==
                PreviewRecordingPhase.Paused;

        PhaseText.Text =
            paused
                ? "PAUSED"
                : "REC";

        PhaseText.Foreground =
            Brush.Parse(
                paused
                    ? "#F1B85B"
                    : "#FF4D57");

        PauseIcon.Kind =
            paused
                ? LucideIconKind.Play
                : LucideIconKind.Pause;

        PauseButton.SetValue(
            AutomationProperties.NameProperty,
            paused
                ? "Resume recording"
                : "Pause recording");

        TimerText.Text =
            FormatElapsed(
                _session.Elapsed);

        MicButton.Opacity =
            _session.MicrophoneEnabled
                ? 1.0
                : 0.48;

        CameraButton.Opacity =
            _session.CameraEnabled
                ? 1.0
                : 0.48;
    }

    private static string FormatElapsed(
        TimeSpan elapsed)
    {
        if (elapsed.TotalHours >= 1)
            return elapsed.ToString(@"hh\:mm\:ss");

        return elapsed.ToString(@"mm\:ss");
    }
}
