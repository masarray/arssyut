using System;
using System.Collections.Generic;
using System.Linq;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Media.Imaging;
using System.IO;
using System.Globalization;
using System.Runtime.InteropServices;
using Avalonia.Platform;
using Avalonia.Platform.Storage;
using Avalonia.Threading;
using Arssyut.UI.Design;
using Arssyut.UI.Interop;
using Arssyut.UI.Preview;

namespace Arssyut.UI;

public sealed partial class SettingsWindow : Window
{
    private readonly SettingsPreviewState _preview;
    private readonly NativeBridgeClient? _nativeBridge;
    private readonly bool _stressLayout;
    private Bitmap? _logoPreviewBitmap;
    private readonly DispatcherTimer _cameraPreviewTimer;
    private WriteableBitmap? _cameraSettingsBitmap;
    private readonly byte[] _cameraSettingsPixels =
        new byte[NativeBridgeClient.WebcamPreviewByteCount];
    private ulong _cameraSettingsSequence;
    private double _cameraSourceAspect = 9.0 / 16.0;
    private bool _cameraActivePicture;
    private readonly Button[] _navButtons;
    private readonly Control[] _pages;
    private readonly Button[] _cameraAnchorButtons;
    private readonly Button[] _hotkeyButtons;
    private readonly DispatcherTimer _audioPreviewTimer;
    private readonly IBrush _warningBrush;
    private readonly IBrush _successBrush;
    private readonly IBrush _textSecondaryBrush;

    private string? _capturingHotkeyAction;
    private ulong _settingsMicrophoneToken;
    private bool _loadingPlaybackDevices;
    private readonly bool _audioSessionLocked;
    private bool _audioReady;
    private bool _uiReady;
    private bool _syncingPreviewControls;

    public SettingsWindow(
        SettingsPreviewState preview,
        NativeBridgeClient? nativeBridge = null,
        bool stressLayout = false,
        bool audioSessionLocked = false)
    {
        _audioSessionLocked = audioSessionLocked;
        _preview = preview;
        _nativeBridge = nativeBridge;
        _stressLayout = stressLayout;
        _audioReady = nativeBridge is not null &&
            string.Equals(Environment.GetEnvironmentVariable(
                "ARSSYUT_AUDIO_PREVIEW"), "1", StringComparison.Ordinal);
        InitializeComponent();

        _warningBrush =
            ArBrushResolver.Require(
                this,
                "ArBrush.Warning");
        _successBrush =
            ArBrushResolver.Require(
                this,
                "ArBrush.Success");
        _textSecondaryBrush =
            ArBrushResolver.Require(
                this,
                "ArBrush.TextSecondary");

        PresenterZoomCombo.ItemsSource =
            SettingsPreviewState.
                PresenterZoomPresets.
                Select(
                    zoom =>
                        $"{zoom:0.00}×").
                ToArray();

        _uiReady = true;

        TransparencyLevelHint =
        [
            WindowTransparencyLevel.Mica,
            WindowTransparencyLevel.AcrylicBlur,
            WindowTransparencyLevel.None
        ];

        _navButtons =
        [
            NavGeneral,
            NavRecording,
            NavOutput,
            NavAudio,
            NavCamera,
            NavLogo,
            NavMouse,
            NavHotkeys,
            NavAdvanced
        ];

        _pages =
        [
            PageGeneral,
            PageRecording,
            PageOutput,
            PageAudio,
            PageCamera,
            PageLogo,
            PageMouse,
            PageHotkeys,
            PageAdvanced
        ];

        _cameraAnchorButtons =
        [
            AnchorTopLeft,
            AnchorTopRight,
            AnchorBottomLeft,
            AnchorBottomRight
        ];

        _hotkeyButtons =
        [
            RecordHotkeyButton,
            ToggleZoomHotkeyButton,
            HoldZoomHotkeyButton,
            ZoomInHotkeyButton,
            ZoomOutHotkeyButton,
            ResetZoomHotkeyButton,
            OverviewPeekHotkeyButton,
            PauseHotkeyButton,
            MicrophoneHotkeyButton,
            CameraHotkeyButton
        ];

        _audioPreviewTimer =
            new DispatcherTimer
            {
                Interval =
                    TimeSpan.FromMilliseconds(
                        180)
            };

        _audioPreviewTimer.Tick +=
            AudioPreviewTimer_OnTick;
        _cameraPreviewTimer = new DispatcherTimer {
            Interval = TimeSpan.FromMilliseconds(120)
        };
        _cameraPreviewTimer.Tick += CameraSettingsPreview_OnTick;

        Opened +=
            (_, _) =>
            {
                if (!_stressLayout)
                    RefreshNativeDevices();

                RefreshPreviewState();
            };

        Closed +=
            (_, _) => {
                _audioPreviewTimer.Stop();
                _cameraPreviewTimer.Stop();
                _cameraSettingsBitmap?.Dispose();
                _logoPreviewBitmap?.Dispose();
            };

        if (stressLayout)
        {
            _preview.SetOutputFolder(
                @"C:\Users\Presentation\Videos\Arssyut\Customer Demonstration Session\Extremely Long Output Folder Name For Layout Stress");

            SelectPage("Output");
        }

        RefreshPreviewState();
    }

    private void TitleBar_OnPointerPressed(
        object? sender,
        PointerPressedEventArgs e)
    {
        if (e.GetCurrentPoint(this).Properties.IsLeftButtonPressed)
            BeginMoveDrag(e);
    }

    private void Close_OnClick(
        object? sender,
        RoutedEventArgs e) =>
        Close();

    private void Window_OnKeyDown(
        object? sender,
        KeyEventArgs e)
    {
        if (_capturingHotkeyAction is not null)
        {
            if (e.Key == Key.Escape)
            {
                CancelHotkeyCapture(
                    "Shortcut capture cancelled.");
                e.Handled = true;
                return;
            }

            if (IsModifierKey(e.Key))
            {
                e.Handled = true;
                return;
            }

            if (!HotkeyChord.TryFromKeyEvent(
                    e,
                    out var chord))
            {
                HotkeyFeedbackText.Text =
                    "That key is not supported by the canonical Windows hotkey map.";
                HotkeyFeedbackText.Foreground =
                    _warningBrush;
                e.Handled = true;
                return;
            }

            var isGlobalTrigger =
                TryNativeHotkeyAction(
                    _capturingHotkeyAction,
                    out _);
            var isMomentaryPresenter =
                IsMomentaryPresenterAction(
                    _capturingHotkeyAction);

            // Probe all native-facing shortcuts, including Raw-Input Hold/Peek.
            // The probe reserves then immediately releases a temporary
            // RegisterHotKey id; it never becomes the runtime owner.
            if (_nativeBridge is not null &&
                (isGlobalTrigger ||
                 isMomentaryPresenter))
            {
                var probe =
                    _nativeBridge.ProbeHotkey(
                        chord.Modifiers,
                        chord.VirtualKey);

                if (probe != NativeBridgeStatus.Ok)
                {
                    HotkeyFeedbackText.Text =
                        probe == NativeBridgeStatus.Busy
                            ? $"{chord.DisplayText} is already reserved by Windows or another application."
                            : "Windows could not validate that shortcut.";
                    HotkeyFeedbackText.Foreground =
                        _warningBrush;
                    e.Handled = true;
                    return;
                }
            }

            if (_preview.TrySetHotkey(
                    _capturingHotkeyAction,
                    chord,
                    out var error))
            {
                EndHotkeyCapture();
                HotkeyFeedbackText.Text =
                    $"{chord.DisplayText} assigned.";
                HotkeyFeedbackText.Foreground =
                    _successBrush;
                MarkPreviewChanged(
                    "Shortcut saved");
            }
            else
            {
                HotkeyFeedbackText.Text =
                    error;
                HotkeyFeedbackText.Foreground =
                    _warningBrush;
            }

            e.Handled = true;
            return;
        }

        if (e.Key == Key.Escape)
        {
            Close();
            e.Handled = true;
        }
    }

    private void Nav_OnClick(
        object? sender,
        RoutedEventArgs e)
    {
        if (sender is not Button selected)
            return;

        SelectPage(
            selected.Tag?.ToString() ??
            "Recording");
    }

    private void SelectPage(
        string tag)
    {
        var index =
            tag switch
            {
                "General" => 0,
                "Recording" => 1,
                "Output" => 2,
                "Audio" => 3,
                "Camera" => 4,
                "Logo" => 5,
                "Mouse" => 6,
                "Hotkeys" => 7,
                "Advanced" => 8,
                _ => 1
            };

        for (var i = 0;
             i < _navButtons.Length;
             ++i)
        {
            var active =
                i == index;

            if (active)
                _navButtons[i].
                    Classes.Add("selected");
            else
                _navButtons[i].
                    Classes.Remove("selected");

            _pages[i].IsVisible =
                active;
        }

        PageTitle.Text =
            tag == "Mouse"
                ? "Mouse & Keystroke"
                : tag;

        SettingsFooterHint.IsVisible = tag != "Audio";
        _audioPreviewTimer.Stop();
        _cameraPreviewTimer.Stop();
        if (tag == "Camera" && _nativeBridge is not null && !_stressLayout) {
            _cameraPreviewTimer.Start();
            CameraSettingsPreview_OnTick(this, EventArgs.Empty);
        }
        if (tag == "Audio" && _audioReady)
        {
            _audioPreviewTimer.Start();
            AudioPreviewTimer_OnTick(this, EventArgs.Empty);
        }

        PageSubtitle.Text =
            tag switch
            {
                "General" =>
                    "Workspace behavior and recorder preferences.",
                "Recording" =>
                    "Quality, presentation, and recording behavior.",
                "Output" =>
                    "Destination, naming, and successful-result behavior.",
                "Audio" =>
                    "Input levels and recording mix.",
                "Camera" =>
                    "GPU picture-in-picture position, size and shape.",
                "Logo" =>
                    "Independent PNG logo compositing on recorded MP4.",
                "Mouse" =>
                    "Pointer, click, and keyboard visualization.",
                "Hotkeys" =>
                    "Available recorder transport shortcuts.",
                "Advanced" =>
                    "Diagnostics and native-engine ownership status.",
                _ =>
                    string.Empty
            };
    }

    private void FrameRate_OnSelectionChanged(
        object? sender,
        SelectionChangedEventArgs e)
    {
        if (!_uiReady ||
            _syncingPreviewControls)
            return;

        if (sender is not ComboBox combo)
            return;

        _preview.SetFrameRate(
            combo.SelectedIndex == 0
                ? 30U
                : 60U);

        MarkPreviewChanged(
            "Frame rate updated");
    }

    private void VisualStyle_OnSelectionChanged(
        object? sender,
        SelectionChangedEventArgs e)
    {
        if (!_uiReady ||
            _syncingPreviewControls)
            return;

        if (sender is not ComboBox combo)
            return;

        _preview.SetVisualStyle(
            combo.SelectedIndex switch
            {
                1 =>
                    RecordingVisualStyle.CleanScreen,
                2 =>
                    RecordingVisualStyle.VividPresentation,
                _ =>
                    RecordingVisualStyle.PixelAccurate
            });

        MarkPreviewChanged(
            "Visual style updated");
    }

    private void PresenterZoom_OnSelectionChanged(
        object? sender,
        SelectionChangedEventArgs e)
    {
        if (!_uiReady ||
            _syncingPreviewControls ||
            sender is not ComboBox combo ||
            combo.SelectedIndex < 0 ||
            combo.SelectedIndex >=
                SettingsPreviewState.PresenterZoomPresets.Count)
            return;

        if (_preview.TrySetPresenterZoom(
                SettingsPreviewState.PresenterZoomPresets[
                    combo.SelectedIndex]))
        {
            MarkPreviewChanged(
                $"Presenter zoom set to {_preview.PresenterZoom:0.00}×");
        }
    }


    private void Spotlight_OnClick(
        object? sender,
        RoutedEventArgs e)
    {
        if (!_uiReady ||
            _syncingPreviewControls)
            return;

        _preview.SetSpotlightEnabled(
            SpotlightToggle.IsChecked ==
                true);

        MarkPreviewChanged(
            _preview.SpotlightEnabled
                ? "Spotlight enabled"
                : "Spotlight disabled");
    }

    private void SpotlightWithZoom_OnClick(
        object? sender,
        RoutedEventArgs e)
    {
        if (!_uiReady ||
            _syncingPreviewControls)
            return;

        _preview.SetSpotlightLinkToZoom(
            SpotlightWithZoomToggle.IsChecked ==
                true);

        MarkPreviewChanged(
            "Spotlight Zoom choreography updated");
    }

    private void SpotlightSize_OnSelectionChanged(
        object? sender,
        SelectionChangedEventArgs e)
    {
        if (!_uiReady ||
            _syncingPreviewControls ||
            sender is not ComboBox combo ||
            combo.SelectedIndex is < 0 or > 2)
            return;

        if (_preview.TrySetSpotlightSize(
                (NativeSpotlightSize)
                    combo.SelectedIndex))
        {
            MarkPreviewChanged(
                "Spotlight focus size updated");
        }
    }

    private void SpotlightStrength_OnSelectionChanged(
        object? sender,
        SelectionChangedEventArgs e)
    {
        if (!_uiReady ||
            _syncingPreviewControls ||
            sender is not ComboBox combo ||
            combo.SelectedIndex < 0 ||
            combo.SelectedIndex >=
                SettingsPreviewState.
                    SpotlightStrengthPresets.Count)
            return;

        if (_preview.TrySetSpotlightDimStrength(
                SettingsPreviewState.
                    SpotlightStrengthPresets[
                        combo.SelectedIndex]))
        {
            MarkPreviewChanged(
                "Spotlight focus strength updated");
        }
    }

    private void SpotlightMotion_OnSelectionChanged(
        object? sender,
        SelectionChangedEventArgs e)
    {
        if (!_uiReady ||
            _syncingPreviewControls ||
            sender is not ComboBox combo ||
            combo.SelectedIndex is < 0 or > 2)
            return;

        if (_preview.TrySetSpotlightMotion(
                (NativeSpotlightMotion)
                    combo.SelectedIndex))
        {
            MarkPreviewChanged(
                "Spotlight motion updated");
        }
    }

    private void SmartZoom_OnClick(
        object? sender,
        RoutedEventArgs e)
    {
        if (!_uiReady ||
            _syncingPreviewControls)
            return;

        _preview.SetSmartZoom(
            SmartZoomToggle.IsChecked ==
                true);

        MarkPreviewChanged(
            "Smart Zoom updated");
    }

    private void ClickHighlight_OnClick(
        object? sender,
        RoutedEventArgs e)
    {
        if (!_uiReady ||
            _syncingPreviewControls)
            return;

        _preview.SetClickHighlight(
            ClickHighlightToggle.IsChecked ==
                true);

        MarkPreviewChanged(
            "Click highlight updated");
    }

    private void ShortcutKeys_OnClick(
        object? sender,
        RoutedEventArgs e)
    {
        if (!_uiReady ||
            _syncingPreviewControls)
            return;

        _preview.SetShortcutKeys(
            ShortcutKeysToggle.IsChecked ==
                true);

        MarkPreviewChanged(
            "Shortcut visualization updated");
    }

    private async void OutputFolder_OnClick(
        object? sender,
        RoutedEventArgs e)
    {
        if (!StorageProvider.CanPickFolder)
        {
            PreviewStatusText.Text =
                "Folder picker unavailable";
            return;
        }

        var folders =
            await StorageProvider.
                OpenFolderPickerAsync(
                    new FolderPickerOpenOptions
                    {
                        Title =
                            "Choose Arssyut output folder",
                        AllowMultiple =
                            false
                    });

        var folder =
            folders.FirstOrDefault();

        if (folder is null)
            return;

        var path =
            folder.Path is
                { IsAbsoluteUri: true } uri
                ? uri.LocalPath
                : folder.Path?.ToString();

        _preview.SetOutputFolder(
            string.IsNullOrWhiteSpace(path)
                ? folder.Name
                : path);

        OutputFolderText.Text =
            _preview.OutputFolder;

        MarkPreviewChanged(
            "Output folder preview updated");
    }

    private void MicrophoneDevice_OnSelectionChanged(
        object? sender,
        SelectionChangedEventArgs e)
    {
        if (_syncingPreviewControls || !_audioReady ||
            sender is not ComboBox combo)
            return;
        if (combo.SelectedItem is ComboBoxItem { Tag: NativeDeviceItem device })
        {
            _settingsMicrophoneToken = device.Token;
            _preview.MicrophoneDevice = device.Name;
        }
    }

    private void CameraDevice_OnSelectionChanged(
        object? sender,
        SelectionChangedEventArgs e)
    {
        if (sender is not ComboBox combo)
            return;

        var value =
            SelectedItemText(
                combo.SelectedItem);

        if (!string.IsNullOrWhiteSpace(value))
            _preview.CameraDevice = value;
    }

    private void RefreshNativeDevices()
    {
        if (!_audioReady || _nativeBridge is null)
        {
            MicrophoneDeviceCombo.IsEnabled = false;
            SystemPlaybackCombo.IsEnabled = false;
            return;
        }
        try
        {
            // Do not call RefreshDevices(): that invalidates the source tokens
            // already selected in MainWindow and its active meter reader.
            var devices = _nativeBridge.SnapshotDevices();
            _syncingPreviewControls = true;
            try
            {
                MicrophoneDeviceCombo.Items.Clear();
                foreach (var device in devices.Microphones)
                {
                    MicrophoneDeviceCombo.Items.Add(new ComboBoxItem
                    {
                        Content = device.Name,
                        Tag = device
                    });
                }
                var selected = devices.Microphones
                    .Select((device, index) => (device, index))
                    .Where(item => string.Equals(item.device.Name,
                        _preview.MicrophoneDevice, StringComparison.Ordinal))
                    .Select(item => item.index)
                    .DefaultIfEmpty(0).First();
                if (devices.Microphones.Count > 0)
                {
                    MicrophoneDeviceCombo.SelectedIndex = selected;
                    _settingsMicrophoneToken =
                        devices.Microphones[selected].Token;
                }
                else
                    _settingsMicrophoneToken = 0;

                MicrophoneDeviceCombo.IsEnabled =
                    devices.Microphones.Count > 0;
                SystemPlaybackCombo.Items.Clear();
                SystemPlaybackCombo.Items.Add(new ComboBoxItem {
                    Content = SettingsPreviewState.DefaultPlaybackDevice
                });
                foreach (var device in devices.Playbacks)
                    SystemPlaybackCombo.Items.Add(new ComboBoxItem {
                        Content = device.Name, Tag = device
                    });
                var desired = _preview.SystemAudioDevice;
                var matched = devices.Playbacks
                    .Select((device, index) => (device,index))
                    .Where(x => string.Equals(x.device.Name, desired,
                                              StringComparison.Ordinal)).ToArray();
                if (desired == SettingsPreviewState.DefaultPlaybackDevice)
                    SystemPlaybackCombo.SelectedIndex = 0;
                else if (matched.Length == 1)
                    SystemPlaybackCombo.SelectedIndex = matched[0].index + 1;
                else {
                    SystemPlaybackCombo.Items.Add(new ComboBoxItem {
                        Content = $"Unavailable: {desired}", IsEnabled = false
                    });
                    SystemPlaybackCombo.SelectedIndex = SystemPlaybackCombo.Items.Count - 1;
                }
                SystemPlaybackCombo.IsEnabled = !_audioSessionLocked;
            }
            finally { _syncingPreviewControls = false; }
        }
        catch (Exception)
        {
            _settingsMicrophoneToken = 0;
            MicrophoneDeviceCombo.IsEnabled = false;
        }
    }

    private void SystemPlayback_OnSelectionChanged(
        object? sender, SelectionChangedEventArgs e)
    {
        if (!_uiReady || _syncingPreviewControls || _audioSessionLocked ||
            sender is not ComboBox combo ||
            combo.SelectedItem is not ComboBoxItem item || !item.IsEnabled)
            return;
        var selected = item.Tag as NativeDeviceItem;
        _preview.SetSystemAudioDevice(selected?.Name ??
            SettingsPreviewState.DefaultPlaybackDevice);
        if (_nativeBridge is not null)
            _ = _nativeBridge.SetSystemAudioEndpoint(selected?.Token ?? 0);
        MarkPreviewChanged("Playback endpoint saved for next recording");
    }

    private static string? SelectedItemText(
        object? selected) =>
        selected switch
        {
            ComboBoxItem item =>
                item.Content?.ToString(),
            null =>
                null,
            _ =>
                selected.ToString()
        };

    private void CameraAppearance_OnChange(object? sender, RoutedEventArgs e)
    {
        ApplyCameraAppearance();
    }

    private void CameraSlider_OnChange(object? sender,
        AvaloniaPropertyChangedEventArgs e)
    {
        if (e.Property == Slider.ValueProperty)
            ApplyCameraAppearance();
    }

    private void ApplyCameraAppearance()
    {
        if (!_uiReady || _syncingPreviewControls ||
            CameraMaskCombo is null || CameraSizeSlider is null)
            return;
        var mask = (CameraPipMask)Math.Max(0, CameraMaskCombo.SelectedIndex);
        var tone = (CameraBorderTone)Math.Max(0, CameraBorderCombo.SelectedIndex);
        if (!_preview.TrySetCameraOverlay(mask, tone,
            (float)(CameraSizeSlider.Value / 100.0),
            (float)(CameraMarginSlider.Value / 100.0),
            (float)(CameraRadiusSlider.Value / 100.0),
            (float)CameraBorderSlider.Value,
            (float)(CameraOpacitySlider.Value / 100.0),
            CameraMirrorCheck.IsChecked == true))
            return;
        CameraSizeText.Text = $"{CameraSizeSlider.Value:0.#}%";
        CameraMarginText.Text = $"{CameraMarginSlider.Value:0.#}%";
        CameraRadiusText.Text = $"{CameraRadiusSlider.Value:0}%";
        CameraBorderText.Text = $"{CameraBorderSlider.Value:0.#} px";
        CameraOpacityText.Text = $"{CameraOpacitySlider.Value:0}%";
        RenderCameraPreviewGeometry();
        MarkPreviewChanged("Camera PIP settings saved for next recording");
    }

    private void LogoAppearance_OnChange(object? sender,
        RoutedEventArgs e) => ApplyLogoAppearance();

    private void LogoSlider_OnChange(object? sender,
        AvaloniaPropertyChangedEventArgs e)
    {
        if (e.Property == Slider.ValueProperty) ApplyLogoAppearance();
    }

    private async void ChooseLogo_OnClick(object? sender, RoutedEventArgs e)
    {
        if (!StorageProvider.CanOpen) {
            LogoValidationText.Text = "PNG file picker unavailable.";
            return;
        }
        var options = new FilePickerOpenOptions {
            Title = "Choose transparent logo PNG",
            AllowMultiple = false,
            FileTypeFilter = new[] {
                new FilePickerFileType("PNG image") {
                    Patterns = new[] {"*.png"}
                }
            }
        };
        var choice = (await StorageProvider.OpenFilePickerAsync(options))
            .FirstOrDefault();
        if (choice is null) return;
        var path = choice.Path.LocalPath;
        if (!TryLogoPreview(path, out var reason)) {
            LogoValidationText.Text = reason;
            return;
        }
        if (!_preview.TrySetLogoOverlay(
            LogoEnabledCheck.IsChecked == true, path,
            (LogoPlacement)Math.Max(0, LogoPlacementCombo.SelectedIndex),
            (float)(LogoSizeSlider.Value / 100),
            (float)(LogoMarginSlider.Value / 100),
            (float)(LogoOpacitySlider.Value / 100))) {
            LogoValidationText.Text = "Logo settings are invalid.";
            return;
        }
        LogoPathText.Text = Path.GetFileName(path);
        LogoValidationText.Text = "PNG accepted; composited on next recording.";
        MarkPreviewChanged("Logo overlay settings saved");
    }

    private bool TryLogoPreview(string path, out string message)
    {
        message = "Invalid logo image.";
        try {
            var file = new FileInfo(path);
            if (!file.Exists || file.Length is <= 0 or > 16777216 ||
                !path.EndsWith(".png", StringComparison.OrdinalIgnoreCase))
                return false;
            var image = new Bitmap(path);
            if (image.PixelSize.Width is < 1 or > 4096 ||
                image.PixelSize.Height is < 1 or > 4096 ||
                (long)image.PixelSize.Width * image.PixelSize.Height > 8388608) {
                image.Dispose();
                return false;
            }
            var old = _logoPreviewBitmap;
            _logoPreviewBitmap = image;
            LogoPreviewImage.Source = image;
            old?.Dispose();
            message = "";
            return true;
        } catch (Exception) { return false; }
    }

    private void ApplyLogoAppearance()
    {
        if (!_uiReady || _syncingPreviewControls || LogoSizeSlider is null)
            return;
        var enabled = LogoEnabledCheck.IsChecked == true;
        if (!_preview.TrySetLogoOverlay(enabled, _preview.LogoImagePath,
                (LogoPlacement)Math.Max(0, LogoPlacementCombo.SelectedIndex),
                (float)(LogoSizeSlider.Value / 100),
                (float)(LogoMarginSlider.Value / 100),
                (float)(LogoOpacitySlider.Value / 100))) {
            if (enabled) LogoEnabledCheck.IsChecked = false;
            LogoValidationText.Text = "Choose a valid PNG before enabling logo.";
            return;
        }
        LogoSizeText.Text = $"{LogoSizeSlider.Value:0.#}%";
        LogoMarginText.Text = $"{LogoMarginSlider.Value:0.#}%";
        LogoOpacityText.Text = $"{LogoOpacitySlider.Value:0}%";
        MarkPreviewChanged("Logo overlay settings saved for next recording");
    }

    private void CameraPlacement_OnClick(
        object? sender,
        RoutedEventArgs e)
    {
        if (sender is not Button selected ||
            !Enum.TryParse<CameraPlacement>(
                selected.Tag?.ToString(),
                out var placement))
            return;

        _preview.SetCameraPlacement(
            placement);

        UpdateCameraPlacementVisual();

        MarkPreviewChanged(
            "Camera placement preview updated");
    }

    private void UpdateCameraPlacementVisual()
    {
        foreach (var button in
                 _cameraAnchorButtons)
            button.Classes.Remove(
                "selected");

        var selected =
            _preview.CameraPlacement switch
            {
                CameraPlacement.TopLeft =>
                    AnchorTopLeft,
                CameraPlacement.TopRight =>
                    AnchorTopRight,
                CameraPlacement.BottomLeft =>
                    AnchorBottomLeft,
                _ =>
                    AnchorBottomRight
            };

        selected.Classes.Add(
            "selected");

        RenderCameraPreviewGeometry();
    }

    private void CameraSettingsPreview_OnTick(object? sender, EventArgs e)
    {
        if (_nativeBridge is null || _stressLayout) return;
        try {
            var (width, height) = _nativeBridge.WebcamSourceDimensions();
            if (width >= 320 && height >= 240) {
                var aspect = (double)height / width;
                if (Math.Abs(aspect - _cameraSourceAspect) > .001) {
                    _cameraSourceAspect = aspect;
                    RenderCameraPreviewGeometry();
                }
            }
            var result = _nativeBridge.ReadWebcamPreview(
                _cameraSettingsPixels, out var seq, out var state);
            if (result != NativeBridgeStatus.Ok) return;
            if ((state != 2 && state != 4) || seq == 0) {
                _cameraActivePicture = false;
                CameraSettingsLiveFrame.IsVisible = false;
                CameraSettingsIdle.IsVisible = true;
                CameraSettingsIdle.Text = state == 1 ?
                    "Opening…" : "Camera OFF";
                if (state == 0) _cameraSettingsSequence = 0;
                UpdateCameraPreviewSourceInfo();
                return;
            }
            if (seq != _cameraSettingsSequence) {
                _cameraSettingsBitmap ??= new WriteableBitmap(
                    new PixelSize(NativeBridgeClient.WebcamPreviewWidth,
                                  NativeBridgeClient.WebcamPreviewHeight),
                    new Vector(96,96), PixelFormat.Bgra8888, AlphaFormat.Opaque);
                using (var fb = _cameraSettingsBitmap.Lock()) {
                    var rowLength = NativeBridgeClient.WebcamPreviewWidth * 4;
                    for (var y = 0; y < NativeBridgeClient.WebcamPreviewHeight; ++y)
                        Marshal.Copy(_cameraSettingsPixels, y * rowLength,
                            IntPtr.Add(fb.Address, y * fb.RowBytes), rowLength);
                }
                CameraSettingsLiveFrame.Source = _cameraSettingsBitmap;
                CameraSettingsLiveFrame.InvalidateVisual();
                _cameraSettingsSequence = seq;
            }
            _cameraActivePicture = true;
            CameraSettingsLiveFrame.IsVisible = true;
            CameraSettingsIdle.IsVisible = false;
            UpdateCameraPreviewSourceInfo();
        } catch (Exception) {
            _cameraActivePicture = false;
            CameraSettingsLiveFrame.IsVisible = false;
            CameraSettingsIdle.IsVisible = true;
            CameraSettingsIdle.Text = "Preview unavailable";
            UpdateCameraPreviewSourceInfo();
        }
    }

    private void UpdateCameraPreviewSourceInfo()
    {
        if (CameraPreviewSourceInfo is null) return;
        CameraPreviewSourceInfo.Text = _cameraActivePicture
            ? "Live webcam · output layout 1920 × 1080"
            : "Camera not live · shape and position represent output geometry";
    }

    // Exact compositor geometry: output-space width/margins, camera-source
    // aspect for noncircle, isotropic circle diameter for Circle.
    // A 16:9 stage represents the actual 1920x1080 encoded output.
    private void RenderCameraPreviewGeometry()
    {
        if (CameraOutputStage is null || CameraOverlaySurface is null)
            return;
        const double stageW = 448.0, stageH = 252.0;
        var mask = _preview.CameraMask;
        var margin = _preview.CameraMarginFraction;
        // Same normalized rule as native: height remains consistent
        // when switching Circle, rounded rectangle, and diagonal masks.
        var h = _preview.CameraWidthFraction * stageW;
        var w = mask == CameraPipMask.Circle
            ? h : h / _cameraSourceAspect;
        var fit = Math.Min(1.0, Math.Min(
            (1.0 - 2.0 * margin) * stageW / w,
            (1.0 - 2.0 * margin) * stageH / h));
        w *= fit;
        h *= fit;
        var place = _preview.CameraPlacement;
        var right = place is CameraPlacement.TopRight or CameraPlacement.BottomRight;
        var bottom = place is CameraPlacement.BottomLeft or CameraPlacement.BottomRight;
        var x = right ? stageW * (1 - margin) - w : stageW * margin;
        var y = bottom ? stageH * (1 - margin) - h : stageH * margin;
        CameraOverlaySurface.Width = w;
        CameraOverlaySurface.Height = h;
        Canvas.SetLeft(CameraOverlaySurface, x);
        Canvas.SetTop(CameraOverlaySurface, y);

        var r = Math.Min(w, h) * _preview.CameraRadiusFraction / 2.0;
        var tl = mask == CameraPipMask.RoundedRectangle ||
                 mask == CameraPipMask.DiagonalTopLeftBottomRight ? r : 0;
        var tr = mask == CameraPipMask.RoundedRectangle ||
                 mask == CameraPipMask.DiagonalTopRightBottomLeft ? r : 0;
        var br = tl;
        var bl = tr;
        string n(double v) => v.ToString("0.###", CultureInfo.InvariantCulture);
        string path;
        if (mask == CameraPipMask.Circle) {
            var half = w / 2.0;
            path = $"M{n(w)},{n(half)} A{n(half)},{n(half)} 0 1 1 0,{n(half)} " +
                   $"A{n(half)},{n(half)} 0 1 1 {n(w)},{n(half)} Z";
        } else {
            path = $"M{n(tl)},0 L{n(w-tr)},0 " +
                   (tr > 0 ? $"Q{n(w)},0 {n(w)},{n(tr)} " : $"L{n(w)},0 ") +
                   $"L{n(w)},{n(h-br)} " +
                   (br > 0 ? $"Q{n(w)},{n(h)} {n(w-br)},{n(h)} " : $"L{n(w)},{n(h)} ") +
                   $"L{n(bl)},{n(h)} " +
                   (bl > 0 ? $"Q0,{n(h)} 0,{n(h-bl)} " : $"L0,{n(h)} ") +
                   $"L0,{n(tl)} " +
                   (tl > 0 ? $"Q0,0 {n(tl)},0 " : "L0,0 ") + "Z";
        }
        var geometry = Geometry.Parse(path);
        CameraOverlaySurface.Clip = geometry;
        CameraPreviewOutline.Data = geometry;
        CameraPreviewOutline.StrokeThickness =
            _preview.CameraBorderWidthPx <= 0 ? 0 :
                Math.Max(.75, _preview.CameraBorderWidthPx * stageW / 1920.0);
        CameraPreviewOutline.Stroke = _preview.CameraBorder switch {
            CameraBorderTone.Dark => Brushes.DarkSlateGray,
            CameraBorderTone.Red => Brushes.IndianRed,
            _ => Brushes.White
        };
        CameraOverlaySurface.Opacity = _preview.CameraOpacity;
        CameraSettingsLiveFrame.RenderTransform =
            new ScaleTransform(_preview.CameraMirror ? -1 : 1, 1);
        CameraSettingsLiveFrame.RenderTransformOrigin =
            new RelativePoint(.5,.5,RelativeUnit.Relative);
    }

    private static bool TryNativeHotkeyAction(
        string action,
        out NativeHotkeyAction nativeAction)
    {
        nativeAction =
            action switch
            {
                "Record" => NativeHotkeyAction.ToggleRecord,
                "Microphone" => NativeHotkeyAction.ToggleMicrophone,
                "Pause" => NativeHotkeyAction.TogglePause,
                "ToggleZoom" => NativeHotkeyAction.ToggleZoom,
                "ZoomIn" => NativeHotkeyAction.ZoomIn,
                "ZoomOut" => NativeHotkeyAction.ZoomOut,
                "ResetZoom" => NativeHotkeyAction.ResetFullFrame,
                "FreezeCamera" => NativeHotkeyAction.FreezeCamera,
                _ => NativeHotkeyAction.ToggleRecord
            };

        return action is
            "Record" or
            "Microphone" or
            "Pause" or
            "ToggleZoom" or
            "ZoomIn" or
            "ZoomOut" or
            "ResetZoom" or
            "FreezeCamera";
    }

    private static bool IsMomentaryPresenterAction(
        string action) =>
        action is
            "HoldZoom" or
            "OverviewPeek";

    private void Hotkey_OnClick(
        object? sender,
        RoutedEventArgs e)
    {
        if (sender is not Button selected)
            return;

        EndHotkeyCapture();

        _capturingHotkeyAction =
            selected.Tag?.ToString();

        if (_capturingHotkeyAction is null)
            return;

        selected.Classes.Add(
            "capturing");

        GetHotkeyText(
            _capturingHotkeyAction).Text =
                "Press shortcut…";

        HotkeyFeedbackText.Text =
            "Press the new key combination. Esc cancels capture.";
        HotkeyFeedbackText.Foreground =
            _textSecondaryBrush;

        selected.Focus();
    }

    private void CancelHotkeyCapture(
        string message)
    {
        EndHotkeyCapture();
        HotkeyFeedbackText.Text =
            message;
        HotkeyFeedbackText.Foreground =
            _textSecondaryBrush;
    }

    private void EndHotkeyCapture()
    {
        _capturingHotkeyAction =
            null;

        foreach (var button in
                 _hotkeyButtons)
            button.Classes.Remove(
                "capturing");

        RefreshHotkeyLabels();
    }

    private void RefreshHotkeyLabels()
    {
        RecordHotkeyText.Text =
            _preview.RecordHotkey;
        PauseHotkeyText.Text =
            _preview.PauseHotkey;
        MicrophoneHotkeyText.Text =
            _preview.MicrophoneHotkey;
        CameraHotkeyText.Text =
            _preview.CameraHotkey;
        ToggleZoomHotkeyText.Text =
            HotkeyLabel(
                _preview.ToggleZoomHotkey);
        HoldZoomHotkeyText.Text =
            HotkeyLabel(
                _preview.HoldZoomHotkey);
        ZoomInHotkeyText.Text =
            HotkeyLabel(
                _preview.ZoomInHotkey);
        ZoomOutHotkeyText.Text =
            HotkeyLabel(
                _preview.ZoomOutHotkey);
        ResetZoomHotkeyText.Text =
            HotkeyLabel(
                _preview.ResetZoomHotkey);
        OverviewPeekHotkeyText.Text =
            HotkeyLabel(
                _preview.OverviewPeekHotkey);
        FreezeCameraHotkeyText.Text =
            HotkeyLabel(
                _preview.FreezeCameraHotkey);
    }

    private static string HotkeyLabel(
        string value) =>
        string.IsNullOrWhiteSpace(value)
            ? "Set key"
            : value;

    private TextBlock GetHotkeyText(
        string action) =>
        action switch
        {
            "Pause" =>
                PauseHotkeyText,
            "Microphone" =>
                MicrophoneHotkeyText,
            "Camera" =>
                CameraHotkeyText,
            "ToggleZoom" =>
                ToggleZoomHotkeyText,
            "HoldZoom" =>
                HoldZoomHotkeyText,
            "ZoomIn" =>
                ZoomInHotkeyText,
            "ZoomOut" =>
                ZoomOutHotkeyText,
            "ResetZoom" =>
                ResetZoomHotkeyText,
            "OverviewPeek" =>
                OverviewPeekHotkeyText,
            "FreezeCamera" =>
                FreezeCameraHotkeyText,
            _ =>
                RecordHotkeyText
        };

    private void AudioPreviewTimer_OnTick(object? sender, EventArgs e)
    {
        if (!_audioReady || _nativeBridge is null)
        {
            ClearAudioMeters();
            return;
        }

        try
        {
            var meter = _nativeBridge.AudioMeter(
                _settingsMicrophoneToken,
                _preview.SystemAudioEnabled,
                _preview.MicrophoneEnabled && _settingsMicrophoneToken != 0);

            // Endpoint levels are PRE-fader. The canonical mixer uses this
            // exact per-source gain and mute for the encoded MP4. Display
            // the resulting POST-fader peaks, independently for L and R.
            var systemL = RecordedAudioMeter.PostFaderPeak(
                meter.SystemLeft, _preview.SystemAudioEnabled,
                _preview.SystemMixPercent, _preview.SystemMixMuted);
            var systemR = RecordedAudioMeter.PostFaderPeak(
                meter.SystemRight, _preview.SystemAudioEnabled &&
                meter.SystemChannels > 1,
                _preview.SystemMixPercent, _preview.SystemMixMuted);
            var micL = RecordedAudioMeter.PostFaderPeak(
                meter.MicrophoneLeft, _preview.MicrophoneEnabled &&
                _settingsMicrophoneToken != 0,
                _preview.MicrophoneMixPercent, _preview.MicrophoneMixMuted);
            var micR = RecordedAudioMeter.PostFaderPeak(
                meter.MicrophoneRight, _preview.MicrophoneEnabled &&
                _settingsMicrophoneToken != 0 && meter.MicrophoneChannels > 1,
                _preview.MicrophoneMixPercent, _preview.MicrophoneMixMuted);

            SystemMeter.Value = RecordedAudioMeter.DisplayPercent(systemL);
            SystemMeterR.Value = RecordedAudioMeter.DisplayPercent(systemR);
            MicMeter.Value = RecordedAudioMeter.DisplayPercent(micL);
            MicMeterR.Value = RecordedAudioMeter.DisplayPercent(micR);
            SystemDbText.Text = RecordedAudioMeter.DbLabel(Math.Max(systemL, systemR));
            MicDbText.Text = RecordedAudioMeter.DbLabel(Math.Max(micL, micR));
        }
        catch (Exception)
        {
            ClearAudioMeters();
        }
    }

    private void ClearAudioMeters()
    {
        SystemMeter.Value = SystemMeterR.Value = 0;
        MicMeter.Value = MicMeterR.Value = 0;
        SystemDbText.Text = MicDbText.Text = "−∞ dB";
    }

    private void SettingsSystemAudio_OnClick(object? sender, RoutedEventArgs e)
    {
        if (!_uiReady || _syncingPreviewControls ||
            !_audioReady || _audioSessionLocked) return;
        _preview.SetAudioPreferences(
            SettingsSystemAudioToggle.IsChecked == true,
            _preview.MicrophoneEnabled);
        RefreshAudioControlState();
        AudioPreviewTimer_OnTick(this, EventArgs.Empty);
    }

    private void SettingsMicrophone_OnClick(object? sender, RoutedEventArgs e)
    {
        if (!_uiReady || _syncingPreviewControls ||
            !_audioReady || _audioSessionLocked) return;
        _preview.SetAudioPreferences(
            _preview.SystemAudioEnabled,
            SettingsMicrophoneToggle.IsChecked == true);
        RefreshAudioControlState();
        AudioPreviewTimer_OnTick(this, EventArgs.Empty);
    }

    private void SystemGain_OnPropertyChanged(
        object? sender, AvaloniaPropertyChangedEventArgs e)
    {
        if (!_uiReady || _syncingPreviewControls ||
            !_audioReady || _audioSessionLocked ||
            e.Property != Slider.ValueProperty ||
            sender is not Slider slider) return;
        _preview.SetMixLevel(false, (int)Math.Round(slider.Value));
        SystemGainLabel.Text = $"{_preview.SystemMixPercent}%";
        AudioPreviewTimer_OnTick(this, EventArgs.Empty);
    }

    private void MicrophoneGain_OnPropertyChanged(
        object? sender, AvaloniaPropertyChangedEventArgs e)
    {
        if (!_uiReady || _syncingPreviewControls ||
            !_audioReady || _audioSessionLocked ||
            e.Property != Slider.ValueProperty ||
            sender is not Slider slider) return;
        _preview.SetMixLevel(true, (int)Math.Round(slider.Value));
        MicrophoneGainLabel.Text = $"{_preview.MicrophoneMixPercent}%";
        AudioPreviewTimer_OnTick(this, EventArgs.Empty);
    }

    private void SystemMute_OnClick(object? sender, RoutedEventArgs e)
    {
        if (!_audioReady || _audioSessionLocked ||
            !_preview.SystemAudioEnabled) return;
        _preview.SetMixMuted(false, !_preview.SystemMixMuted);
        RefreshAudioControlState();
        AudioPreviewTimer_OnTick(this, EventArgs.Empty);
    }

    private void MicrophoneMute_OnClick(object? sender, RoutedEventArgs e)
    {
        if (!_audioReady || _audioSessionLocked ||
            !_preview.MicrophoneEnabled) return;
        _preview.SetMixMuted(true, !_preview.MicrophoneMixMuted);
        RefreshAudioControlState();
        AudioPreviewTimer_OnTick(this, EventArgs.Empty);
    }

    private void RefreshAudioControlState()
    {
        // The shared persisted state is the single source of truth.
        var available = _audioReady && !_audioSessionLocked;
        SettingsSystemAudioToggle.IsEnabled = available;
        SettingsMicrophoneToggle.IsEnabled = available;
        SettingsSystemAudioToggle.IsChecked = _preview.SystemAudioEnabled;
        SettingsMicrophoneToggle.IsChecked = _preview.MicrophoneEnabled;
        SystemGainSlider.IsEnabled = available && _preview.SystemAudioEnabled;
        MicrophoneGainSlider.IsEnabled = available && _preview.MicrophoneEnabled;
        SystemMuteButton.IsEnabled = SystemGainSlider.IsEnabled;
        MicrophoneMuteButton.IsEnabled = MicrophoneGainSlider.IsEnabled;
        SystemMuteButton.Content = _preview.SystemMixMuted ? "Unmute" : "Mute";
        MicrophoneMuteButton.Content =
            _preview.MicrophoneMixMuted ? "Unmute" : "Mute";
        AudioStatusText.Text = !_audioReady
            ? "Audio recording unavailable in this build."
            : _audioSessionLocked
                ? "Recording active; mixer controls are locked."
                : "Changes apply to the next recording.";
    }

    private void ResetPreview_OnClick(
        object? sender,
        RoutedEventArgs e)
    {
        _preview.Reset();

        MicrophoneDeviceCombo.SelectedIndex =
            0;
        CameraDeviceCombo.SelectedIndex =
            0;

        EndHotkeyCapture();
        RefreshPreviewState();

        HotkeyFeedbackText.Text =
            "Defaults restored. Hotkey defaults are persisted in product mode.";
        HotkeyFeedbackText.Foreground =
            _successBrush;

        MarkPreviewChanged(
            "Defaults restored");
    }

    private void RefreshPreviewState()
    {
        _syncingPreviewControls =
            true;

        try
        {
            OutputFolderText.Text =
                _preview.OutputFolder;

            FrameRateCombo.SelectedIndex =
                _preview.FrameRate == 30
                    ? 0
                    : 1;

            VisualStyleCombo.SelectedIndex =
                _preview.VisualStyle switch
                {
                    RecordingVisualStyle.CleanScreen => 1,
                    RecordingVisualStyle.VividPresentation => 2,
                    _ => 0
                };

            SystemGainSlider.Value = _preview.SystemMixPercent;
            MicrophoneGainSlider.Value = _preview.MicrophoneMixPercent;
            SystemGainLabel.Text = $"{_preview.SystemMixPercent}%";
            MicrophoneGainLabel.Text = $"{_preview.MicrophoneMixPercent}%";
            RefreshAudioControlState();

            SmartZoomToggle.IsChecked =
                _preview.SmartZoom;
            ClickHighlightToggle.IsChecked =
                _preview.ClickHighlight;
            ShortcutKeysToggle.IsChecked =
                _preview.ShortcutKeys;

            PresenterZoomCombo.SelectedIndex =
                PresenterZoomPresetIndex(
                    _preview.PresenterZoom);

            SpotlightToggle.IsChecked =
                _preview.SpotlightEnabled;
            SpotlightWithZoomToggle.IsChecked =
                _preview.SpotlightLinkToZoom;
            SpotlightSizeCombo.SelectedIndex =
                (int)_preview.SpotlightSize;
            SpotlightStrengthCombo.SelectedIndex =
                SpotlightStrengthPresetIndex(
                    _preview.SpotlightDimStrength);
            SpotlightMotionCombo.SelectedIndex =
                (int)_preview.SpotlightMotion;

            RefreshHotkeyLabels();
            CameraMaskCombo.SelectedIndex = (int)_preview.CameraMask;
            CameraBorderCombo.SelectedIndex = (int)_preview.CameraBorder;
            CameraSizeSlider.Value = _preview.CameraWidthFraction * 100;
            CameraMarginSlider.Value = _preview.CameraMarginFraction * 100;
            CameraRadiusSlider.Value = _preview.CameraRadiusFraction * 100;
            CameraBorderSlider.Value = _preview.CameraBorderWidthPx;
            CameraOpacitySlider.Value = _preview.CameraOpacity * 100;
            CameraMirrorCheck.IsChecked = _preview.CameraMirror;
            LogoEnabledCheck.IsChecked = _preview.LogoEnabled;
            LogoPlacementCombo.SelectedIndex = (int)_preview.LogoPlacement;
            LogoSizeSlider.Value = _preview.LogoWidthFraction * 100;
            LogoMarginSlider.Value = _preview.LogoMarginFraction * 100;
            LogoOpacitySlider.Value = _preview.LogoOpacity * 100;
            LogoPathText.Text = string.IsNullOrEmpty(_preview.LogoImagePath)
                ? "No PNG selected" : Path.GetFileName(_preview.LogoImagePath);
            if (!string.IsNullOrEmpty(_preview.LogoImagePath) &&
                !TryLogoPreview(_preview.LogoImagePath, out var reason))
                LogoValidationText.Text = reason;
            UpdateCameraPlacementVisual();
            RenderCameraPreviewGeometry();
            UpdateCameraPreviewSourceInfo();
        }
        finally
        {
            _syncingPreviewControls =
                false;
        }
    }

    private void MarkPreviewChanged(
        string message)
    {
        PreviewStatusText.Text =
            message;
    }

    private static int SpotlightStrengthPresetIndex(
        float strength)
    {
        var bestIndex = 0;
        var bestDistance =
            float.MaxValue;

        for (var index = 0;
             index <
                 SettingsPreviewState.
                     SpotlightStrengthPresets.Count;
             ++index)
        {
            var distance =
                Math.Abs(
                    SettingsPreviewState.
                        SpotlightStrengthPresets[index] -
                    strength);

            if (distance <
                bestDistance)
            {
                bestDistance =
                    distance;
                bestIndex =
                    index;
            }
        }

        return bestIndex;
    }

    private static int PresenterZoomPresetIndex(
        float zoom)
    {
        var bestIndex = 0;
        var bestDistance =
            float.MaxValue;

        for (var index = 0;
             index < SettingsPreviewState.PresenterZoomPresets.Count;
             ++index)
        {
            var distance =
                Math.Abs(
                    SettingsPreviewState.PresenterZoomPresets[index] -
                    zoom);

            if (distance <
                bestDistance)
            {
                bestDistance =
                    distance;
                bestIndex =
                    index;
            }
        }

        return bestIndex;
    }

    private static bool IsModifierKey(
        Key key) =>
        key is
            Key.LeftShift or
            Key.RightShift or
            Key.LeftCtrl or
            Key.RightCtrl or
            Key.LeftAlt or
            Key.RightAlt or
            Key.LWin or
            Key.RWin;

}
