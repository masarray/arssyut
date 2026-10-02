#pragma once

#include "presentation/presentation_state.hpp"
#include "presentation/shortcut.hpp"

#include <cstddef>
#include <cstdint>

namespace arssyut::presentation {

[[nodiscard]] bool is_textual_shortcut_key(
    ShortcutKey key) noexcept;

[[nodiscard]] bool should_visualize_shortcut(
    ShortcutChord chord) noexcept;

[[nodiscard]] bool shortcut_key_label(
    ShortcutKey key,
    wchar_t *buffer,
    std::size_t capacity) noexcept;

[[nodiscard]] KeyboardOverlayFrame build_keyboard_overlay(
    ShortcutChord chord,
    std::uint32_t generation) noexcept;

} // namespace arssyut::presentation
