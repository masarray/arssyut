using System.IO;
using System.Linq;
using Avalonia.Input;
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


// P6UI.6A.1 canonical Windows hotkey acceptance matrix. Persistence and
// runtime registration consume the same (modifier mask, VK) identity.
var acceptedChords =
    new (string Input, NativeHotkeyModifiers Modifiers, uint VirtualKey, string Display)[]
    {
        ("Ctrl+`", NativeHotkeyModifiers.Control, 0xC0, "Ctrl+`"),
        ("Ctrl+Shift+`", NativeHotkeyModifiers.Control | NativeHotkeyModifiers.Shift, 0xC0, "Ctrl+Shift+`"),
        ("Ctrl+=", NativeHotkeyModifiers.Control, 0xBB, "Ctrl+="),
        ("Ctrl+-", NativeHotkeyModifiers.Control, 0xBD, "Ctrl+-"),
        ("Alt+[", NativeHotkeyModifiers.Alt, 0xDB, "Alt+["),
        ("Ctrl+Shift+F9", NativeHotkeyModifiers.Control | NativeHotkeyModifiers.Shift, 0x78, "Ctrl+Shift+F9"),
        ("Win+Alt+F12", NativeHotkeyModifiers.Win | NativeHotkeyModifiers.Alt, 0x7B, "Alt+Win+F12"),
        ("Numpad+", NativeHotkeyModifiers.None, 0x6B, "Numpad+"),
        ("Ctrl+]", NativeHotkeyModifiers.Control, 0xDD, "Ctrl+]"),
        ("Ctrl+\\", NativeHotkeyModifiers.Control, 0xDC, "Ctrl+\\"),
        ("Alt+;", NativeHotkeyModifiers.Alt, 0xBA, "Alt+;"),
        ("Shift+F12", NativeHotkeyModifiers.Shift, 0x7B, "Shift+F12"),
        ("Ctrl+Numpad+", NativeHotkeyModifiers.Control, 0x6B, "Ctrl+Numpad+"),
        ("Ctrl+Numpad-", NativeHotkeyModifiers.Control, 0x6D, "Ctrl+Numpad-"),
        ("Home", NativeHotkeyModifiers.None, 0x24, "Home"),
        ("PageDown", NativeHotkeyModifiers.None, 0x22, "PageDown"),
        ("Insert", NativeHotkeyModifiers.None, 0x2D, "Insert")
    };

foreach (var accepted in acceptedChords)
{
    Expect(
        HotkeyChord.TryParse(
            accepted.Input,
            out var chord) &&
        chord.Modifiers ==
            accepted.Modifiers &&
        chord.VirtualKey ==
            accepted.VirtualKey &&
        chord.DisplayText ==
            accepted.Display,
        $"canonical hotkey parser accepts {accepted.Input}");
}

Expect(
    HotkeyChord.TryFromAvaloniaKeyName(
        "OemTilde",
        KeyModifiers.Control,
        out var capturedGrave) &&
    capturedGrave.DisplayText ==
        "Ctrl+`" &&
    capturedGrave.VirtualKey ==
        0xC0,
    "Avalonia OEM grave key maps to Windows VK_OEM_3");

Expect(
    HotkeyChord.TryFromAvaloniaKeyName(
        "Add",
        KeyModifiers.Control,
        out var capturedNumpadPlus) &&
    capturedNumpadPlus.DisplayText ==
        "Ctrl+Numpad+" &&
    capturedNumpadPlus.VirtualKey ==
        0x6B,
    "Avalonia Numpad Add maps to Windows VK_ADD");

var canonicalConflictState =
    new SettingsPreviewState();

Expect(
    canonicalConflictState.TrySetHotkey(
        HotkeyActionIds.ToggleZoom,
        "Control + `",
        out var canonicalFirstError) &&
    string.IsNullOrEmpty(
        canonicalFirstError),
    "canonical state accepts normalized Control alias");

Expect(
    !canonicalConflictState.TrySetHotkey(
        HotkeyActionIds.ZoomIn,
        "Ctrl+`",
        out var canonicalDuplicateError) &&
    !string.IsNullOrEmpty(
        canonicalDuplicateError),
    "duplicate detection compares canonical chord identity, not display spelling");

var persistenceRoot =
    Path.Combine(
        Path.GetTempPath(),
        "arssyut-hotkey-" +
        Guid.NewGuid().ToString("N"));

Directory.CreateDirectory(
    persistenceRoot);

try
{
    var persistencePath =
        Path.Combine(
            persistenceRoot,
            "settings.json");
    var store =
        new ProductSettingsStore(
            persistencePath);
    var saved =
        new SettingsPreviewState();

    Expect(
        saved.PresenterZoom ==
            SettingsPreviewState.DefaultPresenterZoom &&
        saved.TrySetPresenterZoom(
            3.00f) &&
        saved.PresenterZoom ==
            3.00f,
        "presenter zoom starts at 2.00x and accepts the 3.00x product preset");

    Expect(
        !saved.TrySetPresenterZoom(
            1.09f) &&
        saved.PresenterZoom ==
            3.00f,
        "unsupported presenter zoom is rejected without mutating the configured value");

    Expect(
        saved.TrySetHotkey(
            HotkeyActionIds.ToggleZoom,
            "Ctrl+`",
            out _) &&
        saved.TrySetHotkey(
            HotkeyActionIds.HoldZoom,
            "Alt+[",
            out _) &&
        saved.TrySetHotkey(
            HotkeyActionIds.ZoomIn,
            "Ctrl+=",
            out _) &&
        saved.TrySetHotkey(
            HotkeyActionIds.ZoomOut,
            "Ctrl+-",
            out _) &&
        saved.TrySetHotkey(
            HotkeyActionIds.ResetZoom,
            "Win+Alt+F12",
            out _) &&
        saved.TrySetHotkey(
            HotkeyActionIds.OverviewPeek,
            "Numpad+",
            out _) &&
        saved.TrySetHotkey(
            HotkeyActionIds.FreezeCamera,
            "Shift+F12",
            out _),
        "persistence fixture accepts canonical OEM/Numpad/Freeze chords");

    Expect(
        store.TrySave(
            saved,
            out var saveError) &&
        string.IsNullOrEmpty(
            saveError) &&
        File.Exists(
            persistencePath),
        "product settings save atomically to one versioned file");

    Expect(
        store.TryLoad(
            out var loadedSettings,
            out var loadError) &&
        string.IsNullOrEmpty(
            loadError) &&
        loadedSettings.PresenterZoom == 3.00f,
        "product settings round-trip hotkeys and presenter zoom through JSON");

    var restored =
        new SettingsPreviewState();

    Expect(
        restored.TryRestorePersistentSettings(
            loadedSettings,
            out var restoreError) &&
        string.IsNullOrEmpty(
            restoreError) &&
        restored.ToggleZoomHotkey == "Ctrl+`" &&
        restored.HoldZoomHotkey == "Alt+[" &&
        restored.ZoomInHotkey == "Ctrl+=" &&
        restored.ZoomOutHotkey == "Ctrl+-" &&
        restored.ResetZoomHotkey == "Alt+Win+F12" &&
        restored.OverviewPeekHotkey == "Numpad+" &&
        restored.FreezeCameraHotkey == "Shift+F12" &&
        restored.PresenterZoom == 3.00f,
        "restored startup state preserves canonical hotkeys, Freeze and presenter zoom");

    var schemaV3Lines =
        File.ReadAllLines(
            persistencePath);

    // Schema v2 predates Freeze Camera. It must migrate by adding one
    // unassigned Freeze chord rather than quarantining the user's settings.
    var schemaV2Lines =
        new List<string>();
    var skippingFreeze = false;
    var freezeDepth = 0;

    foreach (var line in schemaV3Lines)
    {
        if (!skippingFreeze &&
            line.Contains(
                "\"FreezeCamera\"",
                StringComparison.Ordinal))
        {
            // FreezeCamera is the final schema-v3 hotkey entry. Removing it
            // for a schema-v2 fixture also removes the previous entry's comma.
            if (schemaV2Lines.Count > 0)
            {
                var previous =
                    schemaV2Lines[^1];
                var comma =
                    previous.LastIndexOf(
                        ',');

                if (comma >= 0)
                {
                    schemaV2Lines[^1] =
                        previous.Remove(
                            comma,
                            1);
                }
            }

            skippingFreeze = true;
            freezeDepth = 0;
            continue;
        }

        if (skippingFreeze)
        {
            freezeDepth +=
                line.Count(
                    character =>
                        character == '{');
            freezeDepth -=
                line.Count(
                    character =>
                        character == '}');

            if (line.TrimStart().
                    StartsWith(
                        "}",
                        StringComparison.Ordinal) &&
                freezeDepth <= -1)
            {
                skippingFreeze = false;
            }

            continue;
        }

        schemaV2Lines.Add(
            line.Replace(
                "\"SchemaVersion\": 3",
                "\"SchemaVersion\": 2",
                StringComparison.Ordinal));
    }

    var schemaV2Path =
        Path.Combine(
            persistenceRoot,
            "settings-v2.json");
    File.WriteAllLines(
        schemaV2Path,
        schemaV2Lines);

    var schemaV2Store =
        new ProductSettingsStore(
            schemaV2Path);

    Expect(
        schemaV2Store.TryLoad(
            out var schemaV2Settings,
            out var schemaV2Error) &&
        string.IsNullOrEmpty(
            schemaV2Error) &&
        schemaV2Settings.Hotkeys[
            HotkeyActionIds.FreezeCamera].
            IsEmpty,
        "schema v2 migrates with Freeze Camera unassigned");

    var schemaV1Lines =
        new List<string>();

    foreach (var line in schemaV2Lines)
    {
        if (line.Contains(
                "\"PresenterZoom\"",
                StringComparison.Ordinal))
            continue;

        schemaV1Lines.Add(
            line.Replace(
                "\"SchemaVersion\": 2",
                "\"SchemaVersion\": 1",
                StringComparison.Ordinal));
    }

    var legacyPath =
        Path.Combine(
            persistenceRoot,
            "settings-v1.json");
    File.WriteAllLines(
        legacyPath,
        schemaV1Lines);

    var legacyStore =
        new ProductSettingsStore(
            legacyPath);

    Expect(
        legacyStore.TryLoad(
            out var legacySettings,
            out var legacyLoadError) &&
        string.IsNullOrEmpty(
            legacyLoadError) &&
        legacySettings.PresenterZoom ==
            SettingsPreviewState.DefaultPresenterZoom &&
        legacySettings.Hotkeys[
            HotkeyActionIds.ToggleZoom].
            DisplayText == "Ctrl+`",
        "schema v1 hotkeys migrate without loss and receive the 2.00x zoom default");

    File.WriteAllText(
        persistencePath,
        "{ definitely-not-valid-json ");

    Expect(
        !store.TryLoad(
            out _,
            out var corruptError) &&
        !string.IsNullOrEmpty(
            corruptError) &&
        File.Exists(
            persistencePath +
            ".corrupt"),
        "corrupt product settings fail safe to defaults and quarantine the bad file");
}
finally
{
    Directory.Delete(
        persistenceRoot,
        recursive: true);
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
    settings.ShortcutKeys &&
    settings.PresenterZoom ==
        SettingsPreviewState.DefaultPresenterZoom,
    "Settings preview Reset restores deterministic defaults including presenter zoom");

Console.WriteLine(
    "P6UI interaction-state checks passed.");
