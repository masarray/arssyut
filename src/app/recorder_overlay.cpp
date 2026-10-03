#include "app/recorder_overlay.hpp"

#ifdef _WIN32

#include <dwmapi.h>

#include <algorithm>

namespace arssyut::app {

namespace {

constexpr wchar_t kToolbarClass[] =
    L"ArssyutRecordingToolbar";
constexpr wchar_t kBoundaryClass[] =
    L"ArssyutCaptureBoundary";

constexpr COLORREF kToolbarBackground =
    RGB(24, 27, 32);
constexpr COLORREF kToolbarBorder =
    RGB(55, 61, 70);
constexpr COLORREF kText =
    RGB(236, 239, 243);
constexpr COLORREF kMuted =
    RGB(150, 158, 168);
constexpr COLORREF kRecord =
    RGB(238, 67, 67);
constexpr COLORREF kActive =
    RGB(78, 207, 193);
constexpr COLORREF kBoundary =
    RGB(255, 104, 35);
constexpr COLORREF kTransparentKey =
    RGB(1, 1, 1);

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

void set_capture_exclusion(
    HWND window,
    bool excluded)
{
    if (!window)
        return;

    SetWindowDisplayAffinity(
        window,
        excluded
            ? WDA_EXCLUDEFROMCAPTURE
            : WDA_NONE);
}

void draw_centered_text(
    HDC dc,
    RECT rect,
    const wchar_t *text,
    HFONT font,
    COLORREF color)
{
    HGDIOBJ old_font =
        SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);

    DrawTextW(
        dc,
        text,
        -1,
        &rect,
        DT_CENTER |
            DT_VCENTER |
            DT_SINGLELINE);

    SelectObject(dc, old_font);
}

} // namespace

RecorderOverlay::~RecorderOverlay()
{
    hide_toolbar();
    hide_boundary();

    if (toolbar_)
        DestroyWindow(toolbar_);
    if (boundary_)
        DestroyWindow(boundary_);

    if (small_font_)
        DeleteObject(small_font_);
    if (timer_font_)
        DeleteObject(timer_font_);
}

bool RecorderOverlay::create(
    HINSTANCE instance,
    HWND owner,
    RecorderOverlayCommands commands)
{
    if (!instance || !owner)
        return false;

    instance_ = instance;
    owner_ = owner;
    commands_ = commands;

    WNDCLASSEXW toolbar_class{};
    toolbar_class.cbSize =
        sizeof(toolbar_class);
    toolbar_class.lpfnWndProc =
        toolbar_proc;
    toolbar_class.hInstance =
        instance_;
    toolbar_class.hCursor =
        LoadCursorW(
            nullptr,
            IDC_ARROW);
    toolbar_class.lpszClassName =
        kToolbarClass;
    toolbar_class.hbrBackground =
        nullptr;

    if (!RegisterClassExW(
            &toolbar_class) &&
        GetLastError() !=
            ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    WNDCLASSEXW boundary_class{};
    boundary_class.cbSize =
        sizeof(boundary_class);
    boundary_class.lpfnWndProc =
        boundary_proc;
    boundary_class.hInstance =
        instance_;
    boundary_class.hCursor =
        LoadCursorW(
            nullptr,
            IDC_ARROW);
    boundary_class.lpszClassName =
        kBoundaryClass;
    boundary_class.hbrBackground =
        nullptr;

    if (!RegisterClassExW(
            &boundary_class) &&
        GetLastError() !=
            ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    small_font_ =
        CreateFontW(
            -13, 0, 0, 0, FW_NORMAL,
            FALSE, FALSE, FALSE,
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,
            DEFAULT_PITCH,
            L"Segoe UI");

    timer_font_ =
        CreateFontW(
            -14, 0, 0, 0, FW_SEMIBOLD,
            FALSE, FALSE, FALSE,
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,
            DEFAULT_PITCH,
            L"Segoe UI");

    toolbar_ =
        CreateWindowExW(
            WS_EX_TOOLWINDOW |
                WS_EX_TOPMOST |
                WS_EX_NOACTIVATE,
            kToolbarClass,
            L"",
            WS_POPUP,
            0, 0, 330, 54,
            nullptr,
            nullptr,
            instance_,
            this);

    if (!toolbar_)
        return false;

    BOOL dark = TRUE;
    DwmSetWindowAttribute(
        toolbar_,
        DWMWA_USE_IMMERSIVE_DARK_MODE,
        &dark,
        sizeof(dark));

    status_ =
        CreateWindowExW(
            0,
            L"STATIC",
            L"Recording",
            WS_CHILD | WS_VISIBLE,
            14, 8, 74, 18,
            toolbar_,
            nullptr,
            instance_,
            nullptr);
    set_font(status_, small_font_);

    elapsed_ =
        CreateWindowExW(
            0,
            L"STATIC",
            L"00:00",
            WS_CHILD | WS_VISIBLE,
            14, 27, 74, 18,
            toolbar_,
            nullptr,
            instance_,
            nullptr);
    set_font(elapsed_, timer_font_);

    pause_ =
        CreateWindowExW(
            0,
            L"BUTTON",
            L"",
            WS_CHILD |
                WS_VISIBLE |
                BS_OWNERDRAW,
            98, 9, 38, 36,
            toolbar_,
            reinterpret_cast<HMENU>(
                static_cast<INT_PTR>(
                    commands_.pause)),
            instance_,
            nullptr);

    microphone_ =
        CreateWindowExW(
            0,
            L"BUTTON",
            L"",
            WS_CHILD |
                WS_VISIBLE |
                BS_OWNERDRAW,
            142, 9, 38, 36,
            toolbar_,
            reinterpret_cast<HMENU>(
                static_cast<INT_PTR>(
                    commands_.microphone)),
            instance_,
            nullptr);

    camera_ =
        CreateWindowExW(
            0,
            L"BUTTON",
            L"",
            WS_CHILD |
                WS_VISIBLE |
                BS_OWNERDRAW,
            186, 9, 38, 36,
            toolbar_,
            reinterpret_cast<HMENU>(
                static_cast<INT_PTR>(
                    commands_.camera)),
            instance_,
            nullptr);

    stop_ =
        CreateWindowExW(
            0,
            L"BUTTON",
            L"",
            WS_CHILD |
                WS_VISIBLE |
                BS_OWNERDRAW,
            238, 9, 78, 36,
            toolbar_,
            reinterpret_cast<HMENU>(
                static_cast<INT_PTR>(
                    commands_.stop)),
            instance_,
            nullptr);

    boundary_ =
        CreateWindowExW(
            WS_EX_TOOLWINDOW |
                WS_EX_TOPMOST |
                WS_EX_NOACTIVATE |
                WS_EX_TRANSPARENT |
                WS_EX_LAYERED,
            kBoundaryClass,
            L"",
            WS_POPUP,
            0, 0, 1, 1,
            nullptr,
            nullptr,
            instance_,
            this);

    if (!boundary_)
        return false;

    SetLayeredWindowAttributes(
        boundary_,
        kTransparentKey,
        0,
        LWA_COLORKEY);

    return true;
}

void RecorderOverlay::position_toolbar()
{
    if (!toolbar_)
        return;

    HMONITOR monitor =
        MonitorFromWindow(
            owner_,
            MONITOR_DEFAULTTONEAREST);

    MONITORINFO info{};
    info.cbSize = sizeof(info);

    int x = 32;
    int y = 24;
    constexpr int width = 330;
    constexpr int height = 54;

    if (GetMonitorInfoW(
            monitor,
            &info)) {
        x =
            info.rcWork.left +
            ((info.rcWork.right -
              info.rcWork.left) -
             width) / 2;
        y = info.rcWork.top + 18;
    }

    SetWindowPos(
        toolbar_,
        HWND_TOPMOST,
        x,
        y,
        width,
        height,
        SWP_NOACTIVATE |
            SWP_SHOWWINDOW);
}

void RecorderOverlay::show_toolbar(
    const std::wstring &status,
    const std::wstring &elapsed,
    bool pause_enabled,
    bool paused,
    bool microphone_on,
    bool camera_on)
{
    if (!toolbar_)
        return;

    set_capture_exclusion(
        toolbar_,
        true);

    update_toolbar(
        status,
        elapsed,
        pause_enabled,
        paused,
        microphone_on,
        camera_on);

    position_toolbar();
    toolbar_visible_ = true;
}

void RecorderOverlay::update_toolbar(
    const std::wstring &status,
    const std::wstring &elapsed,
    bool pause_enabled,
    bool paused,
    bool microphone_on,
    bool camera_on)
{
    if (!toolbar_)
        return;

    paused_ = paused;
    microphone_on_ = microphone_on;
    camera_on_ = camera_on;

    SetWindowTextW(
        status_,
        status.c_str());
    SetWindowTextW(
        elapsed_,
        elapsed.c_str());

    EnableWindow(
        pause_,
        pause_enabled ? TRUE : FALSE);

    // P6R exposes the intended recorder toolbar grammar now, but these
    // actions must not claim runtime support before their backends exist.
    EnableWindow(
        microphone_,
        FALSE);
    EnableWindow(
        camera_,
        FALSE);

    InvalidateRect(
        toolbar_,
        nullptr,
        FALSE);
}

void RecorderOverlay::hide_toolbar()
{
    if (!toolbar_)
        return;

    ShowWindow(
        toolbar_,
        SW_HIDE);
    toolbar_visible_ = false;
}

void RecorderOverlay::show_boundary(
    RECT screen_rect,
    bool exclude_from_capture)
{
    if (!boundary_)
        return;

    if (screen_rect.right <=
            screen_rect.left ||
        screen_rect.bottom <=
            screen_rect.top) {
        hide_boundary();
        return;
    }

    set_capture_exclusion(
        boundary_,
        exclude_from_capture);

    const int width =
        screen_rect.right -
        screen_rect.left;
    const int height =
        screen_rect.bottom -
        screen_rect.top;

    SetWindowPos(
        boundary_,
        HWND_TOPMOST,
        screen_rect.left,
        screen_rect.top,
        width,
        height,
        SWP_NOACTIVATE |
            SWP_SHOWWINDOW);

    InvalidateRect(
        boundary_,
        nullptr,
        TRUE);
}

void RecorderOverlay::hide_boundary()
{
    if (boundary_)
        ShowWindow(
            boundary_,
            SW_HIDE);
}

void RecorderOverlay::paint_toolbar(HDC dc)
{
    RECT client{};
    GetClientRect(
        toolbar_,
        &client);

    HBRUSH fill =
        CreateSolidBrush(
            kToolbarBackground);
    HPEN border =
        CreatePen(
            PS_SOLID,
            1,
            kToolbarBorder);

    HGDIOBJ old_brush =
        SelectObject(dc, fill);
    HGDIOBJ old_pen =
        SelectObject(dc, border);

    RoundRect(
        dc,
        0,
        0,
        client.right,
        client.bottom,
        12,
        12);

    SelectObject(dc, old_pen);
    SelectObject(dc, old_brush);

    DeleteObject(border);
    DeleteObject(fill);
}

void RecorderOverlay::draw_toolbar_button(
    const DRAWITEMSTRUCT &item,
    int id)
{
    HDC dc = item.hDC;
    RECT rect = item.rcItem;

    const bool enabled =
        (item.itemState &
         ODS_DISABLED) == 0;

    const bool active =
        (id == commands_.microphone &&
         microphone_on_) ||
        (id == commands_.camera &&
         camera_on_) ||
        (id == commands_.pause &&
         paused_);

    COLORREF fill_color =
        RGB(35, 39, 46);

    if (id == commands_.stop)
        fill_color = kRecord;
    else if (active)
        fill_color = RGB(32, 83, 78);
    else if (!enabled)
        fill_color = RGB(30, 33, 38);

    HBRUSH fill =
        CreateSolidBrush(
            fill_color);
    HPEN pen =
        CreatePen(
            PS_SOLID,
            1,
            active
                ? kActive
                : kToolbarBorder);

    HGDIOBJ old_brush =
        SelectObject(dc, fill);
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

    const COLORREF icon =
        enabled
            ? RGB(244, 246, 249)
            : RGB(100, 105, 112);

    HPEN icon_pen =
        CreatePen(
            PS_SOLID,
            2,
            icon);
    SelectObject(dc, icon_pen);

    const int cx =
        (rect.left + rect.right) / 2;
    const int cy =
        (rect.top + rect.bottom) / 2;

    if (id == commands_.pause) {
        MoveToEx(
            dc,
            cx - 4,
            cy - 7,
            nullptr);
        LineTo(
            dc,
            cx - 4,
            cy + 7);
        MoveToEx(
            dc,
            cx + 4,
            cy - 7,
            nullptr);
        LineTo(
            dc,
            cx + 4,
            cy + 7);
    } else if (
        id == commands_.microphone) {
        RoundRect(
            dc,
            cx - 4,
            cy - 8,
            cx + 4,
            cy + 4,
            6,
            6);
        Arc(
            dc,
            cx - 8,
            cy - 3,
            cx + 8,
            cy + 9,
            cx - 8,
            cy + 1,
            cx + 8,
            cy + 1);
        MoveToEx(
            dc,
            cx,
            cy + 7,
            nullptr);
        LineTo(
            dc,
            cx,
            cy + 11);
        MoveToEx(
            dc,
            cx - 5,
            cy + 11,
            nullptr);
        LineTo(
            dc,
            cx + 5,
            cy + 11);
    } else if (
        id == commands_.camera) {
        Rectangle(
            dc,
            cx - 8,
            cy - 6,
            cx + 5,
            cy + 7);
        MoveToEx(
            dc,
            cx + 5,
            cy - 3,
            nullptr);
        LineTo(
            dc,
            cx + 11,
            cy - 7);
        LineTo(
            dc,
            cx + 11,
            cy + 8);
        LineTo(
            dc,
            cx + 5,
            cy + 4);
    } else if (
        id == commands_.stop) {
        HBRUSH white =
            CreateSolidBrush(icon);
        RECT stop_rect{
            rect.left + 12,
            cy - 5,
            rect.left + 22,
            cy + 5};
        FillRect(
            dc,
            &stop_rect,
            white);
        DeleteObject(white);

        RECT text_rect{
            rect.left + 26,
            rect.top,
            rect.right - 4,
            rect.bottom};
        draw_centered_text(
            dc,
            text_rect,
            L"Stop",
            small_font_,
            icon);
    }

    SelectObject(dc, old_pen);
    SelectObject(dc, old_brush);

    DeleteObject(icon_pen);
    DeleteObject(pen);
    DeleteObject(fill);
}

void RecorderOverlay::paint_boundary(HDC dc)
{
    RECT client{};
    GetClientRect(
        boundary_,
        &client);

    HBRUSH clear =
        CreateSolidBrush(
            kTransparentKey);
    FillRect(
        dc,
        &client,
        clear);
    DeleteObject(clear);

    if (client.right <= 4 ||
        client.bottom <= 4) {
        return;
    }

    HPEN border =
        CreatePen(
            PS_DASH,
            2,
            kBoundary);

    HGDIOBJ old_pen =
        SelectObject(dc, border);
    HGDIOBJ old_brush =
        SelectObject(
            dc,
            GetStockObject(
                HOLLOW_BRUSH));

    Rectangle(
        dc,
        1,
        1,
        client.right - 1,
        client.bottom - 1);

    SelectObject(dc, old_brush);
    SelectObject(dc, old_pen);
    DeleteObject(border);

    HBRUSH handle =
        CreateSolidBrush(
            RGB(255, 255, 255));
    HPEN handle_pen =
        CreatePen(
            PS_SOLID,
            1,
            kBoundary);

    old_brush =
        SelectObject(dc, handle);
    old_pen =
        SelectObject(dc, handle_pen);

    constexpr int radius = 4;
    const POINT points[] = {
        {2, 2},
        {client.right / 2, 2},
        {client.right - 3, 2},
        {2, client.bottom / 2},
        {client.right - 3, client.bottom / 2},
        {2, client.bottom - 3},
        {client.right / 2, client.bottom - 3},
        {client.right - 3, client.bottom - 3},
    };

    for (const auto &point : points) {
        Ellipse(
            dc,
            point.x - radius,
            point.y - radius,
            point.x + radius,
            point.y + radius);
    }

    SelectObject(dc, old_pen);
    SelectObject(dc, old_brush);
    DeleteObject(handle_pen);
    DeleteObject(handle);
}

LRESULT CALLBACK RecorderOverlay::toolbar_proc(
    HWND window,
    UINT message,
    WPARAM wparam,
    LPARAM lparam)
{
    auto *self =
        reinterpret_cast<RecorderOverlay *>(
            GetWindowLongPtrW(
                window,
                GWLP_USERDATA));

    if (message == WM_NCCREATE) {
        const auto *create =
            reinterpret_cast<
                CREATESTRUCTW *>(lparam);
        self =
            static_cast<RecorderOverlay *>(
                create->lpCreateParams);
        SetWindowLongPtrW(
            window,
            GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(
                self));
    }

    if (!self)
        return DefWindowProcW(
            window,
            message,
            wparam,
            lparam);

    switch (message) {
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc =
            BeginPaint(
                window,
                &paint);
        self->paint_toolbar(dc);
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
        SetTextColor(
            dc,
            control == self->status_
                ? kRecord
                : kText);

        return reinterpret_cast<INT_PTR>(
            GetStockObject(
                NULL_BRUSH));
    }

    case WM_DRAWITEM: {
        const auto *item =
            reinterpret_cast<
                DRAWITEMSTRUCT *>(lparam);
        if (!item)
            break;

        self->draw_toolbar_button(
            *item,
            static_cast<int>(
                wparam));
        return TRUE;
    }

    case WM_COMMAND:
        if (HIWORD(wparam) ==
            BN_CLICKED) {
            PostMessageW(
                self->owner_,
                WM_COMMAND,
                MAKEWPARAM(
                    LOWORD(wparam),
                    BN_CLICKED),
                0);
            return 0;
        }
        break;

    default:
        break;
    }

    return DefWindowProcW(
        window,
        message,
        wparam,
        lparam);
}

LRESULT CALLBACK RecorderOverlay::boundary_proc(
    HWND window,
    UINT message,
    WPARAM wparam,
    LPARAM lparam)
{
    auto *self =
        reinterpret_cast<RecorderOverlay *>(
            GetWindowLongPtrW(
                window,
                GWLP_USERDATA));

    if (message == WM_NCCREATE) {
        const auto *create =
            reinterpret_cast<
                CREATESTRUCTW *>(lparam);
        self =
            static_cast<RecorderOverlay *>(
                create->lpCreateParams);
        SetWindowLongPtrW(
            window,
            GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(
                self));
    }

    if (!self)
        return DefWindowProcW(
            window,
            message,
            wparam,
            lparam);

    switch (message) {
    case WM_NCHITTEST:
        return HTTRANSPARENT;

    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc =
            BeginPaint(
                window,
                &paint);
        self->paint_boundary(dc);
        EndPaint(
            window,
            &paint);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

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
