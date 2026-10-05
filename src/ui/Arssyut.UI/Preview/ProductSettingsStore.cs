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
    float PresenterZoom);

public sealed class ProductSettingsStore
{
    private const int LegacyHotkeySchemaVersion = 1;
    private const int CurrentSchemaVersion = 2;

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
                    DefaultPresenterZoom);
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
                     CurrentSchemaVersion))
            {
                throw new InvalidDataException(
                    "Unsupported or incomplete settings schema.");
            }

            var loaded =
                ReadHotkeys(
                    document.Hotkeys);

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

            settings =
                new ProductSettingsSnapshot(
                    loaded,
                    presenterZoom);
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

            var document =
                new PersistedSettings
                {
                    SchemaVersion =
                        CurrentSchemaVersion,
                    PresenterZoom =
                        settings.PresenterZoom
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
            PersistedHotkey> source)
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

        public float? PresenterZoom { get; set; }

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
