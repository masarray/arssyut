#include "presentation/shortcut_visualizer.hpp"

#include <algorithm>
#include <cwchar>

namespace arssyut::presentation {

namespace {

[[nodiscard]] bool is_function_key(
    ShortcutKey key) noexcept
{
    return key >= ShortcutKey::F1 &&
           key <= ShortcutKey::F24;
}


[[nodiscard]] KeycapSize physical_size_for_key(
    ShortcutKey key) noexcept
{
    if (is_function_key(key))
        return KeycapSize::Unit1;

    switch (key) {
    case ShortcutKey::Tab:
    case ShortcutKey::Delete:
    case ShortcutKey::Insert:
    case ShortcutKey::Home:
    case ShortcutKey::End:
    case ShortcutKey::PageUp:
    case ShortcutKey::PageDown:
    case ShortcutKey::PrintScreen:
    case ShortcutKey::Pause:
    case ShortcutKey::Apps:
    case ShortcutKey::VolumeMute:
    case ShortcutKey::VolumeDown:
    case ShortcutKey::VolumeUp:
    case ShortcutKey::MediaNext:
    case ShortcutKey::MediaPrevious:
    case ShortcutKey::MediaStop:
    case ShortcutKey::MediaPlayPause:
        return KeycapSize::Unit125;

    case ShortcutKey::Enter:
        return KeycapSize::Unit150;

    case ShortcutKey::Backspace:
        return KeycapSize::Unit200;

    case ShortcutKey::Space:
        return KeycapSize::Unit350;

    case ShortcutKey::Unknown:
    default:
        return KeycapSize::Unit1;
    }
}

void copy_label(
    wchar_t *destination,
    std::size_t capacity,
    const wchar_t *source) noexcept
{
    if (!destination || capacity == 0)
        return;

    destination[0] = L'\0';
    if (!source)
        return;

    const std::size_t count =
        std::min(
            std::wcslen(source),
            capacity - 1);
    std::wmemcpy(
        destination,
        source,
        count);
    destination[count] = L'\0';
}

void set_keycap(
    KeycapFrame &keycap,
    const wchar_t *label,
    KeycapGlyph glyph,
    KeycapSize size) noexcept
{
    keycap.label.fill(L'\0');
    copy_label(
        keycap.label.data(),
        keycap.label.size(),
        label);
    keycap.glyph = glyph;
    keycap.size = size;
}

bool append_keycap(
    KeyboardOverlayFrame &frame,
    const wchar_t *label,
    KeycapGlyph glyph = KeycapGlyph::Text,
    KeycapSize size = KeycapSize::Unit1) noexcept
{
    if (frame.keycap_count >= frame.keycaps.size())
        return false;

    set_keycap(
        frame.keycaps[frame.keycap_count],
        label,
        glyph,
        size);
    ++frame.keycap_count;
    return true;
}

} // namespace

bool is_textual_shortcut_key(
    ShortcutKey key) noexcept
{
    const auto value =
        static_cast<std::uint16_t>(key);

    if ((value >= static_cast<std::uint16_t>(ShortcutKey::Digit0) &&
         value <= static_cast<std::uint16_t>(ShortcutKey::Digit9)) ||
        (value >= static_cast<std::uint16_t>(ShortcutKey::A) &&
         value <= static_cast<std::uint16_t>(ShortcutKey::Z))) {
        return true;
    }

    switch (key) {
    case ShortcutKey::Space:
    case ShortcutKey::Plus:
    case ShortcutKey::Minus:
    case ShortcutKey::Comma:
    case ShortcutKey::Period:
    case ShortcutKey::Slash:
    case ShortcutKey::Backslash:
    case ShortcutKey::Semicolon:
    case ShortcutKey::Quote:
    case ShortcutKey::LeftBracket:
    case ShortcutKey::RightBracket:
    case ShortcutKey::Grave:
        return true;
    default:
        return false;
    }
}

bool should_visualize_shortcut(
    ShortcutChord chord) noexcept
{
    if (chord.key == ShortcutKey::Unknown)
        return false;

    // Modified keys represent explicit actions. We show the action chord but
    // never reconstruct text or clipboard content.
    if (chord.modifiers != 0)
        return true;

    // Ordinary unmodified printable input is deliberately hidden.
    if (is_textual_shortcut_key(chord.key))
        return false;

    if (is_function_key(chord.key))
        return true;

    switch (chord.key) {
    case ShortcutKey::Escape:
    case ShortcutKey::Tab:
    case ShortcutKey::Enter:
    case ShortcutKey::Backspace:
    case ShortcutKey::Delete:
    case ShortcutKey::Insert:
    case ShortcutKey::Home:
    case ShortcutKey::End:
    case ShortcutKey::PageUp:
    case ShortcutKey::PageDown:
    case ShortcutKey::Left:
    case ShortcutKey::Right:
    case ShortcutKey::Up:
    case ShortcutKey::Down:
    case ShortcutKey::PrintScreen:
    case ShortcutKey::Pause:
    case ShortcutKey::Apps:
    case ShortcutKey::VolumeMute:
    case ShortcutKey::VolumeDown:
    case ShortcutKey::VolumeUp:
    case ShortcutKey::MediaNext:
    case ShortcutKey::MediaPrevious:
    case ShortcutKey::MediaStop:
    case ShortcutKey::MediaPlayPause:
        return true;
    default:
        return false;
    }
}

bool shortcut_key_label(
    ShortcutKey key,
    wchar_t *buffer,
    std::size_t capacity) noexcept
{
    if (!buffer || capacity == 0)
        return false;

    buffer[0] = L'\0';

    const auto value =
        static_cast<std::uint16_t>(key);

    if ((value >= static_cast<std::uint16_t>(ShortcutKey::Digit0) &&
         value <= static_cast<std::uint16_t>(ShortcutKey::Digit9)) ||
        (value >= static_cast<std::uint16_t>(ShortcutKey::A) &&
         value <= static_cast<std::uint16_t>(ShortcutKey::Z))) {
        if (capacity < 2)
            return false;
        buffer[0] =
            static_cast<wchar_t>(value);
        buffer[1] = L'\0';
        return true;
    }

    if (is_function_key(key)) {
        const unsigned number =
            static_cast<unsigned>(
                value -
                static_cast<std::uint16_t>(ShortcutKey::F1) +
                1);
        if (capacity < 3)
            return false;

        buffer[0] = L'F';
        if (number < 10) {
            buffer[1] =
                static_cast<wchar_t>(
                    L'0' + number);
            buffer[2] = L'\0';
        } else {
            if (capacity < 4)
                return false;
            buffer[1] =
                static_cast<wchar_t>(
                    L'0' + (number / 10));
            buffer[2] =
                static_cast<wchar_t>(
                    L'0' + (number % 10));
            buffer[3] = L'\0';
        }
        return true;
    }

    const wchar_t *label = nullptr;
    switch (key) {
    case ShortcutKey::Escape: label = L"Esc"; break;
    case ShortcutKey::Tab: label = L"Tab"; break;
    case ShortcutKey::Enter: label = L"Enter"; break;
    case ShortcutKey::Space: label = L"Space"; break;
    case ShortcutKey::Backspace: label = L"Backspace"; break;
    case ShortcutKey::Delete: label = L"Delete"; break;
    case ShortcutKey::Insert: label = L"Insert"; break;
    case ShortcutKey::Home: label = L"Home"; break;
    case ShortcutKey::End: label = L"End"; break;
    case ShortcutKey::PageUp: label = L"PgUp"; break;
    case ShortcutKey::PageDown: label = L"PgDn"; break;
    case ShortcutKey::Left: label = L"Left"; break;
    case ShortcutKey::Right: label = L"Right"; break;
    case ShortcutKey::Up: label = L"Up"; break;
    case ShortcutKey::Down: label = L"Down"; break;
    case ShortcutKey::PrintScreen: label = L"PrtSc"; break;
    case ShortcutKey::Pause: label = L"Pause"; break;
    case ShortcutKey::Apps: label = L"Menu"; break;
    case ShortcutKey::Plus: label = L"+"; break;
    case ShortcutKey::Minus: label = L"-"; break;
    case ShortcutKey::Comma: label = L","; break;
    case ShortcutKey::Period: label = L"."; break;
    case ShortcutKey::Slash: label = L"/"; break;
    case ShortcutKey::Backslash: label = L"\\";
        break;
    case ShortcutKey::Semicolon: label = L";"; break;
    case ShortcutKey::Quote: label = L"'"; break;
    case ShortcutKey::LeftBracket: label = L"["; break;
    case ShortcutKey::RightBracket: label = L"]"; break;
    case ShortcutKey::Grave: label = L"`"; break;
    case ShortcutKey::VolumeMute: label = L"Mute"; break;
    case ShortcutKey::VolumeDown: label = L"Vol-"; break;
    case ShortcutKey::VolumeUp: label = L"Vol+"; break;
    case ShortcutKey::MediaNext: label = L"Next"; break;
    case ShortcutKey::MediaPrevious: label = L"Prev"; break;
    case ShortcutKey::MediaStop: label = L"Stop"; break;
    case ShortcutKey::MediaPlayPause: label = L"Play"; break;
    case ShortcutKey::Unknown:
    default:
        return false;
    }

    copy_label(
        buffer,
        capacity,
        label);
    return buffer[0] != L'\0';
}

KeyboardOverlayFrame build_keyboard_overlay(
    ShortcutChord chord,
    std::uint32_t generation) noexcept
{
    KeyboardOverlayFrame frame;
    frame.generation = generation;

    if (!should_visualize_shortcut(chord))
        return frame;

    if ((chord.modifiers & ShortcutCtrl) != 0)
        append_keycap(
            frame,
            L"Ctrl",
            KeycapGlyph::Text,
            KeycapSize::Unit125);
    if ((chord.modifiers & ShortcutShift) != 0)
        append_keycap(
            frame,
            L"Shift",
            KeycapGlyph::Text,
            KeycapSize::Unit150);
    if ((chord.modifiers & ShortcutAlt) != 0)
        append_keycap(
            frame,
            L"Alt",
            KeycapGlyph::Text,
            KeycapSize::Unit125);
    if ((chord.modifiers & ShortcutWin) != 0)
        append_keycap(
            frame,
            L"",
            KeycapGlyph::WindowsLogo,
            KeycapSize::Unit1);

    wchar_t label[16]{};
    if (!shortcut_key_label(
            chord.key,
            label,
            sizeof(label) / sizeof(label[0]))) {
        frame.keycap_count = 0;
        frame.generation = 0;
        return frame;
    }

    append_keycap(
        frame,
        label,
        KeycapGlyph::Text,
        physical_size_for_key(chord.key));

    return frame;
}

} // namespace arssyut::presentation
