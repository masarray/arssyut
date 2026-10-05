using System;
using System.Collections.Generic;
using Avalonia.Input;
using Arssyut.UI.Interop;

namespace Arssyut.UI.Preview;

public static class HotkeyActionIds
{
    public const string Record = "Record";
    public const string Pause = "Pause";
    public const string Microphone = "Microphone";
    public const string Camera = "Camera";
    public const string ToggleZoom = "ToggleZoom";
    public const string HoldZoom = "HoldZoom";
    public const string ZoomIn = "ZoomIn";
    public const string ZoomOut = "ZoomOut";
    public const string ResetZoom = "ResetZoom";
    public const string OverviewPeek = "OverviewPeek";

    public static readonly string[] All =
    [
        Record,
        Pause,
        Microphone,
        Camera,
        ToggleZoom,
        HoldZoom,
        ZoomIn,
        ZoomOut,
        ResetZoom,
        OverviewPeek
    ];

    public static bool IsKnown(string action) =>
        Array.IndexOf(All, action) >= 0;
}

public readonly record struct HotkeyChord(
    NativeHotkeyModifiers Modifiers,
    uint VirtualKey)
{
    private const uint ValidModifierMask =
        (uint)(
            NativeHotkeyModifiers.Control |
            NativeHotkeyModifiers.Shift |
            NativeHotkeyModifiers.Alt |
            NativeHotkeyModifiers.Win);

    private static readonly IReadOnlyDictionary<string, uint>
        NamedKeyToVirtualKey =
            new Dictionary<string, uint>(
                StringComparer.OrdinalIgnoreCase)
            {
                ["Backspace"] = 0x08,
                ["Back"] = 0x08,
                ["Tab"] = 0x09,
                ["Enter"] = 0x0D,
                ["Return"] = 0x0D,
                ["Pause"] = 0x13,
                ["CapsLock"] = 0x14,
                ["Capital"] = 0x14,
                ["Escape"] = 0x1B,
                ["Esc"] = 0x1B,
                ["Space"] = 0x20,
                ["PageUp"] = 0x21,
                ["Prior"] = 0x21,
                ["PageDown"] = 0x22,
                ["Next"] = 0x22,
                ["End"] = 0x23,
                ["Home"] = 0x24,
                ["Left"] = 0x25,
                ["Up"] = 0x26,
                ["Right"] = 0x27,
                ["Down"] = 0x28,
                ["PrintScreen"] = 0x2C,
                ["Snapshot"] = 0x2C,
                ["Insert"] = 0x2D,
                ["Delete"] = 0x2E,
                ["Apps"] = 0x5D,
                ["ContextMenu"] = 0x5D,
                ["Numpad*"] = 0x6A,
                ["Multiply"] = 0x6A,
                ["Numpad+"] = 0x6B,
                ["Add"] = 0x6B,
                ["NumpadSeparator"] = 0x6C,
                ["Separator"] = 0x6C,
                ["Numpad-"] = 0x6D,
                ["Subtract"] = 0x6D,
                ["Numpad."] = 0x6E,
                ["Decimal"] = 0x6E,
                ["Numpad/"] = 0x6F,
                ["Divide"] = 0x6F,
                ["NumLock"] = 0x90,
                ["ScrollLock"] = 0x91,
                ["Scroll"] = 0x91,

                // Windows OEM virtual keys. Keep multiple Avalonia/WPF aliases
                // so capture does not depend on one framework enum spelling.
                [";"] = 0xBA,
                ["OemSemicolon"] = 0xBA,
                ["Oem1"] = 0xBA,
                ["="] = 0xBB,
                ["OemPlus"] = 0xBB,
                [","] = 0xBC,
                ["OemComma"] = 0xBC,
                ["-"] = 0xBD,
                ["OemMinus"] = 0xBD,
                ["."] = 0xBE,
                ["OemPeriod"] = 0xBE,
                ["/"] = 0xBF,
                ["OemQuestion"] = 0xBF,
                ["Oem2"] = 0xBF,
                ["`"] = 0xC0,
                ["OemTilde"] = 0xC0,
                ["Oem3"] = 0xC0,
                ["["] = 0xDB,
                ["OemOpenBrackets"] = 0xDB,
                ["Oem4"] = 0xDB,
                ["\\"] = 0xDC,
                ["OemPipe"] = 0xDC,
                ["OemBackslash"] = 0xDC,
                ["Oem5"] = 0xDC,
                ["]"] = 0xDD,
                ["OemCloseBrackets"] = 0xDD,
                ["Oem6"] = 0xDD,
                ["'"] = 0xDE,
                ["OemQuotes"] = 0xDE,
                ["Oem7"] = 0xDE,
                ["Oem8"] = 0xDF
            };

    private static readonly IReadOnlyDictionary<uint, string>
        VirtualKeyToDisplay =
            new Dictionary<uint, string>
            {
                [0x08] = "Backspace",
                [0x09] = "Tab",
                [0x0D] = "Enter",
                [0x13] = "Pause",
                [0x14] = "CapsLock",
                [0x1B] = "Esc",
                [0x20] = "Space",
                [0x21] = "PageUp",
                [0x22] = "PageDown",
                [0x23] = "End",
                [0x24] = "Home",
                [0x25] = "Left",
                [0x26] = "Up",
                [0x27] = "Right",
                [0x28] = "Down",
                [0x2C] = "PrintScreen",
                [0x2D] = "Insert",
                [0x2E] = "Delete",
                [0x5D] = "Apps",
                [0x6A] = "Numpad*",
                [0x6B] = "Numpad+",
                [0x6C] = "NumpadSeparator",
                [0x6D] = "Numpad-",
                [0x6E] = "Numpad.",
                [0x6F] = "Numpad/",
                [0x90] = "NumLock",
                [0x91] = "ScrollLock",
                [0xBA] = ";",
                [0xBB] = "=",
                [0xBC] = ",",
                [0xBD] = "-",
                [0xBE] = ".",
                [0xBF] = "/",
                [0xC0] = "`",
                [0xDB] = "[",
                [0xDC] = "\\",
                [0xDD] = "]",
                [0xDE] = "'",
                [0xDF] = "OEM8"
            };

    public static HotkeyChord Empty =>
        new(
            NativeHotkeyModifiers.None,
            0);

    public bool IsEmpty =>
        VirtualKey == 0;

    public bool IsValid =>
        ((uint)Modifiers & ~ValidModifierMask) == 0 &&
        (IsEmpty
            ? Modifiers == NativeHotkeyModifiers.None
            : IsSupportedVirtualKey(VirtualKey));

    public string DisplayText =>
        IsEmpty
            ? string.Empty
            : FormatDisplay(
                Modifiers,
                VirtualKey);

    public NativeHotkeyChord ToNative() =>
        new(
            Modifiers,
            VirtualKey);

    public static bool TryFromKeyEvent(
        KeyEventArgs e,
        out HotkeyChord chord) =>
        TryFromAvaloniaKeyName(
            e.Key.ToString(),
            e.KeyModifiers,
            out chord);

    public static bool TryFromAvaloniaKeyName(
        string keyName,
        KeyModifiers modifiers,
        out HotkeyChord chord)
    {
        chord = Empty;

        if (!TryVirtualKey(
                keyName,
                out var virtualKey))
            return false;

        chord =
            new HotkeyChord(
                ConvertModifiers(modifiers),
                virtualKey);

        return chord.IsValid;
    }

    public static bool TryParse(
        string? text,
        out HotkeyChord chord)
    {
        chord = Empty;

        if (string.IsNullOrWhiteSpace(text))
            return false;

        var remaining =
            text.Trim().
                Replace(
                    " ",
                    string.Empty,
                    StringComparison.Ordinal);

        var modifiers =
            NativeHotkeyModifiers.None;

        while (TryConsumeModifier(
                   ref remaining,
                   "Ctrl",
                   NativeHotkeyModifiers.Control,
                   ref modifiers) ||
               TryConsumeModifier(
                   ref remaining,
                   "Control",
                   NativeHotkeyModifiers.Control,
                   ref modifiers) ||
               TryConsumeModifier(
                   ref remaining,
                   "Shift",
                   NativeHotkeyModifiers.Shift,
                   ref modifiers) ||
               TryConsumeModifier(
                   ref remaining,
                   "Alt",
                   NativeHotkeyModifiers.Alt,
                   ref modifiers) ||
               TryConsumeModifier(
                   ref remaining,
                   "Win",
                   NativeHotkeyModifiers.Win,
                   ref modifiers) ||
               TryConsumeModifier(
                   ref remaining,
                   "Meta",
                   NativeHotkeyModifiers.Win,
                   ref modifiers))
        {
        }

        if (string.IsNullOrWhiteSpace(remaining) ||
            !TryVirtualKey(
                remaining,
                out var virtualKey))
            return false;

        chord =
            new HotkeyChord(
                modifiers,
                virtualKey);

        return chord.IsValid;
    }

    public static bool IsSupportedVirtualKey(
        uint virtualKey)
    {
        if (virtualKey is >= 0x30 and <= 0x39 ||
            virtualKey is >= 0x41 and <= 0x5A ||
            virtualKey is >= 0x60 and <= 0x69 ||
            virtualKey is >= 0x70 and <= 0x87)
            return true;

        return VirtualKeyToDisplay.ContainsKey(
            virtualKey);
    }

    private static bool TryConsumeModifier(
        ref string remaining,
        string name,
        NativeHotkeyModifiers value,
        ref NativeHotkeyModifiers modifiers)
    {
        var prefix =
            name + "+";

        if (!remaining.StartsWith(
                prefix,
                StringComparison.OrdinalIgnoreCase))
            return false;

        if ((modifiers & value) != 0)
            return false;

        modifiers |= value;
        remaining =
            remaining[prefix.Length..];

        return true;
    }

    private static bool TryVirtualKey(
        string key,
        out uint virtualKey)
    {
        virtualKey = 0;

        if (key.Length == 1)
        {
            var character =
                char.ToUpperInvariant(
                    key[0]);

            if (character is >= 'A' and <= 'Z' ||
                character is >= '0' and <= '9')
            {
                virtualKey = character;
                return true;
            }
        }

        if (key.Length >= 2 &&
            (key[0] == 'F' ||
             key[0] == 'f') &&
            int.TryParse(
                key[1..],
                out var function) &&
            function is >= 1 and <= 24)
        {
            virtualKey =
                checked(
                    (uint)(
                        0x70 +
                        function -
                        1));
            return true;
        }

        if (key.StartsWith(
                "NumPad",
                StringComparison.OrdinalIgnoreCase) &&
            key.Length == 7 &&
            char.IsDigit(key[6]))
        {
            virtualKey =
                checked(
                    (uint)(
                        0x60 +
                        (key[6] - '0')));
            return true;
        }

        if (key.StartsWith(
                "Numpad",
                StringComparison.OrdinalIgnoreCase) &&
            key.Length == 7 &&
            char.IsDigit(key[6]))
        {
            virtualKey =
                checked(
                    (uint)(
                        0x60 +
                        (key[6] - '0')));
            return true;
        }

        return NamedKeyToVirtualKey.TryGetValue(
            key,
            out virtualKey);
    }

    private static NativeHotkeyModifiers ConvertModifiers(
        KeyModifiers modifiers)
    {
        var result =
            NativeHotkeyModifiers.None;

        if (modifiers.HasFlag(
                KeyModifiers.Control))
            result |=
                NativeHotkeyModifiers.Control;
        if (modifiers.HasFlag(
                KeyModifiers.Shift))
            result |=
                NativeHotkeyModifiers.Shift;
        if (modifiers.HasFlag(
                KeyModifiers.Alt))
            result |=
                NativeHotkeyModifiers.Alt;
        if (modifiers.HasFlag(
                KeyModifiers.Meta))
            result |=
                NativeHotkeyModifiers.Win;

        return result;
    }

    private static string FormatDisplay(
        NativeHotkeyModifiers modifiers,
        uint virtualKey)
    {
        var parts =
            new List<string>(5);

        if ((modifiers &
             NativeHotkeyModifiers.Control) != 0)
            parts.Add("Ctrl");
        if ((modifiers &
             NativeHotkeyModifiers.Shift) != 0)
            parts.Add("Shift");
        if ((modifiers &
             NativeHotkeyModifiers.Alt) != 0)
            parts.Add("Alt");
        if ((modifiers &
             NativeHotkeyModifiers.Win) != 0)
            parts.Add("Win");

        parts.Add(
            KeyDisplay(
                virtualKey));

        return string.Join(
            "+",
            parts);
    }

    private static string KeyDisplay(
        uint virtualKey)
    {
        if (virtualKey is >= 0x30 and <= 0x39 ||
            virtualKey is >= 0x41 and <= 0x5A)
        {
            return
                ((char)virtualKey).
                    ToString();
        }

        if (virtualKey is >= 0x60 and <= 0x69)
        {
            return
                "Numpad" +
                (virtualKey - 0x60).
                    ToString();
        }

        if (virtualKey is >= 0x70 and <= 0x87)
        {
            return
                "F" +
                (virtualKey - 0x70 + 1).
                    ToString();
        }

        return VirtualKeyToDisplay.TryGetValue(
                   virtualKey,
                   out var label)
            ? label
            : $"VK_{virtualKey:X2}";
    }
}
