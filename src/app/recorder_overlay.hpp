#pragma once

#ifdef _WIN32

#include <Windows.h>

#include <string>

namespace arssyut::app {

struct RecorderOverlayCommands {
    int stop = 0;
    int pause = 0;
    int microphone = 0;
    int camera = 0;
};

class RecorderOverlay final {
public:
    RecorderOverlay() = default;
    ~RecorderOverlay();

    RecorderOverlay(const RecorderOverlay &) = delete;
    RecorderOverlay &operator=(const RecorderOverlay &) = delete;

    [[nodiscard]] bool create(
        HINSTANCE instance,
        HWND owner,
        RecorderOverlayCommands commands);

    void show_toolbar(
        const std::wstring &status,
        const std::wstring &elapsed,
        bool pause_enabled,
        bool paused,
        bool microphone_on,
        bool camera_on);

    void update_toolbar(
        const std::wstring &status,
        const std::wstring &elapsed,
        bool pause_enabled,
        bool paused,
        bool microphone_on,
        bool camera_on);

    void hide_toolbar();

    void show_boundary(
        RECT screen_rect,
        bool exclude_from_capture);

    void hide_boundary();

    [[nodiscard]] bool toolbar_visible() const noexcept
    {
        return toolbar_visible_;
    }

private:
    static LRESULT CALLBACK toolbar_proc(
        HWND window,
        UINT message,
        WPARAM wparam,
        LPARAM lparam);

    static LRESULT CALLBACK boundary_proc(
        HWND window,
        UINT message,
        WPARAM wparam,
        LPARAM lparam);

    void position_toolbar();
    void paint_toolbar(HDC dc);
    void paint_boundary(HDC dc);
    void draw_toolbar_button(
        const DRAWITEMSTRUCT &item,
        int id);

    HINSTANCE instance_ = nullptr;
    HWND owner_ = nullptr;
    RecorderOverlayCommands commands_{};

    HWND toolbar_ = nullptr;
    HWND status_ = nullptr;
    HWND elapsed_ = nullptr;
    HWND pause_ = nullptr;
    HWND stop_ = nullptr;
    HWND microphone_ = nullptr;
    HWND camera_ = nullptr;
    HWND boundary_ = nullptr;

    HFONT small_font_ = nullptr;
    HFONT timer_font_ = nullptr;

    bool toolbar_visible_ = false;
    bool paused_ = false;
    bool microphone_on_ = false;
    bool camera_on_ = false;
};

} // namespace arssyut::app

#endif
