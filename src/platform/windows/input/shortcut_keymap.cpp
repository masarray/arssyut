#include "platform/windows/input/shortcut_keymap.hpp"

#ifdef _WIN32

#include <Windows.h>

namespace arssyut::windows {

using arssyut::presentation::ShortcutKey;

ShortcutKey shortcut_key_from_virtual_key(
    std::uint16_t vkey) noexcept
{
    if (vkey >= '0' && vkey <= '9') {
        return static_cast<ShortcutKey>(
            static_cast<std::uint16_t>(
                ShortcutKey::Digit0) +
            (vkey - '0'));
    }

    if (vkey >= 'A' && vkey <= 'Z') {
        return static_cast<ShortcutKey>(
            static_cast<std::uint16_t>(
                ShortcutKey::A) +
            (vkey - 'A'));
    }

    if (vkey >= VK_F1 && vkey <= VK_F24) {
        return static_cast<ShortcutKey>(
            static_cast<std::uint16_t>(
                ShortcutKey::F1) +
            (vkey - VK_F1));
    }

    switch (vkey) {
    case VK_ESCAPE: return ShortcutKey::Escape;
    case VK_TAB: return ShortcutKey::Tab;
    case VK_RETURN: return ShortcutKey::Enter;
    case VK_SPACE: return ShortcutKey::Space;
    case VK_BACK: return ShortcutKey::Backspace;
    case VK_DELETE: return ShortcutKey::Delete;
    case VK_INSERT: return ShortcutKey::Insert;
    case VK_HOME: return ShortcutKey::Home;
    case VK_END: return ShortcutKey::End;
    case VK_PRIOR: return ShortcutKey::PageUp;
    case VK_NEXT: return ShortcutKey::PageDown;
    case VK_LEFT: return ShortcutKey::Left;
    case VK_RIGHT: return ShortcutKey::Right;
    case VK_UP: return ShortcutKey::Up;
    case VK_DOWN: return ShortcutKey::Down;
    case VK_SNAPSHOT: return ShortcutKey::PrintScreen;
    case VK_PAUSE: return ShortcutKey::Pause;
    case VK_APPS: return ShortcutKey::Apps;
    case VK_OEM_PLUS: return ShortcutKey::Plus;
    case VK_OEM_MINUS: return ShortcutKey::Minus;
    case VK_OEM_COMMA: return ShortcutKey::Comma;
    case VK_OEM_PERIOD: return ShortcutKey::Period;
    case VK_OEM_2: return ShortcutKey::Slash;
    case VK_OEM_5: return ShortcutKey::Backslash;
    case VK_OEM_1: return ShortcutKey::Semicolon;
    case VK_OEM_7: return ShortcutKey::Quote;
    case VK_OEM_4: return ShortcutKey::LeftBracket;
    case VK_OEM_6: return ShortcutKey::RightBracket;
    case VK_OEM_3: return ShortcutKey::Grave;
    case VK_VOLUME_MUTE: return ShortcutKey::VolumeMute;
    case VK_VOLUME_DOWN: return ShortcutKey::VolumeDown;
    case VK_VOLUME_UP: return ShortcutKey::VolumeUp;
    case VK_MEDIA_NEXT_TRACK: return ShortcutKey::MediaNext;
    case VK_MEDIA_PREV_TRACK: return ShortcutKey::MediaPrevious;
    case VK_MEDIA_STOP: return ShortcutKey::MediaStop;
    case VK_MEDIA_PLAY_PAUSE: return ShortcutKey::MediaPlayPause;
    default:
        return ShortcutKey::Unknown;
    }
}

bool is_modifier_virtual_key(
    std::uint16_t vkey) noexcept
{
    switch (vkey) {
    case VK_CONTROL:
    case VK_LCONTROL:
    case VK_RCONTROL:
    case VK_SHIFT:
    case VK_LSHIFT:
    case VK_RSHIFT:
    case VK_MENU:
    case VK_LMENU:
    case VK_RMENU:
    case VK_LWIN:
    case VK_RWIN:
        return true;
    default:
        return false;
    }
}

} // namespace arssyut::windows

#endif
