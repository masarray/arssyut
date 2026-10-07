using System;
using System.Collections.Generic;
using System.Linq;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Platform.Storage;
using Avalonia.Threading;
using Arssyut.UI.Design;
using Arssyut.UI.Interop;
using Arssyut.UI.Preview;

namespace Arssyut.UI;

public sealed partial class SettingsWindow : Window
{
    private static readonly double[] SystemLevelPattern =
    [
        34, 46, 61, 52, 70, 58, 43, 64,
        76, 55, 48, 67
    ];

    private static readonly double[] MicLevelPattern =
    [
        22, 31, 44, 36, 52, 41, 28, 47,
        58, 39, 33, 49
    ];

    private readonly SettingsPreviewState _preview;
    private readonly NativeBridgeClient? _nativeBridge;
    private readonly bool _stressLayout;
    private readonly Button[] _navButtons;
    private readonly Control[] _pages;
    private readonly Button[] _cameraAnchorButtons;
    private readonly Button[] _hotkeyButtons;
    private readonly DispatcherTimer _audioPreviewTimer;
    private readonly IBrush _warningBrush;
    private readonly IBrush _successBrush;
    private readonly IBrush _textSecondaryBrush;

    private string? _capturingHotkeyAction;
    private int _meterStep;
    private bool _uiReady;
    private bool _syncingPreviewControls;

    public SettingsWindow(
        SettingsPreviewState preview,
        NativeBridgeClient? nativeBridge = null,
        bool stressLayout = false)
    {
        _preview = preview;
        _nativeBridge = nativeBridge;
        _stressLayout = stressLayout;
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

        Opened +=
            (_, _) =>
            {
                if (!_stressLayout)
                    RefreshNativeDevices();

                RefreshPreviewState();
            };

        Closed +=
            (_, _) =>
                _audioPreviewTimer.Stop();

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
                "Mouse" => 5,
                "Hotkeys" => 6,
                "Advanced" => 7,
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
                    "Native audio capability status.",
                "Camera" =>
                    "Native camera compositor capability status.",
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
        if (sender is not ComboBox combo)
            return;

        var value =
            SelectedItemText(
                combo.SelectedItem);

        if (!string.IsNullOrWhiteSpace(value))
            _preview.MicrophoneDevice = value;
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
        if (_nativeBridge is null)
            return;

        try
        {
            var devices =
                _nativeBridge.RefreshDevices();

            if (devices.Microphones.Count > 0)
            {
                MicrophoneDeviceCombo.ItemsSource =
                    devices.Microphones
                        .Select(item => item.Name)
                        .ToArray();
                MicrophoneDeviceCombo.SelectedIndex =
                    0;
            }

            if (devices.Cameras.Count > 0)
            {
                CameraDeviceCombo.ItemsSource =
                    devices.Cameras
                        .Select(item => item.Name)
                        .ToArray();
                CameraDeviceCombo.SelectedIndex =
                    0;
            }
        }
        catch (Exception)
        {
            // Keep accepted preview fixtures visible if native enumeration is
            // unavailable. The UI never creates a second native device catalog.
        }
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

        CameraPreviewBadge.HorizontalAlignment =
            _preview.CameraPlacement is
                CameraPlacement.TopLeft or
                CameraPlacement.BottomLeft
                ? HorizontalAlignment.Left
                : HorizontalAlignment.Right;

        CameraPreviewBadge.VerticalAlignment =
            _preview.CameraPlacement is
                CameraPlacement.TopLeft or
                CameraPlacement.TopRight
                ? VerticalAlignment.Top
                : VerticalAlignment.Bottom;
    }

    private static bool TryNativeHotkeyAction(
        string action,
        out NativeHotkeyAction nativeAction)
    {
        nativeAction =
            action switch
            {
                "Record" => NativeHotkeyAction.ToggleRecord,
                "ToggleZoom" => NativeHotkeyAction.ToggleZoom,
                "ZoomIn" => NativeHotkeyAction.ZoomIn,
                "ZoomOut" => NativeHotkeyAction.ZoomOut,
                "ResetZoom" => NativeHotkeyAction.ResetFullFrame,
                "FreezeCamera" => NativeHotkeyAction.FreezeCamera,
                _ => NativeHotkeyAction.ToggleRecord
            };

        return action is
            "Record" or
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

    private void AudioPreviewTimer_OnTick(
        object? sender,
        EventArgs e)
    {
        var system =
            SystemLevelPattern[
                _meterStep %
                SystemLevelPattern.Length];

        var mic =
            MicLevelPattern[
                _meterStep %
                MicLevelPattern.Length];

        ++_meterStep;

        SystemMeter.Value =
            system;
        MicMeter.Value =
            mic;

        SystemDbText.Text =
            FormatDb(system);
        MicDbText.Text =
            FormatDb(mic);
    }

    private static string FormatDb(
        double level)
    {
        var db =
            -48.0 +
            level * 0.46;

        return
            $"{Math.Round(db):0} dB";
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
            UpdateCameraPlacementVisual();
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
