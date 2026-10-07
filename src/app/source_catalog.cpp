#include "app/source_catalog.hpp"

#ifdef _WIN32

#include <dwmapi.h>

#include <algorithm>
#include <string>
#include <vector>

namespace arssyut::app {

namespace {

struct MonitorEnumContext {
    std::vector<RecorderTarget> *targets = nullptr;
    int index = 0;
};

BOOL CALLBACK monitor_proc(
    HMONITOR monitor,
    HDC,
    LPRECT,
    LPARAM data)
{
    auto *context =
        reinterpret_cast<MonitorEnumContext *>(data);
    if (!context || !context->targets)
        return FALSE;

    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(
            monitor,
            &info)) {
        return TRUE;
    }

    const int width =
        info.rcMonitor.right -
        info.rcMonitor.left;
    const int height =
        info.rcMonitor.bottom -
        info.rcMonitor.top;

    ++context->index;

    RecorderTarget target;
    target.kind =
        arssyut::windows::CaptureTargetKind::Monitor;
    target.monitor = monitor;

    target.label =
        L"Display " +
        std::to_wstring(context->index) +
        L"  —  " +
        std::to_wstring(width) +
        L" × " +
        std::to_wstring(height);

    if (info.dwFlags &
        MONITORINFOF_PRIMARY) {
        target.label += L"  · Primary";
    }

    context->targets->push_back(
        std::move(target));

    return TRUE;
}

struct WindowEnumContext {
    std::vector<RecorderTarget> *targets = nullptr;
    HWND own_window = nullptr;
};

BOOL CALLBACK window_proc(
    HWND window,
    LPARAM data)
{
    auto *context =
        reinterpret_cast<WindowEnumContext *>(data);
    if (!context || !context->targets)
        return FALSE;

    if (window == context->own_window ||
        !IsWindowVisible(window) ||
        GetWindow(window, GW_OWNER) != nullptr) {
        return TRUE;
    }

    const LONG_PTR ex_style =
        GetWindowLongPtrW(
            window,
            GWL_EXSTYLE);
    if ((ex_style & WS_EX_TOOLWINDOW) != 0)
        return TRUE;

    BOOL cloaked = FALSE;
    if (SUCCEEDED(DwmGetWindowAttribute(
            window,
            DWMWA_CLOAKED,
            &cloaked,
            sizeof(cloaked))) &&
        cloaked) {
        return TRUE;
    }

    const int length =
        GetWindowTextLengthW(window);
    if (length <= 0)
        return TRUE;

    std::wstring title(
        static_cast<std::size_t>(length) + 1,
        L'\0');

    const int copied =
        GetWindowTextW(
            window,
            title.data(),
            static_cast<int>(title.size()));

    if (copied <= 0)
        return TRUE;

    title.resize(
        static_cast<std::size_t>(copied));

    if (title.size() > 96) {
        title.resize(93);
        title += L"...";
    }

    RECT rect{};
    if (!GetWindowRect(window, &rect))
        return TRUE;

    if (rect.right <= rect.left ||
        rect.bottom <= rect.top) {
        return TRUE;
    }

    RecorderTarget target;
    target.kind =
        arssyut::windows::CaptureTargetKind::Window;
    target.window = window;
    target.label =
        L"Window  —  " + title;

    context->targets->push_back(
        std::move(target));

    return TRUE;
}

} // namespace

std::vector<RecorderTarget>
enumerate_recorder_targets(HWND own_window)
{
    std::vector<RecorderTarget> targets;
    targets.reserve(32);

    MonitorEnumContext monitor_context;
    monitor_context.targets = &targets;

    EnumDisplayMonitors(
        nullptr,
        nullptr,
        monitor_proc,
        reinterpret_cast<LPARAM>(
            &monitor_context));

    WindowEnumContext window_context;
    window_context.targets = &targets;
    window_context.own_window = own_window;

    EnumWindows(
        window_proc,
        reinterpret_cast<LPARAM>(
            &window_context));

    return targets;
}

bool recorder_target_screen_rect(
    const RecorderTarget &target,
    RECT &rect) noexcept
{
    rect = {};

    if (target.kind ==
        arssyut::windows::
            CaptureTargetKind::Monitor) {
        MONITORINFO info{};
        info.cbSize = sizeof(info);

        if (!target.monitor ||
            !GetMonitorInfoW(
                target.monitor,
                &info)) {
            return false;
        }

        rect = info.rcMonitor;
        return rect.right > rect.left &&
               rect.bottom > rect.top;
    }

    if (!target.window ||
        !IsWindow(target.window)) {
        return false;
    }

    if (SUCCEEDED(
            DwmGetWindowAttribute(
                target.window,
                DWMWA_EXTENDED_FRAME_BOUNDS,
                &rect,
                sizeof(rect))) &&
        rect.right > rect.left &&
        rect.bottom > rect.top) {
        return true;
    }

    if (!GetWindowRect(
            target.window,
            &rect)) {
        return false;
    }

    return rect.right > rect.left &&
           rect.bottom > rect.top;
}

} // namespace arssyut::app

#endif
