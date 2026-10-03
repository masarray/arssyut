using System;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Media;
using Lucide.Avalonia;

namespace Arssyut.UI;

public sealed partial class MainWindow : Window
{
    private readonly Button[] _modeButtons;
    private bool _recording;
    private bool _paused;

    public MainWindow()
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
    }

    private void TitleBar_OnPointerPressed(
        object? sender,
        PointerPressedEventArgs e)
    {
        if (e.GetCurrentPoint(this).Properties.IsLeftButtonPressed)
            BeginMoveDrag(e);
    }

    private void Settings_OnClick(
        object? sender,
        RoutedEventArgs e)
    {
        var settings = new SettingsWindow
        {
            WindowStartupLocation = WindowStartupLocation.CenterOwner
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

        var mode = selected.Tag?.ToString() ?? "Display";

        switch (mode)
        {
            case "Window":
                SourceIcon.Kind = LucideIconKind.AppWindow;
                SourceTitle.Text = "Arssyut — Screen Recorder";
                SourceSubtitle.Text = "Window capture · click to choose another window";
                break;

            case "Region":
                SourceIcon.Kind = LucideIconKind.ScanLine;
                SourceTitle.Text = "Custom region · 1280 × 720";
                SourceSubtitle.Text = "Drag the boundary on screen to move or resize";
                break;

            case "Game":
                SourceIcon.Kind = LucideIconKind.Gamepad2;
                SourceTitle.Text = "Choose a game";
                SourceSubtitle.Text = "Dedicated game capture backend is pending";
                break;

            default:
                SourceIcon.Kind = LucideIconKind.Monitor;
                SourceTitle.Text = "Display 1 · 1920 × 1080";
                SourceSubtitle.Text = "Primary display · 60 Hz";
                break;
        }
    }

    private void Record_OnClick(
        object? sender,
        RoutedEventArgs e)
    {
        _recording = !_recording;
        _paused = false;

        if (_recording)
        {
            RecordIcon.Kind = LucideIconKind.Square;
            RecordText.Text = "Stop";
            PauseButton.IsVisible = true;

            StatusDot.Fill = Brush.Parse("#FF4D57");
            StatusText.Foreground = Brush.Parse("#FF4D57");
            StatusText.Text = "Recording";
            StatusDetail.Text = "00:00 · visual state simulator only";
        }
        else
        {
            RecordIcon.Kind = LucideIconKind.Circle;
            RecordText.Text = "Record";
            PauseButton.IsVisible = false;

            StatusDot.Fill = Brush.Parse("#49D49D");
            StatusText.Foreground = Brush.Parse("#49D49D");
            StatusText.Text = "Ready";
            StatusDetail.Text = "Ready to record · native bridge will bind here";
        }
    }

    private void Pause_OnClick(
        object? sender,
        RoutedEventArgs e)
    {
        if (!_recording)
            return;

        _paused = !_paused;

        PauseIcon.Kind =
            _paused
                ? LucideIconKind.Play
                : LucideIconKind.Pause;

        StatusText.Text =
            _paused
                ? "Paused"
                : "Recording";

        StatusDetail.Text =
            _paused
                ? "Media clock paused · simulator"
                : "00:00 · visual state simulator only";
    }
}
