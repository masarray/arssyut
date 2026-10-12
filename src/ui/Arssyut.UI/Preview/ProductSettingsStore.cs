using System;
using System.Collections.Generic;
using System.IO;
using System.Text.Json;
using Arssyut.UI.Interop;

namespace Arssyut.UI.Preview;

public sealed record ProductSettingsSnapshot(
    IReadOnlyDictionary<
        string,
        HotkeyChord> Hotkeys,
    float PresenterZoom,
    bool SpotlightEnabled,
    bool SpotlightLinkToZoom,
    NativeSpotlightSize SpotlightSize,
    NativeSpotlightMotion SpotlightMotion,
    float SpotlightDimStrength,
    string OutputFolder = SettingsPreviewState.DefaultOutputFolder,
    uint FrameRate = 60,
    RecordingVisualStyle VisualStyle = RecordingVisualStyle.PixelAccurate,
    bool SmartZoom = true,
    bool ClickHighlight = true,
    bool ShortcutKeys = true,
    string CaptureMode = "Display",
    string CaptureSourceId = "",
    bool SystemAudioEnabled = false,
    bool MicrophoneEnabled = false,
    string MicrophoneDevice = "Hi-Fi Cable Output (VB-Audio Virtual Cable)",
    CameraPlacement CameraPlacement = CameraPlacement.TopRight,
    string CameraDevice = "USB2.0 HD UVC Webcam",
    int SystemMixPercent = 100,
    int MicrophoneMixPercent = 100,
    bool SystemMixMuted = false,
    bool MicrophoneMixMuted = false,
    CameraPipMask CameraMask = CameraPipMask.RoundedRectangle,
    CameraBorderTone CameraBorder = CameraBorderTone.White,
    float CameraWidthFraction = .235f,
    float CameraMarginFraction = .035f,
    float CameraRadiusFraction = .15f,
    float CameraBorderWidthPx = 2f,
    float CameraOpacity = 1f,
    bool CameraMirror = false,
    bool LogoEnabled = false,
    string LogoImagePath = "",
    LogoPlacement LogoPlacement = LogoPlacement.BottomRight,
    float LogoWidthFraction = .12f,
    float LogoMarginFraction = .025f,
    float LogoOpacity = 1f);

public sealed class ProductSettingsStore
{
    private const int LegacyHotkeySchemaVersion = 1;
    private const int PresenterZoomSchemaVersion = 2;
    private const int FreezeHotkeySchemaVersion = 3;
    private const int SpotlightSchemaVersion = 4;
    private const int ProductSchemaVersion = 5;
    private const int MixerSchemaVersion = 6;
    private const int CameraSchemaVersion = 7;
    private const int CurrentSchemaVersion = 8;

    private readonly string _path;

    public ProductSettingsStore(
        string path)
    {
        if (string.IsNullOrWhiteSpace(path))
            throw new ArgumentException(
                "Settings path is required.",
                nameof(path));

        _path = path;
    }

    public string Path =>
        _path;

    public static ProductSettingsStore CreateDefault()
    {
        var local =
            Environment.GetFolderPath(
                Environment.SpecialFolder.
                    LocalApplicationData);

        return new ProductSettingsStore(
            System.IO.Path.Combine(
                local,
                "Arssyut",
                "settings.json"));
    }

    public bool TryLoad(
        out ProductSettingsSnapshot settings,
        out string error)
    {
        settings =
            new ProductSettingsSnapshot(
                new Dictionary<
                    string,
                    HotkeyChord>(),
                SettingsPreviewState.
                    DefaultPresenterZoom,
                false,
                true,
                NativeSpotlightSize.Balanced,
                NativeSpotlightMotion.Balanced,
                SettingsPreviewState.
                    DefaultSpotlightDimStrength);
        error = string.Empty;

        if (!File.Exists(_path))
            return false;

        try
        {
            using var stream =
                new FileStream(
                    _path,
                    FileMode.Open,
                    FileAccess.Read,
                    FileShare.Read);

            var document =
                JsonSerializer.Deserialize<
                    PersistedSettings>(
                    stream,
                    JsonOptions());

            if (document is null ||
                document.Hotkeys is null ||
                (document.SchemaVersion !=
                     LegacyHotkeySchemaVersion &&
                 document.SchemaVersion !=
                     PresenterZoomSchemaVersion &&
                 document.SchemaVersion !=
                     FreezeHotkeySchemaVersion &&
                 document.SchemaVersion !=
                     SpotlightSchemaVersion &&
                 document.SchemaVersion !=
                     ProductSchemaVersion &&
                 document.SchemaVersion != MixerSchemaVersion &&
                 document.SchemaVersion != CameraSchemaVersion &&
                 document.SchemaVersion != CurrentSchemaVersion))
            {
                throw new InvalidDataException(
                    "Unsupported or incomplete settings schema.");
            }

            var loaded =
                ReadHotkeys(
                    document.Hotkeys,
                    allowMissingFreezeCamera:
                        document.SchemaVersion <
                            FreezeHotkeySchemaVersion);

            EnsureNoDuplicateChords(
                loaded);

            var presenterZoom =
                document.SchemaVersion ==
                    LegacyHotkeySchemaVersion
                    ? SettingsPreviewState.
                        DefaultPresenterZoom
                    : document.PresenterZoom ??
                        float.NaN;

            if (!SettingsPreviewState.
                    IsSupportedPresenterZoom(
                        presenterZoom))
            {
                throw new InvalidDataException(
                    "Invalid presenter zoom.");
            }

            var spotlightEnabled =
                false;
            var spotlightLinkToZoom =
                true;
            var spotlightSize =
                NativeSpotlightSize.Balanced;
            var spotlightMotion =
                NativeSpotlightMotion.Balanced;
            var spotlightDimStrength =
                SettingsPreviewState.
                    DefaultSpotlightDimStrength;

            if (document.SchemaVersion >=
                SpotlightSchemaVersion)
            {
                if (document.SpotlightEnabled is null ||
                    document.SpotlightLinkToZoom is null ||
                    document.SpotlightSize is null ||
                    document.SpotlightMotion is null ||
                    document.SpotlightDimStrength is null)
                {
                    throw new InvalidDataException(
                        "Incomplete Spotlight settings.");
                }

                spotlightEnabled =
                    document.SpotlightEnabled.Value;
                spotlightLinkToZoom =
                    document.SpotlightLinkToZoom.Value;
                spotlightSize =
                    (NativeSpotlightSize)
                        document.SpotlightSize.Value;
                spotlightMotion =
                    (NativeSpotlightMotion)
                        document.SpotlightMotion.Value;
                spotlightDimStrength =
                    document.SpotlightDimStrength.Value;

                if (!Enum.IsDefined(
                        spotlightSize) ||
                    !Enum.IsDefined(
                        spotlightMotion) ||
                    !SettingsPreviewState.
                        IsSupportedSpotlightDimStrength(
                            spotlightDimStrength))
                {
                    throw new InvalidDataException(
                        "Invalid Spotlight settings.");
                }
            }

            var latest =
                document.SchemaVersion >= ProductSchemaVersion;
            var hasMixer =
                document.SchemaVersion >= MixerSchemaVersion;
            var hasCameraProfile = document.SchemaVersion >= CameraSchemaVersion;
            var hasLogoProfile = document.SchemaVersion >= CurrentSchemaVersion;
            var outputFolder = latest
                ? document.OutputFolder
                : SettingsPreviewState.DefaultOutputFolder;
            var frameRate = latest ? document.FrameRate : 60U;
            var visual = latest ? document.VisualStyle : (int)RecordingVisualStyle.PixelAccurate;
            var captureMode = latest ? document.CaptureMode : "Display";
            var sourceId = latest ? document.CaptureSourceId : string.Empty;
            var micDevice = latest ? document.MicrophoneDevice :
                "Hi-Fi Cable Output (VB-Audio Virtual Cable)";

            if (latest &&
                (string.IsNullOrWhiteSpace(outputFolder) ||
                 outputFolder.Length > 1024 ||
                 frameRate is not (30 or 60) ||
                 visual is null ||
                 !Enum.IsDefined((RecordingVisualStyle)visual.Value) ||
                 captureMode is not ("Display" or "Window" or "Region") ||
                 sourceId is null || sourceId.Length > 1024 ||
                 string.IsNullOrWhiteSpace(micDevice) ||
                 micDevice.Length > 512 ||
                 document.SmartZoom is null ||
                 document.ClickHighlight is null ||
                 document.ShortcutKeys is null ||
                 document.SystemAudioEnabled is null ||
                 document.MicrophoneEnabled is null))
                throw new InvalidDataException("Invalid recording preferences.");

            // Old BottomRight preference was an inert placeholder; migrate
            // deterministically to the actual upper-right recorded default.
            var cameraPlacement = hasCameraProfile && document.CameraPlacement is not null
                ? (CameraPlacement)document.CameraPlacement.Value
                : CameraPlacement.TopRight;
            var cameraDevice = latest && !string.IsNullOrWhiteSpace(document.CameraDevice)
                ? document.CameraDevice! : "USB2.0 HD UVC Webcam";
            if (!Enum.IsDefined(cameraPlacement) ||
                cameraDevice.Length > 512 ||
                (hasMixer &&
                 (document.SystemMixPercent is null or < 0 or > 100 ||
                  document.MicrophoneMixPercent is null or < 0 or > 100 ||
                  document.SystemMixMuted is null ||
                  document.MicrophoneMixMuted is null)))
                throw new InvalidDataException("Invalid camera preferences.");

            var shape = hasCameraProfile
                ? (CameraPipMask)(document.CameraMask ?? -1) : CameraPipMask.RoundedRectangle;
            var tone = hasCameraProfile
                ? (CameraBorderTone)(document.CameraBorder ?? -1) : CameraBorderTone.White;
            var width = hasCameraProfile ? document.CameraWidthFraction ?? float.NaN : .235f;
            var margin = hasCameraProfile ? document.CameraMarginFraction ?? float.NaN : .035f;
            var radius = hasCameraProfile ? document.CameraRadiusFraction ?? float.NaN : .15f;
            var border = hasCameraProfile ? document.CameraBorderWidthPx ?? float.NaN : 2f;
            var opacity = hasCameraProfile ? document.CameraOpacity ?? float.NaN : 1f;
            var mirror = hasCameraProfile && document.CameraMirror == true;
            if (!Enum.IsDefined(shape) || !Enum.IsDefined(tone) ||
                !float.IsFinite(width) || width is < .15f or > .40f ||
                !float.IsFinite(margin) || margin is < 0f or > .15f ||
                !float.IsFinite(radius) || radius is < 0f or > .5f ||
                !float.IsFinite(border) || border is < 0f or > 32f ||
                !float.IsFinite(opacity) || opacity is < 0f or > 1f)
                throw new InvalidDataException("Invalid Camera PIP profile.");

            var logoEnabled = hasLogoProfile && document.LogoEnabled == true;
            var logoPath = hasLogoProfile ? document.LogoImagePath ?? "" : "";
            var logoPlacement = hasLogoProfile
                ? (LogoPlacement)(document.LogoPlacement ?? -1)
                : LogoPlacement.BottomRight;
            var logoWidth = hasLogoProfile ? document.LogoWidthFraction ?? float.NaN : .12f;
            var logoMargin = hasLogoProfile ? document.LogoMarginFraction ?? float.NaN : .025f;
            var logoOpacity = hasLogoProfile ? document.LogoOpacity ?? float.NaN : 1f;
            if (!Enum.IsDefined(logoPlacement) || logoPath.Length > 511 ||
                !float.IsFinite(logoWidth) || logoWidth is < .02f or > .50f ||
                !float.IsFinite(logoMargin) || logoMargin is < 0f or > .15f ||
                !float.IsFinite(logoOpacity) || logoOpacity is < 0f or > 1f)
                throw new InvalidDataException("Invalid logo preferences.");

            settings =
                new ProductSettingsSnapshot(
                    loaded,
                    presenterZoom,
                    spotlightEnabled,
                    spotlightLinkToZoom,
                    spotlightSize,
                    spotlightMotion,
                    spotlightDimStrength,
                    outputFolder!,
                    frameRate!.Value,
                    (RecordingVisualStyle)visual!.Value,
                    latest ? document.SmartZoom!.Value : true,
                    latest ? document.ClickHighlight!.Value : true,
                    latest ? document.ShortcutKeys!.Value : true,
                    captureMode!,
                    sourceId!,
                    latest && document.SystemAudioEnabled!.Value,
                    latest && document.MicrophoneEnabled!.Value,
                    micDevice!,
                    cameraPlacement,
                    cameraDevice,
                    hasMixer ? document.SystemMixPercent ?? 100 : 100,
                    hasMixer ? document.MicrophoneMixPercent ?? 100 : 100,
                    hasMixer && document.SystemMixMuted == true,
                    hasMixer && document.MicrophoneMixMuted == true,
                    shape, tone, width, margin, radius, border, opacity, mirror,
                    logoEnabled, logoPath, logoPlacement, logoWidth, logoMargin,
                    logoOpacity);
            return true;
        }
        catch (Exception exception)
        {
            error =
                "Product settings were invalid and defaults were restored: " +
                exception.GetType().Name;

            QuarantineCorruptFile();
            return false;
        }
    }

    public bool TrySave(
        SettingsPreviewState settings,
        out string error)
    {
        error = string.Empty;

        try
        {
            var directory =
                System.IO.Path.GetDirectoryName(
                    _path);

            if (!string.IsNullOrWhiteSpace(
                    directory))
            {
                Directory.CreateDirectory(
                    directory);
            }

            var hotkeys =
                settings.ExportHotkeys();

            EnsureNoDuplicateChords(
                hotkeys);

            if (!SettingsPreviewState.
                    IsSupportedPresenterZoom(
                        settings.PresenterZoom))
            {
                throw new InvalidDataException(
                    "Cannot persist invalid presenter zoom.");
            }


            if (!Enum.IsDefined(
                    settings.SpotlightSize) ||
                !Enum.IsDefined(
                    settings.SpotlightMotion) ||
                !SettingsPreviewState.
                    IsSupportedSpotlightDimStrength(
                        settings.SpotlightDimStrength))
            {
                throw new InvalidDataException(
                    "Cannot persist invalid Spotlight settings.");
            }

            if (settings.FrameRate is not (30 or 60) ||
                !Enum.IsDefined(settings.VisualStyle) ||
                settings.OutputFolder.Length > 1024 ||
                settings.CaptureMode is not ("Display" or "Window" or "Region") ||
                settings.CaptureSourceId.Length > 1024 ||
                settings.MicrophoneDevice.Length > 512 ||
                settings.SystemMixPercent is < 0 or > 100 ||
                settings.MicrophoneMixPercent is < 0 or > 100 ||
                !Enum.IsDefined(settings.CameraMask) ||
                !Enum.IsDefined(settings.CameraBorder) ||
                !float.IsFinite(settings.CameraWidthFraction) ||
                settings.CameraWidthFraction is < .15f or > .40f ||
                !float.IsFinite(settings.CameraMarginFraction) ||
                settings.CameraMarginFraction is < 0f or > .15f ||
                !float.IsFinite(settings.CameraRadiusFraction) ||
                settings.CameraRadiusFraction is < 0f or > .5f ||
                !float.IsFinite(settings.CameraBorderWidthPx) ||
                settings.CameraBorderWidthPx is < 0f or > 32f ||
                !float.IsFinite(settings.CameraOpacity) ||
                settings.CameraOpacity is < 0f or > 1f ||
                !Enum.IsDefined(settings.LogoPlacement) ||
                settings.LogoImagePath.Length > 511 ||
                !float.IsFinite(settings.LogoWidthFraction) ||
                settings.LogoWidthFraction is < .02f or > .5f ||
                !float.IsFinite(settings.LogoMarginFraction) ||
                settings.LogoMarginFraction is < 0f or > .15f ||
                !float.IsFinite(settings.LogoOpacity) ||
                settings.LogoOpacity is < 0f or > 1f)
                throw new InvalidDataException("Invalid recording preferences.");

            var document =
                new PersistedSettings
                {
                    SchemaVersion =
                        CurrentSchemaVersion,
                    OutputFolder = settings.OutputFolder,
                    FrameRate = settings.FrameRate,
                    VisualStyle = (int)settings.VisualStyle,
                    SmartZoom = settings.SmartZoom,
                    ClickHighlight = settings.ClickHighlight,
                    ShortcutKeys = settings.ShortcutKeys,
                    CaptureMode = settings.CaptureMode,
                    CaptureSourceId = settings.CaptureSourceId,
                    SystemAudioEnabled = settings.SystemAudioEnabled,
                    MicrophoneEnabled = settings.MicrophoneEnabled,
                    MicrophoneDevice = settings.MicrophoneDevice,
                    SystemMixPercent = settings.SystemMixPercent,
                    MicrophoneMixPercent = settings.MicrophoneMixPercent,
                    SystemMixMuted = settings.SystemMixMuted,
                    MicrophoneMixMuted = settings.MicrophoneMixMuted,
                    CameraPlacement = (int)settings.CameraPlacement,
                    CameraDevice = settings.CameraDevice,
                    CameraMask = (int)settings.CameraMask,
                    CameraBorder = (int)settings.CameraBorder,
                    CameraWidthFraction = settings.CameraWidthFraction,
                    CameraMarginFraction = settings.CameraMarginFraction,
                    CameraRadiusFraction = settings.CameraRadiusFraction,
                    CameraBorderWidthPx = settings.CameraBorderWidthPx,
                    CameraOpacity = settings.CameraOpacity,
                    CameraMirror = settings.CameraMirror,
                    LogoEnabled = settings.LogoEnabled,
                    LogoImagePath = settings.LogoImagePath,
                    LogoPlacement = (int)settings.LogoPlacement,
                    LogoWidthFraction = settings.LogoWidthFraction,
                    LogoMarginFraction = settings.LogoMarginFraction,
                    LogoOpacity = settings.LogoOpacity,
                    PresenterZoom =
                        settings.PresenterZoom,
                    SpotlightEnabled =
                        settings.SpotlightEnabled,
                    SpotlightLinkToZoom =
                        settings.SpotlightLinkToZoom,
                    SpotlightSize =
                        (uint)settings.SpotlightSize,
                    SpotlightMotion =
                        (uint)settings.SpotlightMotion,
                    SpotlightDimStrength =
                        settings.SpotlightDimStrength
                };

            foreach (var action in
                     HotkeyActionIds.All)
            {
                if (!hotkeys.TryGetValue(
                        action,
                        out var chord) ||
                    !chord.IsValid)
                {
                    throw new InvalidDataException(
                        $"Cannot persist invalid action {action}.");
                }

                document.Hotkeys[action] =
                    new PersistedHotkey
                    {
                        Modifiers =
                            (uint)chord.Modifiers,
                        VirtualKey =
                            chord.VirtualKey
                    };
            }

            var temporary =
                _path +
                ".tmp-" +
                Guid.NewGuid().
                    ToString("N");

            try
            {
                using (var stream =
                       new FileStream(
                           temporary,
                           FileMode.CreateNew,
                           FileAccess.Write,
                           FileShare.None,
                           4096,
                           FileOptions.WriteThrough))
                {
                    JsonSerializer.Serialize(
                        stream,
                        document,
                        JsonOptions());

                    stream.Flush(
                        flushToDisk: true);
                }

                File.Move(
                    temporary,
                    _path,
                    overwrite: true);
            }
            finally
            {
                if (File.Exists(
                        temporary))
                {
                    File.Delete(
                        temporary);
                }
            }

            return true;
        }
        catch (Exception exception)
        {
            error =
                "Could not save product settings: " +
                exception.GetType().Name;
            return false;
        }
    }

    private static Dictionary<
        string,
        HotkeyChord> ReadHotkeys(
        IReadOnlyDictionary<
            string,
            PersistedHotkey> source,
        bool allowMissingFreezeCamera)
    {
        var loaded =
            new Dictionary<
                string,
                HotkeyChord>(
                StringComparer.Ordinal);

        foreach (var action in
                 HotkeyActionIds.All)
        {
            if (!source.TryGetValue(
                    action,
                    out var persisted))
            {
                if (allowMissingFreezeCamera &&
                    action ==
                        HotkeyActionIds.FreezeCamera)
                {
                    loaded[action] =
                        HotkeyChord.Empty;
                    continue;
                }

                throw new InvalidDataException(
                    $"Missing hotkey action {action}.");
            }

            var chord =
                new HotkeyChord(
                    (NativeHotkeyModifiers)
                        persisted.Modifiers,
                    persisted.VirtualKey);

            if (!chord.IsValid)
            {
                throw new InvalidDataException(
                    $"Invalid hotkey chord for {action}.");
            }

            loaded[action] =
                chord;
        }

        return loaded;
    }

    private void QuarantineCorruptFile()
    {
        try
        {
            if (!File.Exists(_path))
                return;

            var quarantine =
                _path + ".corrupt";

            File.Move(
                _path,
                quarantine,
                overwrite: true);
        }
        catch
        {
            // Defaults are the safety guarantee. Quarantine is best-effort and
            // must never prevent application startup.
        }
    }

    private static void EnsureNoDuplicateChords(
        IReadOnlyDictionary<
            string,
            HotkeyChord> hotkeys)
    {
        var owner =
            new Dictionary<
                HotkeyChord,
                string>();

        foreach (var action in
                 HotkeyActionIds.All)
        {
            if (!hotkeys.TryGetValue(
                    action,
                    out var chord) ||
                chord.IsEmpty)
                continue;

            if (owner.TryGetValue(
                    chord,
                    out var previous))
            {
                throw new InvalidDataException(
                    $"{action} duplicates {previous}.");
            }

            owner[chord] =
                action;
        }
    }

    private static JsonSerializerOptions JsonOptions() =>
        new()
        {
            WriteIndented = true,
            PropertyNameCaseInsensitive = false
        };

    private sealed class PersistedSettings
    {
        public PersistedSettings()
        {
        }

        public int SchemaVersion { get; set; }

        public string? OutputFolder { get; set; }
        public uint? FrameRate { get; set; }
        public int? VisualStyle { get; set; }
        public bool? SmartZoom { get; set; }
        public bool? ClickHighlight { get; set; }
        public bool? ShortcutKeys { get; set; }
        public string? CaptureMode { get; set; }
        public string? CaptureSourceId { get; set; }
        public bool? SystemAudioEnabled { get; set; }
        public bool? MicrophoneEnabled { get; set; }
        public string? MicrophoneDevice { get; set; }
        public int? SystemMixPercent { get; set; }
        public int? MicrophoneMixPercent { get; set; }
        public bool? SystemMixMuted { get; set; }
        public bool? MicrophoneMixMuted { get; set; }
        public int? CameraPlacement { get; set; }
        public string? CameraDevice { get; set; }
        public int? CameraMask { get; set; }
        public int? CameraBorder { get; set; }
        public float? CameraWidthFraction { get; set; }
        public float? CameraMarginFraction { get; set; }
        public float? CameraRadiusFraction { get; set; }
        public float? CameraBorderWidthPx { get; set; }
        public float? CameraOpacity { get; set; }
        public bool? CameraMirror { get; set; }
        public bool? LogoEnabled { get; set; }
        public string? LogoImagePath { get; set; }
        public int? LogoPlacement { get; set; }
        public float? LogoWidthFraction { get; set; }
        public float? LogoMarginFraction { get; set; }
        public float? LogoOpacity { get; set; }

        public float? PresenterZoom { get; set; }

        public bool? SpotlightEnabled { get; set; }
        public bool? SpotlightLinkToZoom { get; set; }
        public uint? SpotlightSize { get; set; }
        public uint? SpotlightMotion { get; set; }
        public float? SpotlightDimStrength { get; set; }

        public Dictionary<
            string,
            PersistedHotkey> Hotkeys { get; set; } =
                new(
                    StringComparer.Ordinal);
    }

    private sealed class PersistedHotkey
    {
        public PersistedHotkey()
        {
        }

        public uint Modifiers { get; set; }
        public uint VirtualKey { get; set; }
    }
}
