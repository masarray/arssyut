#include "app/recorder_settings_window.hpp"
#include "app/lucide_icons.hpp"

#ifdef _WIN32

#include <commctrl.h>
#include <dwmapi.h>
#include <shlobj.h>
#include <uxtheme.h>

#include <array>
#include <string>

namespace arssyut::app {

namespace {

constexpr wchar_t kSettingsClass[] =
    L"ArssyutRecorderSettingsWindow";

constexpr COLORREF kBackground =
    RGB(19, 22, 26);
constexpr COLORREF kSidebar =
    RGB(24, 28, 33);
constexpr COLORREF kText =
    RGB(236, 239, 243);
constexpr COLORREF kMuted =
    RGB(148, 156, 166);
constexpr COLORREF kBorder =
    RGB(49, 55, 64);
constexpr COLORREF kAccent =
    RGB(232, 67, 67);

enum SettingsControlId : int {
    IdCategories = 3001,
    IdCountdown,
    IdBoundary,
    IdHideMain,
    IdFps,
    IdVisual,
    IdBrowseOutput,
    IdSystemAudio,
    IdMicrophone,
    IdMicrophoneDevice,
    IdCamera,
    IdCameraDevice,
    IdSmartZoom,
    IdClicks,
    IdKeys,
};

void set_font(HWND control, HFONT font)
{
    if (control && font) {
        SendMessageW(
            control,
            WM_SETFONT,
            reinterpret_cast<WPARAM>(font),
            TRUE);
    }
}

void apply_dark_theme(HWND control)
{
    if (control)
        SetWindowTheme(
            control,
            L"DarkMode_Explorer",
            nullptr);
}

HWND create_label(
    HWND parent,
    HINSTANCE instance,
    const wchar_t *text,
    int x,
    int y,
    int width,
    int height,
    HFONT font)
{
    HWND control =
        CreateWindowExW(
            0,
            L"STATIC",
            text,
            WS_CHILD | WS_VISIBLE,
            x,
            y,
            width,
            height,
            parent,
            nullptr,
            instance,
            nullptr);
    set_font(control, font);
    return control;
}

void set_checked(
    HWND control,
    bool checked)
{
    SendMessageW(
        control,
        BM_SETCHECK,
        checked
            ? BST_CHECKED
            : BST_UNCHECKED,
        0);
}

bool checked(HWND control)
{
    return SendMessageW(
               control,
               BM_GETCHECK,
               0,
               0) == BST_CHECKED;
}

void show_control(
    HWND control,
    bool visible)
{
    if (control) {
        ShowWindow(
            control,
            visible
                ? SW_SHOW
                : SW_HIDE);
    }
}

void draw_category_item(
    HDC dc,
    RECT rect,
    const wchar_t *text,
    HFONT font,
    bool selected)
{
    HBRUSH fill =
        CreateSolidBrush(
            selected
                ? RGB(92, 31, 35)
                : kSidebar);
    FillRect(
        dc,
        &rect,
        fill);
    DeleteObject(fill);

    if (selected) {
        RECT accent{
            rect.left,
            rect.top,
            rect.left + 3,
            rect.bottom};
        HBRUSH accent_brush =
            CreateSolidBrush(kAccent);
        FillRect(
            dc,
            &accent,
            accent_brush);
        DeleteObject(accent_brush);
    }

    RECT text_rect = rect;
    text_rect.left += 14;
    text_rect.right -= 8;

    SelectObject(
        dc,
        font);
    SetBkMode(
        dc,
        TRANSPARENT);
    SetTextColor(
        dc,
        selected
            ? RGB(251, 252, 253)
            : kText);

    DrawTextW(
        dc,
        text ? text : L"",
        -1,
        &text_rect,
        DT_LEFT |
            DT_VCENTER |
            DT_SINGLELINE |
            DT_END_ELLIPSIS |
            DT_NOPREFIX);
}

void draw_browse_button(
    HDC dc,
    RECT rect,
    HFONT font,
    bool enabled,
    bool pressed)
{
    COLORREF fill =
        enabled
            ? RGB(31, 35, 41)
            : RGB(27, 30, 35);

    if (pressed) {
        fill =
            RGB(24, 28, 33);
    }

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

    const COLORREF foreground =
        enabled
            ? RGB(239, 242, 246)
            : RGB(101, 107, 115);

    RECT icon_rect{
        rect.left + 8,
        rect.top + 7,
        rect.left + 24,
        rect.bottom - 7};

    draw_lucide_icon(
        dc,
        LucideIcon::Folder,
        icon_rect,
        foreground,
        2,
        false);

    RECT text_rect{
        rect.left + 30,
        rect.top,
        rect.right - 5,
        rect.bottom};

    SelectObject(
        dc,
        font);
    SetBkMode(
        dc,
        TRANSPARENT);
    SetTextColor(
        dc,
        foreground);

    DrawTextW(
        dc,
        L"Browse",
        -1,
        &text_rect,
        DT_LEFT |
            DT_VCENTER |
            DT_SINGLELINE);
}

} // namespace

RecorderSettingsWindow::~RecorderSettingsWindow()
{
    if (window_)
        DestroyWindow(window_);

    if (title_font_)
        DeleteObject(title_font_);
    if (normal_font_)
        DeleteObject(normal_font_);
    if (small_font_)
        DeleteObject(small_font_);
}

bool RecorderSettingsWindow::create(
    HINSTANCE instance,
    HWND owner,
    RecorderUiSettings *settings,
    const std::vector<DeviceChoice> *microphones,
    const std::vector<DeviceChoice> *cameras)
{
    if (!instance ||
        !owner ||
        !settings) {
        return false;
    }

    instance_ = instance;
    owner_ = owner;
    settings_ = settings;
    microphones_ = microphones;
    cameras_ = cameras;

    WNDCLASSEXW cls{};
    cls.cbSize = sizeof(cls);
    cls.lpfnWndProc =
        window_proc;
    cls.hInstance =
        instance_;
    cls.hCursor =
        LoadCursorW(
            nullptr,
            IDC_ARROW);
    cls.lpszClassName =
        kSettingsClass;
    cls.hbrBackground =
        nullptr;

    if (!RegisterClassExW(&cls) &&
        GetLastError() !=
            ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    title_font_ =
        CreateFontW(
            -18, 0, 0, 0, FW_SEMIBOLD,
            FALSE, FALSE, FALSE,
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,
            DEFAULT_PITCH,
            L"Segoe UI");

    normal_font_ =
        CreateFontW(
            -14, 0, 0, 0, FW_NORMAL,
            FALSE, FALSE, FALSE,
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,
            DEFAULT_PITCH,
            L"Segoe UI");

    small_font_ =
        CreateFontW(
            -12, 0, 0, 0, FW_NORMAL,
            FALSE, FALSE, FALSE,
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,
            DEFAULT_PITCH,
            L"Segoe UI");

    window_ =
        CreateWindowExW(
            WS_EX_TOOLWINDOW,
            kSettingsClass,
            L"Arssyut Settings",
            WS_OVERLAPPED |
                WS_CAPTION |
                WS_SYSMENU,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            660,
            430,
            owner_,
            nullptr,
            instance_,
            this);

    if (!window_)
        return false;

    BOOL dark = TRUE;
    DwmSetWindowAttribute(
        window_,
        DWMWA_USE_IMMERSIVE_DARK_MODE,
        &dark,
        sizeof(dark));

    create_controls();
    populate_devices();
    sync_from_model();
    show_page(0);
    return true;
}

void RecorderSettingsWindow::create_controls()
{
    categories_ =
        CreateWindowExW(
            0,
            L"LISTBOX",
            L"",
            WS_CHILD |
                WS_VISIBLE |
                WS_TABSTOP |
                LBS_NOTIFY |
                LBS_NOINTEGRALHEIGHT |
                LBS_OWNERDRAWFIXED |
                LBS_HASSTRINGS,
            0, 0, 150, 390,
            window_,
            reinterpret_cast<HMENU>(
                IdCategories),
            instance_,
            nullptr);
    set_font(
        categories_,
        normal_font_);
    apply_dark_theme(categories_);

    const std::array<const wchar_t *, 7>
        categories{
            L"General",
            L"Recording",
            L"Output",
            L"Sound",
            L"Camera",
            L"Mouse & Keystroke",
            L"Hotkeys",
        };

    for (const auto *category :
         categories) {
        SendMessageW(
            categories_,
            LB_ADDSTRING,
            0,
            reinterpret_cast<LPARAM>(
                category));
    }

    SendMessageW(
        categories_,
        LB_SETCURSEL,
        0,
        0);

    page_title_ =
        create_label(
            window_,
            instance_,
            L"General",
            176, 22, 420, 28,
            title_font_);

    countdown_ =
        CreateWindowExW(
            0,
            L"BUTTON",
            L"Show countdown before recording",
            WS_CHILD |
                WS_TABSTOP |
                BS_AUTOCHECKBOX,
            178, 74, 360, 28,
            window_,
            reinterpret_cast<HMENU>(
                IdCountdown),
            instance_,
            nullptr);
    set_font(countdown_, normal_font_);
    apply_dark_theme(countdown_);

    boundary_ =
        CreateWindowExW(
            0,
            L"BUTTON",
            L"Show recording boundary / viewport",
            WS_CHILD |
                WS_TABSTOP |
                BS_AUTOCHECKBOX,
            178, 108, 360, 28,
            window_,
            reinterpret_cast<HMENU>(
                IdBoundary),
            instance_,
            nullptr);
    set_font(boundary_, normal_font_);
    apply_dark_theme(boundary_);

    hide_main_ =
        CreateWindowExW(
            0,
            L"BUTTON",
            L"Hide main window while recording",
            WS_CHILD |
                WS_TABSTOP |
                BS_AUTOCHECKBOX,
            178, 142, 360, 28,
            window_,
            reinterpret_cast<HMENU>(
                IdHideMain),
            instance_,
            nullptr);
    set_font(hide_main_, normal_font_);
    apply_dark_theme(hide_main_);

    fps_label_ =
        create_label(
            window_,
            instance_,
            L"Frame rate",
            178, 76, 120, 22,
            small_font_);
    fps_ =
        CreateWindowExW(
            0,
            WC_COMBOBOXW,
            L"",
            WS_CHILD |
                WS_TABSTOP |
                CBS_DROPDOWNLIST,
            178, 103, 140, 120,
            window_,
            reinterpret_cast<HMENU>(
                IdFps),
            instance_,
            nullptr);
    set_font(fps_, normal_font_);
    apply_dark_theme(fps_);
    SendMessageW(
        fps_,
        CB_ADDSTRING,
        0,
        reinterpret_cast<LPARAM>(
            L"60 fps"));
    SendMessageW(
        fps_,
        CB_ADDSTRING,
        0,
        reinterpret_cast<LPARAM>(
            L"30 fps"));

    visual_label_ =
        create_label(
            window_,
            instance_,
            L"Visual style",
            178, 150, 120, 22,
            small_font_);
    visual_ =
        CreateWindowExW(
            0,
            WC_COMBOBOXW,
            L"",
            WS_CHILD |
                WS_TABSTOP |
                CBS_DROPDOWNLIST,
            178, 177, 210, 120,
            window_,
            reinterpret_cast<HMENU>(
                IdVisual),
            instance_,
            nullptr);
    set_font(visual_, normal_font_);
    apply_dark_theme(visual_);
    SendMessageW(
        visual_,
        CB_ADDSTRING,
        0,
        reinterpret_cast<LPARAM>(
            L"Pixel Accurate"));
    SendMessageW(
        visual_,
        CB_ADDSTRING,
        0,
        reinterpret_cast<LPARAM>(
            L"Clean Screen"));
    SendMessageW(
        visual_,
        CB_ADDSTRING,
        0,
        reinterpret_cast<LPARAM>(
            L"Vivid Presentation"));

    output_label_ =
        create_label(
            window_,
            instance_,
            L"Output folder",
            178, 76, 180, 22,
            small_font_);
    output_path_ =
        CreateWindowExW(
            WS_EX_CLIENTEDGE,
            L"EDIT",
            L"",
            WS_CHILD |
                WS_TABSTOP |
                ES_AUTOHSCROLL |
                ES_READONLY,
            178, 103, 330, 28,
            window_,
            nullptr,
            instance_,
            nullptr);
    set_font(output_path_, normal_font_);

    browse_output_ =
        CreateWindowExW(
            0,
            L"BUTTON",
            L"Browse...",
            WS_CHILD |
                WS_TABSTOP |
                BS_OWNERDRAW,
            518, 103, 94, 30,
            window_,
            reinterpret_cast<HMENU>(
                IdBrowseOutput),
            instance_,
            nullptr);
    set_font(browse_output_, normal_font_);
    apply_dark_theme(browse_output_);

    system_audio_ =
        CreateWindowExW(
            0,
            L"BUTTON",
            L"System audio",
            WS_CHILD |
                WS_TABSTOP |
                BS_AUTOCHECKBOX,
            178, 76, 180, 28,
            window_,
            reinterpret_cast<HMENU>(
                IdSystemAudio),
            instance_,
            nullptr);
    set_font(system_audio_, normal_font_);
    apply_dark_theme(system_audio_);

    microphone_ =
        CreateWindowExW(
            0,
            L"BUTTON",
            L"Microphone",
            WS_CHILD |
                WS_TABSTOP |
                BS_AUTOCHECKBOX,
            178, 118, 180, 28,
            window_,
            reinterpret_cast<HMENU>(
                IdMicrophone),
            instance_,
            nullptr);
    set_font(microphone_, normal_font_);
    apply_dark_theme(microphone_);

    microphone_device_ =
        CreateWindowExW(
            0,
            WC_COMBOBOXW,
            L"",
            WS_CHILD |
                WS_TABSTOP |
                CBS_DROPDOWNLIST |
                WS_VSCROLL,
            178, 156, 360, 180,
            window_,
            reinterpret_cast<HMENU>(
                IdMicrophoneDevice),
            instance_,
            nullptr);
    set_font(
        microphone_device_,
        normal_font_);
    apply_dark_theme(
        microphone_device_);

    camera_ =
        CreateWindowExW(
            0,
            L"BUTTON",
            L"Webcam overlay",
            WS_CHILD |
                WS_TABSTOP |
                BS_AUTOCHECKBOX,
            178, 76, 180, 28,
            window_,
            reinterpret_cast<HMENU>(
                IdCamera),
            instance_,
            nullptr);
    set_font(camera_, normal_font_);
    apply_dark_theme(camera_);

    camera_device_ =
        CreateWindowExW(
            0,
            WC_COMBOBOXW,
            L"",
            WS_CHILD |
                WS_TABSTOP |
                CBS_DROPDOWNLIST |
                WS_VSCROLL,
            178, 118, 360, 180,
            window_,
            reinterpret_cast<HMENU>(
                IdCameraDevice),
            instance_,
            nullptr);
    set_font(
        camera_device_,
        normal_font_);
    apply_dark_theme(
        camera_device_);

    smart_zoom_ =
        CreateWindowExW(
            0,
            L"BUTTON",
            L"Smart zoom",
            WS_CHILD |
                WS_TABSTOP |
                BS_AUTOCHECKBOX,
            178, 76, 220, 28,
            window_,
            reinterpret_cast<HMENU>(
                IdSmartZoom),
            instance_,
            nullptr);
    set_font(smart_zoom_, normal_font_);
    apply_dark_theme(smart_zoom_);

    clicks_ =
        CreateWindowExW(
            0,
            L"BUTTON",
            L"Mouse click visual",
            WS_CHILD |
                WS_TABSTOP |
                BS_AUTOCHECKBOX,
            178, 112, 220, 28,
            window_,
            reinterpret_cast<HMENU>(
                IdClicks),
            instance_,
            nullptr);
    set_font(clicks_, normal_font_);
    apply_dark_theme(clicks_);

    keys_ =
        CreateWindowExW(
            0,
            L"BUTTON",
            L"Shortcut / keystroke visual",
            WS_CHILD |
                WS_TABSTOP |
                BS_AUTOCHECKBOX,
            178, 148, 260, 28,
            window_,
            reinterpret_cast<HMENU>(
                IdKeys),
            instance_,
            nullptr);
    set_font(keys_, normal_font_);
    apply_dark_theme(keys_);

    hotkey_record_ =
        create_label(
            window_,
            instance_,
            L"Record / Stop     F9",
            178, 76, 260, 24,
            normal_font_);
    hotkey_pause_ =
        create_label(
            window_,
            instance_,
            L"Pause / Resume    F8  (engine milestone pending)",
            178, 112, 360, 24,
            normal_font_);

    show_page(0);
}

void RecorderSettingsWindow::populate_devices()
{
    SendMessageW(
        microphone_device_,
        CB_RESETCONTENT,
        0,
        0);

    if (!microphones_ ||
        microphones_->empty()) {
        SendMessageW(
            microphone_device_,
            CB_ADDSTRING,
            0,
            reinterpret_cast<LPARAM>(
                L"No microphone detected"));
    } else {
        for (const auto &device :
             *microphones_) {
            SendMessageW(
                microphone_device_,
                CB_ADDSTRING,
                0,
                reinterpret_cast<LPARAM>(
                    device.name.c_str()));
        }
    }

    SendMessageW(
        camera_device_,
        CB_RESETCONTENT,
        0,
        0);

    if (!cameras_ ||
        cameras_->empty()) {
        SendMessageW(
            camera_device_,
            CB_ADDSTRING,
            0,
            reinterpret_cast<LPARAM>(
                L"No camera detected"));
    } else {
        for (const auto &device :
             *cameras_) {
            SendMessageW(
                camera_device_,
                CB_ADDSTRING,
                0,
                reinterpret_cast<LPARAM>(
                    device.name.c_str()));
        }
    }
}

void RecorderSettingsWindow::sync_from_model()
{
    if (!settings_)
        return;

    set_checked(
        countdown_,
        settings_->countdown);
    set_checked(
        boundary_,
        settings_->show_boundary);
    set_checked(
        hide_main_,
        settings_->hide_main_while_recording);

    SendMessageW(
        fps_,
        CB_SETCURSEL,
        settings_->frame_rate == 30
            ? 1
            : 0,
        0);

    int visual = 0;
    if (settings_->visual_mode ==
        arssyut::visual::
            ArVisualProductMode::CleanScreen) {
        visual = 1;
    } else if (
        settings_->visual_mode ==
        arssyut::visual::
            ArVisualProductMode::
                VividPresentation) {
        visual = 2;
    }

    SendMessageW(
        visual_,
        CB_SETCURSEL,
        visual,
        0);

    SetWindowTextW(
        output_path_,
        settings_->
            output_folder.wstring().c_str());

    set_checked(
        system_audio_,
        settings_->system_audio);
    set_checked(
        microphone_,
        settings_->microphone);

    SendMessageW(
        microphone_device_,
        CB_SETCURSEL,
        static_cast<WPARAM>(
            settings_->
                microphone_device),
        0);

    set_checked(
        camera_,
        settings_->camera);
    SendMessageW(
        camera_device_,
        CB_SETCURSEL,
        static_cast<WPARAM>(
            settings_->camera_device),
        0);

    set_checked(
        smart_zoom_,
        settings_->smart_zoom);
    set_checked(
        clicks_,
        settings_->click_visual);
    set_checked(
        keys_,
        settings_->shortcut_keys);
}

void RecorderSettingsWindow::sync_to_model(
    int id)
{
    if (!settings_)
        return;

    switch (id) {
    case IdCountdown:
        settings_->countdown =
            checked(countdown_);
        break;
    case IdBoundary:
        settings_->show_boundary =
            checked(boundary_);
        break;
    case IdHideMain:
        settings_->
            hide_main_while_recording =
                checked(hide_main_);
        break;
    case IdFps:
        settings_->frame_rate =
            SendMessageW(
                fps_,
                CB_GETCURSEL,
                0,
                0) == 1
                ? 30U
                : 60U;
        break;
    case IdVisual: {
        const int selection =
            static_cast<int>(
                SendMessageW(
                    visual_,
                    CB_GETCURSEL,
                    0,
                    0));
        settings_->visual_mode =
            arssyut::visual::
                ArVisualProductMode::
                    PixelAccurate;
        if (selection == 1) {
            settings_->visual_mode =
                arssyut::visual::
                    ArVisualProductMode::
                        CleanScreen;
        } else if (selection == 2) {
            settings_->visual_mode =
                arssyut::visual::
                    ArVisualProductMode::
                        VividPresentation;
        }
        break;
    }
    case IdSystemAudio:
        settings_->system_audio =
            checked(system_audio_);
        break;
    case IdMicrophone:
        settings_->microphone =
            checked(microphone_);
        break;
    case IdMicrophoneDevice: {
        const LRESULT selection =
            SendMessageW(
                microphone_device_,
                CB_GETCURSEL,
                0,
                0);
        if (selection >= 0) {
            settings_->
                microphone_device =
                    static_cast<std::size_t>(
                        selection);
        }
        break;
    }
    case IdCamera:
        settings_->camera =
            checked(camera_);
        break;
    case IdCameraDevice: {
        const LRESULT selection =
            SendMessageW(
                camera_device_,
                CB_GETCURSEL,
                0,
                0);
        if (selection >= 0) {
            settings_->
                camera_device =
                    static_cast<std::size_t>(
                        selection);
        }
        break;
    }
    case IdSmartZoom:
        settings_->smart_zoom =
            checked(smart_zoom_);
        break;
    case IdClicks:
        settings_->click_visual =
            checked(clicks_);
        break;
    case IdKeys:
        settings_->shortcut_keys =
            checked(keys_);
        break;
    default:
        break;
    }

    notify_owner();
}

void RecorderSettingsWindow::show_page(
    int page)
{
    current_page_ = page;

    const wchar_t *title = L"General";
    if (page == 1)
        title = L"Recording";
    else if (page == 2)
        title = L"Output";
    else if (page == 3)
        title = L"Sound";
    else if (page == 4)
        title = L"Camera";
    else if (page == 5)
        title = L"Mouse & Keystroke";
    else if (page == 6)
        title = L"Hotkeys";

    SetWindowTextW(
        page_title_,
        title);

    show_control(countdown_, page == 0);
    show_control(boundary_, page == 0);
    show_control(hide_main_, page == 0);

    show_control(fps_label_, page == 1);
    show_control(fps_, page == 1);
    show_control(visual_label_, page == 1);
    show_control(visual_, page == 1);

    show_control(output_label_, page == 2);
    show_control(
        output_path_,
        page == 2);
    show_control(
        browse_output_,
        page == 2);

    show_control(
        system_audio_,
        page == 3);
    show_control(
        microphone_,
        page == 3);
    show_control(
        microphone_device_,
        page == 3);

    show_control(camera_, page == 4);
    show_control(
        camera_device_,
        page == 4);

    show_control(
        smart_zoom_,
        page == 5);
    show_control(
        clicks_,
        page == 5);
    show_control(
        keys_,
        page == 5);

    show_control(
        hotkey_record_,
        page == 6);
    show_control(
        hotkey_pause_,
        page == 6);

    InvalidateRect(
        window_,
        nullptr,
        TRUE);
}

void RecorderSettingsWindow::notify_owner()
{
    if (owner_) {
        PostMessageW(
            owner_,
            kUiSettingsChanged,
            0,
            0);
    }
}

void RecorderSettingsWindow::choose_output_folder()
{
    BROWSEINFOW browse{};
    browse.hwndOwner = window_;
    browse.lpszTitle =
        L"Choose Arssyut output folder";
    browse.ulFlags =
        BIF_RETURNONLYFSDIRS |
        BIF_NEWDIALOGSTYLE;

    PIDLIST_ABSOLUTE selected =
        SHBrowseForFolderW(
            &browse);
    if (!selected)
        return;

    wchar_t path[MAX_PATH]{};
    if (SHGetPathFromIDListW(
            selected,
            path)) {
        settings_->output_folder =
            path;
        SetWindowTextW(
            output_path_,
            path);
        notify_owner();
    }

    CoTaskMemFree(selected);
}

void RecorderSettingsWindow::refresh()
{
    if (!window_)
        return;

    populate_devices();
    sync_from_model();
    show_page(current_page_);
}

void RecorderSettingsWindow::show()
{
    if (!window_)
        return;

    refresh();

    RECT owner_rect{};
    GetWindowRect(
        owner_,
        &owner_rect);

    const int x =
        owner_rect.left + 32;
    const int y =
        owner_rect.top + 42;

    SetWindowPos(
        window_,
        HWND_TOP,
        x,
        y,
        660,
        430,
        SWP_SHOWWINDOW);

    SetForegroundWindow(window_);
}

void RecorderSettingsWindow::hide()
{
    if (window_)
        ShowWindow(
            window_,
            SW_HIDE);
}

bool RecorderSettingsWindow::visible() const noexcept
{
    return window_ &&
           IsWindowVisible(window_);
}

LRESULT CALLBACK
RecorderSettingsWindow::window_proc(
    HWND window,
    UINT message,
    WPARAM wparam,
    LPARAM lparam)
{
    auto *self =
        reinterpret_cast<
            RecorderSettingsWindow *>(
                GetWindowLongPtrW(
                    window,
                    GWLP_USERDATA));

    if (message == WM_NCCREATE) {
        const auto *create =
            reinterpret_cast<
                CREATESTRUCTW *>(lparam);
        self =
            static_cast<
                RecorderSettingsWindow *>(
                    create->lpCreateParams);
        self->window_ = window;

        SetWindowLongPtrW(
            window,
            GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(
                self));
    }

    if (!self) {
        return DefWindowProcW(
            window,
            message,
            wparam,
            lparam);
    }

    switch (message) {
    case WM_MEASUREITEM: {
        auto *measure =
            reinterpret_cast<
                MEASUREITEMSTRUCT *>(lparam);

        if (measure &&
            measure->CtlID ==
                IdCategories) {
            measure->itemHeight = 34;
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

        if (item->CtlID ==
            IdCategories) {
            wchar_t text[128]{};
            if (item->itemID !=
                static_cast<UINT>(-1)) {
                SendMessageW(
                    self->categories_,
                    LB_GETTEXT,
                    item->itemID,
                    reinterpret_cast<LPARAM>(
                        text));
            }

            draw_category_item(
                item->hDC,
                item->rcItem,
                text,
                self->normal_font_,
                (item->itemState &
                 ODS_SELECTED) != 0);
            return TRUE;
        }

        if (item->CtlID ==
            IdBrowseOutput) {
            draw_browse_button(
                item->hDC,
                item->rcItem,
                self->normal_font_,
                (item->itemState &
                 ODS_DISABLED) == 0,
                (item->itemState &
                 ODS_SELECTED) != 0);
            return TRUE;
        }
        break;
    }

    case WM_COMMAND: {
        const int id =
            LOWORD(wparam);
        const int code =
            HIWORD(wparam);

        if (id == IdCategories &&
            code == LBN_SELCHANGE) {
            const int page =
                static_cast<int>(
                    SendMessageW(
                        self->categories_,
                        LB_GETCURSEL,
                        0,
                        0));
            self->show_page(
                page >= 0 ? page : 0);
            return 0;
        }

        if (id == IdBrowseOutput &&
            code == BN_CLICKED) {
            self->choose_output_folder();
            return 0;
        }

        if (code == BN_CLICKED ||
            code == CBN_SELCHANGE) {
            self->sync_to_model(id);
            return 0;
        }
        break;
    }

    case WM_CTLCOLORSTATIC: {
        HDC dc =
            reinterpret_cast<HDC>(
                wparam);
        SetBkMode(
            dc,
            TRANSPARENT);
        SetTextColor(
            dc,
            kText);
        return reinterpret_cast<INT_PTR>(
            GetStockObject(
                NULL_BRUSH));
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc =
            BeginPaint(
                window,
                &paint);

        RECT client{};
        GetClientRect(
            window,
            &client);

        HBRUSH background =
            CreateSolidBrush(
                kBackground);
        FillRect(
            dc,
            &client,
            background);
        DeleteObject(background);

        RECT sidebar{
            0,
            0,
            150,
            client.bottom};
        HBRUSH sidebar_brush =
            CreateSolidBrush(
                kSidebar);
        FillRect(
            dc,
            &sidebar,
            sidebar_brush);
        DeleteObject(sidebar_brush);

        EndPaint(
            window,
            &paint);
        return 0;
    }

    case WM_CLOSE:
        self->hide();
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

} // namespace arssyut::app

#endif
