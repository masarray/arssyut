using System;

namespace Arssyut.UI.Preview;

public enum CameraPlacement
{
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight
}

public sealed class SettingsPreviewState
{
    public string OutputFolder { get; private set; } =
        @"Videos\Arssyut";

    public string RecordHotkey { get; private set; } =
        "F9";

    public string PauseHotkey { get; private set; } =
        "F10";

    public string MicrophoneHotkey { get; private set; } =
        "Ctrl+F9";

    public CameraPlacement CameraPlacement { get; private set; } =
        CameraPlacement.BottomRight;

    public string MicrophoneDevice { get; set; } =
        "Hi-Fi Cable Output (VB-Audio Virtual Cable)";

    public string CameraDevice { get; set; } =
        "USB2.0 HD UVC Webcam";

    public void SetOutputFolder(
        string path)
    {
        if (!string.IsNullOrWhiteSpace(path))
            OutputFolder = path.Trim();
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
            default:
                error =
                    "Unknown shortcut action.";
                return false;
        }

        return true;
    }

    public void SetCameraPlacement(
        CameraPlacement placement) =>
        CameraPlacement = placement;

    public void Reset()
    {
        OutputFolder =
            @"Videos\Arssyut";
        RecordHotkey =
            "F9";
        PauseHotkey =
            "F10";
        MicrophoneHotkey =
            "Ctrl+F9";
        CameraPlacement =
            CameraPlacement.BottomRight;
        MicrophoneDevice =
            "Hi-Fi Cable Output (VB-Audio Virtual Cable)";
        CameraDevice =
            "USB2.0 HD UVC Webcam";
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
