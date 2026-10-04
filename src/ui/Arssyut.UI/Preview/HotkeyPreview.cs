using System;
using System.Collections.Generic;
using Avalonia.Input;
using Arssyut.UI.Interop;

namespace Arssyut.UI.Preview;

public static class HotkeyPreview
{
    public static bool Matches(
        KeyEventArgs e,
        string gesture)
    {
        if (string.IsNullOrWhiteSpace(gesture))
            return false;

        var normalized =
            FormatGesture(e);

        return string.Equals(
            normalized,
            gesture.Replace(" ", string.Empty),
            StringComparison.OrdinalIgnoreCase);
    }

    public static string FormatGesture(
        KeyEventArgs e)
    {
        var parts =
            new List<string>();

        if (e.KeyModifiers.HasFlag(KeyModifiers.Control))
            parts.Add("Ctrl");
        if (e.KeyModifiers.HasFlag(KeyModifiers.Shift))
            parts.Add("Shift");
        if (e.KeyModifiers.HasFlag(KeyModifiers.Alt))
            parts.Add("Alt");
        if (e.KeyModifiers.HasFlag(KeyModifiers.Meta))
            parts.Add("Win");

        parts.Add(FormatKey(e.Key));
        return string.Join("+", parts);
    }

    public static bool TryToNativeRegistration(
        string gesture,
        out NativeHotkeyModifiers modifiers,
        out uint virtualKey)
    {
        modifiers = NativeHotkeyModifiers.None;
        virtualKey = 0;

        if (string.IsNullOrWhiteSpace(gesture))
            return false;

        var parts =
            gesture.Split(
                '+',
                StringSplitOptions.RemoveEmptyEntries |
                StringSplitOptions.TrimEntries);

        if (parts.Length == 0)
            return false;

        for (var index = 0;
             index < parts.Length - 1;
             ++index)
        {
            var part = parts[index];

            if (part.Equals("Ctrl", StringComparison.OrdinalIgnoreCase) ||
                part.Equals("Control", StringComparison.OrdinalIgnoreCase))
                modifiers |= NativeHotkeyModifiers.Control;
            else if (part.Equals("Shift", StringComparison.OrdinalIgnoreCase))
                modifiers |= NativeHotkeyModifiers.Shift;
            else if (part.Equals("Alt", StringComparison.OrdinalIgnoreCase))
                modifiers |= NativeHotkeyModifiers.Alt;
            else if (part.Equals("Win", StringComparison.OrdinalIgnoreCase) ||
                     part.Equals("Meta", StringComparison.OrdinalIgnoreCase))
                modifiers |= NativeHotkeyModifiers.Win;
            else
                return false;
        }

        return TryVirtualKey(parts[^1], out virtualKey);
    }

    private static bool TryVirtualKey(
        string key,
        out uint virtualKey)
    {
        virtualKey = 0;

        if (key.Length == 1)
        {
            var character = char.ToUpperInvariant(key[0]);
            if (character is >= 'A' and <= 'Z' ||
                character is >= '0' and <= '9')
            {
                virtualKey = character;
                return true;
            }
        }

        if (key.Length >= 2 &&
            (key[0] == 'F' || key[0] == 'f') &&
            int.TryParse(key[1..], out var function) &&
            function is >= 1 and <= 24)
        {
            virtualKey = (uint)(0x70 + function - 1);
            return true;
        }

        virtualKey = key.ToUpperInvariant() switch
        {
            "BACKSPACE" => 0x08,
            "TAB" => 0x09,
            "ENTER" => 0x0D,
            "PAUSE" => 0x13,
            "ESCAPE" => 0x1B,
            "SPACE" => 0x20,
            "PAGEUP" => 0x21,
            "PAGEDOWN" => 0x22,
            "END" => 0x23,
            "HOME" => 0x24,
            "LEFT" => 0x25,
            "UP" => 0x26,
            "RIGHT" => 0x27,
            "DOWN" => 0x28,
            "PRINTSCREEN" => 0x2C,
            "INSERT" => 0x2D,
            "DELETE" => 0x2E,
            _ => 0
        };

        return virtualKey != 0;
    }

    private static string FormatKey(
        Key key) =>
        key switch
        {
            Key.D0 => "0",
            Key.D1 => "1",
            Key.D2 => "2",
            Key.D3 => "3",
            Key.D4 => "4",
            Key.D5 => "5",
            Key.D6 => "6",
            Key.D7 => "7",
            Key.D8 => "8",
            Key.D9 => "9",
            Key.Space => "Space",
            Key.Return => "Enter",
            Key.Back => "Backspace",
            _ => key.ToString()
        };
}
