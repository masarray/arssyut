using System;
using Avalonia;
using Avalonia.Automation;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Media;
using Avalonia.Threading;
using Arssyut.UI.Interop;
using Arssyut.UI.Preview;
using Lucide.Avalonia;

namespace Arssyut.UI;

public sealed partial class RecordingControllerWindow : Window
{
    private readonly PreviewRecorderSession? _previewSession;
    private readonly NativeBridgeClient? _nativeBridge;
    private readonly SettingsPreviewState _settings;
    private DispatcherTimer _timer = null!;

    private readonly bool _nativeMicrophoneIntent;
    private readonly bool _nativeCameraIntent;

    private bool _closingFromStop;
    private bool _nativeStopRequested;

    public event EventHandler? StopRequested;

    public RecordingControllerWindow(
        PreviewRecorderSession session,
        SettingsPreviewState settings)
    {
        _previewSession = session;
        _settings = settings;

        InitializeController();
    }

    public RecordingControllerWindow(
        NativeBridgeClient nativeBridge,
        SettingsPreviewState settings,
        bool microphoneIntent,
        bool cameraIntent)
    {
        _nativeBridge = nativeBridge;
        _settings = settings;
        _nativeMicrophoneIntent =
            microphoneIntent;
        _nativeCameraIntent =
            cameraIntent;

        InitializeController();
    }

    private void InitializeController()
    {
        InitializeComponent();

        TransparencyLevelHint =
        [
            WindowTransparencyLevel.AcrylicBlur,
            WindowTransparencyLevel.Mica,
            WindowTransparencyLevel.None
        ];

        _timer = new DispatcherTimer
        {
            Interval =
                TimeSpan.FromMilliseconds(
                    200)
        };
        _timer.Tick +=
            (_, _) => RefreshState();

        ToolTip.SetTip(
            StopButton,
            $"Stop recording ({_settings.RecordHotkey})");

        if (_nativeBridge is null)
        {
            ToolTip.SetTip(
                PauseButton,
                $"Pause / Resume ({_settings.PauseHotkey})");
        }
        else
        {
            PauseButton.IsEnabled = true;
            // Both controls now invoke authoritative native session commands.
            // Camera remains unavailable until its compositor integration.
            MicButton.IsEnabled =
                _nativeMicrophoneIntent;
            CameraButton.IsEnabled =
                false;

            ToolTip.SetTip(
                PauseButton,
                $"Pause / Resume recording ({_settings.PauseHotkey})");
            ToolTip.SetTip(
                MicButton,
                _nativeMicrophoneIntent
                    ? $"Mute / Unmute recorded microphone ({_settings.MicrophoneHotkey})"
                    : "No microphone selected for this recording.");
            ToolTip.SetTip(
                CameraButton,
                _nativeCameraIntent
                    ? "Camera selected for this MP4 (mid-recording toggle not enabled)."
                    : "Camera was not selected for this recording.");
        }

        Opened +=
            (_, _) =>
            {
                PlaceNearTopCenter();

                // A visible recorder surface must never become part of a
                // Display/Region recording. Apply affinity only to this UI
                // HWND; capture remains entirely native.
                if (!WindowsCaptureExclusion.TryApply(this))
                {
                    ToolTip.SetTip(
                        this,
                        "Windows could not mark the floating controller as capture-excluded.");
                }

                _timer.Start();
                RefreshState();
            };

        Closing +=
            Controller_OnClosing;

        Closed +=
            (_, _) =>
            {
                _timer.Stop();

                if (_nativeBridge is null &&
                    !_closingFromStop &&
                    _previewSession?.Phase is
                        PreviewRecordingPhase.Recording or
                        PreviewRecordingPhase.Paused)
                {
                    _previewSession.Stop();
                    StopRequested?.Invoke(
                        this,
                        EventArgs.Empty);
                }
            };
    }

    private void Controller_OnClosing(
        object? sender,
        WindowClosingEventArgs e)
    {
        if (_nativeBridge is null ||
            _closingFromStop)
            return;

        NativeRecorderSnapshot snapshot;

        try
        {
            snapshot =
                _nativeBridge.Snapshot();
        }
        catch (Exception)
        {
            return;
        }

        if (IsNativeActive(
                snapshot.State))
        {
            e.Cancel = true;
            RequestStop();
        }
    }

    private void PlaceNearTopCenter()
    {
        var screen =
            Screens.Primary;
        if (screen is null)
            return;

        var scale =
            screen.Scaling;
        var widthPixels =
            (int)Math.Round(
                Width * scale);

        Position =
            new PixelPoint(
                screen.WorkingArea.X +
                    Math.Max(
                        0,
                        (screen.WorkingArea.Width -
                         widthPixels) /
                        2),
                screen.WorkingArea.Y +
                    (int)Math.Round(
                        18 * scale));
    }

    private void Surface_OnPointerPressed(
        object? sender,
        PointerPressedEventArgs e)
    {
        if (e.GetCurrentPoint(this).
                Properties.
                IsLeftButtonPressed)
            BeginMoveDrag(e);
    }

    private void Window_OnKeyDown(
        object? sender,
        KeyEventArgs e)
    {
        // Product-native recording is controlled by the bridge-owned global
        // hotkey. Keep focused KeyDown only for explicit interaction preview,
        // otherwise one F9 could request Stop twice.
        if (_nativeBridge is null &&
            HotkeyPreview.Matches(
                e,
                _settings.RecordHotkey))
        {
            RequestStop();
            e.Handled = true;
            return;
        }

        if (_nativeBridge is null &&
            HotkeyPreview.Matches(
                e,
                _settings.PauseHotkey))
        {
            _previewSession?.
                TogglePause();
            RefreshState();
            e.Handled = true;
        }
    }

    private void Mic_OnClick(
        object? sender,
        RoutedEventArgs e)
    {
        if (_nativeBridge is not null)
        {
            try
            {
                if (_nativeBridge.ToggleMicrophoneMute() ==
                    NativeBridgeStatus.Ok)
                    RefreshState();
            }
            catch (Exception)
            {
                // Recorder may be closing; never invent a muted UI state.
            }
            return;
        }
        if (_previewSession is null) return;
        _previewSession.MicrophoneEnabled =
            !_previewSession.MicrophoneEnabled;
        RefreshState();
    }

    private void Camera_OnClick(
        object? sender,
        RoutedEventArgs e)
    {
        if (_nativeBridge is not null ||
            _previewSession is null)
            return;

        _previewSession.CameraEnabled =
            !_previewSession.
                CameraEnabled;
        RefreshState();
    }

    private void Pause_OnClick(
        object? sender,
        RoutedEventArgs e)
    {
        if (_nativeBridge is not null)
        {
            try
            {
                if (_nativeBridge.TogglePause() == NativeBridgeStatus.Ok)
                    RefreshState();
            }
            catch (Exception)
            {
                // Writer teardown may be underway; no speculative UI state.
            }
            return;
        }

        _previewSession?.TogglePause();
        RefreshState();
    }

    private void Stop_OnClick(
        object? sender,
        RoutedEventArgs e) =>
        RequestStop();

    private void RequestStop()
    {
        if (_nativeBridge is not null)
        {
            if (_nativeStopRequested)
                return;

            NativeBridgeStatus status;

            try
            {
                status =
                    _nativeBridge.
                        StopRecording();
            }
            catch (Exception)
            {
                PhaseText.Text =
                    "ERROR";
                PhaseText.Foreground =
                    Brush.Parse(
                        "#FF6671");
                return;
            }

            if (status is
                NativeBridgeStatus.Ok or
                NativeBridgeStatus.InvalidState)
            {
                _nativeStopRequested =
                    true;
                StopButton.IsEnabled =
                    false;
                RefreshState();
            }

            return;
        }

        if (_previewSession is null)
            return;

        _previewSession.Stop();
        RefreshState();

        _closingFromStop = true;
        StopRequested?.Invoke(
            this,
            EventArgs.Empty);
        Close();
    }

    private void RefreshState()
    {
        if (_nativeBridge is not null)
        {
            RefreshNativeState();
            return;
        }

        RefreshPreviewState();
    }

    private void RefreshNativeState()
    {
        NativeRecorderSnapshot snapshot;

        try
        {
            snapshot =
                _nativeBridge!.Snapshot();
        }
        catch (Exception)
        {
            PhaseText.Text =
                "ERROR";
            PhaseText.Foreground =
                Brush.Parse(
                    "#FF6671");
            return;
        }

        PhaseText.Text =
            snapshot.State switch
            {
                NativeRecorderState.Preparing =>
                    "PREP",
                NativeRecorderState.Armed =>
                    "ARMED",
                NativeRecorderState.Recording =>
                    "REC",
                NativeRecorderState.Paused =>
                    "PAUSED",
                NativeRecorderState.Stopping =>
                    "STOP",
                NativeRecorderState.Finalizing =>
                    "SAVE",
                NativeRecorderState.Ready =>
                    "SAVED",
                NativeRecorderState.Failed =>
                    "ERROR",
                _ =>
                    "READY"
            };

        PhaseText.Foreground =
            Brush.Parse(
                snapshot.State switch
                {
                    NativeRecorderState.Failed =>
                        "#FF6671",
                    NativeRecorderState.Stopping or
                    NativeRecorderState.Finalizing or
                    NativeRecorderState.Paused =>
                        "#F1B85B",
                    NativeRecorderState.Ready =>
                        "#49D49D",
                    _ =>
                        "#FF5360"
                });

        PauseButton.IsEnabled =
            snapshot.State is NativeRecorderState.Recording or
                NativeRecorderState.Paused;
        PauseIcon.Kind = snapshot.State == NativeRecorderState.Paused
            ? LucideIconKind.Play : LucideIconKind.Pause;
        PauseButton.SetValue(
            AutomationProperties.NameProperty,
            snapshot.State == NativeRecorderState.Paused
                ? "Resume recording" : "Pause recording");

        TimerText.Text =
            FormatElapsed(
                TimeSpan.FromTicks(
                    Math.Max(
                        0,
                        snapshot.ElapsedTicks)));

        MicButton.IsEnabled =
            snapshot.MicrophoneEnabled &&
            snapshot.State is
                (NativeRecorderState.Recording or NativeRecorderState.Paused);
        MicIcon.Kind = snapshot.MicrophoneMuted
            ? LucideIconKind.MicOff
            : LucideIconKind.Mic;
        MicButton.Opacity =
            !snapshot.MicrophoneEnabled ? 0.42
            : snapshot.MicrophoneMuted ? 0.50 : 1.0;
        MicButton.SetValue(
            AutomationProperties.NameProperty,
            snapshot.MicrophoneMuted
                ? "Unmute recorded microphone"
                : "Mute recorded microphone");
        CameraButton.Opacity =
            _nativeCameraIntent
                ? 0.70
                : 0.42;

        if (snapshot.WorkerFinished &&
            snapshot.State is
                NativeRecorderState.Ready or
                NativeRecorderState.Failed)
        {
            CompleteNativeController();
        }
    }

    private void CompleteNativeController()
    {
        if (_closingFromStop)
            return;

        _closingFromStop = true;
        _timer.Stop();

        StopRequested?.Invoke(
            this,
            EventArgs.Empty);

        Close();
    }

    private void RefreshPreviewState()
    {
        if (_previewSession is null)
            return;

        var paused =
            _previewSession.Phase ==
                PreviewRecordingPhase.Paused;

        PhaseText.Text =
            paused
                ? "PAUSED"
                : "REC";

        PhaseText.Foreground =
            Brush.Parse(
                paused
                    ? "#F1B85B"
                    : "#FF5360");

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
                _previewSession.Elapsed);

        MicButton.Opacity =
            _previewSession.MicrophoneEnabled
                ? 1.0
                : 0.48;

        CameraButton.Opacity =
            _previewSession.CameraEnabled
                ? 1.0
                : 0.48;
    }

    private static bool IsNativeActive(
        NativeRecorderState state) =>
        state is
            NativeRecorderState.Preparing or
            NativeRecorderState.Armed or
            NativeRecorderState.Recording or
            NativeRecorderState.Paused or
            NativeRecorderState.Stopping or
            NativeRecorderState.Finalizing;

    private static string FormatElapsed(
        TimeSpan elapsed)
    {
        if (elapsed.TotalHours >= 1)
            return elapsed.ToString(
                @"hh\:mm\:ss");

        return elapsed.ToString(
            @"mm\:ss");
    }
}
