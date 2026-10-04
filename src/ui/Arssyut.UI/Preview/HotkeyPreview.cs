using System;
using System.Collections.Generic;
using Avalonia.Input;

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
