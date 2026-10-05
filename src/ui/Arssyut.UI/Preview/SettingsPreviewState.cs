using System;
using System.Collections.Generic;
using Arssyut.UI.Interop;

namespace Arssyut.UI.Preview;

public enum CameraPlacement
{
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight
}

public enum RecordingVisualStyle
{
    PixelAccurate,
    CleanScreen,
    VividPresentation
}

public sealed class SettingsPreviewState
{
    private readonly Dictionary<
        string,
        HotkeyChord> _hotkeys =
            CreateDefaultHotkeys();

    public event EventHandler? Changed;
    public event EventHandler? HotkeysChanged;

    public string OutputFolder { get; private set; } =
        @"Videos\Arssyut";

    public uint FrameRate { get; private set; } =
        60;

    public RecordingVisualStyle VisualStyle { get; private set; } =
        RecordingVisualStyle.PixelAccurate;

    public bool SmartZoom { get; private set; } =
        true;

    public bool ClickHighlight { get; private set; } =
        true;

    public bool ShortcutKeys { get; private set; } =
        true;

    public string RecordHotkey =>
        HotkeyText(
            HotkeyActionIds.Record);

    public string PauseHotkey =>
        HotkeyText(
            HotkeyActionIds.Pause);

    public string MicrophoneHotkey =>
        HotkeyText(
            HotkeyActionIds.Microphone);

    public string CameraHotkey =>
        HotkeyText(
            HotkeyActionIds.Camera);

    public string ToggleZoomHotkey =>
        HotkeyText(
            HotkeyActionIds.ToggleZoom);

    public string HoldZoomHotkey =>
        HotkeyText(
            HotkeyActionIds.HoldZoom);

    public string ZoomInHotkey =>
        HotkeyText(
            HotkeyActionIds.ZoomIn);

    public string ZoomOutHotkey =>
        HotkeyText(
            HotkeyActionIds.ZoomOut);

    public string ResetZoomHotkey =>
        HotkeyText(
            HotkeyActionIds.ResetZoom);

    public string OverviewPeekHotkey =>
        HotkeyText(
            HotkeyActionIds.OverviewPeek);

    public CameraPlacement CameraPlacement { get; private set; } =
        CameraPlacement.BottomRight;

    public string MicrophoneDevice { get; set; } =
        "Hi-Fi Cable Output (VB-Audio Virtual Cable)";

    public string CameraDevice { get; set; } =
        "USB2.0 HD UVC Webcam";

    public void SetFrameRate(
        uint frameRate)
    {
        if (frameRate is not (30 or 60) ||
            FrameRate == frameRate)
            return;

        FrameRate = frameRate;
        Changed?.Invoke(this, EventArgs.Empty);
    }

    public void SetVisualStyle(
        RecordingVisualStyle style)
    {
        if (VisualStyle == style)
            return;

        VisualStyle = style;
        Changed?.Invoke(this, EventArgs.Empty);
    }

    public void SetSmartZoom(
        bool enabled)
    {
        if (SmartZoom == enabled)
            return;

        SmartZoom = enabled;
        Changed?.Invoke(this, EventArgs.Empty);
    }

    public void SetClickHighlight(
        bool enabled)
    {
        if (ClickHighlight == enabled)
            return;

        ClickHighlight = enabled;
        Changed?.Invoke(this, EventArgs.Empty);
    }

    public void SetShortcutKeys(
        bool enabled)
    {
        if (ShortcutKeys == enabled)
            return;

        ShortcutKeys = enabled;
        Changed?.Invoke(this, EventArgs.Empty);
    }

    public void SetOutputFolder(
        string path)
    {
        if (!string.IsNullOrWhiteSpace(path))
        {
            OutputFolder = path.Trim();
            Changed?.Invoke(this, EventArgs.Empty);
        }
    }

    public bool TryGetHotkeyChord(
        string action,
        out HotkeyChord chord) =>
        _hotkeys.TryGetValue(
            action,
            out chord);

    public IReadOnlyDictionary<
        string,
        HotkeyChord> ExportHotkeys() =>
            new Dictionary<
                string,
                HotkeyChord>(
                _hotkeys,
                StringComparer.Ordinal);

    public bool TryRestoreHotkeys(
        IReadOnlyDictionary<
            string,
            HotkeyChord> hotkeys,
        out string error)
    {
        error = string.Empty;

        var candidate =
            new Dictionary<
                string,
                HotkeyChord>(
                    StringComparer.Ordinal);
        var owner =
            new Dictionary<
                HotkeyChord,
                string>();

        foreach (var action in
                 HotkeyActionIds.All)
        {
            if (!hotkeys.TryGetValue(
                    action,
                    out var chord))
            {
                error =
                    $"Missing persisted shortcut {action}.";
                return false;
            }

            if (!chord.IsValid)
            {
                error =
                    $"Persisted shortcut {action} is invalid.";
                return false;
            }

            if (!chord.IsEmpty)
            {
                if (owner.TryGetValue(
                        chord,
                        out var previous))
                {
                    error =
                        $"{action} duplicates {previous}.";
                    return false;
                }

                owner[chord] =
                    action;
            }

            candidate[action] =
                chord;
        }

        _hotkeys.Clear();

        foreach (var pair in candidate)
            _hotkeys[pair.Key] = pair.Value;

        return true;
    }

    public bool TrySetHotkey(
        string action,
        string gesture,
        out string error)
    {
        if (!HotkeyChord.TryParse(
                gesture,
                out var chord))
        {
            error =
                "Shortcut is not a supported Windows key chord.";
            return false;
        }

        return TrySetHotkey(
            action,
            chord,
            out error);
    }

    public bool TrySetHotkey(
        string action,
        HotkeyChord chord,
        out string error)
    {
        error = string.Empty;

        if (!HotkeyActionIds.IsKnown(
                action))
        {
            error =
                "Unknown shortcut action.";
            return false;
        }

        if (!chord.IsValid ||
            chord.IsEmpty)
        {
            error =
                "Shortcut cannot be empty or unsupported.";
            return false;
        }

        if (_hotkeys.TryGetValue(
                action,
                out var current) &&
            current == chord)
            return true;

        foreach (var pair in _hotkeys)
        {
            if (pair.Key != action &&
                !pair.Value.IsEmpty &&
                pair.Value == chord)
            {
                error =
                    $"{chord.DisplayText} is already assigned to {pair.Key}.";
                return false;
            }
        }

        _hotkeys[action] =
            chord;

        HotkeysChanged?.Invoke(
            this,
            EventArgs.Empty);
        Changed?.Invoke(
            this,
            EventArgs.Empty);
        return true;
    }

    public void SetCameraPlacement(
        CameraPlacement placement)
    {
        CameraPlacement = placement;
        Changed?.Invoke(this, EventArgs.Empty);
    }

    public void Reset()
    {
        OutputFolder =
            @"Videos\Arssyut";
        FrameRate =
            60;
        VisualStyle =
            RecordingVisualStyle.PixelAccurate;
        SmartZoom =
            true;
        ClickHighlight =
            true;
        ShortcutKeys =
            true;

        _hotkeys.Clear();
        foreach (var pair in
                 CreateDefaultHotkeys())
        {
            _hotkeys[pair.Key] =
                pair.Value;
        }

        CameraPlacement =
            CameraPlacement.BottomRight;
        MicrophoneDevice =
            "Hi-Fi Cable Output (VB-Audio Virtual Cable)";
        CameraDevice =
            "USB2.0 HD UVC Webcam";

        HotkeysChanged?.Invoke(
            this,
            EventArgs.Empty);
        Changed?.Invoke(
            this,
            EventArgs.Empty);
    }

    private string HotkeyText(
        string action) =>
        _hotkeys.TryGetValue(
            action,
            out var chord)
            ? chord.DisplayText
            : string.Empty;

    private static Dictionary<
        string,
        HotkeyChord> CreateDefaultHotkeys() =>
            new(
                StringComparer.Ordinal)
            {
                [HotkeyActionIds.Record] =
                    new HotkeyChord(
                        NativeHotkeyModifiers.None,
                        0x78),
                [HotkeyActionIds.Pause] =
                    new HotkeyChord(
                        NativeHotkeyModifiers.None,
                        0x79),
                [HotkeyActionIds.Microphone] =
                    new HotkeyChord(
                        NativeHotkeyModifiers.Control,
                        0x78),
                [HotkeyActionIds.Camera] =
                    new HotkeyChord(
                        NativeHotkeyModifiers.Control,
                        0x79),
                [HotkeyActionIds.ToggleZoom] =
                    HotkeyChord.Empty,
                [HotkeyActionIds.HoldZoom] =
                    HotkeyChord.Empty,
                [HotkeyActionIds.ZoomIn] =
                    HotkeyChord.Empty,
                [HotkeyActionIds.ZoomOut] =
                    HotkeyChord.Empty,
                [HotkeyActionIds.ResetZoom] =
                    HotkeyChord.Empty,
                [HotkeyActionIds.OverviewPeek] =
                    HotkeyChord.Empty
            };
}
