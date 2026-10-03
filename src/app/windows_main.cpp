#include "app/device_catalog.hpp"
#include "app/lucide_icons.hpp"
#include "app/recorder_overlay.hpp"
#include "app/region_geometry.hpp"
#include "app/recorder_session.hpp"
#include "app/recorder_settings_window.hpp"
#include "app/recorder_ui_model.hpp"
#include "app/source_catalog.hpp"

#ifdef _WIN32

#include <Windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <uxtheme.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace {

using arssyut::app::CaptureMode;
using arssyut::app::DeviceChoice;
using arssyut::app::RecorderConfig;
using arssyut::app::RecorderOverlay;
using arssyut::app::RecorderOverlayCommands;
using arssyut::app::RecorderSession;
using arssyut::app::RecorderSettingsWindow;
using arssyut::app::RecorderSnapshot;
using arssyut::app::RecorderState;
using arssyut::app::RecorderTarget;
using arssyut::app::RecorderUiSettings;
using arssyut::windows::mf_writer_stage_name;

constexpr wchar_t kWindowClass[] =
    L"ArssyutRecorderWindow";
constexpr UINT_PTR kUiTimer = 1;
constexpr int kHotkeyRecordStop = 1;

constexpr int kWindowWidth = 820;
constexpr int kWindowHeight = 404;

constexpr COLORREF kBackground =
    RGB(14, 17, 20);
constexpr COLORREF kHeader =
    RGB(18, 21, 25);
constexpr COLORREF kCard =
    RGB(23, 27, 32);
constexpr COLORREF kCardRaised =
    RGB(27, 31, 37);
constexpr COLORREF kBorder =
    RGB(43, 49, 57);
constexpr COLORREF kText =
    RGB(235, 238, 242);
constexpr COLORREF kMuted =
    RGB(145, 153, 164);
constexpr COLORREF kAccent =
    RGB(232, 67, 67);
constexpr COLORREF kAccentDark =
    RGB(158, 47, 47);
constexpr COLORREF kSuccess =
    RGB(73, 207, 163);
constexpr COLORREF kAqua =
    RGB(76, 205, 193);

enum ControlId : int {
    IdModeDisplay = 1001,
    IdModeWindow,
    IdModeRegion,
    IdModeGame,
    IdSource,
    IdRefresh,
    IdSystemAudio,
    IdMicrophone,
    IdMicrophoneDevice,
    IdCamera,
    IdCameraDevice,
    IdSettings,
    IdPause,
    IdRecord,
    IdOpen,
    IdOpenDiagnostics,
};

struct AppWindow {
    HWND window = nullptr;

    HWND title_text = nullptr;
    HWND subtitle_text = nullptr;
    HWND settings_button = nullptr;

    HWND mode_label = nullptr;
    HWND mode_display = nullptr;
    HWND mode_window = nullptr;
    HWND mode_region = nullptr;
    HWND mode_game = nullptr;

    HWND source_label = nullptr;
    HWND source_combo = nullptr;
    HWND refresh_button = nullptr;

    HWND input_label = nullptr;
    HWND system_audio_button = nullptr;
    HWND microphone_button = nullptr;
    HWND microphone_combo = nullptr;
    HWND camera_button = nullptr;
    HWND camera_combo = nullptr;

    HWND summary_text = nullptr;
    HWND status_text = nullptr;
    HWND result_text = nullptr;
    HWND open_button = nullptr;
    HWND diagnostics_button = nullptr;

    HWND pause_button = nullptr;
    HWND record_button = nullptr;

    HFONT title_font = nullptr;
    HFONT normal_font = nullptr;
    HFONT small_font = nullptr;
    HFONT tiny_font = nullptr;
    HFONT record_font = nullptr;

    HBRUSH background_brush = nullptr;
    HBRUSH card_brush = nullptr;

    RecorderUiSettings ui{};
    std::vector<DeviceChoice> microphones;
    std::vector<DeviceChoice> cameras;

    std::vector<RecorderTarget> targets;
    std::vector<std::size_t> visible_target_indices;

    std::unique_ptr<RecorderSession> session;
    RecorderSettingsWindow settings_window;
    RecorderOverlay overlay;

    std::filesystem::path last_output;
    std::filesystem::path last_diagnostics;

    RecorderState visible_state =
        RecorderState::Idle;
    bool completion_handled = false;
    bool overlay_created = false;
    bool settings_created = false;
};

[[nodiscard]] bool active_state(
    RecorderState state) noexcept
{
    return state ==
               RecorderState::Preparing ||
           state ==
               RecorderState::Recording ||
           state ==
               RecorderState::Stopping ||
           state ==
               RecorderState::Finalizing;
}

[[nodiscard]] std::wstring state_text(
    RecorderState state)
{
    switch (state) {
    case RecorderState::Preparing:
        return L"Preparing";
    case RecorderState::Recording:
        return L"Recording";
    case RecorderState::Stopping:
        return L"Stopping";
    case RecorderState::Finalizing:
        return L"Finalizing";
    case RecorderState::Ready:
        return L"Saved";
    case RecorderState::Failed:
        return L"Recording failed";
    case RecorderState::Idle:
    default:
        return L"Ready";
    }
}

[[nodiscard]] const wchar_t *
capture_mode_name(CaptureMode mode) noexcept
{
    switch (mode) {
    case CaptureMode::Window:
        return L"Window";
    case CaptureMode::Region:
        return L"Custom area";
    case CaptureMode::Game:
        return L"Game";
    case CaptureMode::Display:
    default:
        return L"Display";
    }
}

[[nodiscard]] bool capture_mode_backend_ready(
    CaptureMode mode) noexcept
{
    return mode == CaptureMode::Display ||
           mode == CaptureMode::Window ||
           mode == CaptureMode::Region;
}

[[nodiscard]] std::wstring format_elapsed(
    std::int64_t ticks)
{
    const std::int64_t seconds =
        std::max<std::int64_t>(
            0,
            ticks /
                arssyut::core::
                    MonotonicClock::
                        ticks_per_second);

    const auto hours =
        seconds / 3600;
    const auto minutes =
        (seconds % 3600) / 60;
    const auto secs =
        seconds % 60;

    wchar_t buffer[64]{};
    if (hours > 0) {
        swprintf_s(
            buffer,
            L"%02lld:%02lld:%02lld",
            hours,
            minutes,
            secs);
    } else {
        swprintf_s(
            buffer,
            L"%02lld:%02lld",
            minutes,
            secs);
    }
    return buffer;
}

[[nodiscard]] std::wstring widen_ascii(
    const char *value)
{
    std::wstring result;
    if (!value)
        return result;

    while (*value != '\0') {
        result.push_back(
            static_cast<wchar_t>(
                static_cast<
                    unsigned char>(*value)));
        ++value;
    }
    return result;
}

[[nodiscard]] const wchar_t *
visual_mode_file_suffix(
    arssyut::visual::
        ArVisualProductMode mode) noexcept
{
    switch (mode) {
    case arssyut::visual::
        ArVisualProductMode::CleanScreen:
        return L"clean-screen";
    case arssyut::visual::
        ArVisualProductMode::
            VividPresentation:
        return L"vivid-presentation";
    case arssyut::visual::
        ArVisualProductMode::
            PixelAccurate:
    default:
        return L"pixel-accurate";
    }
}

[[nodiscard]]
std::filesystem::path
default_output_folder()
{
    PWSTR videos = nullptr;
    std::filesystem::path folder;

    if (SUCCEEDED(
            SHGetKnownFolderPath(
                FOLDERID_Videos,
                KF_FLAG_DEFAULT,
                nullptr,
                &videos)) &&
        videos) {
        folder = videos;
        CoTaskMemFree(videos);
    } else {
        folder =
            std::filesystem::
                current_path();
    }

    folder /= L"Arssyut";
    return folder;
}

[[nodiscard]]
std::filesystem::path
default_output_path(
    const RecorderUiSettings &ui)
{
    std::filesystem::path folder =
        ui.output_folder.empty()
            ? default_output_folder()
            : ui.output_folder;

    SYSTEMTIME time{};
    GetLocalTime(&time);

    wchar_t filename[192]{};
    swprintf_s(
        filename,
        L"Arssyut-%04u%02u%02u-%02u%02u%02u-%ls.mp4",
        time.wYear,
        time.wMonth,
        time.wDay,
        time.wHour,
        time.wMinute,
        time.wSecond,
        visual_mode_file_suffix(
            ui.visual_mode));

    return folder / filename;
}

void set_font(
    HWND control,
    HFONT font)
{
    if (!control || !font)
        return;

    SendMessageW(
        control,
        WM_SETFONT,
        reinterpret_cast<WPARAM>(font),
        TRUE);
}

void apply_dark_theme(HWND control)
{
    if (control) {
        SetWindowTheme(
            control,
            L"DarkMode_Explorer",
            nullptr);
    }
}

HWND create_label(
    AppWindow &app,
    const wchar_t *text,
    int x,
    int y,
    int width,
    int height,
    HFONT font)
{
    HWND label =
        CreateWindowExW(
            0,
            L"STATIC",
            text,
            WS_CHILD | WS_VISIBLE,
            x,
            y,
            width,
            height,
            app.window,
            nullptr,
            GetModuleHandleW(nullptr),
            nullptr);

    set_font(label, font);
    return label;
}

void open_path(
    HWND owner,
    const std::filesystem::path &path)
{
    if (path.empty())
        return;

    ShellExecuteW(
        owner,
        L"open",
        path.c_str(),
        nullptr,
        nullptr,
        SW_SHOWNORMAL);
}

[[nodiscard]] int selected_source_row(
    const AppWindow &app)
{
    return static_cast<int>(
        SendMessageW(
            app.source_combo,
            CB_GETCURSEL,
            0,
            0));
}

[[nodiscard]] const RecorderTarget *
selected_target(
    const AppWindow &app)
{
    const int row =
        selected_source_row(app);

    if (row < 0 ||
        row >= static_cast<int>(
            app.visible_target_indices.size())) {
        return nullptr;
    }

    const std::size_t target_index =
        app.visible_target_indices[
            static_cast<std::size_t>(row)];

    if (target_index >=
        app.targets.size()) {
        return nullptr;
    }

    return &app.targets[target_index];
}

void refresh_devices(AppWindow &app)
{
    app.microphones =
        arssyut::app::
            enumerate_microphones();
    app.cameras =
        arssyut::app::
            enumerate_cameras();

    SendMessageW(
        app.microphone_combo,
        CB_RESETCONTENT,
        0,
        0);

    if (app.microphones.empty()) {
        SendMessageW(
            app.microphone_combo,
            CB_ADDSTRING,
            0,
            reinterpret_cast<LPARAM>(
                L"No microphone"));
    } else {
        for (const auto &device :
             app.microphones) {
            SendMessageW(
                app.microphone_combo,
                CB_ADDSTRING,
                0,
                reinterpret_cast<LPARAM>(
                    device.name.c_str()));
        }
    }

    SendMessageW(
        app.camera_combo,
        CB_RESETCONTENT,
        0,
        0);

    if (app.cameras.empty()) {
        SendMessageW(
            app.camera_combo,
            CB_ADDSTRING,
            0,
            reinterpret_cast<LPARAM>(
                L"No camera"));
    } else {
        for (const auto &device :
             app.cameras) {
            SendMessageW(
                app.camera_combo,
                CB_ADDSTRING,
                0,
                reinterpret_cast<LPARAM>(
                    device.name.c_str()));
        }
    }

    if (app.ui.microphone_device >=
        std::max<std::size_t>(
            app.microphones.size(),
            1)) {
        app.ui.microphone_device = 0;
    }

    if (app.ui.camera_device >=
        std::max<std::size_t>(
            app.cameras.size(),
            1)) {
        app.ui.camera_device = 0;
    }

    SendMessageW(
        app.microphone_combo,
        CB_SETCURSEL,
        static_cast<WPARAM>(
            app.ui.microphone_device),
        0);

    SendMessageW(
        app.camera_combo,
        CB_SETCURSEL,
        static_cast<WPARAM>(
            app.ui.camera_device),
        0);

    EnableWindow(
        app.microphone_combo,
        !app.microphones.empty());
    EnableWindow(
        app.camera_combo,
        !app.cameras.empty());

    if (app.settings_created)
        app.settings_window.refresh();
}

void refresh_sources(AppWindow &app)
{
    const RecorderTarget *previous_target =
        selected_target(app);

    HMONITOR previous_monitor = nullptr;
    HWND previous_window = nullptr;

    if (previous_target) {
        previous_monitor =
            previous_target->monitor;
        previous_window =
            previous_target->window;
    }

    app.targets =
        arssyut::app::
            enumerate_recorder_targets(
                app.window);
    app.visible_target_indices.clear();

    SendMessageW(
        app.source_combo,
        CB_RESETCONTENT,
        0,
        0);

    const bool want_monitor =
        app.ui.capture_mode ==
            CaptureMode::Display ||
        app.ui.capture_mode ==
            CaptureMode::Region;

    for (std::size_t i = 0;
         i < app.targets.size();
         ++i) {
        const auto &target =
            app.targets[i];

        const bool monitor =
            target.kind ==
            arssyut::windows::
                CaptureTargetKind::Monitor;

        if (monitor != want_monitor)
            continue;

        const std::size_t row =
            app.visible_target_indices.size();

        app.visible_target_indices.
            push_back(i);

        SendMessageW(
            app.source_combo,
            CB_ADDSTRING,
            0,
            reinterpret_cast<LPARAM>(
                target.label.c_str()));

        bool same = false;
        if (monitor) {
            same =
                previous_monitor &&
                previous_monitor ==
                    target.monitor;
        } else {
            same =
                previous_window &&
                previous_window ==
                    target.window;
        }

        if (same) {
            SendMessageW(
                app.source_combo,
                CB_SETCURSEL,
                static_cast<WPARAM>(row),
                0);
        }
    }

    if (!app.visible_target_indices.empty() &&
        SendMessageW(
            app.source_combo,
            CB_GETCURSEL,
            0,
            0) < 0) {
        SendMessageW(
            app.source_combo,
            CB_SETCURSEL,
            0,
            0);
    }
}

[[nodiscard]] RECT camera_viewport_rect(
    RECT target,
    const RecorderSnapshot *snapshot)
{
    if (!snapshot)
        return target;

    const LONG source_width =
        target.right - target.left;
    const LONG source_height =
        target.bottom - target.top;

    if (source_width <= 0 ||
        source_height <= 0) {
        return target;
    }

    const float zoom =
        std::clamp(
            snapshot->
                presentation_camera_zoom,
            1.0f,
            4.0f);

    const float center_x =
        std::clamp(
            snapshot->
                presentation_camera_center_x,
            0.0f,
            1.0f);
    const float center_y =
        std::clamp(
            snapshot->
                presentation_camera_center_y,
            0.0f,
            1.0f);

    const float viewport_width =
        static_cast<float>(
            source_width) / zoom;
    const float viewport_height =
        static_cast<float>(
            source_height) / zoom;

    float left =
        static_cast<float>(target.left) +
        center_x *
            static_cast<float>(
                source_width) -
        viewport_width * 0.5f;

    float top =
        static_cast<float>(target.top) +
        center_y *
            static_cast<float>(
                source_height) -
        viewport_height * 0.5f;

    const float min_left =
        static_cast<float>(target.left);
    const float max_left =
        static_cast<float>(
            target.right) -
        viewport_width;

    const float min_top =
        static_cast<float>(target.top);
    const float max_top =
        static_cast<float>(
            target.bottom) -
        viewport_height;

    left =
        std::clamp(
            left,
            min_left,
            std::max(
                min_left,
                max_left));

    top =
        std::clamp(
            top,
            min_top,
            std::max(
                min_top,
                max_top));

    RECT result{};
    result.left =
        static_cast<LONG>(
            std::lround(left));
    result.top =
        static_cast<LONG>(
            std::lround(top));
    result.right =
        result.left +
        static_cast<LONG>(
            std::lround(
                viewport_width));
    result.bottom =
        result.top +
        static_cast<LONG>(
            std::lround(
                viewport_height));

    return result;
}

[[nodiscard]] bool ensure_region_rect(
    AppWindow &app,
    const RecorderTarget &target)
{
    RECT bounds{};
    if (!arssyut::app::
            recorder_target_screen_rect(
                target,
                bounds)) {
        return false;
    }

    if (!app.ui.
            region_screen_rect_valid) {
        app.ui.region_screen_rect =
            arssyut::app::
                default_region_rect(
                    bounds);
        app.ui.region_screen_rect_valid =
            true;
        return true;
    }

    app.ui.region_screen_rect =
        arssyut::app::
            clamp_region_rect(
                app.ui.region_screen_rect,
                bounds);

    return true;
}

[[nodiscard]] bool apply_region_to_config(
    AppWindow &app,
    const RecorderTarget &target,
    RecorderConfig &config)
{
    if (!ensure_region_rect(
            app,
            target)) {
        return false;
    }

    RECT bounds{};
    if (!arssyut::app::
            recorder_target_screen_rect(
                target,
                bounds)) {
        return false;
    }

    arssyut::app::
        RegionCropMapping mapping;

    if (!arssyut::app::
            map_region_to_crop(
                bounds,
                app.ui.
                    region_screen_rect,
                mapping)) {
        return false;
    }

    config.crop =
        mapping.crop;
    config.output_size =
        mapping.output_size;
    config.presentation_screen_rect =
        mapping.screen_rect;
    config.
        presentation_screen_rect_valid =
            true;

    app.ui.region_screen_rect =
        mapping.screen_rect;
    app.ui.region_screen_rect_valid =
        true;

    return true;
}

void update_capture_boundary(
    AppWindow &app,
    const RecorderSnapshot *snapshot)
{
    if (!app.overlay_created ||
        !app.ui.show_boundary) {
        if (app.overlay_created)
            app.overlay.hide_boundary();
        return;
    }

    const RecorderTarget *target =
        selected_target(app);

    if (!target) {
        app.overlay.hide_boundary();
        return;
    }

    const bool recording =
        snapshot &&
        active_state(snapshot->state);

    RECT rect{};

    if (app.ui.capture_mode ==
        CaptureMode::Region) {
        if (!ensure_region_rect(
                app,
                *target)) {
            app.overlay.hide_boundary();
            return;
        }

        rect =
            app.ui.region_screen_rect;

        if (recording) {
            rect =
                camera_viewport_rect(
                    rect,
                    snapshot);
        }

        app.overlay.show_boundary(
            rect,
            recording,
            !recording);
        return;
    }

    if (!arssyut::app::
            recorder_target_screen_rect(
                *target,
                rect)) {
        app.overlay.hide_boundary();
        return;
    }

    if (recording) {
        rect =
            camera_viewport_rect(
                rect,
                snapshot);
    }

    app.overlay.show_boundary(
        rect,
        recording,
        false);
}

void sync_main_controls_from_model(
    AppWindow &app)
{
    InvalidateRect(
        app.mode_display,
        nullptr,
        FALSE);
    InvalidateRect(
        app.mode_window,
        nullptr,
        FALSE);
    InvalidateRect(
        app.mode_region,
        nullptr,
        FALSE);
    InvalidateRect(
        app.mode_game,
        nullptr,
        FALSE);

    SendMessageW(
        app.system_audio_button,
        BM_SETCHECK,
        app.ui.system_audio
            ? BST_CHECKED
            : BST_UNCHECKED,
        0);

    SendMessageW(
        app.microphone_button,
        BM_SETCHECK,
        app.ui.microphone
            ? BST_CHECKED
            : BST_UNCHECKED,
        0);

    SendMessageW(
        app.camera_button,
        BM_SETCHECK,
        app.ui.camera
            ? BST_CHECKED
            : BST_UNCHECKED,
        0);

    SendMessageW(
        app.microphone_combo,
        CB_SETCURSEL,
        static_cast<WPARAM>(
            app.ui.microphone_device),
        0);

    SendMessageW(
        app.camera_combo,
        CB_SETCURSEL,
        static_cast<WPARAM>(
            app.ui.camera_device),
        0);

    InvalidateRect(
        app.system_audio_button,
        nullptr,
        FALSE);
    InvalidateRect(
        app.microphone_button,
        nullptr,
        FALSE);
    InvalidateRect(
        app.camera_button,
        nullptr,
        FALSE);
}

void update_summary(AppWindow &app)
{
    const wchar_t *visual =
        L"Pixel Accurate";

    if (app.ui.visual_mode ==
        arssyut::visual::
            ArVisualProductMode::
                CleanScreen) {
        visual = L"Clean Screen";
    } else if (
        app.ui.visual_mode ==
        arssyut::visual::
            ArVisualProductMode::
                VividPresentation) {
        visual =
            L"Vivid Presentation";
    }

    wchar_t dimensions[64]{};
    wcscpy_s(
        dimensions,
        L"1920 × 1080");

    if (app.ui.capture_mode ==
            CaptureMode::Region &&
        app.ui.
            region_screen_rect_valid) {
        const RECT region =
            app.ui.region_screen_rect;
        swprintf_s(
            dimensions,
            L"%ld × %ld",
            region.right - region.left,
            region.bottom - region.top);
    }

    wchar_t summary[512]{};
    swprintf_s(
        summary,
        L"%s · %s · %u fps · %s · Zoom %s · Clicks %s · Keys %s",
        capture_mode_name(
            app.ui.capture_mode),
        dimensions,
        app.ui.frame_rate,
        visual,
        app.ui.smart_zoom
            ? L"On"
            : L"Off",
        app.ui.click_visual
            ? L"On"
            : L"Off",
        app.ui.shortcut_keys
            ? L"On"
            : L"Off");

    SetWindowTextW(
        app.summary_text,
        summary);

    if (!capture_mode_backend_ready(
            app.ui.capture_mode)) {
        SetWindowTextW(
            app.result_text,
            L"Game mode UX ready · dedicated game capture backend is the next capture milestone");
    } else if (!app.session) {
        SetWindowTextW(
            app.result_text,
            app.ui.capture_mode ==
                    CaptureMode::Region
                ? L"Drag the top pill to move · drag orange edges/corners to resize"
                : L"MP4 output · native H.264 · recording boundary enabled");
    }
}

void set_capture_mode(
    AppWindow &app,
    CaptureMode mode)
{
    if (app.ui.capture_mode == mode)
        return;

    app.ui.capture_mode = mode;
    refresh_sources(app);
    sync_main_controls_from_model(app);
    update_capture_boundary(
        app,
        nullptr);
    update_summary(app);
}

void set_recording_controls(
    AppWindow &app,
    bool active)
{
    EnableWindow(
        app.mode_display,
        !active);
    EnableWindow(
        app.mode_window,
        !active);
    EnableWindow(
        app.mode_region,
        !active);
    EnableWindow(
        app.mode_game,
        !active);
    EnableWindow(
        app.source_combo,
        !active);
    EnableWindow(
        app.refresh_button,
        !active);
    EnableWindow(
        app.system_audio_button,
        !active);
    EnableWindow(
        app.microphone_button,
        !active);
    EnableWindow(
        app.microphone_combo,
        !active);
    EnableWindow(
        app.camera_button,
        !active);
    EnableWindow(
        app.camera_combo,
        !active);
    EnableWindow(
        app.settings_button,
        !active);

    EnableWindow(
        app.pause_button,
        FALSE);
    EnableWindow(
        app.record_button,
        TRUE);

    InvalidateRect(
        app.record_button,
        nullptr,
        FALSE);
}

[[nodiscard]] bool unsupported_input_enabled(
    const AppWindow &app)
{
    return app.ui.system_audio ||
           app.ui.microphone ||
           app.ui.camera;
}

void show_backend_pending_message(
    AppWindow &app,
    const wchar_t *feature)
{
    std::wstring message =
        feature;
    message +=
        L" is present in the P6 recorder UX, but its capture backend is intentionally not wired in this branch yet.\n\nDisable it to validate the current screen-recording engine. The backend is now explicitly scheduled in the roadmap instead of being hidden from the product design.";

    MessageBoxW(
        app.window,
        message.c_str(),
        L"Arssyut P6 UX preview",
        MB_OK |
            MB_ICONINFORMATION);
}

void start_recording(AppWindow &app)
{
    if (!capture_mode_backend_ready(
            app.ui.capture_mode)) {
        show_backend_pending_message(
            app,
            app.ui.capture_mode ==
                    CaptureMode::Region
                ? L"Custom area capture"
                : L"Game capture");
        return;
    }

    if (unsupported_input_enabled(app)) {
        if (app.ui.microphone) {
            show_backend_pending_message(
                app,
                L"Microphone recording");
        } else if (app.ui.camera) {
            show_backend_pending_message(
                app,
                L"Webcam overlay");
        } else {
            show_backend_pending_message(
                app,
                L"System audio recording");
        }
        return;
    }

    const RecorderTarget *target =
        selected_target(app);

    if (!target) {
        MessageBoxW(
            app.window,
            L"Choose a display or window first.",
            L"Arssyut",
            MB_OK |
                MB_ICONINFORMATION);
        return;
    }

    if (app.session) {
        const RecorderSnapshot previous =
            app.session->snapshot();

        if (active_state(
                previous.state) ||
            !previous.worker_finished) {
            return;
        }

        app.session->wait();
        app.session.reset();
    }

    RecorderConfig config;
    config.target = *target;
    config.visual_mode =
        app.ui.visual_mode;
    config.output_path =
        default_output_path(app.ui);
    config.output_size =
        {1920, 1080};

    if (app.ui.capture_mode ==
        CaptureMode::Region) {
        if (!apply_region_to_config(
                app,
                *target,
                config)) {
            MessageBoxW(
                app.window,
                L"Choose a valid recording region first.",
                L"Arssyut",
                MB_OK |
                    MB_ICONINFORMATION);
            return;
        }
    }

    config.frame_rate =
        {app.ui.frame_rate, 1};
    config.bitrate_bps =
        app.ui.frame_rate == 60
            ? 18'000'000U
            : 12'000'000U;

    config.presentation.smart_zoom =
        app.ui.smart_zoom;
    config.presentation.click_visual =
        app.ui.click_visual;
    config.presentation.shortcut_keys =
        app.ui.shortcut_keys;
    config.presentation.zoom = 2.0f;

    auto session =
        std::make_unique<
            RecorderSession>();

    const auto status =
        session->start(config);

    if (!status.ok()) {
        MessageBoxW(
            app.window,
            L"Could not start the recording session.",
            L"Arssyut",
            MB_OK |
                MB_ICONERROR);
        return;
    }

    app.last_output =
        config.output_path;
    app.last_diagnostics =
        session->diagnostics_path();
    app.session =
        std::move(session);
    app.completion_handled = false;
    app.visible_state =
        RecorderState::Preparing;

    SetWindowTextW(
        app.status_text,
        L"Preparing");

    const std::wstring saving =
        L"Saving to " +
        app.last_output.
            filename().wstring();

    SetWindowTextW(
        app.result_text,
        saving.c_str());

    EnableWindow(
        app.open_button,
        FALSE);
    EnableWindow(
        app.diagnostics_button,
        FALSE);

    set_recording_controls(
        app,
        true);

    if (app.overlay_created) {
        app.overlay.show_toolbar(
            L"Preparing",
            L"00:00",
            false,
            false,
            app.ui.microphone,
            app.ui.camera);
    }

    if (app.ui.hide_main_while_recording) {
        ShowWindow(
            app.window,
            SW_HIDE);
    }
}

void stop_recording(AppWindow &app)
{
    if (!app.session)
        return;

    const RecorderState state =
        app.session->snapshot().state;

    if (!active_state(state))
        return;

    app.session->request_stop();

    if (app.overlay_created) {
        app.overlay.update_toolbar(
            L"Stopping",
            format_elapsed(
                app.session->
                    snapshot().
                    elapsed_ticks),
            false,
            false,
            app.ui.microphone,
            app.ui.camera);
    }

    EnableWindow(
        app.record_button,
        FALSE);
}

void complete_recording(
    AppWindow &app,
    const RecorderSnapshot &snapshot)
{
    app.completion_handled = true;

    if (app.overlay_created) {
        app.overlay.hide_toolbar();
    }

    ShowWindow(
        app.window,
        SW_SHOW);
    SetForegroundWindow(
        app.window);

    set_recording_controls(
        app,
        false);

    const bool output_exists =
        std::filesystem::exists(
            app.last_output);

    EnableWindow(
        app.open_button,
        output_exists
            ? TRUE
            : FALSE);

    const bool diagnostics_exists =
        std::filesystem::exists(
            app.last_diagnostics);

    EnableWindow(
        app.diagnostics_button,
        diagnostics_exists
            ? TRUE
            : FALSE);

    if (snapshot.state ==
        RecorderState::Ready) {
        SetWindowTextW(
            app.status_text,
            L"Saved");

        const std::wstring result =
            L"Saved · " +
            app.last_output.
                filename().wstring();

        SetWindowTextW(
            app.result_text,
            result.c_str());
    } else {
        SetWindowTextW(
            app.status_text,
            L"Recording failed");

        const std::wstring stage =
            widen_ascii(
                mf_writer_stage_name(
                    snapshot.
                        encoder_failure_stage));

        wchar_t error[512]{};
        swprintf_s(
            error,
            L"Error %u · detail 0x%08X · encoder %s · diagnostics available",
            static_cast<unsigned>(
                snapshot.
                    last_error.code),
            snapshot.last_error.detail,
            stage.c_str());

        SetWindowTextW(
            app.result_text,
            error);
    }

    update_capture_boundary(
        app,
        nullptr);
}

void update_ui(AppWindow &app)
{
    if (!app.session) {
        if (app.visible_state !=
            RecorderState::Idle) {
            app.visible_state =
                RecorderState::Idle;
            SetWindowTextW(
                app.status_text,
                L"Ready");
            update_summary(app);
        }

        // Idle overlays are event-driven. Repositioning the Region boundary
        // from a 33 ms timer would fight the user's native move/resize loop.
        return;
    }

    const RecorderSnapshot snapshot =
        app.session->snapshot();

    app.visible_state =
        snapshot.state;

    SetWindowTextW(
        app.status_text,
        state_text(
            snapshot.state).c_str());

    if (active_state(snapshot.state)) {
        if (app.overlay_created) {
            app.overlay.update_toolbar(
                state_text(snapshot.state),
                format_elapsed(
                    snapshot.elapsed_ticks),
                false,
                false,
                app.ui.microphone,
                app.ui.camera);
        }

        update_capture_boundary(
            app,
            &snapshot);
    }

    if ((snapshot.state ==
             RecorderState::Ready ||
         snapshot.state ==
             RecorderState::Failed) &&
        snapshot.worker_finished &&
        !app.completion_handled) {
        complete_recording(
            app,
            snapshot);
    }
}

void draw_rounded_card(
    HDC dc,
    const RECT &rect,
    COLORREF fill)
{
    HBRUSH brush =
        CreateSolidBrush(fill);
    HPEN pen =
        CreatePen(
            PS_SOLID,
            1,
            kBorder);

    HGDIOBJ old_brush =
        SelectObject(dc, brush);
    HGDIOBJ old_pen =
        SelectObject(dc, pen);

    RoundRect(
        dc,
        rect.left,
        rect.top,
        rect.right,
        rect.bottom,
        12,
        12);

    SelectObject(dc, old_pen);
    SelectObject(dc, old_brush);
    DeleteObject(pen);
    DeleteObject(brush);
}

void paint_background(
    AppWindow &app,
    HDC dc)
{
    RECT client{};
    GetClientRect(
        app.window,
        &client);

    HBRUSH background =
        CreateSolidBrush(
            kBackground);
    FillRect(
        dc,
        &client,
        background);
    DeleteObject(background);

    RECT header{
        0,
        0,
        client.right,
        58};

    HBRUSH header_brush =
        CreateSolidBrush(
            kHeader);
    FillRect(
        dc,
        &header,
        header_brush);
    DeleteObject(header_brush);

    draw_rounded_card(
        dc,
        RECT{
            20,
            70,
            client.right - 20,
            198},
        kCard);

    draw_rounded_card(
        dc,
        RECT{
            20,
            208,
            client.right - 20,
            304},
        kCardRaised);

    draw_rounded_card(
        dc,
        RECT{
            20,
            314,
            client.right - 20,
            client.bottom - 12},
        kCard);

    HPEN accent =
        CreatePen(
            PS_SOLID,
            2,
            kAqua);

    HGDIOBJ old_pen =
        SelectObject(
            dc,
            accent);

    MoveToEx(
        dc,
        20,
        57,
        nullptr);
    LineTo(
        dc,
        116,
        57);

    SelectObject(
        dc,
        old_pen);
    DeleteObject(accent);
}

void draw_button_surface(
    HDC dc,
    RECT rect,
    COLORREF fill,
    COLORREF border,
    bool pressed)
{
    if (pressed) {
        fill =
            RGB(
                std::max(
                    0,
                    static_cast<int>(
                        GetRValue(fill)) - 8),
                std::max(
                    0,
                    static_cast<int>(
                        GetGValue(fill)) - 8),
                std::max(
                    0,
                    static_cast<int>(
                        GetBValue(fill)) - 8));
    }

    HBRUSH brush =
        CreateSolidBrush(fill);
    HPEN pen =
        CreatePen(
            PS_SOLID,
            1,
            border);

    HGDIOBJ old_brush =
        SelectObject(dc, brush);
    HGDIOBJ old_pen =
        SelectObject(dc, pen);

    RoundRect(
        dc,
        rect.left + 1,
        rect.top + 1,
        rect.right - 1,
        rect.bottom - 1,
        9,
        9);

    SelectObject(dc, old_pen);
    SelectObject(dc, old_brush);
    DeleteObject(pen);
    DeleteObject(brush);
}

void draw_mode_button(
    AppWindow &app,
    const DRAWITEMSTRUCT &item,
    CaptureMode mode,
    const wchar_t *label)
{
    const bool selected =
        app.ui.capture_mode == mode;
    const bool pressed =
        (item.itemState &
         ODS_SELECTED) != 0;
    const bool enabled =
        (item.itemState &
         ODS_DISABLED) == 0;

    HDC dc = item.hDC;
    RECT rect = item.rcItem;

    const COLORREF fill =
        selected
            ? RGB(104, 31, 36)
            : RGB(31, 35, 41);
    const COLORREF border =
        selected
            ? kAccent
            : kBorder;
    const COLORREF foreground =
        enabled
            ? RGB(246, 248, 250)
            : RGB(105, 111, 119);

    draw_button_surface(
        dc,
        rect,
        fill,
        border,
        pressed);

    LucideIcon icon =
        LucideIcon::Monitor;

    switch (mode) {
    case CaptureMode::Window:
        icon =
            LucideIcon::AppWindow;
        break;
    case CaptureMode::Region:
        icon =
            LucideIcon::Region;
        break;
    case CaptureMode::Game:
        icon =
            LucideIcon::Gamepad;
        break;
    case CaptureMode::Display:
    default:
        icon =
            LucideIcon::Monitor;
        break;
    }

    RECT icon_rect{
        rect.left + 8,
        rect.top + 7,
        rect.left + 26,
        rect.bottom - 7};

    draw_lucide_icon(
        dc,
        icon,
        icon_rect,
        foreground,
        2,
        false);

    RECT text_rect{
        rect.left + 30,
        rect.top,
        rect.right - 6,
        rect.bottom};

    SelectObject(
        dc,
        app.small_font);
    SetBkMode(
        dc,
        TRANSPARENT);
    SetTextColor(
        dc,
        foreground);

    DrawTextW(
        dc,
        label,
        -1,
        &text_rect,
        DT_LEFT |
            DT_VCENTER |
            DT_SINGLELINE |
            DT_END_ELLIPSIS);
}

void draw_input_button(
    AppWindow &app,
    const DRAWITEMSTRUCT &item,
    int id)
{
    bool active = false;
    if (id == IdSystemAudio)
        active = app.ui.system_audio;
    else if (id == IdMicrophone)
        active = app.ui.microphone;
    else if (id == IdCamera)
        active = app.ui.camera;

    const bool pressed =
        (item.itemState &
         ODS_SELECTED) != 0;
    const bool enabled =
        (item.itemState &
         ODS_DISABLED) == 0;

    HDC dc = item.hDC;
    RECT rect = item.rcItem;

    const COLORREF fill =
        active
            ? RGB(104, 31, 36)
            : RGB(31, 35, 41);
    const COLORREF border =
        active
            ? kAccent
            : kBorder;
    const COLORREF foreground =
        enabled
            ? RGB(248, 249, 251)
            : RGB(105, 111, 119);

    draw_button_surface(
        dc,
        rect,
        fill,
        border,
        pressed);

    LucideIcon icon =
        LucideIcon::Volume;
    const wchar_t *label =
        L"System";

    if (id == IdMicrophone) {
        icon = LucideIcon::Mic;
        label = L"Mic";
    } else if (id == IdCamera) {
        icon = LucideIcon::Video;
        label = L"Camera";
    }

    RECT icon_rect{
        rect.left + 8,
        rect.top + 7,
        rect.left + 27,
        rect.bottom - 7};

    draw_lucide_icon(
        dc,
        icon,
        icon_rect,
        foreground,
        2,
        false);

    RECT text_rect{
        rect.left + 33,
        rect.top,
        rect.right - 5,
        rect.bottom};

    SelectObject(
        dc,
        app.tiny_font);
    SetBkMode(
        dc,
        TRANSPARENT);
    SetTextColor(
        dc,
        foreground);

    DrawTextW(
        dc,
        label,
        -1,
        &text_rect,
        DT_LEFT |
            DT_VCENTER |
            DT_SINGLELINE);
}

void draw_primary_button(
    AppWindow &app,
    const DRAWITEMSTRUCT &item,
    bool pause)
{
    HDC dc = item.hDC;
    RECT rect = item.rcItem;

    const bool enabled =
        IsWindowEnabled(
            item.hwndItem) != FALSE;
    const bool pressed =
        (item.itemState &
         ODS_SELECTED) != 0;

    const COLORREF fill =
        pause
            ? RGB(31, 35, 41)
            : kAccent;
    const COLORREF border =
        pause
            ? kBorder
            : kAccentDark;
    const COLORREF foreground =
        enabled
            ? RGB(255, 255, 255)
            : RGB(108, 113, 121);

    draw_button_surface(
        dc,
        rect,
        fill,
        border,
        pressed);

    RECT icon_rect = rect;
    InflateRect(
        &icon_rect,
        -10,
        -8);

    const bool stopping =
        !pause &&
        active_state(
            app.visible_state);

    draw_lucide_icon(
        dc,
        pause
            ? LucideIcon::Pause
            : stopping
                ? LucideIcon::Stop
                : LucideIcon::Record,
        icon_rect,
        foreground,
        2,
        !pause);
}

void draw_secondary_button(
    AppWindow &app,
    const DRAWITEMSTRUCT &item,
    int id)
{
    HDC dc = item.hDC;
    RECT rect = item.rcItem;

    const bool enabled =
        IsWindowEnabled(
            item.hwndItem) != FALSE;
    const bool pressed =
        (item.itemState &
         ODS_SELECTED) != 0;

    draw_button_surface(
        dc,
        rect,
        enabled
            ? RGB(31, 35, 41)
            : RGB(27, 30, 35),
        kBorder,
        pressed);

    const COLORREF foreground =
        enabled
            ? RGB(237, 240, 244)
            : RGB(103, 109, 117);

    LucideIcon icon =
        LucideIcon::Sliders;
    const wchar_t *label =
        L"Settings";

    if (id == IdRefresh) {
        icon =
            LucideIcon::Refresh;
        label =
            L"Refresh";
    } else if (id == IdOpen) {
        icon =
            LucideIcon::Folder;
        label =
            L"Open";
    } else if (id ==
               IdOpenDiagnostics) {
        icon =
            LucideIcon::Sliders;
        label =
            L"Logs";
    }

    RECT icon_rect{
        rect.left + 8,
        rect.top + 7,
        rect.left + 24,
        rect.bottom - 7};

    draw_lucide_icon(
        dc,
        icon,
        icon_rect,
        foreground,
        2,
        false);

    RECT text_rect{
        rect.left + 29,
        rect.top,
        rect.right - 5,
        rect.bottom};

    SelectObject(
        dc,
        app.tiny_font);
    SetBkMode(
        dc,
        TRANSPARENT);
    SetTextColor(
        dc,
        foreground);

    DrawTextW(
        dc,
        label,
        -1,
        &text_rect,
        DT_LEFT |
            DT_VCENTER |
            DT_SINGLELINE |
            DT_END_ELLIPSIS);
}

void draw_combo_item(
    AppWindow &app,
    const DRAWITEMSTRUCT &item)
{
    HWND combo = item.hwndItem;
    if (!combo)
        return;

    HDC dc = item.hDC;
    RECT rect = item.rcItem;

    const bool enabled =
        IsWindowEnabled(combo) != FALSE;
    const bool selected =
        (item.itemState &
         ODS_SELECTED) != 0;

    HBRUSH fill =
        CreateSolidBrush(
            selected
                ? RGB(49, 39, 43)
                : RGB(25, 29, 34));

    FillRect(
        dc,
        &rect,
        fill);
    DeleteObject(fill);

    int index =
        static_cast<int>(
            item.itemID);

    if (index < 0) {
        index =
            static_cast<int>(
                SendMessageW(
                    combo,
                    CB_GETCURSEL,
                    0,
                    0));
    }

    wchar_t text[384]{};
    if (index >= 0) {
        SendMessageW(
            combo,
            CB_GETLBTEXT,
            static_cast<WPARAM>(
                index),
            reinterpret_cast<LPARAM>(
                text));
    }

    RECT text_rect = rect;
    text_rect.left += 8;
    text_rect.right -= 6;

    SelectObject(
        dc,
        app.tiny_font);
    SetBkMode(
        dc,
        TRANSPARENT);
    SetTextColor(
        dc,
        enabled
            ? RGB(238, 241, 245)
            : RGB(102, 108, 116));

    DrawTextW(
        dc,
        text,
        -1,
        &text_rect,
        DT_LEFT |
            DT_VCENTER |
            DT_SINGLELINE |
            DT_END_ELLIPSIS |
            DT_NOPREFIX);

    if ((item.itemState &
         ODS_FOCUS) != 0) {
        RECT focus = rect;
        InflateRect(
            &focus,
            -2,
            -2);
        DrawFocusRect(
            dc,
            &focus);
    }
}

LRESULT CALLBACK window_proc(
    HWND window,
    UINT message,
    WPARAM wparam,
    LPARAM lparam)
{
    auto *app =
        reinterpret_cast<AppWindow *>(
            GetWindowLongPtrW(
                window,
                GWLP_USERDATA));

    if (message == WM_NCCREATE) {
        const auto *create =
            reinterpret_cast<
                CREATESTRUCTW *>(lparam);

        app =
            static_cast<AppWindow *>(
                create->lpCreateParams);

        app->window = window;

        SetWindowLongPtrW(
            window,
            GWLP_USERDATA,
            reinterpret_cast<
                LONG_PTR>(app));
    }

    if (!app) {
        return DefWindowProcW(
            window,
            message,
            wparam,
            lparam);
    }

    switch (message) {
    case WM_CREATE: {
        BOOL dark = TRUE;
        DwmSetWindowAttribute(
            window,
            DWMWA_USE_IMMERSIVE_DARK_MODE,
            &dark,
            sizeof(dark));

        app->ui.output_folder =
            default_output_folder();

        app->background_brush =
            CreateSolidBrush(
                kBackground);
        app->card_brush =
            CreateSolidBrush(
                kCard);

        app->title_font =
            CreateFontW(
                -20, 0, 0, 0,
                FW_SEMIBOLD,
                FALSE, FALSE, FALSE,
                DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS,
                CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY,
                DEFAULT_PITCH,
                L"Segoe UI");

        app->normal_font =
            CreateFontW(
                -15, 0, 0, 0,
                FW_NORMAL,
                FALSE, FALSE, FALSE,
                DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS,
                CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY,
                DEFAULT_PITCH,
                L"Segoe UI");

        app->small_font =
            CreateFontW(
                -13, 0, 0, 0,
                FW_NORMAL,
                FALSE, FALSE, FALSE,
                DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS,
                CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY,
                DEFAULT_PITCH,
                L"Segoe UI");

        app->tiny_font =
            CreateFontW(
                -12, 0, 0, 0,
                FW_NORMAL,
                FALSE, FALSE, FALSE,
                DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS,
                CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY,
                DEFAULT_PITCH,
                L"Segoe UI");

        app->record_font =
            CreateFontW(
                -13, 0, 0, 0,
                FW_SEMIBOLD,
                FALSE, FALSE, FALSE,
                DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS,
                CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY,
                DEFAULT_PITCH,
                L"Segoe UI");

        app->title_text =
            create_label(
                *app,
                L"Arssyut",
                22, 11, 260, 26,
                app->title_font);

        app->subtitle_text =
            create_label(
                *app,
                L"Screen · game · camera recorder",
                22, 36, 420, 18,
                app->tiny_font);

        app->settings_button =
            CreateWindowExW(
                0,
                L"BUTTON",
                L"Settings",
                WS_CHILD |
                    WS_VISIBLE |
                    WS_TABSTOP |
                    BS_OWNERDRAW,
                682, 15, 106, 30,
                window,
                reinterpret_cast<HMENU>(
                    IdSettings),
                GetModuleHandleW(
                    nullptr),
                nullptr);
        set_font(
            app->settings_button,
            app->tiny_font);
        apply_dark_theme(
            app->settings_button);

        app->mode_label =
            create_label(
                *app,
                L"CAPTURE MODE",
                32, 82, 120, 18,
                app->tiny_font);

        const struct {
            int id;
            int x;
            const wchar_t *label;
        } modes[] = {
            {IdModeDisplay, 32, L"Display"},
            {IdModeWindow, 124, L"Window"},
            {IdModeRegion, 216, L"Region"},
            {IdModeGame, 308, L"Game"},
        };

        HWND *mode_handles[] = {
            &app->mode_display,
            &app->mode_window,
            &app->mode_region,
            &app->mode_game,
        };

        for (std::size_t i = 0;
             i < 4;
             ++i) {
            *mode_handles[i] =
                CreateWindowExW(
                    0,
                    L"BUTTON",
                    L"",
                    WS_CHILD |
                        WS_VISIBLE |
                        WS_TABSTOP |
                        BS_OWNERDRAW,
                    modes[i].x,
                    104,
                    84,
                    32,
                    window,
                    reinterpret_cast<HMENU>(
                        static_cast<INT_PTR>(
                            modes[i].id)),
                    GetModuleHandleW(
                        nullptr),
                    nullptr);
        }

        app->source_label =
            create_label(
                *app,
                L"TARGET",
                416, 82, 100, 18,
                app->tiny_font);

        app->source_combo =
            CreateWindowExW(
                0,
                WC_COMBOBOXW,
                L"",
                WS_CHILD |
                    WS_VISIBLE |
                    WS_TABSTOP |
                    CBS_DROPDOWNLIST |
                    CBS_OWNERDRAWFIXED |
                    CBS_HASSTRINGS |
                    WS_VSCROLL,
                416, 104, 278, 190,
                window,
                reinterpret_cast<HMENU>(
                    IdSource),
                GetModuleHandleW(
                    nullptr),
                nullptr);
        set_font(
            app->source_combo,
            app->small_font);
        apply_dark_theme(
            app->source_combo);

        app->refresh_button =
            CreateWindowExW(
                0,
                L"BUTTON",
                L"Refresh",
                WS_CHILD |
                    WS_VISIBLE |
                    WS_TABSTOP |
                    BS_OWNERDRAW,
                704, 104, 70, 32,
                window,
                reinterpret_cast<HMENU>(
                    IdRefresh),
                GetModuleHandleW(
                    nullptr),
                nullptr);
        set_font(
            app->refresh_button,
            app->tiny_font);
        apply_dark_theme(
            app->refresh_button);

        app->summary_text =
            create_label(
                *app,
                L"",
                32, 151, 742, 20,
                app->tiny_font);

        app->input_label =
            create_label(
                *app,
                L"INPUTS",
                32, 218, 100, 18,
                app->tiny_font);

        app->system_audio_button =
            CreateWindowExW(
                0,
                L"BUTTON",
                L"",
                WS_CHILD |
                    WS_VISIBLE |
                    WS_TABSTOP |
                    BS_OWNERDRAW,
                32, 240, 92, 34,
                window,
                reinterpret_cast<HMENU>(
                    IdSystemAudio),
                GetModuleHandleW(
                    nullptr),
                nullptr);

        app->microphone_button =
            CreateWindowExW(
                0,
                L"BUTTON",
                L"",
                WS_CHILD |
                    WS_VISIBLE |
                    WS_TABSTOP |
                    BS_OWNERDRAW,
                134, 240, 78, 34,
                window,
                reinterpret_cast<HMENU>(
                    IdMicrophone),
                GetModuleHandleW(
                    nullptr),
                nullptr);

        app->microphone_combo =
            CreateWindowExW(
                0,
                WC_COMBOBOXW,
                L"",
                WS_CHILD |
                    WS_VISIBLE |
                    WS_TABSTOP |
                    CBS_DROPDOWNLIST |
                    CBS_OWNERDRAWFIXED |
                    CBS_HASSTRINGS |
                    WS_VSCROLL,
                220, 240, 184, 160,
                window,
                reinterpret_cast<HMENU>(
                    IdMicrophoneDevice),
                GetModuleHandleW(
                    nullptr),
                nullptr);
        set_font(
            app->microphone_combo,
            app->tiny_font);
        apply_dark_theme(
            app->microphone_combo);

        app->camera_button =
            CreateWindowExW(
                0,
                L"BUTTON",
                L"",
                WS_CHILD |
                    WS_VISIBLE |
                    WS_TABSTOP |
                    BS_OWNERDRAW,
                414, 240, 90, 34,
                window,
                reinterpret_cast<HMENU>(
                    IdCamera),
                GetModuleHandleW(
                    nullptr),
                nullptr);

        app->camera_combo =
            CreateWindowExW(
                0,
                WC_COMBOBOXW,
                L"",
                WS_CHILD |
                    WS_VISIBLE |
                    WS_TABSTOP |
                    CBS_DROPDOWNLIST |
                    CBS_OWNERDRAWFIXED |
                    CBS_HASSTRINGS |
                    WS_VSCROLL,
                512, 240, 174, 160,
                window,
                reinterpret_cast<HMENU>(
                    IdCameraDevice),
                GetModuleHandleW(
                    nullptr),
                nullptr);
        set_font(
            app->camera_combo,
            app->tiny_font);
        apply_dark_theme(
            app->camera_combo);

        app->pause_button =
            CreateWindowExW(
                0,
                L"BUTTON",
                L"",
                WS_CHILD |
                    WS_VISIBLE |
                    WS_TABSTOP |
                    BS_OWNERDRAW,
                696, 238, 44, 38,
                window,
                reinterpret_cast<HMENU>(
                    IdPause),
                GetModuleHandleW(
                    nullptr),
                nullptr);
        EnableWindow(
            app->pause_button,
            FALSE);

        app->record_button =
            CreateWindowExW(
                0,
                L"BUTTON",
                L"",
                WS_CHILD |
                    WS_VISIBLE |
                    WS_TABSTOP |
                    BS_OWNERDRAW,
                744, 238, 44, 38,
                window,
                reinterpret_cast<HMENU>(
                    IdRecord),
                GetModuleHandleW(
                    nullptr),
                nullptr);

        app->status_text =
            create_label(
                *app,
                L"Ready",
                32, 326, 120, 24,
                app->normal_font);

        app->result_text =
            create_label(
                *app,
                L"",
                154, 328, 414, 20,
                app->tiny_font);

        app->open_button =
            CreateWindowExW(
                0,
                L"BUTTON",
                L"Open video",
                WS_CHILD |
                    WS_VISIBLE |
                    WS_TABSTOP |
                    BS_OWNERDRAW,
                584, 326, 92, 32,
                window,
                reinterpret_cast<HMENU>(
                    IdOpen),
                GetModuleHandleW(
                    nullptr),
                nullptr);
        set_font(
            app->open_button,
            app->tiny_font);
        apply_dark_theme(
            app->open_button);
        EnableWindow(
            app->open_button,
            FALSE);

        app->diagnostics_button =
            CreateWindowExW(
                0,
                L"BUTTON",
                L"Diagnostics",
                WS_CHILD |
                    WS_VISIBLE |
                    WS_TABSTOP |
                    BS_OWNERDRAW,
                686, 326, 102, 32,
                window,
                reinterpret_cast<HMENU>(
                    IdOpenDiagnostics),
                GetModuleHandleW(
                    nullptr),
                nullptr);
        set_font(
            app->diagnostics_button,
            app->tiny_font);
        apply_dark_theme(
            app->diagnostics_button);
        EnableWindow(
            app->diagnostics_button,
            FALSE);

        refresh_devices(*app);
        refresh_sources(*app);
        sync_main_controls_from_model(
            *app);
        update_summary(*app);

        RecorderOverlayCommands commands;
        commands.stop = IdRecord;
        commands.pause = IdPause;
        commands.microphone =
            IdMicrophone;
        commands.camera =
            IdCamera;

        app->overlay_created =
            app->overlay.create(
                GetModuleHandleW(
                    nullptr),
                window,
                commands);

        app->settings_created =
            app->settings_window.create(
                GetModuleHandleW(
                    nullptr),
                window,
                &app->ui,
                &app->microphones,
                &app->cameras);

        update_capture_boundary(
            *app,
            nullptr);

        SetTimer(
            window,
            kUiTimer,
            33,
            nullptr);

        RegisterHotKey(
            window,
            kHotkeyRecordStop,
            MOD_NOREPEAT,
            VK_F9);

        return 0;
    }

    case kUiRegionChanged:
        if (app->ui.capture_mode ==
                CaptureMode::Region &&
            app->overlay_created &&
            app->overlay.
                boundary_editable()) {
            const RecorderTarget *target =
                selected_target(*app);

            if (target) {
                RECT bounds{};
                if (arssyut::app::
                        recorder_target_screen_rect(
                            *target,
                            bounds)) {
                    app->ui.region_screen_rect =
                        arssyut::app::
                            clamp_region_rect(
                                app->overlay.
                                    boundary_rect(),
                                bounds);
                    app->ui.
                        region_screen_rect_valid =
                            true;

                    update_capture_boundary(
                        *app,
                        nullptr);
                    update_summary(*app);
                }
            }
        }
        return 0;

    case WM_COMMAND: {
        const int id =
            LOWORD(wparam);
        const int code =
            HIWORD(wparam);

        switch (id) {
        case IdModeDisplay:
            if (code == BN_CLICKED)
                set_capture_mode(
                    *app,
                    CaptureMode::Display);
            return 0;

        case IdModeWindow:
            if (code == BN_CLICKED)
                set_capture_mode(
                    *app,
                    CaptureMode::Window);
            return 0;

        case IdModeRegion:
            if (code == BN_CLICKED)
                set_capture_mode(
                    *app,
                    CaptureMode::Region);
            return 0;

        case IdModeGame:
            if (code == BN_CLICKED)
                set_capture_mode(
                    *app,
                    CaptureMode::Game);
            return 0;

        case IdRefresh:
            if (code == BN_CLICKED) {
                refresh_devices(*app);
                refresh_sources(*app);
                update_capture_boundary(
                    *app,
                    nullptr);
            }
            return 0;

        case IdSource:
            if (code == CBN_SELCHANGE) {
                if (app->ui.capture_mode ==
                    CaptureMode::Region) {
                    app->ui.
                        region_screen_rect_valid =
                            false;
                }

                update_capture_boundary(
                    *app,
                    nullptr);
                update_summary(*app);
            }
            return 0;

        case IdSystemAudio:
            if (code == BN_CLICKED) {
                app->ui.system_audio =
                    !app->ui.system_audio;
                sync_main_controls_from_model(
                    *app);
                if (app->settings_created)
                    app->settings_window.refresh();
            }
            return 0;

        case IdMicrophone:
            if (code == BN_CLICKED) {
                app->ui.microphone =
                    !app->ui.microphone;
                sync_main_controls_from_model(
                    *app);
                if (app->settings_created)
                    app->settings_window.refresh();
            }
            return 0;

        case IdCamera:
            if (code == BN_CLICKED) {
                app->ui.camera =
                    !app->ui.camera;
                sync_main_controls_from_model(
                    *app);
                if (app->settings_created)
                    app->settings_window.refresh();
            }
            return 0;

        case IdMicrophoneDevice:
            if (code == CBN_SELCHANGE) {
                const LRESULT selection =
                    SendMessageW(
                        app->microphone_combo,
                        CB_GETCURSEL,
                        0,
                        0);
                if (selection >= 0) {
                    app->ui.
                        microphone_device =
                        static_cast<
                            std::size_t>(
                                selection);
                }
                if (app->settings_created)
                    app->settings_window.refresh();
            }
            return 0;

        case IdCameraDevice:
            if (code == CBN_SELCHANGE) {
                const LRESULT selection =
                    SendMessageW(
                        app->camera_combo,
                        CB_GETCURSEL,
                        0,
                        0);
                if (selection >= 0) {
                    app->ui.camera_device =
                        static_cast<
                            std::size_t>(
                                selection);
                }
                if (app->settings_created)
                    app->settings_window.refresh();
            }
            return 0;

        case IdSettings:
            if (code == BN_CLICKED &&
                app->settings_created) {
                app->settings_window.show();
            }
            return 0;

        case IdPause:
            return 0;

        case IdRecord:
            if (app->session &&
                active_state(
                    app->session->
                        snapshot().state)) {
                stop_recording(*app);
            } else {
                start_recording(*app);
            }
            return 0;

        case IdOpen:
            if (code == BN_CLICKED)
                open_path(
                    window,
                    app->last_output);
            return 0;

        case IdOpenDiagnostics:
            if (code == BN_CLICKED)
                open_path(
                    window,
                    app->last_diagnostics);
            return 0;

        default:
            break;
        }
        break;
    }

    case arssyut::app::
        kUiSettingsChanged:
        sync_main_controls_from_model(
            *app);
        refresh_sources(*app);
        update_summary(*app);
        update_capture_boundary(
            *app,
            nullptr);
        return 0;

    case WM_HOTKEY:
        if (wparam ==
            kHotkeyRecordStop) {
            if (app->session &&
                active_state(
                    app->session->
                        snapshot().state)) {
                stop_recording(*app);
            } else {
                start_recording(*app);
            }
            return 0;
        }
        break;

    case WM_TIMER:
        if (wparam == kUiTimer) {
            update_ui(*app);
            return 0;
        }
        break;

    case WM_MEASUREITEM: {
        auto *measure =
            reinterpret_cast<
                MEASUREITEMSTRUCT *>(lparam);

        if (measure &&
            measure->CtlType ==
                ODT_COMBOBOX) {
            measure->itemHeight = 24;
            return TRUE;
        }
        break;
    }

    case WM_DRAWITEM: {
        const auto *item =
            reinterpret_cast<
                DRAWITEMSTRUCT *>(lparam);

        if (!item)
            break;

        if (item->CtlType ==
            ODT_COMBOBOX) {
            draw_combo_item(
                *app,
                *item);
            return TRUE;
        }

        switch (static_cast<int>(
            wparam)) {
        case IdModeDisplay:
            draw_mode_button(
                *app,
                *item,
                CaptureMode::Display,
                L"Display");
            return TRUE;
        case IdModeWindow:
            draw_mode_button(
                *app,
                *item,
                CaptureMode::Window,
                L"Window");
            return TRUE;
        case IdModeRegion:
            draw_mode_button(
                *app,
                *item,
                CaptureMode::Region,
                L"Region");
            return TRUE;
        case IdModeGame:
            draw_mode_button(
                *app,
                *item,
                CaptureMode::Game,
                L"Game");
            return TRUE;
        case IdSystemAudio:
        case IdMicrophone:
        case IdCamera:
            draw_input_button(
                *app,
                *item,
                static_cast<int>(
                    wparam));
            return TRUE;
        case IdPause:
            draw_primary_button(
                *app,
                *item,
                true);
            return TRUE;
        case IdRecord:
            draw_primary_button(
                *app,
                *item,
                false);
            return TRUE;
        case IdSettings:
        case IdRefresh:
        case IdOpen:
        case IdOpenDiagnostics:
            draw_secondary_button(
                *app,
                *item,
                static_cast<int>(
                    wparam));
            return TRUE;
        default:
            break;
        }
        break;
    }

    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc =
            BeginPaint(
                window,
                &paint);
        paint_background(
            *app,
            dc);
        EndPaint(
            window,
            &paint);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_CTLCOLORSTATIC: {
        HDC dc =
            reinterpret_cast<HDC>(
                wparam);
        HWND control =
            reinterpret_cast<HWND>(
                lparam);

        SetBkMode(
            dc,
            TRANSPARENT);

        if (control ==
                app->subtitle_text ||
            control ==
                app->mode_label ||
            control ==
                app->source_label ||
            control ==
                app->input_label ||
            control ==
                app->summary_text ||
            control ==
                app->result_text) {
            SetTextColor(
                dc,
                kMuted);
        } else if (
            control ==
            app->status_text) {
            if (app->visible_state ==
                RecorderState::Failed) {
                SetTextColor(
                    dc,
                    kAccent);
            } else if (
                app->visible_state ==
                RecorderState::Ready) {
                SetTextColor(
                    dc,
                    kSuccess);
            } else {
                SetTextColor(
                    dc,
                    kText);
            }
        } else {
            SetTextColor(
                dc,
                kText);
        }

        return reinterpret_cast<
            INT_PTR>(
                GetStockObject(
                    NULL_BRUSH));
    }

    case WM_CTLCOLORBTN:
        return reinterpret_cast<
            INT_PTR>(
                app->card_brush);

    case WM_CLOSE:
        if (app->session) {
            const RecorderSnapshot snapshot =
                app->session->snapshot();

            if (active_state(
                    snapshot.state)) {
                const int answer =
                    MessageBoxW(
                        window,
                        L"A recording is active. Stop and close Arssyut?",
                        L"Arssyut",
                        MB_YESNO |
                            MB_ICONQUESTION);

                if (answer != IDYES)
                    return 0;

                app->session->
                    request_stop();
                app->session->wait();
            } else if (
                snapshot.worker_finished) {
                app->session->wait();
            }
        }

        DestroyWindow(window);
        return 0;

    case WM_DESTROY:
        KillTimer(
            window,
            kUiTimer);

        UnregisterHotKey(
            window,
            kHotkeyRecordStop);

        if (app->overlay_created) {
            app->overlay.hide_toolbar();
            app->overlay.hide_boundary();
        }

        if (app->session) {
            app->session->
                request_stop();
            app->session->wait();
        }

        if (app->title_font)
            DeleteObject(
                app->title_font);
        if (app->normal_font)
            DeleteObject(
                app->normal_font);
        if (app->small_font)
            DeleteObject(
                app->small_font);
        if (app->tiny_font)
            DeleteObject(
                app->tiny_font);
        if (app->record_font)
            DeleteObject(
                app->record_font);
        if (app->background_brush)
            DeleteObject(
                app->background_brush);
        if (app->card_brush)
            DeleteObject(
                app->card_brush);

        PostQuitMessage(0);
        return 0;

    default:
        break;
    }

    return DefWindowProcW(
        window,
        message,
        wparam,
        lparam);
}

} // namespace

int WINAPI wWinMain(
    HINSTANCE instance,
    HINSTANCE,
    PWSTR,
    int show_command)
{
    SetProcessDpiAwarenessContext(
        DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    INITCOMMONCONTROLSEX common{};
    common.dwSize =
        sizeof(common);
    common.dwICC =
        ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&common);

    WNDCLASSEXW cls{};
    cls.cbSize = sizeof(cls);
    cls.lpfnWndProc =
        window_proc;
    cls.hInstance = instance;
    cls.hCursor =
        LoadCursorW(
            nullptr,
            IDC_ARROW);
    cls.hIcon =
        LoadIconW(
            nullptr,
            IDI_APPLICATION);
    cls.hbrBackground =
        nullptr;
    cls.lpszClassName =
        kWindowClass;

    if (!RegisterClassExW(&cls))
        return 1;

    AppWindow app;

    HWND window =
        CreateWindowExW(
            WS_EX_APPWINDOW,
            kWindowClass,
            L"Arssyut — Screen Recorder",
            WS_OVERLAPPED |
                WS_CAPTION |
                WS_SYSMENU |
                WS_MINIMIZEBOX,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            kWindowWidth,
            kWindowHeight,
            nullptr,
            nullptr,
            instance,
            &app);

    if (!window)
        return 2;

    ShowWindow(
        window,
        show_command);
    UpdateWindow(window);

    MSG message{};
    while (GetMessageW(
               &message,
               nullptr,
               0,
               0) > 0) {
        TranslateMessage(
            &message);
        DispatchMessageW(
            &message);
    }

    return static_cast<int>(
        message.wParam);
}

#endif
