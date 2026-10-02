#include "platform/windows/input/presentation_input_worker.hpp"
#include "platform/windows/input/shortcut_keymap.hpp"
#include "presentation/shortcut_visualizer.hpp"

#ifdef _WIN32

#include <algorithm>
#include <iterator>

namespace arssyut::windows {

thread_local PresentationInputWorker *
    PresentationInputWorker::hook_owner_ = nullptr;

namespace {

using arssyut::core::MonotonicClock;
using arssyut::core::Status;
using arssyut::core::StatusCode;

constexpr wchar_t kInputWindowClass[] =
    L"ArssyutPresentationInputWindow";

[[nodiscard]] std::uint32_t detail(HRESULT value) noexcept
{
    return static_cast<std::uint32_t>(value);
}

} // namespace

PresentationInputWorker::~PresentationInputWorker()
{
    stop();

    if (ready_event_) {
        CloseHandle(ready_event_);
        ready_event_ = nullptr;
    }
}

Status PresentationInputWorker::start() noexcept
{
    if (worker_.joinable())
        return Status::failure(StatusCode::InvalidStateTransition);

    if (!ready_event_) {
        ready_event_ = CreateEventW(
            nullptr,
            TRUE,
            FALSE,
            nullptr);
        if (!ready_event_) {
            return Status::failure(
                StatusCode::PlatformFailure,
                GetLastError());
        }
    }

    ResetEvent(ready_event_);
    startup_status_.store(
        static_cast<std::uint32_t>(StatusCode::Ok),
        std::memory_order_release);
    startup_detail_.store(0, std::memory_order_release);

    for (auto &key : pressed_)
        key.store(false, std::memory_order_relaxed);

    try {
        worker_ = std::thread(
            [this]() noexcept {
                thread_main();
            });
    } catch (...) {
        return Status::failure(StatusCode::InternalError);
    }

    const DWORD wait = WaitForSingleObject(
        ready_event_,
        5000);
    if (wait != WAIT_OBJECT_0) {
        stop();
        return Status::failure(
            StatusCode::PlatformFailure,
            wait);
    }

    const auto code = static_cast<StatusCode>(
        startup_status_.load(std::memory_order_acquire));
    if (code != StatusCode::Ok) {
        const auto failure = Status::failure(
            code,
            startup_detail_.load(std::memory_order_acquire));
        stop();
        return failure;
    }

    return Status::success();
}

void PresentationInputWorker::stop() noexcept
{
    const DWORD id =
        thread_id_.load(std::memory_order_acquire);

    if (id != 0)
        PostThreadMessageW(id, WM_QUIT, 0, 0);

    if (worker_.joinable())
        worker_.join();

    thread_id_.store(0, std::memory_order_release);
}

PointerSnapshot PresentationInputWorker::pointer() const noexcept
{
    PointerSnapshot result;
    result.screen_x =
        pointer_x_.load(std::memory_order_relaxed);
    result.screen_y =
        pointer_y_.load(std::memory_order_relaxed);
    result.valid =
        pointer_valid_.load(std::memory_order_acquire);
    result.last_activity.ticks_100ns =
        pointer_activity_ticks_.load(std::memory_order_relaxed);
    return result;
}

void PresentationInputWorker::publish_pointer_activity() noexcept
{
    POINT point{};
    if (!GetCursorPos(&point))
        return;

    pointer_x_.store(point.x, std::memory_order_relaxed);
    pointer_y_.store(point.y, std::memory_order_relaxed);
    pointer_activity_ticks_.store(
        MonotonicClock::now().ticks_100ns,
        std::memory_order_relaxed);
    pointer_valid_.store(true, std::memory_order_release);
}

void PresentationInputWorker::thread_main() noexcept
{
    thread_id_.store(
        GetCurrentThreadId(),
        std::memory_order_release);

    const HINSTANCE instance =
        GetModuleHandleW(nullptr);

    WNDCLASSEXW cls{};
    cls.cbSize = sizeof(cls);
    cls.lpfnWndProc = &PresentationInputWorker::window_proc;
    cls.hInstance = instance;
    cls.lpszClassName = kInputWindowClass;

    ATOM atom = RegisterClassExW(&cls);
    if (!atom) {
        const DWORD error = GetLastError();
        if (error != ERROR_CLASS_ALREADY_EXISTS) {
            startup_status_.store(
                static_cast<std::uint32_t>(
                    StatusCode::PlatformFailure),
                std::memory_order_release);
            startup_detail_.store(
                error,
                std::memory_order_release);
            SetEvent(ready_event_);
            return;
        }
    }

    HWND window = CreateWindowExW(
        0,
        kInputWindowClass,
        L"",
        0,
        0,
        0,
        0,
        0,
        HWND_MESSAGE,
        nullptr,
        instance,
        this);

    if (!window) {
        startup_status_.store(
            static_cast<std::uint32_t>(
                StatusCode::PlatformFailure),
            std::memory_order_release);
        startup_detail_.store(
            GetLastError(),
            std::memory_order_release);
        SetEvent(ready_event_);
        return;
    }

    RAWINPUTDEVICE devices[2]{};
    devices[0].usUsagePage = 0x01;
    devices[0].usUsage = 0x02;
    devices[0].dwFlags = RIDEV_INPUTSINK;
    devices[0].hwndTarget = window;

    devices[1].usUsagePage = 0x01;
    devices[1].usUsage = 0x06;
    devices[1].dwFlags = RIDEV_INPUTSINK;
    devices[1].hwndTarget = window;

    if (!RegisterRawInputDevices(
            devices,
            static_cast<UINT>(std::size(devices)),
            sizeof(RAWINPUTDEVICE))) {
        startup_status_.store(
            static_cast<std::uint32_t>(
                StatusCode::PlatformFailure),
            std::memory_order_release);
        startup_detail_.store(
            GetLastError(),
            std::memory_order_release);
        DestroyWindow(window);
        SetEvent(ready_event_);
        return;
    }

    hook_pressed_.fill(false);
    hook_left_win_down_ = false;
    hook_right_win_down_ = false;
    hook_ctrl_down_ = false;
    hook_shift_down_ = false;
    hook_alt_down_ = false;

    hook_owner_ = this;
    keyboard_hook_ = SetWindowsHookExW(
        WH_KEYBOARD_LL,
        &PresentationInputWorker::keyboard_hook_proc,
        GetModuleHandleW(nullptr),
        0);
    system_shortcut_hook_active_.store(
        keyboard_hook_ != nullptr,
        std::memory_order_release);

    publish_pointer_activity();
    SetEvent(ready_event_);

    MSG message{};
    while (GetMessageW(
               &message,
               nullptr,
               0,
               0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    if (keyboard_hook_) {
        UnhookWindowsHookEx(
            keyboard_hook_);
        keyboard_hook_ = nullptr;
    }
    system_shortcut_hook_active_.store(
        false,
        std::memory_order_release);
    hook_owner_ = nullptr;

    RAWINPUTDEVICE remove[2] = {
        devices[0],
        devices[1]
    };
    for (auto &device : remove) {
        device.dwFlags = RIDEV_REMOVE;
        device.hwndTarget = nullptr;
    }

    (void)RegisterRawInputDevices(
        remove,
        static_cast<UINT>(std::size(remove)),
        sizeof(RAWINPUTDEVICE));

    DestroyWindow(window);
}

LRESULT CALLBACK PresentationInputWorker::keyboard_hook_proc(
    int code,
    WPARAM wparam,
    LPARAM lparam)
{
    if (code == HC_ACTION &&
        hook_owner_ &&
        lparam != 0) {
        const auto *keyboard =
            reinterpret_cast<const KBDLLHOOKSTRUCT *>(lparam);

        hook_owner_->handle_windows_key_hook(
            wparam,
            *keyboard);
    }

    return CallNextHookEx(
        nullptr,
        code,
        wparam,
        lparam);
}

LRESULT CALLBACK PresentationInputWorker::window_proc(
    HWND window,
    UINT message,
    WPARAM wparam,
    LPARAM lparam)
{
    auto *self =
        reinterpret_cast<PresentationInputWorker *>(
            GetWindowLongPtrW(
                window,
                GWLP_USERDATA));

    if (message == WM_NCCREATE) {
        const auto *create =
            reinterpret_cast<const CREATESTRUCTW *>(lparam);

        self =
            static_cast<PresentationInputWorker *>(
                create->lpCreateParams);

        SetWindowLongPtrW(
            window,
            GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(self));
    }

    if (self && message == WM_INPUT) {
        self->handle_raw_input(
            reinterpret_cast<HRAWINPUT>(lparam));
        return 0;
    }

    return DefWindowProcW(
        window,
        message,
        wparam,
        lparam);
}

void PresentationInputWorker::handle_raw_input(
    HRAWINPUT input) noexcept
{
    RAWINPUT raw{};
    UINT size = sizeof(raw);

    if (GetRawInputData(
            input,
            RID_INPUT,
            &raw,
            &size,
            sizeof(RAWINPUTHEADER)) ==
        static_cast<UINT>(-1)) {
        return;
    }

    if (raw.header.dwType == RIM_TYPEMOUSE)
        handle_mouse(raw.data.mouse);
    else if (raw.header.dwType == RIM_TYPEKEYBOARD)
        handle_keyboard(raw.data.keyboard);
}

void PresentationInputWorker::handle_mouse(
    const RAWMOUSE &mouse) noexcept
{
    const USHORT flags = mouse.usButtonFlags;

    if (mouse.lLastX != 0 ||
        mouse.lLastY != 0 ||
        flags != 0) {
        publish_pointer_activity();
    }

    arssyut::presentation::ClickKind kind =
        arssyut::presentation::ClickKind::None;

    if (flags & RI_MOUSE_LEFT_BUTTON_DOWN)
        kind = arssyut::presentation::ClickKind::Left;
    else if (flags & RI_MOUSE_RIGHT_BUTTON_DOWN)
        kind = arssyut::presentation::ClickKind::Right;
    else if (flags & RI_MOUSE_MIDDLE_BUTTON_DOWN)
        kind = arssyut::presentation::ClickKind::Middle;

    if (kind == arssyut::presentation::ClickKind::None)
        return;

    POINT point{};
    if (!GetCursorPos(&point))
        return;

    MouseClickEvent event;
    event.kind = kind;
    event.screen_x = point.x;
    event.screen_y = point.y;
    event.time = MonotonicClock::now();

    if (!click_events_.try_push(event)) {
        dropped_events_.fetch_add(
            1,
            std::memory_order_relaxed);
    }
}

void PresentationInputWorker::publish_shortcut(
    arssyut::presentation::ShortcutChord chord,
    arssyut::core::TimePoint time) noexcept
{
    if (!arssyut::presentation::should_visualize_shortcut(chord))
        return;

    ShortcutEvent event;
    event.chord = chord;
    event.time = time;

    if (!shortcut_events_.try_push(event)) {
        dropped_events_.fetch_add(
            1,
            std::memory_order_relaxed);
    }
}

std::uint8_t PresentationInputWorker::modifier_mask() const noexcept
{
    std::uint8_t result = 0;

    const bool ctrl =
        pressed_[VK_LCONTROL].load(std::memory_order_relaxed) ||
        pressed_[VK_RCONTROL].load(std::memory_order_relaxed);
    const bool shift =
        pressed_[VK_LSHIFT].load(std::memory_order_relaxed) ||
        pressed_[VK_RSHIFT].load(std::memory_order_relaxed);
    const bool alt =
        pressed_[VK_LMENU].load(std::memory_order_relaxed) ||
        pressed_[VK_RMENU].load(std::memory_order_relaxed);
    const bool win =
        pressed_[VK_LWIN].load(std::memory_order_relaxed) ||
        pressed_[VK_RWIN].load(std::memory_order_relaxed);

    if (ctrl)
        result |= arssyut::presentation::ShortcutCtrl;
    if (shift)
        result |= arssyut::presentation::ShortcutShift;
    if (alt)
        result |= arssyut::presentation::ShortcutAlt;
    if (win)
        result |= arssyut::presentation::ShortcutWin;

    return result;
}

void PresentationInputWorker::handle_keyboard(
    const RAWKEYBOARD &keyboard) noexcept
{
    const std::uint16_t key =
        physical_virtual_key(
            static_cast<std::uint16_t>(keyboard.VKey),
            static_cast<std::uint16_t>(keyboard.MakeCode),
            static_cast<std::uint16_t>(keyboard.Flags));

    if (key >= pressed_.size())
        return;

    const bool released =
        (keyboard.Flags & RI_KEY_BREAK) != 0;

    if (released) {
        pressed_[key].store(
            false,
            std::memory_order_relaxed);
        return;
    }

    const bool already_pressed =
        pressed_[key].exchange(
            true,
            std::memory_order_relaxed);

    if (already_pressed)
        return;

    const std::uint8_t modifiers =
        modifier_mask();

    arssyut::presentation::ShortcutChord chord;
    chord.key = shortcut_key_from_virtual_key(key);
    chord.modifiers = modifiers;

    publish_shortcut(
        chord,
        MonotonicClock::now());
}

void PresentationInputWorker::handle_windows_key_hook(
    WPARAM message,
    const KBDLLHOOKSTRUCT &keyboard) noexcept
{
    const bool key_down =
        message == WM_KEYDOWN ||
        message == WM_SYSKEYDOWN;
    const bool key_up =
        message == WM_KEYUP ||
        message == WM_SYSKEYUP;

    if (!key_down && !key_up)
        return;

    const std::uint16_t vkey =
        static_cast<std::uint16_t>(keyboard.vkCode);

    if (vkey >= hook_pressed_.size())
        return;

    if (vkey == VK_LWIN ||
        vkey == VK_RWIN) {
        const bool down = key_down;

        if (vkey == VK_LWIN)
            hook_left_win_down_ = down;
        else
            hook_right_win_down_ = down;

        hook_pressed_[vkey] = down;
        return;
    }

    if (vkey == VK_LCONTROL ||
        vkey == VK_RCONTROL ||
        vkey == VK_CONTROL) {
        hook_ctrl_down_ = key_down;
        hook_pressed_[vkey] = key_down;
        return;
    }

    if (vkey == VK_LSHIFT ||
        vkey == VK_RSHIFT ||
        vkey == VK_SHIFT) {
        hook_shift_down_ = key_down;
        hook_pressed_[vkey] = key_down;
        return;
    }

    if (vkey == VK_LMENU ||
        vkey == VK_RMENU ||
        vkey == VK_MENU) {
        hook_alt_down_ = key_down;
        hook_pressed_[vkey] = key_down;
        return;
    }

    if (key_up) {
        hook_pressed_[vkey] = false;
        return;
    }

    if (hook_pressed_[vkey])
        return;
    hook_pressed_[vkey] = true;

    const bool win_down =
        hook_left_win_down_ ||
        hook_right_win_down_;
    if (!win_down)
        return;

    const auto chord =
        windows_system_shortcut_chord(
            vkey,
            hook_ctrl_down_,
            hook_shift_down_,
            hook_alt_down_);

    publish_shortcut(
        chord,
        MonotonicClock::now());
}

} // namespace arssyut::windows

#endif
