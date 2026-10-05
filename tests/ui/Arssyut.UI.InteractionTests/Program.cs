using Arssyut.UI.Interop;
using Arssyut.UI.Preview;

static void Expect(
    bool condition,
    string message)
{
    if (!condition)
        throw new InvalidOperationException(
            $"P6UI interaction test failed: {message}");
}

var session =
    new PreviewRecorderSession();

Expect(
    session.Phase ==
        PreviewRecordingPhase.Ready,
    "new session is Ready");

session.TogglePause();
Expect(
    session.Phase ==
        PreviewRecordingPhase.Ready,
    "pause is ignored while Ready");

session.Start();
Expect(
    session.Phase ==
        PreviewRecordingPhase.Recording,
    "Start enters Recording");

session.Start();
Expect(
    session.Phase ==
        PreviewRecordingPhase.Recording,
    "repeated Start does not create a second state transition");

session.TogglePause();
Expect(
    session.Phase ==
        PreviewRecordingPhase.Paused,
    "TogglePause enters Paused");

session.TogglePause();
Expect(
    session.Phase ==
        PreviewRecordingPhase.Recording,
    "TogglePause resumes Recording");

session.MicrophoneEnabled = false;
session.CameraEnabled = true;

session.Stop();
Expect(
    session.Phase ==
        PreviewRecordingPhase.Saved,
    "Stop enters Saved");
Expect(
    !string.IsNullOrWhiteSpace(
        session.SavedFileName),
    "Saved state publishes a preview filename");
Expect(
    !session.MicrophoneEnabled &&
        session.CameraEnabled,
    "input preview state survives transport transitions");

session.TogglePause();
Expect(
    session.Phase ==
        PreviewRecordingPhase.Saved,
    "pause is ignored after Saved");

session.Reset();
Expect(
    session.Phase ==
        PreviewRecordingPhase.Ready,
    "Reset returns to Ready");
Expect(
    string.IsNullOrEmpty(
        session.SavedFileName),
    "Reset clears the preview result");



var settings =
    new SettingsPreviewState();

Expect(
    settings.OutputFolder == @"Videos\Arssyut",
    "Settings preview starts with the compact default output folder");

Expect(
    settings.FrameRate == 60 &&
    settings.VisualStyle ==
        RecordingVisualStyle.PixelAccurate &&
    settings.SmartZoom &&
    settings.ClickHighlight &&
    settings.ShortcutKeys,
    "Settings preview starts with canonical recorder presentation defaults");

settings.SetFrameRate(30);
settings.SetVisualStyle(
    RecordingVisualStyle.CleanScreen);
settings.SetSmartZoom(false);
settings.SetClickHighlight(false);
settings.SetShortcutKeys(false);

Expect(
    settings.FrameRate == 30 &&
    settings.VisualStyle ==
        RecordingVisualStyle.CleanScreen &&
    !settings.SmartZoom &&
    !settings.ClickHighlight &&
    !settings.ShortcutKeys,
    "Settings preview retains recorder presentation choices");


Expect(
    HotkeyPreview.TryToNativeRegistration(
        "Ctrl+Shift+F9",
        out var nativeModifiers,
        out var nativeVirtualKey) &&
    nativeModifiers ==
        (NativeHotkeyModifiers.Control |
         NativeHotkeyModifiers.Shift) &&
    nativeVirtualKey == 0x78,
    "multi-key chord maps deterministically to Windows modifiers + VK_F9");

Expect(
    string.IsNullOrEmpty(settings.ToggleZoomHotkey) &&
    string.IsNullOrEmpty(settings.HoldZoomHotkey) &&
    string.IsNullOrEmpty(settings.ZoomInHotkey) &&
    string.IsNullOrEmpty(settings.ZoomOutHotkey) &&
    string.IsNullOrEmpty(settings.ResetZoomHotkey) &&
    string.IsNullOrEmpty(settings.OverviewPeekHotkey),
    "ArZoom presenter hotkeys start unassigned instead of inventing defaults");

Expect(
    settings.TrySetHotkey(
        "ToggleZoom",
        "Ctrl+Alt+Z",
        out var toggleZoomError) &&
    string.IsNullOrEmpty(toggleZoomError) &&
    settings.ToggleZoomHotkey == "Ctrl+Alt+Z",
    "Settings accepts a global Toggle Zoom chord");

Expect(
    settings.TrySetHotkey(
        "HoldZoom",
        "Ctrl+Shift+H",
        out var holdZoomError) &&
    string.IsNullOrEmpty(holdZoomError) &&
    settings.HoldZoomHotkey == "Ctrl+Shift+H",
    "Settings accepts a Hold Zoom press/release chord");

Expect(
    settings.TrySetHotkey(
        "OverviewPeek",
        "Alt+O",
        out var overviewError) &&
    string.IsNullOrEmpty(overviewError) &&
    settings.OverviewPeekHotkey == "Alt+O",
    "Settings accepts an Overview Peek press/release chord");

Expect(
    !settings.TrySetHotkey(
        "ZoomIn",
        "Ctrl+Alt+Z",
        out var zoomConflictError) &&
    !string.IsNullOrEmpty(zoomConflictError),
    "presenter zoom shortcuts participate in duplicate-chord conflict checks");

Expect(
    !settings.TrySetHotkey(
        "OverviewPeek",
        "Ctrl+Shift+H",
        out var momentaryConflictError) &&
    !string.IsNullOrEmpty(momentaryConflictError),
    "momentary presenter shortcuts share the same duplicate-chord authority");

Expect(
    settings.TrySetHotkey(
        "Record",
        "Ctrl+Shift+R",
        out var recordError) &&
    string.IsNullOrEmpty(recordError) &&
    settings.RecordHotkey ==
        "Ctrl+Shift+R",
    "Settings preview accepts a new Record shortcut");

Expect(
    !settings.TrySetHotkey(
        "Pause",
        "Ctrl+Shift+R",
        out var conflictError) &&
    !string.IsNullOrEmpty(
        conflictError) &&
    settings.PauseHotkey ==
        "F10",
    "Settings preview rejects duplicate shortcut conflicts");

settings.SetCameraPlacement(
    CameraPlacement.TopLeft);
Expect(
    settings.CameraPlacement ==
        CameraPlacement.TopLeft,
    "camera placement preview updates deterministically");

settings.SetOutputFolder(
    @"D:\Recordings\Arssyut");
Expect(
    settings.OutputFolder ==
        @"D:\Recordings\Arssyut",
    "folder picker preview state accepts a selected folder");

settings.Reset();
Expect(
    settings.RecordHotkey == "F9" &&
    settings.PauseHotkey == "F10" &&
    settings.MicrophoneHotkey ==
        "Ctrl+F9" &&
    settings.CameraHotkey ==
        "Ctrl+F10" &&
    string.IsNullOrEmpty(settings.ToggleZoomHotkey) &&
    string.IsNullOrEmpty(settings.HoldZoomHotkey) &&
    string.IsNullOrEmpty(settings.ZoomInHotkey) &&
    string.IsNullOrEmpty(settings.ZoomOutHotkey) &&
    string.IsNullOrEmpty(settings.ResetZoomHotkey) &&
    string.IsNullOrEmpty(settings.OverviewPeekHotkey) &&
    settings.CameraPlacement ==
        CameraPlacement.BottomRight &&
    settings.OutputFolder ==
        @"Videos\Arssyut" &&
    settings.FrameRate == 60 &&
    settings.VisualStyle ==
        RecordingVisualStyle.PixelAccurate &&
    settings.SmartZoom &&
    settings.ClickHighlight &&
    settings.ShortcutKeys,
    "Settings preview Reset restores deterministic defaults");

Console.WriteLine(
    "P6UI interaction-state checks passed.");
