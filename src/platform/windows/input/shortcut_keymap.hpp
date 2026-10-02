#pragma once

#ifdef _WIN32

#include "presentation/shortcut.hpp"

#include <cstdint>

namespace arssyut::windows {

[[nodiscard]] arssyut::presentation::ShortcutKey
shortcut_key_from_virtual_key(std::uint16_t vkey) noexcept;

[[nodiscard]] bool is_modifier_virtual_key(
    std::uint16_t vkey) noexcept;

} // namespace arssyut::windows

#endif
