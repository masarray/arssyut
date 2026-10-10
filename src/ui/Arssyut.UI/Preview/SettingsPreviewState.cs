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
    public const float MinimumPresenterZoom = 1.10f;
    public const float MaximumPresenterZoom = 4.00f;
    public const float DefaultPresenterZoom = 2.00f;
    public const float DefaultSpotlightDimStrength = 0.38f;

    private static readonly IReadOnlyList<float>
        PresenterZoomPresetValues =
            Array.AsReadOnly(
                new float[]
                {
                    1.10f,
                    1.25f,
                    1.50f,
                    1.75f,
                    2.00f,
                    2.50f,
                    3.00f,
                    4.00f
                });

    public static IReadOnlyList<float>
        PresenterZoomPresets =>
            PresenterZoomPresetValues;

    private static readonly IReadOnlyList<float>
        SpotlightStrengthPresetValues =
            Array.AsReadOnly(
                new float[]
                {
                    0.30f,
                    DefaultSpotlightDimStrength,
                    0.46f
                });

    public static IReadOnlyList<float>
        SpotlightStrengthPresets =>
            SpotlightStrengthPresetValues;

    private readonly Dictionary<
        string,
        HotkeyChord> _hotkeys =
            CreateDefaultHotkeys();

    public event EventHandler? Changed;
    public event EventHandler? PersistentSettingsChanged;

    public const string DefaultOutputFolder = @"Videos\Arssyut";

    public string OutputFolder { get; private set; } =
        DefaultOutputFolder;

    // Only user intent is persisted: device tokens and native capture
    // generations are always refreshed on launch.
    public string CaptureMode { get; private set; } = "Display";
    public string CaptureSourceId { get; private set; } = string.Empty;
    public bool SystemAudioEnabled { get; private set; }
    public bool MicrophoneEnabled { get; private set; }

    // Output attenuation, not Windows device volume. Unity preserves fidelity.
    public int SystemMixPercent { get; private set; } = 100;
    public int MicrophoneMixPercent { get; private set; } = 100;
    public bool SystemMixMuted { get; private set; }
    public bool MicrophoneMixMuted { get; private set; }

    public void SetMixLevel(bool microphone, int percent)
    {
        if (percent is < 0 or > 100) return;
        if (microphone)
        {
            if (MicrophoneMixPercent == percent) return;
            MicrophoneMixPercent = percent;
        }
        else
        {
            if (SystemMixPercent == percent) return;
            SystemMixPercent = percent;
        }
        PersistentSettingsChanged?.Invoke(this, EventArgs.Empty);
        Changed?.Invoke(this, EventArgs.Empty);
    }

    public void SetMixMuted(bool microphone, bool muted)
    {
        if (microphone)
        {
            if (MicrophoneMixMuted == muted) return;
            MicrophoneMixMuted = muted;
        }
        else
        {
            if (SystemMixMuted == muted) return;
            SystemMixMuted = muted;
        }
        PersistentSettingsChanged?.Invoke(this, EventArgs.Empty);
        Changed?.Invoke(this, EventArgs.Empty);
    }


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

    public float PresenterZoom { get; private set; } =
        DefaultPresenterZoom;

    public bool SpotlightEnabled { get; private set; } =
        false;

    public bool SpotlightLinkToZoom { get; private set; } =
        true;

    public NativeSpotlightSize SpotlightSize { get; private set; } =
        NativeSpotlightSize.Balanced;

    public NativeSpotlightMotion SpotlightMotion { get; private set; } =
        NativeSpotlightMotion.Balanced;

    public float SpotlightDimStrength { get; private set; } =
        DefaultSpotlightDimStrength;

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

    public string FreezeCameraHotkey =>
        HotkeyText(
            HotkeyActionIds.FreezeCamera);

    public CameraPlacement CameraPlacement { get; private set; } =
        CameraPlacement.BottomRight;

    private string _microphoneDevice =
        "Hi-Fi Cable Output (VB-Audio Virtual Cable)";

    public string MicrophoneDevice
    {
        get => _microphoneDevice;
        set
        {
            if (string.IsNullOrWhiteSpace(value) ||
                value.Length > 512 || _microphoneDevice == value)
                return;
            _microphoneDevice = value;
            PersistentSettingsChanged?.Invoke(this, EventArgs.Empty);
            Changed?.Invoke(this, EventArgs.Empty);
        }
    }


    private string _cameraDevice = "USB2.0 HD UVC Webcam";
    public string CameraDevice
    {
        get => _cameraDevice;
        set
        {
            if (string.IsNullOrWhiteSpace(value) ||
                value.Length > 512 || _cameraDevice == value)
                return;
            _cameraDevice = value;
            PersistentSettingsChanged?.Invoke(this, EventArgs.Empty);
            Changed?.Invoke(this, EventArgs.Empty);
        }
    }

    public void SetCaptureChoice(string mode, string? sourceId)
    {
        if (mode is not ("Display" or "Window" or "Region") ||
            (sourceId?.Length ?? 0) > 1024)
            return;
        sourceId ??= string.Empty;
        if (CaptureMode == mode && CaptureSourceId == sourceId)
            return;
        CaptureMode = mode;
        CaptureSourceId = sourceId;
        PersistentSettingsChanged?.Invoke(this, EventArgs.Empty);
    }

    public void SetAudioPreferences(bool systemAudio, bool microphone)
    {
        if (SystemAudioEnabled == systemAudio &&
            MicrophoneEnabled == microphone)
            return;
        SystemAudioEnabled = systemAudio;
        MicrophoneEnabled = microphone;
        PersistentSettingsChanged?.Invoke(this, EventArgs.Empty);
    }

    public void SetFrameRate(
        uint frameRate)
    {
        if (frameRate is not (30 or 60) ||
            FrameRate == frameRate)
            return;

        FrameRate = frameRate;
        PersistentSettingsChanged?.Invoke(this, EventArgs.Empty);
        Changed?.Invoke(this, EventArgs.Empty);
    }

    public void SetVisualStyle(
        RecordingVisualStyle style)
    {
        if (VisualStyle == style)
            return;

        VisualStyle = style;
        PersistentSettingsChanged?.Invoke(this, EventArgs.Empty);
        Changed?.Invoke(this, EventArgs.Empty);
    }

    public void SetSmartZoom(
        bool enabled)
    {
        if (SmartZoom == enabled)
            return;

        SmartZoom = enabled;
        PersistentSettingsChanged?.Invoke(this, EventArgs.Empty);
        Changed?.Invoke(this, EventArgs.Empty);
    }

    public void SetClickHighlight(
        bool enabled)
    {
        if (ClickHighlight == enabled)
            return;

        ClickHighlight = enabled;
        PersistentSettingsChanged?.Invoke(this, EventArgs.Empty);
        Changed?.Invoke(this, EventArgs.Empty);
    }

    public void SetShortcutKeys(
        bool enabled)
    {
        if (ShortcutKeys == enabled)
            return;

        ShortcutKeys = enabled;
        PersistentSettingsChanged?.Invoke(this, EventArgs.Empty);
        Changed?.Invoke(this, EventArgs.Empty);
    }

    public bool TrySetPresenterZoom(
        float zoom)
    {
        if (!IsSupportedPresenterZoom(
                zoom))
            return false;

        if (Math.Abs(
                PresenterZoom -
                zoom) <= 0.0005f)
            return true;

        PresenterZoom =
            zoom;

        PersistentSettingsChanged?.Invoke(
            this,
            EventArgs.Empty);
        Changed?.Invoke(
            this,
            EventArgs.Empty);
        return true;
    }


    public void SetSpotlightEnabled(
        bool enabled)
    {
        if (SpotlightEnabled == enabled)
            return;

        SpotlightEnabled = enabled;
        PersistentSettingsChanged?.Invoke(
            this,
            EventArgs.Empty);
        Changed?.Invoke(
            this,
            EventArgs.Empty);
    }

    public void SetSpotlightLinkToZoom(
        bool enabled)
    {
        if (SpotlightLinkToZoom == enabled)
            return;

        SpotlightLinkToZoom = enabled;
        PersistentSettingsChanged?.Invoke(
            this,
            EventArgs.Empty);
        Changed?.Invoke(
            this,
            EventArgs.Empty);
    }

    public bool TrySetSpotlightSize(
        NativeSpotlightSize size)
    {
        if (!Enum.IsDefined(size))
            return false;

        if (SpotlightSize == size)
            return true;

        SpotlightSize = size;
        PersistentSettingsChanged?.Invoke(
            this,
            EventArgs.Empty);
        Changed?.Invoke(
            this,
            EventArgs.Empty);
        return true;
    }

    public bool TrySetSpotlightMotion(
        NativeSpotlightMotion motion)
    {
        if (!Enum.IsDefined(motion))
            return false;

        if (SpotlightMotion == motion)
            return true;

        SpotlightMotion = motion;
        PersistentSettingsChanged?.Invoke(
            this,
            EventArgs.Empty);
        Changed?.Invoke(
            this,
            EventArgs.Empty);
        return true;
    }

    public bool TrySetSpotlightDimStrength(
        float strength)
    {
        if (!IsSupportedSpotlightDimStrength(
                strength))
            return false;

        if (Math.Abs(
                SpotlightDimStrength -
                strength) <= 0.0005f)
            return true;

        SpotlightDimStrength = strength;
        PersistentSettingsChanged?.Invoke(
            this,
            EventArgs.Empty);
        Changed?.Invoke(
            this,
            EventArgs.Empty);
        return true;
    }

    public void SetOutputFolder(
        string path)
    {
        if (string.IsNullOrWhiteSpace(path) ||
            path.Length > 1024 ||
            string.Equals(OutputFolder, path.Trim(), StringComparison.Ordinal))
            return;

        OutputFolder = path.Trim();
        PersistentSettingsChanged?.Invoke(this, EventArgs.Empty);
        Changed?.Invoke(this, EventArgs.Empty);
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

    public bool TryRestorePersistentSettings(
        ProductSettingsSnapshot snapshot,
        out string error)
    {
        error = string.Empty;

        if (!IsSupportedPresenterZoom(
                snapshot.PresenterZoom))
        {
            error =
                "Persisted presenter zoom is invalid.";
            return false;
        }

        if (!Enum.IsDefined(
                snapshot.SpotlightSize) ||
            !Enum.IsDefined(
                snapshot.SpotlightMotion) ||
            !IsSupportedSpotlightDimStrength(
                snapshot.SpotlightDimStrength))
        {
            error =
                "Persisted Spotlight settings are invalid.";
            return false;
        }

        if (snapshot.FrameRate is not (30 or 60) ||
            !Enum.IsDefined(snapshot.VisualStyle) ||
            string.IsNullOrWhiteSpace(snapshot.OutputFolder) ||
            snapshot.OutputFolder.Length > 1024 ||
            snapshot.CaptureMode is not ("Display" or "Window" or "Region") ||
            snapshot.CaptureSourceId.Length > 1024 ||
            string.IsNullOrWhiteSpace(snapshot.MicrophoneDevice) ||
            snapshot.MicrophoneDevice.Length > 512 ||
            !Enum.IsDefined(snapshot.CameraPlacement) ||
            string.IsNullOrWhiteSpace(snapshot.CameraDevice) ||
            snapshot.CameraDevice.Length > 512 ||
            snapshot.SystemMixPercent is < 0 or > 100 ||
            snapshot.MicrophoneMixPercent is < 0 or > 100)
        {
            error = "Invalid persisted recording preferences.";
            return false;
        }

        if (!TryBuildHotkeyCandidate(
                snapshot.Hotkeys,
                out var candidate,
                out error))
            return false;

        _hotkeys.Clear();

        foreach (var pair in candidate)
            _hotkeys[pair.Key] = pair.Value;

        PresenterZoom =
            snapshot.PresenterZoom;
        SpotlightEnabled =
            snapshot.SpotlightEnabled;
        SpotlightLinkToZoom =
            snapshot.SpotlightLinkToZoom;
        SpotlightSize =
            snapshot.SpotlightSize;
        SpotlightMotion =
            snapshot.SpotlightMotion;
        SpotlightDimStrength =
            snapshot.SpotlightDimStrength;
        OutputFolder = snapshot.OutputFolder;
        FrameRate = snapshot.FrameRate;
        VisualStyle = snapshot.VisualStyle;
        SmartZoom = snapshot.SmartZoom;
        ClickHighlight = snapshot.ClickHighlight;
        ShortcutKeys = snapshot.ShortcutKeys;
        CaptureMode = snapshot.CaptureMode;
        CaptureSourceId = snapshot.CaptureSourceId;
        SystemAudioEnabled = snapshot.SystemAudioEnabled;
        MicrophoneEnabled = snapshot.MicrophoneEnabled;
        SystemMixPercent = snapshot.SystemMixPercent;
        MicrophoneMixPercent = snapshot.MicrophoneMixPercent;
        SystemMixMuted = snapshot.SystemMixMuted;
        MicrophoneMixMuted = snapshot.MicrophoneMixMuted;
        _microphoneDevice = snapshot.MicrophoneDevice;
        CameraPlacement = snapshot.CameraPlacement;
        _cameraDevice = snapshot.CameraDevice;
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

        PersistentSettingsChanged?.Invoke(
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
        if (CameraPlacement == placement)
            return;
        CameraPlacement = placement;
        PersistentSettingsChanged?.Invoke(this, EventArgs.Empty);
        Changed?.Invoke(this, EventArgs.Empty);
    }

    public void Reset()
    {
        OutputFolder = DefaultOutputFolder;
        CaptureMode = "Display";
        CaptureSourceId = string.Empty;
        SystemAudioEnabled = false;
        MicrophoneEnabled = false;
        SystemMixPercent = 100;
        MicrophoneMixPercent = 100;
        SystemMixMuted = false;
        MicrophoneMixMuted = false;
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
        PresenterZoom =
            DefaultPresenterZoom;
        SpotlightEnabled =
            false;
        SpotlightLinkToZoom =
            true;
        SpotlightSize =
            NativeSpotlightSize.Balanced;
        SpotlightMotion =
            NativeSpotlightMotion.Balanced;
        SpotlightDimStrength =
            DefaultSpotlightDimStrength;

        _hotkeys.Clear();
        foreach (var pair in
                 CreateDefaultHotkeys())
        {
            _hotkeys[pair.Key] =
                pair.Value;
        }

        CameraPlacement =
            CameraPlacement.BottomRight;
        _microphoneDevice =
            "Hi-Fi Cable Output (VB-Audio Virtual Cable)";
        _cameraDevice =
            "USB2.0 HD UVC Webcam";

        PersistentSettingsChanged?.Invoke(
            this,
            EventArgs.Empty);
        Changed?.Invoke(
            this,
            EventArgs.Empty);
    }

    public static bool IsSupportedPresenterZoom(
        float zoom)
    {
        if (!float.IsFinite(
                zoom))
            return false;

        foreach (var preset in
                 PresenterZoomPresetValues)
        {
            if (Math.Abs(
                    preset -
                    zoom) <= 0.0005f)
                return true;
        }

        return false;
    }


    public static bool IsSupportedSpotlightDimStrength(
        float strength)
    {
        if (!float.IsFinite(
                strength))
            return false;

        foreach (var preset in
                 SpotlightStrengthPresetValues)
        {
            if (Math.Abs(
                    preset -
                    strength) <= 0.0005f)
                return true;
        }

        return false;
    }

    private static bool TryBuildHotkeyCandidate(
        IReadOnlyDictionary<
            string,
            HotkeyChord> hotkeys,
        out Dictionary<
            string,
            HotkeyChord> candidate,
        out string error)
    {
        error = string.Empty;
        candidate =
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

        return true;
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
                    HotkeyChord.Empty,
                [HotkeyActionIds.FreezeCamera] =
                    HotkeyChord.Empty
            };
}
