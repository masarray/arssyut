#pragma once

#ifdef _WIN32

#include <Windows.h>

namespace arssyut::app {

enum class LucideIcon {
    Monitor = 0,
    AppWindow,
    Region,
    Gamepad,
    Volume,
    Mic,
    Video,
    Sliders,
    Refresh,
    Pause,
    Record,
    Stop,
    Folder,
};

void draw_lucide_icon(
    HDC dc,
    LucideIcon icon,
    RECT bounds,
    COLORREF color,
    int stroke_width = 2,
    bool filled = false);

} // namespace arssyut::app

#endif
