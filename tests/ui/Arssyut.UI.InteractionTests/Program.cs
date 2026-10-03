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
    settings.CameraPlacement ==
        CameraPlacement.BottomRight &&
    settings.OutputFolder ==
        @"Videos\Arssyut",
    "Settings preview Reset restores deterministic defaults");

Console.WriteLine(
    "P6UI interaction-state checks passed.");
