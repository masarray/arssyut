#include "app/recorder_session.hpp"
#include "app/source_catalog.hpp"

#ifdef _WIN32

#include <Windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <uxtheme.h>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace {

using arssyut::app::RecorderConfig;
using arssyut::app::RecorderSession;
using arssyut::app::RecorderSnapshot;
using arssyut::app::RecorderState;
using arssyut::app::RecorderTarget;
using arssyut::windows::mf_writer_stage_name;

constexpr wchar_t kWindowClass[] = L"ArssyutRecorderWindow";
constexpr UINT_PTR kUiTimer = 1;

constexpr COLORREF kBackground = RGB(17, 20, 24);
constexpr COLORREF kHeader = RGB(24, 28, 34);
constexpr COLORREF kCard = RGB(29, 34, 41);
constexpr COLORREF kCardMuted = RGB(25, 29, 35);
constexpr COLORREF kText = RGB(238, 241, 245);
constexpr COLORREF kMuted = RGB(155, 163, 174);
constexpr COLORREF kAccent = RGB(239, 61, 61);
constexpr COLORREF kAccentDark = RGB(185, 42, 42);
constexpr COLORREF kAqua = RGB(73, 215, 199);

enum ControlId : int {
    IdSource = 1001,
    IdRefresh,
    IdFps,
    IdRecord,
    IdOpen,
    IdOpenDiagnostics,
    IdSmartZoom,
    IdClickVisual,
    IdShortcutKeys,
    IdVisualMode,
};

struct AppWindow {
    HWND window = nullptr;

    HWND title_text = nullptr;
    HWND subtitle_text = nullptr;

    HWND source_label = nullptr;
    HWND source_combo = nullptr;
    HWND refresh_button = nullptr;

    HWND quality_label = nullptr;
    HWND fps_combo = nullptr;

    HWND audio_title = nullptr;
    HWND audio_value = nullptr;
    HWND mic_title = nullptr;
    HWND mic_value = nullptr;

    HWND record_button = nullptr;

    HWND status_text = nullptr;
    HWND timer_text = nullptr;
    HWND metrics_text = nullptr;
    HWND output_text = nullptr;
    HWND open_button = nullptr;
    HWND diagnostics_button = nullptr;
    HWND zoom_checkbox = nullptr;
    HWND click_checkbox = nullptr;
    HWND keys_checkbox = nullptr;
    HWND visual_mode_combo = nullptr;

    HFONT title_font = nullptr;
    HFONT normal_font = nullptr;
    HFONT small_font = nullptr;
    HFONT tiny_font = nullptr;
    HFONT record_font = nullptr;

    HBRUSH background_brush = nullptr;
    HBRUSH card_brush = nullptr;

    std::vector<RecorderTarget> targets;
    std::unique_ptr<RecorderSession> session;

    std::filesystem::path last_output;
    std::filesystem::path last_diagnostics;

    RECT idle_window_rect{};
    bool idle_rect_valid = false;
    bool compact_mode = false;
    bool capture_excluded = false;
    bool completion_handled = false;
};

[[nodiscard]] bool active_state(RecorderState state) noexcept
{
    return state == RecorderState::Preparing ||
           state == RecorderState::Recording ||
           state == RecorderState::Stopping ||
           state == RecorderState::Finalizing;
}

[[nodiscard]] std::wstring state_text(RecorderState state)
{
    switch (state) {
    case RecorderState::Preparing:
        return L"Preparing capture + H.264 encoder";
    case RecorderState::Recording:
        return L"Recording";
    case RecorderState::Stopping:
        return L"Stopping capture";
    case RecorderState::Finalizing:
        return L"Finalizing MP4";
    case RecorderState::Ready:
        return L"Recording saved";
    case RecorderState::Failed:
        return L"Recording failed";
    case RecorderState::Idle:
    default:
        return L"Ready to record";
    }
}

[[nodiscard]] std::wstring format_elapsed(std::int64_t ticks)
{
    const std::int64_t seconds =
        std::max<std::int64_t>(
            0,
            ticks /
                arssyut::core::MonotonicClock::ticks_per_second);

    const auto hours = seconds / 3600;
    const auto minutes = (seconds % 3600) / 60;
    const auto secs = seconds % 60;

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

[[nodiscard]] std::wstring widen_ascii(const char *value)
{
    std::wstring result;
    if (!value)
        return result;

    while (*value != '\0') {
        result.push_back(
            static_cast<wchar_t>(
                static_cast<unsigned char>(*value)));
        ++value;
    }
    return result;
}

[[nodiscard]] const wchar_t *visual_mode_file_suffix(
    arssyut::visual::ArVisualProductMode mode) noexcept
{
    switch (mode) {
    case arssyut::visual::ArVisualProductMode::CleanScreen:
        return L"clean-screen";
    case arssyut::visual::ArVisualProductMode::VividPresentation:
        return L"vivid-presentation";
    case arssyut::visual::ArVisualProductMode::PixelAccurate:
    default:
        return L"pixel-accurate";
    }
}

[[nodiscard]] std::filesystem::path default_output_path(
    arssyut::visual::ArVisualProductMode mode)
{
    PWSTR videos = nullptr;
    std::filesystem::path folder;

    if (SUCCEEDED(SHGetKnownFolderPath(
            FOLDERID_Videos,
            KF_FLAG_DEFAULT,
            nullptr,
            &videos)) &&
        videos) {
        folder = videos;
        CoTaskMemFree(videos);
    } else {
        folder = std::filesystem::current_path();
    }

    folder /= L"Arssyut";

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
        visual_mode_file_suffix(mode));

    return folder / filename;
}

void set_font(HWND control, HFONT font)
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
    if (control)
        SetWindowTheme(control, L"DarkMode_Explorer", nullptr);
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
    HWND label = CreateWindowExW(
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

void show(HWND control, bool visible)
{
    if (control)
        ShowWindow(control, visible ? SW_SHOW : SW_HIDE);
}

void move(
    HWND control,
    int x,
    int y,
    int width,
    int height)
{
    if (control)
        MoveWindow(control, x, y, width, height, TRUE);
}

void set_capture_exclusion(AppWindow &app, bool excluded)
{
    if (!app.window || app.capture_excluded == excluded)
        return;

    const DWORD affinity =
        excluded ? WDA_EXCLUDEFROMCAPTURE : WDA_NONE;

    if (SetWindowDisplayAffinity(app.window, affinity))
        app.capture_excluded = excluded;
}

void refresh_sources(AppWindow &app)
{
    const int previous =
        static_cast<int>(
            SendMessageW(
                app.source_combo,
                CB_GETCURSEL,
                0,
                0));

    app.targets =
        arssyut::app::enumerate_recorder_targets(
            app.window);

    SendMessageW(
        app.source_combo,
        CB_RESETCONTENT,
        0,
        0);

    for (std::size_t i = 0; i < app.targets.size(); ++i) {
        const LRESULT item =
            SendMessageW(
                app.source_combo,
                CB_ADDSTRING,
                0,
                reinterpret_cast<LPARAM>(
                    app.targets[i].label.c_str()));

        if (item >= 0) {
            SendMessageW(
                app.source_combo,
                CB_SETITEMDATA,
                static_cast<WPARAM>(item),
                static_cast<LPARAM>(i));
        }
    }

    if (!app.targets.empty()) {
        const int select =
            previous >= 0 &&
                    previous < static_cast<int>(app.targets.size())
                ? previous
                : 0;

        SendMessageW(
            app.source_combo,
            CB_SETCURSEL,
            select,
            0);
    }
}

void layout_idle(AppWindow &app)
{
    show(app.title_text, true);
    show(app.subtitle_text, true);
    show(app.source_label, true);
    show(app.source_combo, true);
    show(app.refresh_button, true);
    show(app.quality_label, true);
    show(app.fps_combo, true);
    show(app.audio_title, true);
    show(app.audio_value, true);
    show(app.mic_title, true);
    show(app.mic_value, true);
    show(app.open_button, true);
    show(app.diagnostics_button, true);
    show(app.output_text, true);
    show(app.metrics_text, true);
    show(app.zoom_checkbox, true);
    show(app.click_checkbox, true);
    show(app.keys_checkbox, true);
    show(app.visual_mode_combo, true);

    move(app.title_text, 22, 12, 260, 28);
    move(app.subtitle_text, 22, 39, 420, 20);

    move(app.source_label, 34, 77, 120, 20);
    move(app.source_combo, 32, 101, 238, 160);
    move(app.refresh_button, 32, 137, 80, 24);

    move(app.quality_label, 310, 77, 100, 20);
    move(app.fps_combo, 307, 101, 126, 120);

    move(app.audio_title, 472, 77, 100, 20);
    move(app.audio_value, 472, 105, 100, 42);

    move(app.mic_title, 596, 77, 80, 20);
    move(app.mic_value, 596, 105, 80, 42);

    move(app.record_button, 695, 69, 96, 96);

    move(app.status_text, 32, 190, 198, 24);
    move(app.zoom_checkbox, 236, 188, 108, 26);
    move(app.click_checkbox, 348, 188, 84, 26);
    move(app.keys_checkbox, 436, 188, 70, 26);
    move(app.visual_mode_combo, 512, 185, 158, 120);
    move(app.timer_text, 680, 190, 100, 24);
    move(app.metrics_text, 32, 216, 515, 22);
    move(app.output_text, 32, 239, 510, 20);
    move(app.open_button, 560, 216, 106, 34);
    move(app.diagnostics_button, 674, 216, 106, 34);

    if (app.idle_rect_valid) {
        const int width =
            app.idle_window_rect.right -
            app.idle_window_rect.left;
        const int height =
            app.idle_window_rect.bottom -
            app.idle_window_rect.top;

        SetWindowPos(
            app.window,
            nullptr,
            app.idle_window_rect.left,
            app.idle_window_rect.top,
            width,
            height,
            SWP_NOZORDER | SWP_NOACTIVATE);
    }

    app.compact_mode = false;
    InvalidateRect(app.window, nullptr, TRUE);
}

void layout_recording(AppWindow &app)
{
    if (!app.compact_mode) {
        GetWindowRect(app.window, &app.idle_window_rect);
        app.idle_rect_valid = true;
    }

    show(app.title_text, false);
    show(app.subtitle_text, false);
    show(app.source_label, false);
    show(app.source_combo, false);
    show(app.refresh_button, false);
    show(app.quality_label, false);
    show(app.fps_combo, false);
    show(app.audio_title, false);
    show(app.audio_value, false);
    show(app.mic_title, false);
    show(app.mic_value, false);
    show(app.open_button, false);
    show(app.diagnostics_button, false);
    show(app.output_text, false);
    show(app.metrics_text, false);
    show(app.zoom_checkbox, false);
    show(app.click_checkbox, false);
    show(app.keys_checkbox, false);
    show(app.visual_mode_combo, false);

    move(app.status_text, 28, 27, 92, 28);
    move(app.timer_text, 118, 23, 102, 34);
    move(app.record_button, 236, 7, 74, 74);

    MONITORINFO info{};
    info.cbSize = sizeof(info);

    HMONITOR monitor =
        MonitorFromWindow(
            app.window,
            MONITOR_DEFAULTTONEAREST);

    int x = CW_USEDEFAULT;
    int y = 32;

    if (GetMonitorInfoW(monitor, &info)) {
        const int width = 330;
        x = info.rcWork.left +
            ((info.rcWork.right - info.rcWork.left) - width) / 2;
        y = info.rcWork.top + 36;
    }

    SetWindowPos(
        app.window,
        HWND_TOPMOST,
        x,
        y,
        330,
        94,
        SWP_SHOWWINDOW);

    app.compact_mode = true;
    InvalidateRect(app.window, nullptr, TRUE);
}

void set_recording_controls(
    AppWindow &app,
    bool recording)
{
    EnableWindow(app.source_combo, !recording);
    EnableWindow(app.refresh_button, !recording);
    EnableWindow(app.fps_combo, !recording);
    EnableWindow(app.zoom_checkbox, !recording);
    EnableWindow(app.click_checkbox, !recording);
    EnableWindow(app.keys_checkbox, !recording);
    EnableWindow(app.visual_mode_combo, !recording);
    EnableWindow(app.record_button, TRUE);

    InvalidateRect(app.record_button, nullptr, TRUE);
}

void start_recording(AppWindow &app)
{
    const int selection =
        static_cast<int>(
            SendMessageW(
                app.source_combo,
                CB_GETCURSEL,
                0,
                0));

    if (selection < 0 ||
        selection >= static_cast<int>(app.targets.size())) {
        MessageBoxW(
            app.window,
            L"Choose a display or window first.",
            L"Arssyut",
            MB_OK | MB_ICONINFORMATION);
        return;
    }

    if (app.session) {
        app.session->request_stop();
        app.session->wait();
        app.session.reset();
    }

    const int fps_selection =
        static_cast<int>(
            SendMessageW(
                app.fps_combo,
                CB_GETCURSEL,
                0,
                0));

    const std::uint32_t fps =
        fps_selection == 0 ? 60U : 30U;

    const int visual_selection =
        static_cast<int>(
            SendMessageW(
                app.visual_mode_combo,
                CB_GETCURSEL,
                0,
                0));

    arssyut::visual::ArVisualProductMode visual_mode =
        arssyut::visual::ArVisualProductMode::PixelAccurate;

    if (visual_selection == 1) {
        visual_mode =
            arssyut::visual::ArVisualProductMode::CleanScreen;
    } else if (visual_selection == 2) {
        visual_mode =
            arssyut::visual::ArVisualProductMode::VividPresentation;
    }

    RecorderConfig config;
    config.target =
        app.targets[
            static_cast<std::size_t>(selection)];
    config.visual_mode = visual_mode;
    config.output_path =
        default_output_path(visual_mode);
    config.output_size = {1920, 1080};
    config.frame_rate = {fps, 1};
    config.bitrate_bps =
        fps == 60 ? 12'000'000U : 8'000'000U;

    config.presentation.smart_zoom =
        SendMessageW(
            app.zoom_checkbox,
            BM_GETCHECK,
            0,
            0) == BST_CHECKED;
    config.presentation.click_visual =
        SendMessageW(
            app.click_checkbox,
            BM_GETCHECK,
            0,
            0) == BST_CHECKED;
    config.presentation.shortcut_keys =
        SendMessageW(
            app.keys_checkbox,
            BM_GETCHECK,
            0,
            0) == BST_CHECKED;
    config.presentation.zoom = 2.0f;

    auto session =
        std::make_unique<RecorderSession>();

    // Exclude only during a real recording attempt. Before Record the GUI
    // remains visible to screenshots, which is important for UI review.
    set_capture_exclusion(app, true);

    const auto status = session->start(config);
    if (!status.ok()) {
        set_capture_exclusion(app, false);
        MessageBoxW(
            app.window,
            L"Could not start the recording session.",
            L"Arssyut",
            MB_OK | MB_ICONERROR);
        return;
    }

    app.last_output = config.output_path;
    app.last_diagnostics = session->diagnostics_path();
    app.session = std::move(session);
    app.completion_handled = false;

    SetWindowTextW(
        app.output_text,
        app.last_output.c_str());

    EnableWindow(app.open_button, FALSE);
    EnableWindow(app.diagnostics_button, FALSE);

    set_recording_controls(app, true);
    InvalidateRect(app.window, nullptr, TRUE);
}

void stop_recording(AppWindow &app)
{
    if (!app.session)
        return;

    app.session->request_stop();
    EnableWindow(app.record_button, FALSE);
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

void update_ui(AppWindow &app)
{
    if (!app.session) {
        SetWindowTextW(app.status_text, L"Ready to record");
        SetWindowTextW(app.timer_text, L"");
        return;
    }

    const RecorderSnapshot snapshot =
        app.session->snapshot();

    SetWindowTextW(
        app.status_text,
        state_text(snapshot.state).c_str());

    if (active_state(snapshot.state)) {
        SetWindowTextW(
            app.timer_text,
            format_elapsed(snapshot.elapsed_ticks).c_str());
    } else {
        SetWindowTextW(app.timer_text, L"");
    }

    if (!app.compact_mode) {
        wchar_t metrics[384]{};
        swprintf_s(
            metrics,
            L"Frames %llu  ·  Encoded %llu  ·  Coalesced %llu  ·  Drop %llu  ·  Capture p95 %u us",
            snapshot.capture_received,
            snapshot.encoder_submitted,
            snapshot.capture_replaced,
            snapshot.capture_busy_drops +
                snapshot.encoder_backpressure,
            snapshot.capture_p95_us);
        SetWindowTextW(app.metrics_text, metrics);
    }

    if ((snapshot.state == RecorderState::Recording ||
         snapshot.state == RecorderState::Stopping ||
         snapshot.state == RecorderState::Finalizing) &&
        !app.compact_mode) {
        layout_recording(app);
    }

    if ((snapshot.state == RecorderState::Ready ||
         snapshot.state == RecorderState::Failed) &&
        !app.completion_handled) {
        app.session->wait();
        app.completion_handled = true;

        set_capture_exclusion(app, false);
        SetWindowPos(
            app.window,
            HWND_NOTOPMOST,
            0,
            0,
            0,
            0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

        layout_idle(app);
        set_recording_controls(app, false);
        EnableWindow(app.record_button, TRUE);

        const bool output_exists =
            std::filesystem::exists(app.last_output);
        EnableWindow(
            app.open_button,
            output_exists ? TRUE : FALSE);

        const bool diagnostics_exists =
            std::filesystem::exists(app.last_diagnostics);
        EnableWindow(
            app.diagnostics_button,
            diagnostics_exists ? TRUE : FALSE);

        if (snapshot.state == RecorderState::Failed) {
            const std::wstring stage =
                widen_ascii(
                    mf_writer_stage_name(
                        snapshot.encoder_failure_stage));

            wchar_t error[512]{};
            swprintf_s(
                error,
                L"Recording failed.\n\nStatus: %u\nDetail: 0x%08X\nEncoder stage: %s\n\nDiagnostics:\n%s",
                static_cast<unsigned>(snapshot.last_error.code),
                snapshot.last_error.detail,
                stage.c_str(),
                app.last_diagnostics.c_str());

            MessageBoxW(
                app.window,
                error,
                L"Arssyut recording error",
                MB_OK | MB_ICONERROR);
        }
    }

    InvalidateRect(app.record_button, nullptr, FALSE);
}

void draw_rounded_card(
    HDC dc,
    const RECT &rect,
    COLORREF fill)
{
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(39, 45, 53));

    HGDIOBJ old_brush = SelectObject(dc, brush);
    HGDIOBJ old_pen = SelectObject(dc, pen);

    RoundRect(
        dc,
        rect.left,
        rect.top,
        rect.right,
        rect.bottom,
        14,
        14);

    SelectObject(dc, old_pen);
    SelectObject(dc, old_brush);
    DeleteObject(pen);
    DeleteObject(brush);
}

void paint_background(AppWindow &app, HDC dc)
{
    RECT client{};
    GetClientRect(app.window, &client);

    HBRUSH background = CreateSolidBrush(kBackground);
    FillRect(dc, &client, background);
    DeleteObject(background);

    if (app.compact_mode) {
        RECT card{10, 8, client.right - 10, client.bottom - 8};
        draw_rounded_card(dc, card, kCard);
        return;
    }

    RECT header{0, 0, client.right, 62};
    HBRUSH header_brush = CreateSolidBrush(kHeader);
    FillRect(dc, &header, header_brush);
    DeleteObject(header_brush);

    draw_rounded_card(dc, RECT{20, 68, 286, 169}, kCard);
    draw_rounded_card(dc, RECT{297, 68, 445, 169}, kCard);
    draw_rounded_card(dc, RECT{455, 68, 572, 169}, kCardMuted);
    draw_rounded_card(dc, RECT{582, 68, 681, 169}, kCardMuted);
    draw_rounded_card(dc, RECT{20, 181, client.right - 20, 268}, kCard);

    HPEN accent = CreatePen(PS_SOLID, 2, kAqua);
    HGDIOBJ old_pen = SelectObject(dc, accent);
    MoveToEx(dc, 20, 61, nullptr);
    LineTo(dc, 170, 61);
    SelectObject(dc, old_pen);
    DeleteObject(accent);
}

void draw_record_button(
    AppWindow &app,
    const DRAWITEMSTRUCT &item)
{
    HDC dc = item.hDC;
    RECT rect = item.rcItem;

    HBRUSH clear = CreateSolidBrush(
        app.compact_mode ? kCard : kBackground);
    FillRect(dc, &rect, clear);
    DeleteObject(clear);

    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    const int diameter = std::min(width, height) - 8;
    const int left = rect.left + (width - diameter) / 2;
    const int top = rect.top + (height - diameter) / 2;

    HPEN outer_pen = CreatePen(PS_SOLID, 2, kAccentDark);
    HBRUSH outer_brush = CreateSolidBrush(RGB(49, 31, 34));

    HGDIOBJ old_pen = SelectObject(dc, outer_pen);
    HGDIOBJ old_brush = SelectObject(dc, outer_brush);

    Ellipse(
        dc,
        left,
        top,
        left + diameter,
        top + diameter);

    const int inset = 7;
    HBRUSH red = CreateSolidBrush(kAccent);
    SelectObject(dc, red);

    Ellipse(
        dc,
        left + inset,
        top + inset,
        left + diameter - inset,
        top + diameter - inset);

    SelectObject(dc, app.record_font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));

    const wchar_t *text =
        app.compact_mode ? L"STOP" : L"REC";

    DrawTextW(
        dc,
        text,
        -1,
        &rect,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    SelectObject(dc, old_brush);
    SelectObject(dc, old_pen);
    DeleteObject(red);
    DeleteObject(outer_brush);
    DeleteObject(outer_pen);
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
            reinterpret_cast<CREATESTRUCTW *>(lparam);

        app =
            static_cast<AppWindow *>(
                create->lpCreateParams);

        app->window = window;

        SetWindowLongPtrW(
            window,
            GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(app));
    }

    if (!app)
        return DefWindowProcW(window, message, wparam, lparam);

    switch (message) {
    case WM_CREATE: {
        BOOL dark = TRUE;
        DwmSetWindowAttribute(
            window,
            DWMWA_USE_IMMERSIVE_DARK_MODE,
            &dark,
            sizeof(dark));

        app->background_brush =
            CreateSolidBrush(kBackground);
        app->card_brush =
            CreateSolidBrush(kCard);

        app->title_font =
            CreateFontW(
                -22, 0, 0, 0, FW_SEMIBOLD,
                FALSE, FALSE, FALSE,
                DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS,
                CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY,
                DEFAULT_PITCH,
                L"Segoe UI");

        app->normal_font =
            CreateFontW(
                -16, 0, 0, 0, FW_NORMAL,
                FALSE, FALSE, FALSE,
                DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS,
                CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY,
                DEFAULT_PITCH,
                L"Segoe UI");

        app->small_font =
            CreateFontW(
                -14, 0, 0, 0, FW_NORMAL,
                FALSE, FALSE, FALSE,
                DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS,
                CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY,
                DEFAULT_PITCH,
                L"Segoe UI");

        app->tiny_font =
            CreateFontW(
                -12, 0, 0, 0, FW_NORMAL,
                FALSE, FALSE, FALSE,
                DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS,
                CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY,
                DEFAULT_PITCH,
                L"Segoe UI");

        app->record_font =
            CreateFontW(
                -20, 0, 0, 0, FW_SEMIBOLD,
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
                L"Arssyut Recorder",
                22, 12, 260, 28,
                app->title_font);

        app->subtitle_text =
            create_label(
                *app,
                L"Screen recording · lightweight native pipeline",
                22, 39, 420, 20,
                app->tiny_font);

        app->source_label =
            create_label(
                *app,
                L"SCREEN",
                34, 77, 120, 20,
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
                    WS_VSCROLL,
                32, 101, 238, 160,
                window,
                reinterpret_cast<HMENU>(IdSource),
                GetModuleHandleW(nullptr),
                nullptr);
        set_font(app->source_combo, app->small_font);
        apply_dark_theme(app->source_combo);

        app->refresh_button =
            CreateWindowExW(
                0,
                L"BUTTON",
                L"Refresh",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                32, 137, 80, 24,
                window,
                reinterpret_cast<HMENU>(IdRefresh),
                GetModuleHandleW(nullptr),
                nullptr);
        set_font(app->refresh_button, app->tiny_font);
        apply_dark_theme(app->refresh_button);

        app->quality_label =
            create_label(
                *app,
                L"QUALITY",
                310, 77, 100, 20,
                app->tiny_font);

        app->fps_combo =
            CreateWindowExW(
                0,
                WC_COMBOBOXW,
                L"",
                WS_CHILD |
                    WS_VISIBLE |
                    WS_TABSTOP |
                    CBS_DROPDOWNLIST,
                307, 101, 126, 120,
                window,
                reinterpret_cast<HMENU>(IdFps),
                GetModuleHandleW(nullptr),
                nullptr);
        set_font(app->fps_combo, app->small_font);
        apply_dark_theme(app->fps_combo);

        SendMessageW(
            app->fps_combo,
            CB_ADDSTRING,
            0,
            reinterpret_cast<LPARAM>(L"1080p · 60"));
        SendMessageW(
            app->fps_combo,
            CB_ADDSTRING,
            0,
            reinterpret_cast<LPARAM>(L"1080p · 30"));
        SendMessageW(
            app->fps_combo,
            CB_SETCURSEL,
            0,
            0);

        app->audio_title =
            create_label(
                *app,
                L"SYSTEM AUDIO",
                472, 77, 100, 20,
                app->tiny_font);
        app->audio_value =
            create_label(
                *app,
                L"Next\nphase",
                472, 105, 100, 42,
                app->small_font);

        app->mic_title =
            create_label(
                *app,
                L"MIC",
                596, 77, 80, 20,
                app->tiny_font);
        app->mic_value =
            create_label(
                *app,
                L"Next\nphase",
                596, 105, 80, 42,
                app->small_font);

        app->record_button =
            CreateWindowExW(
                0,
                L"BUTTON",
                L"",
                WS_CHILD |
                    WS_VISIBLE |
                    WS_TABSTOP |
                    BS_OWNERDRAW,
                695, 69, 96, 96,
                window,
                reinterpret_cast<HMENU>(IdRecord),
                GetModuleHandleW(nullptr),
                nullptr);

        app->status_text =
            create_label(
                *app,
                L"Ready to record",
                32, 190, 250, 24,
                app->normal_font);

        app->timer_text =
            create_label(
                *app,
                L"",
                676, 190, 104, 24,
                app->normal_font);

        app->metrics_text =
            create_label(
                *app,
                L"Diagnostics ready",
                32, 216, 515, 22,
                app->tiny_font);

        app->output_text =
            create_label(
                *app,
                L"Videos\\Arssyut\\…",
                32, 239, 510, 20,
                app->tiny_font);

        app->open_button =
            CreateWindowExW(
                0,
                L"BUTTON",
                L"Open video",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                560, 216, 106, 34,
                window,
                reinterpret_cast<HMENU>(IdOpen),
                GetModuleHandleW(nullptr),
                nullptr);
        set_font(app->open_button, app->tiny_font);
        apply_dark_theme(app->open_button);
        EnableWindow(app->open_button, FALSE);

        app->diagnostics_button =
            CreateWindowExW(
                0,
                L"BUTTON",
                L"Diagnostics",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                674, 216, 106, 34,
                window,
                reinterpret_cast<HMENU>(IdOpenDiagnostics),
                GetModuleHandleW(nullptr),
                nullptr);
        set_font(app->diagnostics_button, app->tiny_font);
        apply_dark_theme(app->diagnostics_button);
        EnableWindow(app->diagnostics_button, FALSE);

        app->zoom_checkbox =
            CreateWindowExW(
                0,
                L"BUTTON",
                L"Smart zoom",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                    BS_AUTOCHECKBOX,
                270, 188, 112, 26,
                window,
                reinterpret_cast<HMENU>(IdSmartZoom),
                GetModuleHandleW(nullptr),
                nullptr);
        set_font(app->zoom_checkbox, app->tiny_font);
        apply_dark_theme(app->zoom_checkbox);
        SendMessageW(
            app->zoom_checkbox,
            BM_SETCHECK,
            BST_CHECKED,
            0);

        app->click_checkbox =
            CreateWindowExW(
                0,
                L"BUTTON",
                L"Clicks",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                    BS_AUTOCHECKBOX,
                386, 188, 94, 26,
                window,
                reinterpret_cast<HMENU>(IdClickVisual),
                GetModuleHandleW(nullptr),
                nullptr);
        set_font(app->click_checkbox, app->tiny_font);
        apply_dark_theme(app->click_checkbox);
        SendMessageW(
            app->click_checkbox,
            BM_SETCHECK,
            BST_CHECKED,
            0);

        app->keys_checkbox =
            CreateWindowExW(
                0,
                L"BUTTON",
                L"Keys",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                    BS_AUTOCHECKBOX,
                484, 188, 78, 26,
                window,
                reinterpret_cast<HMENU>(IdShortcutKeys),
                GetModuleHandleW(nullptr),
                nullptr);
        set_font(app->keys_checkbox, app->tiny_font);
        apply_dark_theme(app->keys_checkbox);
        SendMessageW(
            app->keys_checkbox,
            BM_SETCHECK,
            BST_CHECKED,
            0);

        app->visual_mode_combo =
            CreateWindowExW(
                0,
                WC_COMBOBOXW,
                L"",
                WS_CHILD |
                    WS_VISIBLE |
                    WS_TABSTOP |
                    CBS_DROPDOWNLIST,
                512, 185, 158, 120,
                window,
                reinterpret_cast<HMENU>(IdVisualMode),
                GetModuleHandleW(nullptr),
                nullptr);
        set_font(app->visual_mode_combo, app->tiny_font);
        apply_dark_theme(app->visual_mode_combo);

        SendMessageW(
            app->visual_mode_combo,
            CB_ADDSTRING,
            0,
            reinterpret_cast<LPARAM>(L"Pixel Accurate"));
        SendMessageW(
            app->visual_mode_combo,
            CB_ADDSTRING,
            0,
            reinterpret_cast<LPARAM>(L"Clean Screen"));
        SendMessageW(
            app->visual_mode_combo,
            CB_ADDSTRING,
            0,
            reinterpret_cast<LPARAM>(L"Vivid Presentation"));
        SendMessageW(
            app->visual_mode_combo,
            CB_SETCURSEL,
            0,
            0);

        refresh_sources(*app);

        GetWindowRect(app->window, &app->idle_window_rect);
        app->idle_rect_valid = true;

        SetTimer(window, kUiTimer, 100, nullptr);
        return 0;
    }

    case WM_COMMAND:
        switch (LOWORD(wparam)) {
        case IdRefresh:
            refresh_sources(*app);
            return 0;

        case IdRecord:
            if (app->session) {
                const RecorderState state =
                    app->session->snapshot().state;

                if (active_state(state)) {
                    stop_recording(*app);
                    return 0;
                }
            }

            start_recording(*app);
            return 0;

        case IdOpen:
            open_path(window, app->last_output);
            return 0;

        case IdOpenDiagnostics:
            open_path(window, app->last_diagnostics);
            return 0;

        default:
            break;
        }
        break;

    case WM_TIMER:
        if (wparam == kUiTimer) {
            update_ui(*app);
            return 0;
        }
        break;

    case WM_DRAWITEM:
        if (wparam == IdRecord) {
            const auto *item =
                reinterpret_cast<DRAWITEMSTRUCT *>(lparam);
            if (item)
                draw_record_button(*app, *item);
            return TRUE;
        }
        break;

    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        paint_background(*app, dc);
        EndPaint(window, &paint);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(wparam);
        HWND control = reinterpret_cast<HWND>(lparam);

        SetBkMode(dc, TRANSPARENT);

        if (control == app->subtitle_text ||
            control == app->source_label ||
            control == app->quality_label ||
            control == app->audio_title ||
            control == app->mic_title ||
            control == app->metrics_text ||
            control == app->output_text) {
            SetTextColor(dc, kMuted);
        } else if (control == app->audio_value ||
                   control == app->mic_value) {
            SetTextColor(dc, RGB(116, 124, 135));
        } else if ((control == app->timer_text ||
                    control == app->status_text) &&
                   app->compact_mode) {
            SetTextColor(dc, kAccent);
        } else {
            SetTextColor(dc, kText);
        }

        return reinterpret_cast<INT_PTR>(
            GetStockObject(NULL_BRUSH));
    }

    case WM_CTLCOLORBTN:
        return reinterpret_cast<INT_PTR>(
            app->card_brush);

    case WM_CLOSE:
        if (app->session) {
            const RecorderState state =
                app->session->snapshot().state;

            if (active_state(state)) {
                const int answer =
                    MessageBoxW(
                        window,
                        L"A recording is active. Stop and close Arssyut?",
                        L"Arssyut",
                        MB_YESNO | MB_ICONQUESTION);

                if (answer != IDYES)
                    return 0;

                app->session->request_stop();
                app->session->wait();
            }
        }

        DestroyWindow(window);
        return 0;

    case WM_DESTROY:
        KillTimer(window, kUiTimer);

        set_capture_exclusion(*app, false);

        if (app->session) {
            app->session->request_stop();
            app->session->wait();
        }

        if (app->title_font)
            DeleteObject(app->title_font);
        if (app->normal_font)
            DeleteObject(app->normal_font);
        if (app->small_font)
            DeleteObject(app->small_font);
        if (app->tiny_font)
            DeleteObject(app->tiny_font);
        if (app->record_font)
            DeleteObject(app->record_font);
        if (app->background_brush)
            DeleteObject(app->background_brush);
        if (app->card_brush)
            DeleteObject(app->card_brush);

        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(window, message, wparam, lparam);
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
    common.dwSize = sizeof(common);
    common.dwICC = ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&common);

    WNDCLASSEXW cls{};
    cls.cbSize = sizeof(cls);
    cls.lpfnWndProc = window_proc;
    cls.hInstance = instance;
    cls.hCursor =
        LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    cls.hIcon =
        LoadIconW(nullptr, MAKEINTRESOURCEW(32512));
    cls.hbrBackground =
        reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    cls.lpszClassName = kWindowClass;

    if (!RegisterClassExW(&cls))
        return 1;

    AppWindow app;

    HWND window = CreateWindowExW(
        WS_EX_APPWINDOW,
        kWindowClass,
        L"Arssyut — Screen Recorder",
        WS_OVERLAPPED |
            WS_CAPTION |
            WS_SYSMENU |
            WS_MINIMIZEBOX,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        820,
        310,
        nullptr,
        nullptr,
        instance,
        &app);

    if (!window)
        return 2;

    ShowWindow(window, show_command);
    UpdateWindow(window);

    MSG message{};
    while (GetMessageW(
               &message,
               nullptr,
               0,
               0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    return static_cast<int>(message.wParam);
}

#endif
