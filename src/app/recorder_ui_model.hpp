#pragma once

#ifdef _WIN32

#include "visual/arvisual_modes.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>

namespace arssyut::app {

enum class CaptureMode : std::uint8_t {
    Display = 0,
    Window,
    Region,
    Game,
};

struct RecorderUiSettings {
    CaptureMode capture_mode =
        CaptureMode::Display;

    std::uint32_t frame_rate = 60;

    bool system_audio = false;
    bool microphone = false;
    std::size_t microphone_device = 0;

    bool camera = false;
    std::size_t camera_device = 0;

    bool smart_zoom = true;
    bool click_visual = true;
    bool shortcut_keys = true;

    bool countdown = true;
    bool show_boundary = true;
    bool hide_main_while_recording = true;

    arssyut::visual::ArVisualProductMode visual_mode =
        arssyut::visual::ArVisualProductMode::PixelAccurate;

    std::filesystem::path output_folder;
};

constexpr UINT kUiSettingsChanged =
    WM_APP + 40;

} // namespace arssyut::app

#endif
