using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using Avalonia;
using Avalonia.Automation;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Threading;
using Arssyut.UI.Design;
using Arssyut.UI.Interop;
using Arssyut.UI.Preview;
using Lucide.Avalonia;

namespace Arssyut.UI;

public sealed partial class MainWindow : Window
{
    private readonly Button[] _microphoneOptions;
    private readonly Button[] _cameraOptions;
    private readonly PreviewRecorderSession _session = new();
    private readonly SettingsPreviewState _settings;
    private readonly NativeBridgeClient? _nativeBridge;
    private readonly NativeBridgeAvailability _bridgeAvailability;
    private readonly bool _stressLongNames;
    private readonly bool _allowInteractionPreview;
    private readonly bool _audioHardwarePreview;
    private readonly DispatcherTimer _hotkeyTimer;
    private readonly IBrush _recordBrush;
    private readonly IBrush _recordHoverBrush;
    private readonly IBrush _recordSoftBrush;
    private readonly IBrush _recordTextBrightBrush;
    private readonly IBrush _warningBrush;
    private readonly IBrush _successBrush;

    private readonly List<PreviewSourceItem> _sources = [];
    private RecordingControllerWindow? _controller;
    private RecordingCountdownWindow? _countdown;
    private bool _startSequenceActive;
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
    private bool _mainUiReady;
    private bool _settingsOpen;

    private readonly Dictionary<
        NativeHotkeyAction,
        HotkeyChord> _registeredHotkeys = [];

    public MainWindow(
        SettingsPreviewState settings,
        NativeBridgeClient? nativeBridge,
        NativeBridgeAvailability bridgeAvailability,
        bool stressLongNames = false,
        bool autoStartRecording = false,
        bool allowInteractionPreview = false)
    {
        _settings = settings;
        _nativeBridge = nativeBridge;
        _bridgeAvailability = bridgeAvailability;
        _stressLongNames = stressLongNames;
        _allowInteractionPreview =
            allowInteractionPreview;
        // This never enables audio in the public/default artifact.
        // The native bridge independently enforces product-on + opt-in.
        _audioHardwarePreview =
            nativeBridge is not null &&
            !allowInteractionPreview &&
            string.Equals(
                Environment.GetEnvironmentVariable("ARSSYUT_AUDIO_PREVIEW"),
                "1", StringComparison.Ordinal);

        InitializeComponent();

        MicrophoneDeviceComboMain.SelectionChanged += (_, _) =>
        {
            if (!_audioHardwarePreview ||
                MicrophoneDeviceComboMain.SelectedItem is not ComboBoxItem
                {
                    Tag: NativeDeviceItem selected
                })
                return;

            _microphoneDevice = selected.Name;
            _microphoneDeviceToken = selected.Token;
            RefreshInputLabels();
        };

        _recordBrush =
            ArBrushResolver.Require(
                this,
                "ArBrush.Record");
        _recordHoverBrush =
            ArBrushResolver.Require(
                this,
                "ArBrush.RecordHover");
        _recordSoftBrush =
            ArBrushResolver.Require(
                this,
                "ArBrush.RecordSoft");
        _recordTextBrightBrush =
            ArBrushResolver.Require(
                this,
                "ArBrush.RecordTextBright");
        _warningBrush =
            ArBrushResolver.Require(
                this,
                "ArBrush.Warning");
        _successBrush =
            ArBrushResolver.Require(
                this,
                "ArBrush.Success");

        TransparencyLevelHint =
        [
            WindowTransparencyLevel.Mica,
            WindowTransparencyLevel.AcrylicBlur,
            WindowTransparencyLevel.None
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

        _hotkeyTimer =
            new DispatcherTimer
            {
                Interval = TimeSpan.FromMilliseconds(75)
            };
        _hotkeyTimer.Tick += GlobalHotkeyTimer_OnTick;
        _mainUiReady = true;

        if (_nativeBridge is null &&
            _allowInteractionPreview)
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
                ApplyProductCapabilitySurface();
                RefreshSettingsSurface();
                ApplySessionState();
                SyncGlobalHotkeys();
                _hotkeyTimer.Start();

                if (autoStartRecording &&
                    _allowInteractionPreview)
                {
                    StartInteractionPreview();
                }
            };

        Closed +=
            (_, _) =>
            {
                _settings.Changed -=
                    Settings_OnChanged;
                _hotkeyTimer.Stop();
                _countdown?.Close();
                _countdown = null;
                SuspendGlobalHotkeys();

                if (_nativeBridge is not null)
                {
                    try
                    {
                        _nativeBridge.
                            HideOverlay();
                    }
                    catch (Exception)
                    {
                        // Application teardown still owns bridge disposal.
                    }
                }
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
        if ((_allowInteractionPreview ||
             !IsHotkeyRegistered(
                 NativeHotkeyAction.ToggleRecord)) &&
            HotkeyPreview.Matches(
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
        EventArgs e)
    {
        RefreshSettingsSurface();
        SyncGlobalHotkeys();
    }

    private async void Settings_OnClick(
        object? sender,
        RoutedEventArgs e)
    {
        _settingsOpen = true;
        SuspendGlobalHotkeys();

        try
        {
            var settings =
                new SettingsWindow(
                    _settings,
                    _nativeBridge)
                {
                    WindowStartupLocation =
                        WindowStartupLocation.CenterOwner
                };

            await settings.ShowDialog(this);
        }
        finally
        {
            _settingsOpen = false;
            SyncGlobalHotkeys();
        }
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
        if (!_mainUiReady ||
            sender is not ToggleButton selected ||
            !selected.IsEnabled)
            return;

        var mode =
            selected.Tag?.ToString();

        ModeDisplay.IsChecked =
            mode == "Display";
        ModeWindow.IsChecked =
            mode == "Window";
        ModeRegion.IsChecked =
            mode == "Region";
        ModeGame.IsChecked =
            mode == "Game";

        _captureMode =
            mode switch
            {
                "Window" => PreviewCaptureMode.Window,
                "Region" => PreviewCaptureMode.Region,
                "Game" => PreviewCaptureMode.Game,
                _ => PreviewCaptureMode.Display
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
                            _recordSoftBrush,
                        VerticalAlignment =
                            VerticalAlignment.Center,
                        Child =
                            new TextBlock
                            {
                                Text = "Primary",
                                FontSize = 9.5,
                                Foreground =
                                    _recordTextBrightBrush
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
            if (_nativeBridge is not null)
            {
                try
                {
                    _nativeBridge.
                        HideOverlay();
                }
                catch (Exception)
                {
                    // Source-empty presentation remains usable if overlay teardown fails.
                }
            }

            SourceTitle.Text =
                "Native source unavailable";
            SourceSubtitle.Text =
                BridgeUnavailableMessage();
            SourceIcon.Kind =
                LucideIconKind.Monitor;
            UpdateRecordAvailability();
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
                ? $"{_selectedSource.Subtitle} · native area editor active"
                : _selectedSource.Subtitle;

        if (_nativeBridge is not null)
        {
            try
            {
                var overlayStatus =
                    _nativeBridge.
                        SetOverlayTarget(
                            _captureMode,
                            _selectedSource.
                                NativeToken);

                if (overlayStatus ==
                    NativeBridgeStatus.Unsupported)
                {
                    _nativeBridge.
                        HideOverlay();
                }
                else if (overlayStatus !=
                         NativeBridgeStatus.Ok)
                {
                    SourceSubtitle.Text =
                        $"{_selectedSource.Subtitle} · overlay status {overlayStatus}";
                }
            }
            catch (Exception)
            {
                SourceSubtitle.Text =
                    $"{_selectedSource.Subtitle} · native overlay unavailable";
            }
        }

        UpdateRecordAvailability();
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

            // Preserve the user's selection across an MMDevice refresh.
            // Tokens have a new generation after refresh, so pin the new
            // token by a uniquely matched displayed device name.
            var selectedMicIndex = 0;
            if (!string.IsNullOrEmpty(_microphoneDevice))
            {
                var matches = devices.Microphones
                    .Select((device, index) => (device, index))
                    .Where(item => string.Equals(
                        item.device.Name, _microphoneDevice,
                        StringComparison.Ordinal))
                    .ToArray();
                if (matches.Length == 1)
                    selectedMicIndex = matches[0].index;
            }
            ApplyReadOnlyDeviceCombo(
                MicrophoneDeviceComboMain,
                devices.Microphones,
                "No microphone detected",
                selectedMicIndex);

            if (devices.Microphones.Count > 0)
            {
                _microphoneDevice =
                    devices.Microphones[selectedMicIndex].Name;
                _microphoneDeviceToken =
                    devices.Microphones[selectedMicIndex].Token;
            }
            else
            {
                _microphoneDeviceToken = 0;
            }

            ApplyReadOnlyDeviceCombo(
                CameraDeviceComboMain,
                devices.Cameras,
                "No camera detected");

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

    private static void ApplyReadOnlyDeviceCombo(
        ComboBox combo,
        IReadOnlyList<NativeDeviceItem> devices,
        string emptyText,
        int selectedIndex = 0)
    {
        combo.Items.Clear();

        if (devices.Count == 0)
        {
            combo.Items.Add(
                new ComboBoxItem
                {
                    Content = emptyText
                });
            combo.SelectedIndex = 0;
            return;
        }

        foreach (var device in devices)
        {
            combo.Items.Add(
                new ComboBoxItem
                {
                    Content = device.Name,
                    Tag = device
                });
        }

        combo.SelectedIndex = selectedIndex >= 0 &&
            selectedIndex < devices.Count
                ? selectedIndex
                : 0;
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

    private void SystemAudioToggle_OnClick(
        object? sender,
        RoutedEventArgs e)
    {
        if (!_allowInteractionPreview && !_audioHardwarePreview)
            return;

        _systemAudioEnabled =
            SystemAudioToggle.IsChecked == true;
        RefreshInputLabels();
        UpdateReadyDetail();
    }

    private void MicToggle_OnClick(
        object? sender,
        RoutedEventArgs e)
    {
        if (!_allowInteractionPreview && !_audioHardwarePreview)
            return;

        _session.MicrophoneEnabled =
            MicToggle.IsChecked == true;
        RefreshInputLabels();
        UpdateReadyDetail();
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
        if (!_allowInteractionPreview && !_audioHardwarePreview)
        {
            SystemAudioToggle.IsChecked =
                false;
            SystemAudioToggle.IsEnabled =
                false;

            MicToggle.IsChecked =
                false;
            MicToggle.IsEnabled =
                false;
            MicDeviceButton.IsEnabled =
                false;
            MicDeviceText.Text =
                _nativeBridge is null
                    ? "Engine unavailable"
                    : "Backend pending";

            CameraToggle.IsChecked =
                false;
            CameraToggle.IsEnabled =
                false;
            CameraDeviceButton.IsEnabled =
                false;
            CameraDeviceText.Text =
                _nativeBridge is null
                    ? "Engine unavailable"
                    : "Backend pending";
            return;
        }

        MicDeviceText.Text =
            _session.MicrophoneEnabled
                ? _microphoneDevice
                : $"Off · {_microphoneDevice}";

        CameraDeviceText.Text =
            _session.CameraEnabled
                ? _cameraDevice
                : $"Off · {_cameraDevice}";

        SystemAudioToggle.IsChecked =
            _systemAudioEnabled;
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
            $"Starts recording through the native bridge. Shortcut {_settings.RecordHotkey}.");

        UpdateReadyDetail();
    }

    private void GlobalHotkeyTimer_OnTick(
        object? sender,
        EventArgs e)
    {
        if (_nativeBridge is null ||
            _registeredHotkeys.Count == 0 ||
            _settingsOpen)
            return;

        try
        {
            var events =
                _nativeBridge.TakeHotkeyEvents();

            if ((events &
                 NativeHotkeyEvents.ToggleRecord) != 0) {
                HandleGlobalRecordHotkey();
            }
        }
        catch (Exception)
        {
            SuspendGlobalHotkeys();
        }
    }

    private void HandleGlobalRecordHotkey()
    {
        if (_nativeBridge is null)
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

        if (snapshot.State is
            NativeRecorderState.Preparing or
            NativeRecorderState.Armed or
            NativeRecorderState.Recording)
        {
            _nativeBridge.StopRecording();
            return;
        }

        if (snapshot.State is
            NativeRecorderState.Stopping or
            NativeRecorderState.Finalizing)
            return;

        StartNativeRecording();
    }

    private void SyncGlobalHotkeys()
    {
        if (!_mainUiReady ||
            _nativeBridge is null ||
            _allowInteractionPreview ||
            _settingsOpen)
            return;

        var desired =
            new Dictionary<
                NativeHotkeyAction,
                HotkeyChord>();

        AddHotkeyIfAssigned(
            desired,
            NativeHotkeyAction.ToggleRecord,
            HotkeyActionIds.Record);
        AddHotkeyIfAssigned(
            desired,
            NativeHotkeyAction.ToggleZoom,
            HotkeyActionIds.ToggleZoom);
        AddHotkeyIfAssigned(
            desired,
            NativeHotkeyAction.ZoomIn,
            HotkeyActionIds.ZoomIn);
        AddHotkeyIfAssigned(
            desired,
            NativeHotkeyAction.ZoomOut,
            HotkeyActionIds.ZoomOut);
        AddHotkeyIfAssigned(
            desired,
            NativeHotkeyAction.ResetFullFrame,
            HotkeyActionIds.ResetZoom);
        AddHotkeyIfAssigned(
            desired,
            NativeHotkeyAction.FreezeCamera,
            HotkeyActionIds.FreezeCamera);

        if (SameHotkeyBindings(
                desired,
                _registeredHotkeys))
            return;

        SuspendGlobalHotkeys();

        foreach (var binding in desired)
        {
            RegisterGlobalHotkey(
                binding.Key,
                binding.Value);
        }

        UpdateReadyDetail();
    }

    private void AddHotkeyIfAssigned(
        IDictionary<
            NativeHotkeyAction,
            HotkeyChord> target,
        NativeHotkeyAction nativeAction,
        string productAction)
    {
        if (_settings.TryGetHotkeyChord(
                productAction,
                out var chord) &&
            !chord.IsEmpty)
        {
            target[nativeAction] =
                chord;
        }
    }

    private static bool SameHotkeyBindings(
        IReadOnlyDictionary<
            NativeHotkeyAction,
            HotkeyChord> desired,
        IReadOnlyDictionary<
            NativeHotkeyAction,
            HotkeyChord> current)
    {
        if (desired.Count != current.Count)
            return false;

        foreach (var binding in desired)
        {
            if (!current.TryGetValue(
                    binding.Key,
                    out var value) ||
                value != binding.Value)
            {
                return false;
            }
        }

        return true;
    }

    private bool HasPresenterZoomHotkeys() =>
        HasAssignedHotkey(
            HotkeyActionIds.ToggleZoom) ||
        HasAssignedHotkey(
            HotkeyActionIds.HoldZoom) ||
        HasAssignedHotkey(
            HotkeyActionIds.ZoomIn) ||
        HasAssignedHotkey(
            HotkeyActionIds.ZoomOut) ||
        HasAssignedHotkey(
            HotkeyActionIds.ResetZoom) ||
        HasAssignedHotkey(
            HotkeyActionIds.OverviewPeek) ||
        HasAssignedHotkey(
            HotkeyActionIds.FreezeCamera);

    private bool HasAssignedHotkey(
        string action) =>
        _settings.TryGetHotkeyChord(
            action,
            out var chord) &&
        !chord.IsEmpty;

    private NativeHotkeyChord MomentaryHotkey(
        string action)
    {
        if (!_settings.TryGetHotkeyChord(
                action,
                out var chord) ||
            chord.IsEmpty)
        {
            return NativeHotkeyChord.None;
        }

        return chord.ToNative();
    }

    private void RegisterGlobalHotkey(
        NativeHotkeyAction action,
        HotkeyChord chord)
    {
        if (_nativeBridge is null ||
            chord.IsEmpty ||
            !chord.IsValid)
            return;

        try
        {
            if (_nativeBridge.RegisterHotkey(
                    action,
                    chord.Modifiers,
                    chord.VirtualKey) ==
                NativeBridgeStatus.Ok)
            {
                _registeredHotkeys[action] =
                    chord;
            }
        }
        catch (Exception)
        {
        }
    }

    private bool IsHotkeyRegistered(
        NativeHotkeyAction action) =>
        _registeredHotkeys.ContainsKey(
            action);

    private void SuspendGlobalHotkeys()
    {
        if (_nativeBridge is not null)
        {
            foreach (var action in
                     _registeredHotkeys.Keys.ToArray())
            {
                try
                {
                    _nativeBridge.UnregisterHotkey(
                        action);
                }
                catch (Exception)
                {
                }
            }
        }

        _registeredHotkeys.Clear();
    }

    private void Record_OnClick(
        object? sender,
        RoutedEventArgs e) =>
        StartPreviewRecording();

    private void StartPreviewRecording()
    {
        if (_nativeBridge is not null)
        {
            StartNativeRecording();
            return;
        }

        if (_allowInteractionPreview)
        {
            StartInteractionPreview();
            return;
        }

        ShowCommandFeedback(
            "Native engine unavailable",
            BridgeUnavailableMessage());
    }

    private async void StartNativeRecording()
    {
        if (_startSequenceActive)
            return;

        var bridge =
            _nativeBridge;

        if (bridge is null)
        {
            if (_allowInteractionPreview)
            {
                StartInteractionPreview();
            }
            else
            {
                ShowCommandFeedback(
                    "Native engine unavailable",
                    BridgeUnavailableMessage());
            }
            return;
        }

        if (_selectedSource is null ||
            _selectedSource.NativeToken == 0)
        {
            ShowCommandFeedback(
                "Choose a source",
                "Refresh the native source list and choose a display or window.");
            return;
        }

        var flags =
            NativeStartFlags.ArmedStart;

        if (_settings.SmartZoom)
            flags |=
                NativeStartFlags.SmartZoom;
        if (_settings.ClickHighlight)
            flags |=
                NativeStartFlags.ClickVisual;
        if (_settings.ShortcutKeys)
            flags |=
                NativeStartFlags.ShortcutKeys;

        if (HasPresenterZoomHotkeys())
            flags |=
                NativeStartFlags.PresenterControls;

        if (_settings.SpotlightEnabled)
            flags |=
                NativeStartFlags.Spotlight;
        if (_settings.SpotlightLinkToZoom)
            flags |=
                NativeStartFlags.SpotlightLinkToZoom;

        if (_systemAudioEnabled)
            flags |=
                NativeStartFlags.SystemAudio;
        if (_session.MicrophoneEnabled)
            flags |=
                NativeStartFlags.Microphone;
        if (_session.CameraEnabled)
            flags |=
                NativeStartFlags.Camera;

        NativeBridgeStatus status;

        try
        {
            status =
                bridge.StartRecording(
                    new NativeStartRequest(
                        _captureMode,
                        _selectedSource.NativeToken,
                        _settings.FrameRate,
                        _settings.VisualStyle switch
                        {
                            RecordingVisualStyle.CleanScreen =>
                                NativeVisualMode.CleanScreen,
                            RecordingVisualStyle.VividPresentation =>
                                NativeVisualMode.VividPresentation,
                            _ =>
                                NativeVisualMode.PixelAccurate
                        },
                        flags,
                        _microphoneDeviceToken,
                        _cameraDeviceToken,
                        _settings.OutputFolder,
                        MomentaryHotkey(
                            HotkeyActionIds.HoldZoom),
                        MomentaryHotkey(
                            HotkeyActionIds.OverviewPeek),
                        _settings.PresenterZoom,
                        _settings.SpotlightSize,
                        _settings.SpotlightMotion,
                        _settings.SpotlightDimStrength));
        }
        catch (Exception)
        {
            ShowCommandFeedback(
                "Native bridge error",
                "Recorder command bridge became unavailable.");
            return;
        }

        if (status !=
            NativeBridgeStatus.Ok)
        {
            HandleNativeStartFailure(
                status);
            return;
        }

        _nativeResult = null;
        _lastNativeSnapshot =
            bridge.Snapshot();

        ApplyNativeSessionState(
            _lastNativeSnapshot);

        var countdownBounds =
            bridge.CountdownBounds(
                _captureMode,
                _selectedSource.Bounds);

        _startSequenceActive =
            true;
        _countdown =
            new RecordingCountdownWindow(
                countdownBounds);

        // First visible countdown frame must already contain "3". Starting
        // the monotonic visual clock before Show() overlaps native Preparing
        // instead of exposing a dark, apparently stalled pre-roll.
        var countdownClock =
            Stopwatch.StartNew();
        _countdown.ShowNumber(
            RecordingStartCountdownPolicy.
                NumberForElapsed(
                    TimeSpan.Zero));
        _countdown.Show();
        Hide();

        try
        {
            await RunArmedCountdownAsync(
                bridge,
                countdownClock);
        }
        finally
        {
            _countdown?.Close();
            _countdown = null;
            _startSequenceActive =
                false;
        }
    }

    private async System.Threading.Tasks.Task
        RunArmedCountdownAsync(
            NativeBridgeClient bridge,
            Stopwatch countdownClock)
    {
        var visibleNumber = 3;

        // Preparation and the three-second visual countdown intentionally run
        // in parallel. Native Armed remains the hard readiness barrier:
        // ACTION is impossible until BOTH the visual minimum and native
        // readiness are satisfied.
        while (true)
        {
            await System.Threading.Tasks.Task.Delay(
                25);

            NativeRecorderSnapshot snapshot;
            try
            {
                snapshot =
                    bridge.Snapshot();
            }
            catch (Exception)
            {
                Show();
                ShowCommandFeedback(
                    "Native bridge error",
                    "Could not observe recorder preparation.");
                return;
            }

            _lastNativeSnapshot =
                snapshot;

            if (snapshot.State ==
                NativeRecorderState.Failed)
            {
                Show();
                ApplyNativeSessionState(
                    snapshot);
                return;
            }

            if (snapshot.State ==
                NativeRecorderState.Idle)
            {
                Show();
                _lastNativeSnapshot =
                    null;
                ApplyNativeReadyState();
                return;
            }

            if (!RecordingStartCountdownPolicy.
                    IsPreCommitState(
                        snapshot.State))
            {
                Show();
                ApplyNativeSessionState(
                    snapshot);
                return;
            }

            var nextNumber =
                RecordingStartCountdownPolicy.
                    NumberForElapsed(
                        countdownClock.Elapsed);

            if (nextNumber !=
                visibleNumber)
            {
                visibleNumber =
                    nextNumber;
                _countdown?.ShowNumber(
                    visibleNumber);
            }

            if (RecordingStartCountdownPolicy.
                    CanCommit(
                        countdownClock.Elapsed,
                        snapshot.State))
            {
                break;
            }
        }

        // Keep the user-facing target dimmed while commit crosses into the
        // native media-clock authority. The countdown HWND is capture-excluded,
        // so WGC still sees the normal source behind it. This guarantees the
        // user never sees a normal desktop while the recorder is still Armed.
        NativeBridgeStatus commit;
        try
        {
            commit =
                bridge.CommitStart();
        }
        catch (Exception)
        {
            Show();
            ShowCommandFeedback(
                "Native bridge error",
                "Could not commit the armed recording start.");
            return;
        }

        if (commit !=
            NativeBridgeStatus.Ok)
        {
            Show();
            ApplyNativeReadyState();
            return;
        }

        NativeRecorderSnapshot recording;
        while (true)
        {
            await System.Threading.Tasks.Task.Delay(
                10);

            recording =
                bridge.Snapshot();
            _lastNativeSnapshot =
                recording;

            if (RecordingStartCountdownPolicy.
                    CanRevealAction(
                        recording.State))
                break;

            if (recording.State ==
                    NativeRecorderState.Failed ||
                recording.State ==
                    NativeRecorderState.Idle)
            {
                Show();
                ApplyNativeSessionState(
                    recording);
                return;
            }
        }

        // Native media time is already live before the dim layer clears.
        // ACTION therefore becomes a truthful recording-start cue rather than
        // a promise that recording will begin shortly afterwards.
        _countdown?.ShowAction();

        _controller =
            new RecordingControllerWindow(
                bridge,
                _settings,
                _session.MicrophoneEnabled,
                _session.CameraEnabled);

        _controller.StopRequested +=
            Controller_OnStopRequested;

        _controller.Show();

        // Let ACTION read as an intentional cue without dimming the captured
        // desktop. The countdown HWND is excluded from WGC throughout.
        await System.Threading.Tasks.Task.Delay(
            180);
    }

    private void StartInteractionPreview()
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

    private void HandleNativeStartFailure(
        NativeBridgeStatus status)
    {
        switch (status)
        {
            case NativeBridgeStatus.Unsupported:
                if (_captureMode ==
                    PreviewCaptureMode.Game)
                {
                    ShowCommandFeedback(
                        "Game backend pending",
                        "Game remains visible product intent, but recording is blocked until its native backend exists.");
                }
                else
                {
                    ShowCommandFeedback(
                        "Audio unavailable",
                        _audioHardwarePreview
                            ? "Selected audio device is unavailable or the native bridge is not an experimental FFmpeg build."
                            : "System audio, Microphone and Camera require the experimental audio build; public release remains video-only.");
                }
                break;

            case NativeBridgeStatus.StaleToken:
                RefreshSources(
                    keepCurrentSelection: false);
                ShowCommandFeedback(
                    "Source changed",
                    "The native source generation changed. Choose the refreshed source and try again.");
                break;

            case NativeBridgeStatus.Busy:
                ShowCommandFeedback(
                    "Recorder busy",
                    "The previous native session is still finalizing.");
                break;

            case NativeBridgeStatus.StartFailed:
                ShowCommandFeedback(
                    "Could not start",
                    "The native RecorderSession rejected the recording configuration.");
                break;

            default:
                ShowCommandFeedback(
                    "Could not start",
                    $"Native command status: {status}.");
                break;
        }
    }

    private void ShowCommandFeedback(
        string title,
        string detail)
    {
        StatusDot.Fill =
            _warningBrush;
        StatusText.Foreground =
            _warningBrush;
        StatusText.Text =
            title;
        StatusDetail.Text =
            detail;
        SavedActions.IsVisible =
            false;
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

        if (_nativeBridge is not null)
        {
            try
            {
                ApplyNativeSessionState(
                    _nativeBridge.Snapshot());
            }
            catch (Exception)
            {
                ShowCommandFeedback(
                    "Native bridge error",
                    "Could not read the completed recorder state.");
            }
        }
        else
        {
            ApplySessionState();
        }
    }

    private void OpenSaved_OnClick(
        object? sender,
        RoutedEventArgs e)
    {
        if (_nativeResult is null ||
            string.IsNullOrWhiteSpace(
                _nativeResult.OutputPath))
        {
            StatusDetail.Text =
                "No native recording result is available.";
            return;
        }

        try
        {
            Process.Start(
                new ProcessStartInfo(
                    _nativeResult.OutputPath)
                {
                    UseShellExecute = true
                });
        }
        catch (Exception)
        {
            StatusDetail.Text =
                "Windows could not open the saved recording.";
        }
    }

    private void ShowFolder_OnClick(
        object? sender,
        RoutedEventArgs e)
    {
        if (_nativeResult is null ||
            string.IsNullOrWhiteSpace(
                _nativeResult.OutputPath))
        {
            StatusDetail.Text =
                "No native output path is available.";
            return;
        }

        try
        {
            Process.Start(
                new ProcessStartInfo
                {
                    FileName =
                        "explorer.exe",
                    Arguments =
                        $"/select,\"{_nativeResult.OutputPath}\"",
                    UseShellExecute =
                        true
                });
        }
        catch (Exception)
        {
            var folder =
                Path.GetDirectoryName(
                    _nativeResult.OutputPath);

            StatusDetail.Text =
                string.IsNullOrWhiteSpace(folder)
                    ? "Windows could not open the output folder."
                    : folder;
        }
    }

    private void ApplySessionState()
    {
        if (_nativeBridge is not null)
        {
            try
            {
                ApplyNativeSessionState(
                    _nativeBridge.Snapshot());
            }
            catch (Exception)
            {
                ShowCommandFeedback(
                    "Native bridge error",
                    "Recorder state is temporarily unavailable.");
            }

            return;
        }

        if (!_allowInteractionPreview)
        {
            ApplyEngineUnavailableState();
            return;
        }

        RefreshInputLabels();

        switch (_session.Phase)
        {
            case PreviewRecordingPhase.Recording:
                StatusDot.Fill =
                    _recordBrush;
                StatusText.Foreground =
                    _recordHoverBrush;
                StatusText.Text =
                    "Recording";
                StatusDetail.Text =
                    "Floating controller interaction preview";
                SavedActions.IsVisible =
                    false;
                break;

            case PreviewRecordingPhase.Paused:
                StatusDot.Fill =
                    _warningBrush;
                StatusText.Foreground =
                    _warningBrush;
                StatusText.Text =
                    "Paused";
                StatusDetail.Text =
                    "Recording interaction preview paused";
                SavedActions.IsVisible =
                    false;
                break;

            case PreviewRecordingPhase.Saved:
                StatusDot.Fill =
                    _successBrush;
                StatusText.Foreground =
                    _successBrush;
                StatusText.Text =
                    "Saved";
                StatusDetail.Text =
                    $"{_session.SavedFileName} · {FormatElapsed(_session.Elapsed)}";
                SavedActions.IsVisible =
                    true;
                RecordText.Text =
                    "Start";
                RecordIcon.Kind =
                    LucideIconKind.Circle;
                RecordButton.SetValue(
                    AutomationProperties.NameProperty,
                    "Start another recording");
                break;

            default:
                ApplyNativeReadyState();
                break;
        }
    }

    private void ApplyNativeSessionState(
        NativeRecorderSnapshot snapshot)
    {
        _lastNativeSnapshot =
            snapshot;

        RefreshInputLabels();

        switch (snapshot.State)
        {
            case NativeRecorderState.Preparing:
                StatusDot.Fill =
                    _recordBrush;
                StatusText.Foreground =
                    _recordHoverBrush;
                StatusText.Text =
                    "Preparing";
                StatusDetail.Text =
                    "Native RecorderSession is preparing the capture pipeline.";
                SavedActions.IsVisible =
                    false;
                break;

            case NativeRecorderState.Armed:
                StatusDot.Fill =
                    _warningBrush;
                StatusText.Foreground =
                    _warningBrush;
                StatusText.Text =
                    "Armed";
                StatusDetail.Text =
                    "Capture pipeline is warm; recording starts on ACTION.";
                SavedActions.IsVisible =
                    false;
                break;

            case NativeRecorderState.Recording:
                StatusDot.Fill =
                    _recordBrush;
                StatusText.Foreground =
                    _recordHoverBrush;
                StatusText.Text =
                    "Recording";
                StatusDetail.Text =
                    $"Native video · {FormatNativeElapsed(snapshot.ElapsedTicks)}";
                SavedActions.IsVisible =
                    false;
                break;

            case NativeRecorderState.Stopping:
            case NativeRecorderState.Finalizing:
                StatusDot.Fill =
                    _warningBrush;
                StatusText.Foreground =
                    _warningBrush;
                StatusText.Text =
                    snapshot.State ==
                            NativeRecorderState.Stopping
                        ? "Stopping"
                        : "Finalizing";
                StatusDetail.Text =
                    "Native recorder is finishing the MP4; the UI remains nonblocking.";
                SavedActions.IsVisible =
                    false;
                break;

            case NativeRecorderState.Ready:
                try
                {
                    _nativeResult =
                        _nativeBridge?.Result();
                }
                catch (Exception)
                {
                    _nativeResult = null;
                }

                StatusDot.Fill =
                    _successBrush;
                StatusText.Foreground =
                    _successBrush;
                StatusText.Text =
                    "Saved";
                StatusDetail.Text =
                    _nativeResult is not null &&
                    !string.IsNullOrWhiteSpace(
                        _nativeResult.OutputPath)
                        ? $"{Path.GetFileName(_nativeResult.OutputPath)} · {FormatNativeElapsed(snapshot.ElapsedTicks)}"
                        : $"Native recording saved · {FormatNativeElapsed(snapshot.ElapsedTicks)}";
                SavedActions.IsVisible =
                    _nativeResult is not null &&
                    !string.IsNullOrWhiteSpace(
                        _nativeResult.OutputPath);
                RecordText.Text =
                    "Start";
                RecordIcon.Kind =
                    LucideIconKind.Circle;
                RecordButton.SetValue(
                    AutomationProperties.NameProperty,
                    "Start another recording");
                break;

            case NativeRecorderState.Failed:
                try
                {
                    _nativeResult =
                        _nativeBridge?.Result();
                }
                catch (Exception)
                {
                    _nativeResult = null;
                }

                StatusDot.Fill =
                    _recordBrush;
                StatusText.Foreground =
                    _recordHoverBrush;
                StatusText.Text =
                    "Recording failed";
                StatusDetail.Text =
                    $"Native error {snapshot.ErrorCode} · detail 0x{snapshot.ErrorDetail:X8}";
                SavedActions.IsVisible =
                    false;
                break;

            default:
                ApplyNativeReadyState();
                break;
        }
    }

    private void ApplyNativeReadyState()
    {
        StatusDot.Fill =
            _successBrush;
        StatusText.Foreground =
            _successBrush;
        StatusText.Text =
            "Ready";
        SavedActions.IsVisible =
            false;
        RecordText.Text =
            "Start";
        RecordIcon.Kind =
            LucideIconKind.Circle;
        RecordButton.SetValue(
            AutomationProperties.NameProperty,
            "Start recording");
        UpdateRecordAvailability();
        UpdateReadyDetail();
    }

    private void ApplyEngineUnavailableState()
    {
        StatusDot.Fill =
            _warningBrush;
        StatusText.Foreground =
            _warningBrush;
        StatusText.Text =
            "Engine unavailable";
        StatusDetail.Text =
            BridgeUnavailableMessage();
        SavedActions.IsVisible =
            false;
        RecordText.Text =
            "Start";
        RecordIcon.Kind =
            LucideIconKind.Circle;
        RecordButton.IsEnabled =
            false;
        RecordButton.SetValue(
            AutomationProperties.NameProperty,
            "Recording unavailable");
    }

    private void UpdateRecordAvailability()
    {
        RecordButton.IsEnabled =
            _allowInteractionPreview ||
            (_nativeBridge is not null &&
             _captureMode !=
                 PreviewCaptureMode.Game &&
             _selectedSource is not null &&
             _selectedSource.NativeToken != 0);
    }

    private void ApplyProductCapabilitySurface()
    {
        // The public build is deliberately video-only. Hardware acceptance
        // binaries, however, record real WASAPI audio, so a static
        // "backend pending" tooltip is misleading and is NOT acceptable.
        // Keep this capability-derived instead of relying on XAML defaults.
        if (_audioHardwarePreview)
        {
            ToolTip.SetTip(SystemAudioToggle,
                "Record sound from the Windows default playback device.");
            ToolTip.SetTip(MicToggle,
                "Record audio from the selected microphone.");
            ToolTip.SetTip(MicrophoneDeviceComboMain,
                "Choose the microphone to use for the recording.");
        }
        else if (_allowInteractionPreview)
        {
            ToolTip.SetTip(SystemAudioToggle,
                "Audio controls are a visual preview; no audio is recorded.");
            ToolTip.SetTip(MicToggle,
                "Audio controls are a visual preview; no audio is recorded.");
            ToolTip.SetTip(MicrophoneDeviceComboMain,
                "Device selection is simulated in interaction preview.");
        }
        else
        {
            ToolTip.SetTip(SystemAudioToggle,
                "System audio recording is unavailable in this build.");
            ToolTip.SetTip(MicToggle,
                "Microphone recording is unavailable in this build.");
            ToolTip.SetTip(MicrophoneDeviceComboMain,
                "Microphone selection is unavailable in this build.");
        }

        if (_allowInteractionPreview)
            return;

        if (_audioHardwarePreview)
        {
            // Deliberate hardware acceptance only, not a default release.
            // Mic selection is the actual MMDevice token, not fake examples.
            SystemAudioToggle.IsEnabled = true;
            MicToggle.IsEnabled = true;
            MicrophoneDeviceComboMain.IsEnabled =
                _microphoneDeviceToken != 0;
            // Once chosen, preserve these flags until the user changes them
            // (never rewrite them on Ready/Saved UI refresh).
            RefreshInputLabels();
            SystemAudioDeviceComboMain.IsEnabled = false;
            CameraToggle.IsChecked = false;
            CameraToggle.IsEnabled = false;
            CameraDeviceButton.IsEnabled = false;
            CameraDeviceComboMain.IsEnabled = false;
            CameraDeviceChevron.IsVisible = false;
            foreach (var option in _cameraOptions)
                option.IsVisible = false;
            UpdateRecordAvailability();
            return;
        }

        _systemAudioEnabled =
            false;
        _session.MicrophoneEnabled =
            false;
        _session.CameraEnabled =
            false;

        SystemAudioToggle.IsChecked =
            false;
        SystemAudioToggle.IsEnabled =
            false;

        MicDeviceButton.IsEnabled =
            false;
        MicDeviceChevron.IsVisible =
            false;
        MicToggle.IsChecked =
            false;
        MicToggle.IsEnabled =
            false;
        MicDeviceText.Text =
            _nativeBridge is null
                ? "Engine unavailable"
                : "Backend pending";

        CameraDeviceButton.IsEnabled =
            false;
        CameraDeviceChevron.IsVisible =
            false;
        CameraToggle.IsChecked =
            false;
        CameraToggle.IsEnabled =
            false;
        CameraDeviceText.Text =
            _nativeBridge is null
                ? "Engine unavailable"
                : "Backend pending";

        foreach (var option in
                 _microphoneOptions)
        {
            option.IsVisible =
                false;
        }

        foreach (var option in
                 _cameraOptions)
        {
            option.IsVisible =
                false;
        }

        UpdateRecordAvailability();
    }

    private void UpdateReadyDetail()
    {
        if (_nativeBridge is not null)
        {
            if (_lastNativeSnapshot?.State is
                NativeRecorderState.Preparing or
                NativeRecorderState.Armed or
                NativeRecorderState.Recording or
                NativeRecorderState.Stopping or
                NativeRecorderState.Finalizing or
                NativeRecorderState.Ready or
                NativeRecorderState.Failed)
            {
                return;
            }
        }
        else if (_session.Phase is
                 PreviewRecordingPhase.Recording or
                 PreviewRecordingPhase.Paused or
                 PreviewRecordingPhase.Saved)
        {
            return;
        }

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

        if (_nativeBridge is null &&
            !_allowInteractionPreview)
        {
            StatusDetail.Text =
                BridgeUnavailableMessage();
            return;
        }

        var bridge =
            _nativeBridge is null
                ? "UI preview only"
                : "native engine ready";

        var inputState =
            _audioHardwarePreview
                ? (_systemAudioEnabled || _session.MicrophoneEnabled
                    ? "audio hardware test" : "audio test available")
                : _allowInteractionPreview &&
                  (_systemAudioEnabled ||
                   _session.MicrophoneEnabled ||
                   _session.CameraEnabled)
                    ? "preview inputs"
                    : "video ready";

        var hotkey =
            _nativeBridge is not null &&
            !_allowInteractionPreview
                ? IsHotkeyRegistered(
                      NativeHotkeyAction.ToggleRecord)
                    ? $"global {_settings.RecordHotkey}"
                    : $"{_settings.RecordHotkey} unavailable"
                : _settings.RecordHotkey;

        StatusDetail.Text =
            $"{source} · {bridge} · {inputState} · {_settings.FrameRate} fps · {VisualStyleLabel()} · Zoom {(_settings.SmartZoom ? "on" : "off")} · {hotkey}";
    }

    private static string FormatNativeElapsed(
        long ticks) =>
        FormatElapsed(
            TimeSpan.FromTicks(
                Math.Max(
                    0,
                    ticks)));

    private string VisualStyleLabel() =>
        _settings.VisualStyle switch
        {
            RecordingVisualStyle.CleanScreen =>
                "Clean Screen",
            RecordingVisualStyle.VividPresentation =>
                "Vivid Presentation",
            _ =>
                "Pixel Accurate"
        };

    private string BridgeUnavailableMessage() =>
        _bridgeAvailability switch
        {
            NativeBridgeAvailability.MissingLibrary =>
                "Native engine package is incomplete. Use the complete Arssyut build instead of running a detached EXE.",
            NativeBridgeAvailability.LibraryLoadFailed =>
                "The native engine file was found, but Windows could not load it. Reinstall the complete build.",
            NativeBridgeAvailability.IncompatibleAbi =>
                "Native engine and UI versions do not match. Reinstall the complete build.",
            NativeBridgeAvailability.InitializationFailed =>
                "Native engine could not initialize on this system.",
            _ =>
                "Native engine source snapshot is unavailable."
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
