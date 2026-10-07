#pragma once

#ifdef _WIN32

#include "app/device_catalog.hpp"
#include "app/recorder_ui_model.hpp"

#include <Windows.h>

#include <vector>

namespace arssyut::app {

class RecorderSettingsWindow final {
public:
    RecorderSettingsWindow() = default;
    ~RecorderSettingsWindow();

    RecorderSettingsWindow(
        const RecorderSettingsWindow &) = delete;
    RecorderSettingsWindow &operator=(
        const RecorderSettingsWindow &) = delete;

    [[nodiscard]] bool create(
        HINSTANCE instance,
        HWND owner,
        RecorderUiSettings *settings,
        const std::vector<DeviceChoice> *microphones,
        const std::vector<DeviceChoice> *cameras);

    void show();
    void hide();
    void refresh();

    [[nodiscard]] bool visible() const noexcept;

private:
    static LRESULT CALLBACK window_proc(
        HWND window,
        UINT message,
        WPARAM wparam,
        LPARAM lparam);

    void create_controls();
    void populate_devices();
    void sync_from_model();
    void sync_to_model(int id);
    void show_page(int page);
    void notify_owner();
    void choose_output_folder();

    HINSTANCE instance_ = nullptr;
    HWND owner_ = nullptr;
    HWND window_ = nullptr;

    RecorderUiSettings *settings_ = nullptr;
    const std::vector<DeviceChoice> *microphones_ = nullptr;
    const std::vector<DeviceChoice> *cameras_ = nullptr;

    HWND categories_ = nullptr;
    HWND page_title_ = nullptr;

    HWND countdown_ = nullptr;
    HWND boundary_ = nullptr;
    HWND hide_main_ = nullptr;

    HWND fps_label_ = nullptr;
    HWND fps_ = nullptr;
    HWND visual_label_ = nullptr;
    HWND visual_ = nullptr;

    HWND output_label_ = nullptr;
    HWND output_path_ = nullptr;
    HWND browse_output_ = nullptr;

    HWND system_audio_ = nullptr;
    HWND microphone_ = nullptr;
    HWND microphone_device_ = nullptr;

    HWND camera_ = nullptr;
    HWND camera_device_ = nullptr;

    HWND smart_zoom_ = nullptr;
    HWND clicks_ = nullptr;
    HWND keys_ = nullptr;

    HWND hotkey_record_ = nullptr;
    HWND hotkey_pause_ = nullptr;

    HFONT title_font_ = nullptr;
    HFONT normal_font_ = nullptr;
    HFONT small_font_ = nullptr;

    int current_page_ = 0;
};

} // namespace arssyut::app

#endif
