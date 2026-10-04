using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using Avalonia;
using Avalonia.Automation;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Layout;
using Avalonia.Media;
using Arssyut.UI.Interop;
using Arssyut.UI.Preview;
using Lucide.Avalonia;

namespace Arssyut.UI;

public sealed partial class MainWindow : Window
{
    private readonly Button[] _modeButtons;
    private readonly Button[] _microphoneOptions;
    private readonly Button[] _cameraOptions;
    private readonly PreviewRecorderSession _session = new();
    private readonly SettingsPreviewState _settings;
    private readonly NativeBridgeClient? _nativeBridge;
    private readonly NativeBridgeAvailability _bridgeAvailability;
    private readonly bool _stressLongNames;

    private readonly List<PreviewSourceItem> _sources = [];
    private RecordingControllerWindow? _controller;
    private PreviewSourceItem? _selectedSource;
    private PreviewCaptureMode _captureMode =
        PreviewCaptureMode.Display;

    private string _microphoneDevice =
        "Microphone";
    private string _cameraDevice =
        "Camera";
    private ulong _microphoneDeviceToken;
    private ulong _cameraDeviceToken;
    private bool _systemAudioEnabled;
    private NativeRecorderSnapshot? _lastNativeSnapshot;
    private NativeRecorderResult? _nativeResult;

    public MainWindow(
        SettingsPreviewState settings,
        NativeBridgeClient? nativeBridge,
        NativeBridgeAvailability bridgeAvailability,
        bool stressLongNames = false,
        bool autoStartRecording = false)
    {
        _settings = settings;
        _nativeBridge = nativeBridge;
        _bridgeAvailability = bridgeAvailability;
        _stressLongNames = stressLongNames;

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

        if (_nativeBridge is null)
        {
            _session.Changed +=
                (_, _) => ApplySessionState();
        }

        _settings.Changed +=
            Settings_OnChanged;

        Opened +=
            (_, _) =>
            {
                RefreshSources(
                    keepCurrentSelection: false);
                RefreshNativeDevices();

                if (_stressLongNames)
                    ApplyLongNameStressPreview();

                RefreshInputLabels();
                RefreshSettingsSurface();
                ApplySessionState();

                if (autoStartRecording)
                    StartPreviewRecording();
            };

        Closed +=
            (_, _) =>
            {
                _settings.Changed -=
                    Settings_OnChanged;
            };
    }

    private void TitleBar_OnPointerPressed(
        object? sender,
        PointerPressedEventArgs e)
    {
        if (e.GetCurrentPoint(this).
                Properties.
                IsLeftButtonPressed)
        {
            BeginMoveDrag(e);
        }
    }

    private void Window_OnKeyDown(
        object? sender,
        KeyEventArgs e)
    {
        if (HotkeyPreview.Matches(
                e,
                _settings.RecordHotkey))
        {
            StartPreviewRecording();
            e.Handled = true;
            return;
        }

        if (e.Key == Key.Escape)
        {
            if (_nativeBridge is null &&
                _session.Phase ==
                    PreviewRecordingPhase.Saved)
            {
                _session.Reset();
                e.Handled = true;
            }
            else if (_nativeBridge is not null &&
                     _lastNativeSnapshot?.State ==
                         NativeRecorderState.Ready)
            {
                _lastNativeSnapshot = null;
                _nativeResult = null;
                ApplyNativeReadyState();
                e.Handled = true;
            }
        }
    }

    private void Settings_OnChanged(
        object? sender,
        EventArgs e) =>
        RefreshSettingsSurface();

    private void Settings_OnClick(
        object? sender,
        RoutedEventArgs e)
    {
        var settings =
            new SettingsWindow(
                _settings,
                _nativeBridge)
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

        _captureMode =
            selected.Tag?.ToString() switch
            {
                "Window" =>
                    PreviewCaptureMode.Window,
                "Region" =>
                    PreviewCaptureMode.Region,
                "Game" =>
                    PreviewCaptureMode.Game,
                _ =>
                    PreviewCaptureMode.Display
            };

        RefreshSources(
            keepCurrentSelection: false);
    }

    private void RefreshSources_OnClick(
        object? sender,
        RoutedEventArgs e) =>
        RefreshSources(
            keepCurrentSelection: true);

    private void RefreshSources(
        bool keepCurrentSelection)
    {
        var previousId =
            keepCurrentSelection
                ? _selectedSource?.Id
                : null;

        _sources.Clear();

        if (_nativeBridge is not null)
        {
            try
            {
                var ownWindow =
                    TryGetPlatformHandle()?.Handle ??
                    IntPtr.Zero;

                _sources.AddRange(
                    _nativeBridge.RefreshSources(
                        _captureMode,
                        ownWindow));
            }
            catch (Exception)
            {
                // The presentation remains usable if the bridge fails. Do not
                // silently fall back to a second source-enumeration authority.
            }
        }

        _selectedSource =
            previousId is null
                ? null
                : _sources.FirstOrDefault(
                    item =>
                        string.Equals(
                            item.Id,
                            previousId,
                            StringComparison.Ordinal));

        _selectedSource ??=
            _sources.FirstOrDefault(
                item => item.IsPrimary) ??
            _sources.FirstOrDefault();

        RebuildSourceFlyout();
        ApplySelectedSource();
    }

    private void RebuildSourceFlyout()
    {
        SourceListPanel.Children.Clear();

        SourceFlyoutTitle.Text =
            _captureMode switch
            {
                PreviewCaptureMode.Display =>
                    "Displays",
                PreviewCaptureMode.Window =>
                    "Windows",
                PreviewCaptureMode.Region =>
                    "Region display",
                PreviewCaptureMode.Game =>
                    "Applications",
                _ =>
                    "Sources"
            };

        SourceFlyoutSubtitle.Text =
            _captureMode switch
            {
                PreviewCaptureMode.Display =>
                    "Native monitor sources",
                PreviewCaptureMode.Window =>
                    "Native visible-window sources",
                PreviewCaptureMode.Region =>
                    "Choose the native base display",
                PreviewCaptureMode.Game =>
                    "Application candidates; native game capture is not enabled yet",
                _ =>
                    "Choose the capture target"
            };

        SourceCountText.Text =
            $"{_sources.Count} " +
            (_sources.Count == 1
                ? "source"
                : "sources");

        if (_sources.Count == 0)
        {
            var message =
                _nativeBridge is null
                    ? BridgeUnavailableMessage()
                    : "No native sources are available for this mode.";

            SourceListPanel.Children.Add(
                new TextBlock
                {
                    Text = message,
                    Classes =
                    {
                        "micro"
                    },
                    TextWrapping =
                        TextWrapping.Wrap,
                    Margin =
                        new Thickness(
                            9,
                            8,
                            9,
                            10)
                });

            return;
        }

        foreach (var item in _sources)
        {
            var button =
                new Button
                {
                    Tag = item,
                    HorizontalAlignment =
                        HorizontalAlignment.Stretch
                };

            button.Classes.Add(
                "device-option");

            if (_selectedSource?.Id ==
                item.Id)
            {
                button.Classes.Add(
                    "selected");
            }

            button.Click +=
                SourceOption_OnClick;

            var grid =
                new Grid
                {
                    ColumnDefinitions =
                        new ColumnDefinitions(
                            "*,Auto")
                };

            var text =
                new StackPanel
                {
                    Spacing = 2,
                    VerticalAlignment =
                        VerticalAlignment.Center
                };

            text.Children.Add(
                new TextBlock
                {
                    Text = item.Title,
                    FontWeight =
                        FontWeight.Medium,
                    TextTrimming =
                        TextTrimming.CharacterEllipsis
                });

            var subtitle =
                new TextBlock
                {
                    Text = item.Subtitle,
                    TextTrimming =
                        TextTrimming.CharacterEllipsis
                };
            subtitle.Classes.Add(
                "micro");
            text.Children.Add(
                subtitle);

            grid.Children.Add(
                text);

            if (item.IsPrimary)
            {
                var badge =
                    new Border
                    {
                        Padding =
                            new Thickness(
                                7,
                                3),
                        CornerRadius =
                            new CornerRadius(99),
                        Background =
                            Brush.Parse(
                                "#1AFF5864"),
                        VerticalAlignment =
                            VerticalAlignment.Center,
                        Child =
                            new TextBlock
                            {
                                Text = "Primary",
                                FontSize = 9.5,
                                Foreground =
                                    Brush.Parse(
                                        "#FF7A84")
                            }
                    };

                Grid.SetColumn(
                    badge,
                    1);
                grid.Children.Add(
                    badge);
            }

            button.Content =
                grid;

            SourceListPanel.Children.Add(
                button);
        }
    }

    private void SourceOption_OnClick(
        object? sender,
        RoutedEventArgs e)
    {
        if (sender is not Button
            {
                Tag:
                    PreviewSourceItem item
            })
            return;

        _selectedSource =
            item;

        RebuildSourceFlyout();
        ApplySelectedSource();
        SourcePickerButton.Flyout?.Hide();
    }

    private void ApplySelectedSource()
    {
        if (_selectedSource is null)
        {
            SourceTitle.Text =
                "Native source unavailable";
            SourceSubtitle.Text =
                BridgeUnavailableMessage();
            SourceIcon.Kind =
                LucideIconKind.Monitor;
            UpdateReadyDetail();
            return;
        }

        SourceIcon.Kind =
            _captureMode switch
            {
                PreviewCaptureMode.Window =>
                    LucideIconKind.AppWindow,
                PreviewCaptureMode.Region =>
                    LucideIconKind.ScanLine,
                PreviewCaptureMode.Game =>
                    LucideIconKind.Gamepad2,
                _ =>
                    LucideIconKind.Monitor
            };

        SourceTitle.Text =
            _selectedSource.Title;

        SourceSubtitle.Text =
            _captureMode ==
                    PreviewCaptureMode.Region
                ? $"{_selectedSource.Subtitle} · native area editor binding follows"
                : _selectedSource.Subtitle;

        UpdateReadyDetail();
    }

    private void RefreshNativeDevices()
    {
        if (_nativeBridge is null)
            return;

        try
        {
            var devices =
                _nativeBridge.RefreshDevices();

            ApplyNativeDeviceButtons(
                _microphoneOptions,
                devices.Microphones);

            ApplyNativeDeviceButtons(
                _cameraOptions,
                devices.Cameras);

            if (devices.Microphones.Count > 0)
            {
                _microphoneDevice =
                    devices.Microphones[0].Name;
                _microphoneDeviceToken =
                    devices.Microphones[0].Token;
            }

            if (devices.Cameras.Count > 0)
            {
                _cameraDevice =
                    devices.Cameras[0].Name;
                _cameraDeviceToken =
                    devices.Cameras[0].Token;
            }
        }
        catch (Exception)
        {
            // Device snapshots are read-only in P6UI.4A. Keep the UI alive if
            // device enumeration is temporarily unavailable.
        }
    }

    private static void ApplyNativeDeviceButtons(
        Button[] buttons,
        IReadOnlyList<NativeDeviceItem> devices)
    {
        for (var index = 0;
             index < buttons.Length;
             ++index)
        {
            var button =
                buttons[index];

            button.Classes.Remove(
                "selected");

            if (index >= devices.Count)
            {
                button.IsVisible =
                    false;
                continue;
            }

            var device =
                devices[index];

            button.IsVisible =
                true;
            button.Tag =
                device;

            var stack =
                new StackPanel
                {
                    Spacing = 2
                };

            stack.Children.Add(
                new TextBlock
                {
                    Text =
                        device.Name,
                    TextTrimming =
                        TextTrimming.CharacterEllipsis
                });

            var subtitle =
                new TextBlock
                {
                    Text =
                        "Native device snapshot"
                };
            subtitle.Classes.Add(
                "micro");

            stack.Children.Add(
                subtitle);

            button.Content =
                stack;

            if (index == 0)
                button.Classes.Add(
                    "selected");
        }
    }

    private void MicrophoneDevice_OnClick(
        object? sender,
        RoutedEventArgs e)
    {
        if (sender is not Button selected)
            return;

        var device =
            selected.Tag as NativeDeviceItem;
        var name =
            device?.Name ??
            selected.Tag?.ToString();

        if (string.IsNullOrWhiteSpace(name))
            return;

        _microphoneDevice = name;
        _microphoneDeviceToken =
            device?.Token ?? 0;
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

        var device =
            selected.Tag as NativeDeviceItem;
        var name =
            device?.Name ??
            selected.Tag?.ToString();

        if (string.IsNullOrWhiteSpace(name))
            return;

        _cameraDevice = name;
        _cameraDeviceToken =
            device?.Token ?? 0;
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
            option.Classes.Remove(
                "selected");

        selected.Classes.Add(
            "selected");
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

    private void RefreshSettingsSurface()
    {
        RecordHotkeyText.Text =
            _settings.RecordHotkey;

        RecordButton.SetValue(
            AutomationProperties.HelpTextProperty,
            $"Starts the interaction preview. Shortcut {_settings.RecordHotkey}.");

        UpdateReadyDetail();
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
                _session,
                _settings);

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
            "Native result-path binding arrives in P6UI.4B";
    }

    private void ShowFolder_OnClick(
        object? sender,
        RoutedEventArgs e)
    {
        StatusDetail.Text =
            "Native output-folder action arrives in P6UI.4B";
    }

    private void ApplySessionState()
    {
        RefreshInputLabels();

        switch (_session.Phase)
        {
            case PreviewRecordingPhase.Recording:
                StatusDot.Fill =
                    Brush.Parse("#FF5360");
                StatusText.Foreground =
                    Brush.Parse("#FF6671");
                StatusText.Text =
                    "Recording";
                StatusDetail.Text =
                    "Floating controller interaction preview";
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
                    "Recording interaction preview paused";
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
                SavedActions.IsVisible =
                    false;
                RecordText.Text =
                    "Record";
                RecordIcon.Kind =
                    LucideIconKind.Circle;
                RecordButton.SetValue(
                    AutomationProperties.NameProperty,
                    "Start recording");
                UpdateReadyDetail();
                break;
        }
    }

    private void UpdateReadyDetail()
    {
        if (_session.Phase is
            PreviewRecordingPhase.Recording or
            PreviewRecordingPhase.Paused or
            PreviewRecordingPhase.Saved)
            return;

        var source =
            _captureMode switch
            {
                PreviewCaptureMode.Window =>
                    "Window",
                PreviewCaptureMode.Region =>
                    "Region",
                PreviewCaptureMode.Game =>
                    "Game",
                _ =>
                    "Display"
            };

        var bridge =
            _nativeBridge is null
                ? "native bridge unavailable"
                : "native source";

        StatusDetail.Text =
            $"{source} · {bridge} · 60 fps · Smart Zoom on · {_settings.RecordHotkey}";
    }

    private string BridgeUnavailableMessage() =>
        _bridgeAvailability switch
        {
            NativeBridgeAvailability.MissingLibrary =>
                "Native bridge DLL is missing from this build.",
            NativeBridgeAvailability.IncompatibleAbi =>
                "Native bridge ABI does not match this UI build.",
            NativeBridgeAvailability.InitializationFailed =>
                "Native bridge could not initialize.",
            _ =>
                "Native bridge source snapshot is unavailable."
        };

    public void ApplyLongNameStressPreview()
    {
        Width = MinWidth;

        _microphoneDevice =
            "Professional USB Condenser Microphone — Conference Room Interface Channel 1/2";
        _cameraDevice =
            "4K Conference Camera — Ultra Wide Room Camera with AI Auto Framing";

        SourceTitle.Text =
            "Display 1 — Samsung Odyssey Neo G9 Super Ultra Wide";
        SourceSubtitle.Text =
            "7680 × 2160 · native source · extremely long display descriptor";

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
