using System;

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
    public event EventHandler? Changed;

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

    public string RecordHotkey { get; private set; } =
        "F9";

    public string PauseHotkey { get; private set; } =
        "F10";

    public string MicrophoneHotkey { get; private set; } =
        "Ctrl+F9";

    public string CameraHotkey { get; private set; } =
        "Ctrl+F10";

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

    public bool TrySetHotkey(
        string action,
        string gesture,
        out string error)
    {
        error = string.Empty;

        var normalized =
            NormalizeGesture(
                gesture);

        if (string.IsNullOrWhiteSpace(normalized))
        {
            error =
                "Shortcut cannot be empty.";
            return false;
        }

        var current =
            action switch
            {
                "Record" => RecordHotkey,
                "Pause" => PauseHotkey,
                "Microphone" => MicrophoneHotkey,
                "Camera" => CameraHotkey,
                _ => string.Empty
            };

        if (string.Equals(
                current,
                normalized,
                StringComparison.OrdinalIgnoreCase))
            return true;

        if (ConflictsWithOtherAction(
                action,
                normalized))
        {
            error =
                $"{normalized} is already assigned.";
            return false;
        }

        switch (action)
        {
            case "Record":
                RecordHotkey = normalized;
                break;
            case "Pause":
                PauseHotkey = normalized;
                break;
            case "Microphone":
                MicrophoneHotkey = normalized;
                break;
            case "Camera":
                CameraHotkey = normalized;
                break;
            default:
                error =
                    "Unknown shortcut action.";
                return false;
        }

        Changed?.Invoke(this, EventArgs.Empty);
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
        RecordHotkey =
            "F9";
        PauseHotkey =
            "F10";
        MicrophoneHotkey =
            "Ctrl+F9";
        CameraHotkey =
            "Ctrl+F10";
        CameraPlacement =
            CameraPlacement.BottomRight;
        MicrophoneDevice =
            "Hi-Fi Cable Output (VB-Audio Virtual Cable)";
        CameraDevice =
            "USB2.0 HD UVC Webcam";
        Changed?.Invoke(this, EventArgs.Empty);
    }

    private bool ConflictsWithOtherAction(
        string action,
        string gesture)
    {
        if (action != "Record" &&
            string.Equals(
                gesture,
                RecordHotkey,
                StringComparison.OrdinalIgnoreCase))
            return true;

        if (action != "Pause" &&
            string.Equals(
                gesture,
                PauseHotkey,
                StringComparison.OrdinalIgnoreCase))
            return true;

        if (action != "Microphone" &&
            string.Equals(
                gesture,
                MicrophoneHotkey,
                StringComparison.OrdinalIgnoreCase))
            return true;

        if (action != "Camera" &&
            string.Equals(
                gesture,
                CameraHotkey,
                StringComparison.OrdinalIgnoreCase))
            return true;

        return false;
    }

    private static string NormalizeGesture(
        string gesture) =>
        gesture
            .Replace(
                "Control",
                "Ctrl",
                StringComparison.OrdinalIgnoreCase)
            .Replace(
                " ",
                string.Empty,
                StringComparison.Ordinal)
            .Trim();
}
