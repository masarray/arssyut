#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace arssyut::presentation {

enum ShortcutModifier : std::uint8_t {
    ShortcutCtrl = 1u << 0,
    ShortcutShift = 1u << 1,
    ShortcutAlt = 1u << 2,
    ShortcutWin = 1u << 3,
};

enum class ShortcutKey : std::uint16_t {
    Unknown = 0,

    Digit0 = 0x30,
    Digit1 = 0x31,
    Digit2 = 0x32,
    Digit3 = 0x33,
    Digit4 = 0x34,
    Digit5 = 0x35,
    Digit6 = 0x36,
    Digit7 = 0x37,
    Digit8 = 0x38,
    Digit9 = 0x39,

    A = 0x41,
    B = 0x42,
    C = 0x43,
    D = 0x44,
    E = 0x45,
    F = 0x46,
    G = 0x47,
    H = 0x48,
    I = 0x49,
    J = 0x4A,
    K = 0x4B,
    L = 0x4C,
    M = 0x4D,
    N = 0x4E,
    O = 0x4F,
    P = 0x50,
    Q = 0x51,
    R = 0x52,
    S = 0x53,
    T = 0x54,
    U = 0x55,
    V = 0x56,
    W = 0x57,
    X = 0x58,
    Y = 0x59,
    Z = 0x5A,

    Escape = 0x0100,
    Tab,
    Enter,
    Space,
    Backspace,
    Delete,
    Insert,
    Home,
    End,
    PageUp,
    PageDown,
    Left,
    Right,
    Up,
    Down,
    PrintScreen,
    Pause,
    Apps,
    Plus,
    Minus,
    Comma,
    Period,
    Slash,
    Backslash,
    Semicolon,
    Quote,
    LeftBracket,
    RightBracket,
    Grave,

    F1 = 0x0140,
    F2,
    F3,
    F4,
    F5,
    F6,
    F7,
    F8,
    F9,
    F10,
    F11,
    F12,
    F13,
    F14,
    F15,
    F16,
    F17,
    F18,
    F19,
    F20,
    F21,
    F22,
    F23,
    F24,

    VolumeMute = 0x0180,
    VolumeDown,
    VolumeUp,
    MediaNext,
    MediaPrevious,
    MediaStop,
    MediaPlayPause,
};

struct ShortcutChord {
    ShortcutKey key = ShortcutKey::Unknown;
    std::uint8_t modifiers = 0;
};

[[nodiscard]] constexpr bool same_shortcut(
    ShortcutChord a,
    ShortcutChord b) noexcept
{
    return a.key == b.key &&
           a.modifiers == b.modifiers;
}

enum class KeycapGlyph : std::uint8_t {
    Text = 0,
    WindowsLogo,
};

enum class KeycapSize : std::uint8_t {
    Unit1 = 0,
    Unit125,
    Unit150,
    Unit200,
    Unit350,
};

struct KeycapFrame {
    std::array<wchar_t, 16> label{};
    KeycapGlyph glyph = KeycapGlyph::Text;
    KeycapSize size = KeycapSize::Unit1;
};

constexpr std::size_t kMaxShortcutKeycaps = 5;

} // namespace arssyut::presentation
