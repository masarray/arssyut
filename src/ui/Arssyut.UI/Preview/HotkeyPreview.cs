using Avalonia.Input;
using Arssyut.UI.Interop;

namespace Arssyut.UI.Preview;

// Compatibility facade for focused-window preview paths. Canonical hotkey
// identity belongs to HotkeyChord; strings are display-only.
public static class HotkeyPreview
{
    public static bool Matches(
        KeyEventArgs e,
        string gesture)
    {
        if (!HotkeyChord.TryFromKeyEvent(
                e,
                out var eventChord) ||
            !HotkeyChord.TryParse(
                gesture,
                out var configured))
            return false;

        return eventChord ==
            configured;
    }

    public static string FormatGesture(
        KeyEventArgs e) =>
        HotkeyChord.TryFromKeyEvent(
            e,
            out var chord)
            ? chord.DisplayText
            : string.Empty;

    public static bool TryToNativeRegistration(
        string gesture,
        out NativeHotkeyModifiers modifiers,
        out uint virtualKey)
    {
        modifiers =
            NativeHotkeyModifiers.None;
        virtualKey = 0;

        if (!HotkeyChord.TryParse(
                gesture,
                out var chord))
            return false;

        modifiers =
            chord.Modifiers;
        virtualKey =
            chord.VirtualKey;
        return true;
    }
}
