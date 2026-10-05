using System;
using System.Collections.Generic;
using System.IO;
using System.Text.Json;
using Arssyut.UI.Interop;

namespace Arssyut.UI.Preview;

public sealed class HotkeySettingsStore
{
    private const int CurrentSchemaVersion = 1;

    private readonly string _path;

    public HotkeySettingsStore(
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

    public static HotkeySettingsStore CreateDefault()
    {
        var local =
            Environment.GetFolderPath(
                Environment.SpecialFolder.
                    LocalApplicationData);

        return new HotkeySettingsStore(
            System.IO.Path.Combine(
                local,
                "Arssyut",
                "settings.json"));
    }

    public bool TryLoad(
        out IReadOnlyDictionary<
            string,
            HotkeyChord> hotkeys,
        out string error)
    {
        hotkeys =
            new Dictionary<
                string,
                HotkeyChord>();
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
                document.SchemaVersion !=
                    CurrentSchemaVersion ||
                document.Hotkeys is null)
            {
                throw new InvalidDataException(
                    "Unsupported or incomplete settings schema.");
            }

            var loaded =
                new Dictionary<
                    string,
                    HotkeyChord>(
                    StringComparer.Ordinal);

            foreach (var action in
                     HotkeyActionIds.All)
            {
                if (!document.Hotkeys.
                        TryGetValue(
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

            EnsureNoDuplicateChords(
                loaded);

            hotkeys = loaded;
            return true;
        }
        catch (Exception exception)
        {
            error =
                "Hotkey settings were invalid and defaults were restored: " +
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

            var document =
                new PersistedSettings
                {
                    SchemaVersion =
                        CurrentSchemaVersion
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
                "Could not save hotkey settings: " +
                exception.GetType().Name;
            return false;
        }
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
            // Loading defaults is the safety guarantee. Quarantine is
            // best-effort and must never prevent application startup.
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
