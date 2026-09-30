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
#include <chrono>
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

constexpr wchar_t kWindowClass[] =
    L"ArssyutRecorderWindow";

constexpr UINT_PTR kUiTimer = 1;

enum ControlId : int {
    IdSource = 1001,
    IdRefresh,
    IdFps,
    IdRecord,
    IdOpen,
    IdOpenDiagnostics,
};

struct AppWindow {
    HWND window = nullptr;
    HWND source_combo = nullptr;
    HWND refresh_button = nullptr;
    HWND fps_combo = nullptr;
    HWND record_button = nullptr;
    HWND open_button = nullptr;
    HWND diagnostics_button = nullptr;
    HWND status_text = nullptr;
    HWND timer_text = nullptr;
    HWND metrics_text = nullptr;
    HWND output_text = nullptr;

    HFONT title_font = nullptr;
    HFONT normal_font = nullptr;
    HFONT small_font = nullptr;

    HBRUSH background_brush = nullptr;
    HBRUSH field_brush = nullptr;

    std::vector<RecorderTarget> targets;
    std::unique_ptr<RecorderSession> session;

    std::filesystem::path last_output;
    std::filesystem::path last_diagnostics;
    bool completion_handled = false;
};

[[nodiscard]] std::wstring state_text(
    RecorderState state)
{
    switch (state) {
    case RecorderState::Preparing:
        return L"Preparing capture + H.264 encoder…";
    case RecorderState::Recording:
        return L"Recording";
    case RecorderState::Stopping:
        return L"Stopping capture…";
    case RecorderState::Finalizing:
        return L"Finalizing MP4…";
    case RecorderState::Ready:
        return L"Recording saved";
    case RecorderState::Failed:
        return L"Recording failed";
    case RecorderState::Idle:
    default:
        return L"Ready";
    }
}

[[nodiscard]] std::wstring format_elapsed(
    std::int64_t ticks)
{
    const std::int64_t seconds =
        std::max<std::int64_t>(
            0,
            ticks /
                arssyut::core::MonotonicClock::
                    ticks_per_second);

    const auto hours = seconds / 3600;
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

[[nodiscard]] std::filesystem::path
default_output_path()
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
        folder =
            std::filesystem::current_path();
    }

    folder /= L"Arssyut";

    SYSTEMTIME time{};
    GetLocalTime(&time);

    wchar_t filename[128]{};
    swprintf_s(
        filename,
        L"Arssyut-%04u%02u%02u-%02u%02u%02u.mp4",
        time.wYear,
        time.wMonth,
        time.wDay,
        time.wHour,
        time.wMinute,
        time.wSecond);

    return folder / filename;
}

void set_font(
    HWND control,
    HFONT font)
{
    SendMessageW(
        control,
        WM_SETFONT,
        reinterpret_cast<WPARAM>(font),
        TRUE);
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

void apply_dark_theme(HWND control)
{
    if (control) {
        SetWindowTheme(
            control,
            L"DarkMode_Explorer",
            nullptr);
    }
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
        arssyut::app::
            enumerate_recorder_targets(
                app.window);

    SendMessageW(
        app.source_combo,
        CB_RESETCONTENT,
        0,
        0);

    for (std::size_t i = 0;
         i < app.targets.size();
         ++i) {
        const LRESULT item =
            SendMessageW(
                app.source_combo,
                CB_ADDSTRING,
                0,
                reinterpret_cast<LPARAM>(
                    app.targets[i].
                        label.c_str()));

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
                    previous <
                        static_cast<int>(
                            app.targets.size())
                ? previous
                : 0;

        SendMessageW(
            app.source_combo,
            CB_SETCURSEL,
            select,
            0);
    }
}

void set_recording_controls(
    AppWindow &app,
    bool recording)
{
    EnableWindow(
        app.source_combo,
        !recording);
    EnableWindow(
        app.refresh_button,
        !recording);
    EnableWindow(
        app.fps_combo,
        !recording);

    SetWindowTextW(
        app.record_button,
        recording
            ? L"Stop"
            : L"Record");
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
        selection >=
            static_cast<int>(
                app.targets.size())) {
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

    RecorderConfig config;
    config.target =
        app.targets[
            static_cast<std::size_t>(
                selection)];
    config.output_path =
        default_output_path();
    config.output_size =
        {1920, 1080};
    config.frame_rate =
        {fps, 1};
    config.bitrate_bps =
        fps == 60
            ? 12'000'000U
            : 8'000'000U;

    auto session =
        std::make_unique<RecorderSession>();

    const auto status =
        session->start(config);

    if (!status.ok()) {
        MessageBoxW(
            app.window,
            L"Could not start the recording session.",
            L"Arssyut",
            MB_OK | MB_ICONERROR);
        return;
    }

    app.last_output =
        config.output_path;
    app.last_diagnostics =
        session->diagnostics_path();
    app.session =
        std::move(session);
    app.completion_handled = false;

    SetWindowTextW(
        app.output_text,
        app.last_output.c_str());

    EnableWindow(
        app.open_button,
        FALSE);
    EnableWindow(
        app.diagnostics_button,
        FALSE);

    set_recording_controls(
        app,
        true);
}

void stop_recording(AppWindow &app)
{
    if (!app.session)
        return;

    app.session->request_stop();
    EnableWindow(
        app.record_button,
        FALSE);
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
        SetWindowTextW(
            app.status_text,
            L"Ready");
        SetWindowTextW(
            app.timer_text,
            L"");
        return;
    }

    const RecorderSnapshot snapshot =
        app.session->snapshot();

    SetWindowTextW(
        app.status_text,
        state_text(snapshot.state).c_str());

    if (snapshot.state ==
            RecorderState::Recording ||
        snapshot.state ==
            RecorderState::Stopping ||
        snapshot.state ==
            RecorderState::Finalizing) {
        const std::wstring elapsed =
            L"●  " +
            format_elapsed(
                snapshot.elapsed_ticks);
        SetWindowTextW(
            app.timer_text,
            elapsed.c_str());
    } else {
        SetWindowTextW(
            app.timer_text,
            L"");
    }

    wchar_t metrics[512]{};
    swprintf_s(
        metrics,
        L"Frames %llu  · Encoded %llu  · Coalesced %llu  · Busy drop %llu\r\n"
        L"Capture p95 ≤ %u µs  · CPU p95 ≤ %u µs  · GPU p95 ≤ %u µs  · Memory %.1f MB",
        snapshot.capture_received,
        snapshot.encoder_submitted,
        snapshot.capture_replaced,
        snapshot.encoder_backpressure +
            snapshot.capture_busy_drops,
        snapshot.capture_p95_us,
        snapshot.compositor_cpu_p95_us,
        snapshot.compositor_gpu_p95_us,
        static_cast<double>(
            snapshot.memory_private_bytes) /
            (1024.0 * 1024.0));

    SetWindowTextW(
        app.metrics_text,
        metrics);

    if ((snapshot.state ==
             RecorderState::Ready ||
         snapshot.state ==
             RecorderState::Failed) &&
        !app.completion_handled) {
        app.session->wait();
        app.completion_handled = true;

        set_recording_controls(
            app,
            false);
        EnableWindow(
            app.record_button,
            TRUE);

        const bool output_exists =
            std::filesystem::exists(
                app.last_output);

        EnableWindow(
            app.open_button,
            output_exists ? TRUE : FALSE);

        const bool diagnostics_exists =
            std::filesystem::exists(
                app.last_diagnostics);

        EnableWindow(
            app.diagnostics_button,
            diagnostics_exists
                ? TRUE
                : FALSE);

        if (snapshot.state ==
            RecorderState::Failed) {
            wchar_t error[256]{};
            swprintf_s(
                error,
                L"Recording failed.\n\nStatus: %u\nDetail: 0x%08X\n\nDiagnostics: %s",
                static_cast<unsigned>(
                    snapshot.last_error.code),
                snapshot.last_error.detail,
                app.last_diagnostics.c_str());

            MessageBoxW(
                app.window,
                error,
                L"Arssyut recording error",
                MB_OK | MB_ICONERROR);
        }
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
            reinterpret_cast<LONG_PTR>(app));
    }

    if (!app)
        return DefWindowProcW(
            window,
            message,
            wparam,
            lparam);

    switch (message) {
    case WM_CREATE: {
        BOOL dark = TRUE;
        DwmSetWindowAttribute(
            window,
            DWMWA_USE_IMMERSIVE_DARK_MODE,
            &dark,
            sizeof(dark));

        // Keep the recorder controls out of monitor captures where the
        // operating system supports capture exclusion.
        (void)SetWindowDisplayAffinity(
            window,
            WDA_EXCLUDEFROMCAPTURE);

        app->background_brush =
            CreateSolidBrush(
                RGB(24, 25, 28));
        app->field_brush =
            CreateSolidBrush(
                RGB(34, 36, 41));

        app->title_font =
            CreateFontW(
                -24,
                0,
                0,
                0,
                FW_SEMIBOLD,
                FALSE,
                FALSE,
                FALSE,
                DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS,
                CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY,
                DEFAULT_PITCH,
                L"Segoe UI");

        app->normal_font =
            CreateFontW(
                -16,
                0,
                0,
                0,
                FW_NORMAL,
                FALSE,
                FALSE,
                FALSE,
                DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS,
                CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY,
                DEFAULT_PITCH,
                L"Segoe UI");

        app->small_font =
            CreateFontW(
                -14,
                0,
                0,
                0,
                FW_NORMAL,
                FALSE,
                FALSE,
                FALSE,
                DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS,
                CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY,
                DEFAULT_PITCH,
                L"Segoe UI");

        create_label(
            *app,
            L"Arssyut",
            24,
            18,
            200,
            34,
            app->title_font);

        create_label(
            *app,
            L"Native screen recording · real MP4 output",
            24,
            50,
            420,
            24,
            app->small_font);

        create_label(
            *app,
            L"Source",
            24,
            91,
            100,
            22,
            app->small_font);

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
                24,
                115,
                485,
                280,
                window,
                reinterpret_cast<HMENU>(
                    IdSource),
                GetModuleHandleW(nullptr),
                nullptr);

        set_font(
            app->source_combo,
            app->normal_font);
        apply_dark_theme(
            app->source_combo);

        app->refresh_button =
            CreateWindowExW(
                0,
                L"BUTTON",
                L"Refresh",
                WS_CHILD |
                    WS_VISIBLE |
                    WS_TABSTOP,
                520,
                114,
                76,
                28,
                window,
                reinterpret_cast<HMENU>(
                    IdRefresh),
                GetModuleHandleW(nullptr),
                nullptr);

        set_font(
            app->refresh_button,
            app->small_font);
        apply_dark_theme(
            app->refresh_button);

        create_label(
            *app,
            L"Format",
            24,
            159,
            90,
            22,
            app->small_font);

        create_label(
            *app,
            L"1920 × 1080  ·  H.264 MP4",
            24,
            184,
            280,
            26,
            app->normal_font);

        create_label(
            *app,
            L"FPS",
            432,
            159,
            50,
            22,
            app->small_font);

        app->fps_combo =
            CreateWindowExW(
                0,
                WC_COMBOBOXW,
                L"",
                WS_CHILD |
                    WS_VISIBLE |
                    WS_TABSTOP |
                    CBS_DROPDOWNLIST,
                432,
                183,
                164,
                120,
                window,
                reinterpret_cast<HMENU>(
                    IdFps),
                GetModuleHandleW(nullptr),
                nullptr);

        set_font(
            app->fps_combo,
            app->normal_font);
        apply_dark_theme(
            app->fps_combo);

        SendMessageW(
            app->fps_combo,
            CB_ADDSTRING,
            0,
            reinterpret_cast<LPARAM>(
                L"60 FPS · 12 Mbps"));
        SendMessageW(
            app->fps_combo,
            CB_ADDSTRING,
            0,
            reinterpret_cast<LPARAM>(
                L"30 FPS · 8 Mbps"));
        SendMessageW(
            app->fps_combo,
            CB_SETCURSEL,
            0,
            0);

        create_label(
            *app,
            L"Output",
            24,
            224,
            80,
            22,
            app->small_font);

        app->output_text =
            create_label(
                *app,
                L"Videos\\Arssyut\\…",
                24,
                248,
                572,
                23,
                app->small_font);

        app->record_button =
            CreateWindowExW(
                0,
                L"BUTTON",
                L"Record",
                WS_CHILD |
                    WS_VISIBLE |
                    WS_TABSTOP |
                    BS_DEFPUSHBUTTON,
                24,
                289,
                156,
                42,
                window,
                reinterpret_cast<HMENU>(
                    IdRecord),
                GetModuleHandleW(nullptr),
                nullptr);

        set_font(
            app->record_button,
            app->normal_font);
        apply_dark_theme(
            app->record_button);

        app->timer_text =
            create_label(
                *app,
                L"",
                194,
                299,
                120,
                28,
                app->normal_font);

        app->open_button =
            CreateWindowExW(
                0,
                L"BUTTON",
                L"Open recording",
                WS_CHILD |
                    WS_VISIBLE |
                    WS_TABSTOP,
                320,
                289,
                134,
                42,
                window,
                reinterpret_cast<HMENU>(
                    IdOpen),
                GetModuleHandleW(nullptr),
                nullptr);

        set_font(
            app->open_button,
            app->small_font);
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
                    WS_TABSTOP,
                464,
                289,
                132,
                42,
                window,
                reinterpret_cast<HMENU>(
                    IdOpenDiagnostics),
                GetModuleHandleW(nullptr),
                nullptr);

        set_font(
            app->diagnostics_button,
            app->small_font);
        apply_dark_theme(
            app->diagnostics_button);
        EnableWindow(
            app->diagnostics_button,
            FALSE);

        app->status_text =
            create_label(
                *app,
                L"Ready",
                24,
                352,
                572,
                22,
                app->normal_font);

        app->metrics_text =
            create_label(
                *app,
                L"Internal diagnostics will appear here while recording.",
                24,
                382,
                572,
                48,
                app->small_font);

        create_label(
            *app,
            L"Diagnostics are saved beside every MP4 for analysis.",
            24,
            446,
            572,
            20,
            app->small_font);

        refresh_sources(*app);

        SetTimer(
            window,
            kUiTimer,
            100,
            nullptr);

        return 0;
    }

    case WM_COMMAND: {
        const int id =
            LOWORD(wparam);

        if (id == IdRefresh &&
            HIWORD(wparam) == BN_CLICKED) {
            refresh_sources(*app);
            return 0;
        }

        if (id == IdRecord &&
            HIWORD(wparam) == BN_CLICKED) {
            if (app->session) {
                const auto state =
                    app->session->snapshot().state;

                if (state ==
                        RecorderState::Preparing ||
                    state ==
                        RecorderState::Recording) {
                    stop_recording(*app);
                    return 0;
                }

                if (state ==
                        RecorderState::Stopping ||
                    state ==
                        RecorderState::Finalizing) {
                    return 0;
                }
            }

            start_recording(*app);
            return 0;
        }

        if (id == IdOpen &&
            HIWORD(wparam) == BN_CLICKED) {
            open_path(
                window,
                app->last_output);
            return 0;
        }

        if (id == IdOpenDiagnostics &&
            HIWORD(wparam) == BN_CLICKED) {
            open_path(
                window,
                app->last_diagnostics);
            return 0;
        }

        break;
    }

    case WM_TIMER:
        if (wparam == kUiTimer) {
            update_ui(*app);
            return 0;
        }
        break;

    case WM_CTLCOLORSTATIC: {
        HDC dc =
            reinterpret_cast<HDC>(wparam);
        SetTextColor(
            dc,
            RGB(231, 233, 238));
        SetBkColor(
            dc,
            RGB(24, 25, 28));
        return reinterpret_cast<LRESULT>(
            app->background_brush);
    }

    case WM_ERASEBKGND: {
        RECT rect{};
        GetClientRect(
            window,
            &rect);
        FillRect(
            reinterpret_cast<HDC>(wparam),
            &rect,
            app->background_brush);
        return 1;
    }

    case WM_CLOSE:
        if (app->session) {
            const auto state =
                app->session->snapshot().state;

            if (state ==
                    RecorderState::Recording ||
                state ==
                    RecorderState::Preparing) {
                const int answer =
                    MessageBoxW(
                        window,
                        L"A recording is active. Stop and close Arssyut?",
                        L"Arssyut",
                        MB_YESNO |
                            MB_ICONQUESTION);

                if (answer != IDYES)
                    return 0;

                app->session->request_stop();
                app->session->wait();
            }
        }

        DestroyWindow(window);
        return 0;

    case WM_DESTROY:
        KillTimer(
            window,
            kUiTimer);

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
        if (app->background_brush)
            DeleteObject(
                app->background_brush);
        if (app->field_brush)
            DeleteObject(
                app->field_brush);

        PostQuitMessage(0);
        return 0;
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
    int show)
{
    SetProcessDpiAwarenessContext(
        DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    INITCOMMONCONTROLSEX common{};
    common.dwSize = sizeof(common);
    common.dwICC =
        ICC_STANDARD_CLASSES;
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
        reinterpret_cast<HBRUSH>(
            COLOR_WINDOW + 1);
    cls.lpszClassName =
        kWindowClass;

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
        640,
        520,
        nullptr,
        nullptr,
        instance,
        &app);

    if (!window)
        return 2;

    ShowWindow(window, show);
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

    return static_cast<int>(
        message.wParam);
}

#endif
