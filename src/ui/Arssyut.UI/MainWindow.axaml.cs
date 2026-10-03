using System;
using Avalonia;
using Avalonia.Automation;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Media;
using Arssyut.UI.Preview;
using Lucide.Avalonia;

namespace Arssyut.UI;

public sealed partial class MainWindow : Window
{
    private readonly Button[] _modeButtons;
    private readonly Button[] _microphoneOptions;
    private readonly Button[] _cameraOptions;
    private readonly PreviewRecorderSession _session = new();

    private RecordingControllerWindow? _controller;
    private string _microphoneDevice =
        "Hi-Fi Cable Output (VB-Audio Virtual Cable)";
    private string _cameraDevice =
        "USB2.0 HD UVC Webcam";

    public MainWindow(
        bool stressLongNames = false,
        bool autoStartRecording = false)
    {
        InitializeComponent();

        TransparencyLevelHint =
        [
            WindowTransparencyLevel.Mica,
            WindowTransparencyLevel.AcrylicBlur,
            WindowTransparencyLevel.None
        ];

        _modeButtons =
        [
            ModeDisplay,
            ModeWindow,
            ModeRegion,
            ModeGame
        ];

        _microphoneOptions =
        [
            MicOptionCable,
            MicOptionArray,
            MicOptionLong
        ];

        _cameraOptions =
        [
            CameraOptionUsb,
            CameraOptionObs,
            CameraOptionLong
        ];

        _session.Changed +=
            (_, _) => ApplySessionState();

        if (stressLongNames)
            ApplyLongNameStressPreview();

        RefreshInputLabels();
        ApplySessionState();

        if (autoStartRecording)
        {
            Opened +=
                (_, _) => StartPreviewRecording();
        }
    }

    private void TitleBar_OnPointerPressed(
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
            StartPreviewRecording();
            e.Handled = true;
        }
        else if (e.Key == Key.Escape &&
                 _session.Phase == PreviewRecordingPhase.Saved)
        {
            _session.Reset();
            e.Handled = true;
        }
    }

    private void Settings_OnClick(
        object? sender,
        RoutedEventArgs e)
    {
        var settings = new SettingsWindow
        {
            WindowStartupLocation =
                WindowStartupLocation.CenterOwner
        };

        settings.ShowDialog(this);
    }

    private void Minimize_OnClick(
        object? sender,
        RoutedEventArgs e) =>
        WindowState = WindowState.Minimized;

    private void Close_OnClick(
        object? sender,
        RoutedEventArgs e) =>
        Close();

    private void CaptureMode_OnClick(
        object? sender,
        RoutedEventArgs e)
    {
        if (sender is not Button selected)
            return;

        foreach (var button in _modeButtons)
            button.Classes.Remove("selected");

        selected.Classes.Add("selected");

        var mode =
            selected.Tag?.ToString() ??
            "Display";

        switch (mode)
        {
            case "Window":
                SourceIcon.Kind =
                    LucideIconKind.AppWindow;
                SourceTitle.Text =
                    "Arssyut — Screen Recorder";
                SourceSubtitle.Text =
                    "Window capture · choose a target window";
                break;

            case "Region":
                SourceIcon.Kind =
                    LucideIconKind.ScanLine;
                SourceTitle.Text =
                    "Custom region · 1280 × 720";
                SourceSubtitle.Text =
                    "Drag the native boundary to move or resize";
                break;

            case "Game":
                SourceIcon.Kind =
                    LucideIconKind.Gamepad2;
                SourceTitle.Text =
                    "Choose a game";
                SourceSubtitle.Text =
                    "Dedicated game capture backend is pending";
                break;

            default:
                SourceIcon.Kind =
                    LucideIconKind.Monitor;
                SourceTitle.Text =
                    "Display 1 · 1920 × 1080";
                SourceSubtitle.Text =
                    "Primary display · 60 Hz";
                break;
        }
    }

    private void MicrophoneDevice_OnClick(
        object? sender,
        RoutedEventArgs e)
    {
        if (sender is not Button selected)
            return;

        var name =
            selected.Tag?.ToString();

        if (string.IsNullOrWhiteSpace(name))
            return;

        _microphoneDevice = name;
        SelectDeviceOption(
            _microphoneOptions,
            selected);

        MicDeviceButton.Flyout?.Hide();
        RefreshInputLabels();
    }

    private void CameraDevice_OnClick(
        object? sender,
        RoutedEventArgs e)
    {
        if (sender is not Button selected)
            return;

        var name =
            selected.Tag?.ToString();

        if (string.IsNullOrWhiteSpace(name))
            return;

        _cameraDevice = name;
        SelectDeviceOption(
            _cameraOptions,
            selected);

        CameraDeviceButton.Flyout?.Hide();
        RefreshInputLabels();
    }

    private void MicToggle_OnClick(
        object? sender,
        RoutedEventArgs e)
    {
        _session.MicrophoneEnabled =
            MicToggle.IsChecked == true;
        RefreshInputLabels();
    }

    private void CameraToggle_OnClick(
        object? sender,
        RoutedEventArgs e)
    {
        _session.CameraEnabled =
            CameraToggle.IsChecked == true;
        RefreshInputLabels();
    }

    private static void SelectDeviceOption(
        Button[] options,
        Button selected)
    {
        foreach (var option in options)
            option.Classes.Remove("selected");

        selected.Classes.Add("selected");
    }

    private void RefreshInputLabels()
    {
        MicDeviceText.Text =
            _session.MicrophoneEnabled
                ? _microphoneDevice
                : $"Off · {_microphoneDevice}";

        CameraDeviceText.Text =
            _session.CameraEnabled
                ? _cameraDevice
                : $"Off · {_cameraDevice}";

        MicToggle.IsChecked =
            _session.MicrophoneEnabled;
        CameraToggle.IsChecked =
            _session.CameraEnabled;
    }

    private void Record_OnClick(
        object? sender,
        RoutedEventArgs e) =>
        StartPreviewRecording();

    private void StartPreviewRecording()
    {
        if (_session.Phase is
            PreviewRecordingPhase.Recording or
            PreviewRecordingPhase.Paused)
            return;

        _session.Start();

        _controller =
            new RecordingControllerWindow(
                _session);

        _controller.StopRequested +=
            Controller_OnStopRequested;

        _controller.Show();
        Hide();
    }

    private void Controller_OnStopRequested(
        object? sender,
        EventArgs e)
    {
        if (_controller is not null)
        {
            _controller.StopRequested -=
                Controller_OnStopRequested;
            _controller = null;
        }

        Show();
        Activate();
        ApplySessionState();
    }

    private void OpenSaved_OnClick(
        object? sender,
        RoutedEventArgs e)
    {
        StatusDetail.Text =
            "Preview action · native output path will open after P6UI.4";
    }

    private void ShowFolder_OnClick(
        object? sender,
        RoutedEventArgs e)
    {
        StatusDetail.Text =
            "Preview action · output folder binding arrives with native bridge";
    }

    private void ApplySessionState()
    {
        RefreshInputLabels();

        switch (_session.Phase)
        {
            case PreviewRecordingPhase.Recording:
                StatusDot.Fill =
                    Brush.Parse("#FF4D57");
                StatusText.Foreground =
                    Brush.Parse("#FF4D57");
                StatusText.Text =
                    "Recording";
                StatusDetail.Text =
                    "Floating controller active";
                SavedActions.IsVisible =
                    false;
                break;

            case PreviewRecordingPhase.Paused:
                StatusDot.Fill =
                    Brush.Parse("#F1B85B");
                StatusText.Foreground =
                    Brush.Parse("#F1B85B");
                StatusText.Text =
                    "Paused";
                StatusDetail.Text =
                    "Recording preview paused";
                SavedActions.IsVisible =
                    false;
                break;

            case PreviewRecordingPhase.Saved:
                StatusDot.Fill =
                    Brush.Parse("#49D49D");
                StatusText.Foreground =
                    Brush.Parse("#49D49D");
                StatusText.Text =
                    "Saved";
                StatusDetail.Text =
                    $"{_session.SavedFileName} · {FormatElapsed(_session.Elapsed)}";
                SavedActions.IsVisible =
                    true;
                RecordText.Text =
                    "Record";
                RecordIcon.Kind =
                    LucideIconKind.Circle;
                RecordButton.SetValue(
                    AutomationProperties.NameProperty,
                    "Start another recording");
                break;

            default:
                StatusDot.Fill =
                    Brush.Parse("#49D49D");
                StatusText.Foreground =
                    Brush.Parse("#49D49D");
                StatusText.Text =
                    "Ready";
                StatusDetail.Text =
                    "Ready to record · F9 starts";
                SavedActions.IsVisible =
                    false;
                RecordText.Text =
                    "Record";
                RecordIcon.Kind =
                    LucideIconKind.Circle;
                RecordButton.SetValue(
                    AutomationProperties.NameProperty,
                    "Start recording");
                break;
        }
    }

    public void ApplyLongNameStressPreview()
    {
        Width = MinWidth;

        SourceTitle.Text =
            "Display 1 — Samsung Odyssey Neo G9 Super Ultra Wide · 7680 × 2160";
        SourceSubtitle.Text =
            "Primary display · HDR · 240 Hz · extremely long display descriptor";

        _microphoneDevice =
            "Professional USB Condenser Microphone — Conference Room Interface Channel 1/2";
        SelectDeviceOption(
            _microphoneOptions,
            MicOptionLong);

        _cameraDevice =
            "4K Conference Camera — Ultra Wide Room Camera with AI Auto Framing";
        SelectDeviceOption(
            _cameraOptions,
            CameraOptionLong);

        RefreshInputLabels();
    }

    private static string FormatElapsed(
        TimeSpan elapsed)
    {
        if (elapsed.TotalHours >= 1)
            return elapsed.ToString(@"hh\:mm\:ss");

        return elapsed.ToString(@"mm\:ss");
    }
}
