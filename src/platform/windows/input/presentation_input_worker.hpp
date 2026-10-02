#pragma once

#ifdef _WIN32

#include "core/concurrency/spsc_ring.hpp"
#include "core/result/status.hpp"
#include "core/time/monotonic_clock.hpp"
#include "presentation/presentation_state.hpp"
#include "presentation/shortcut.hpp"

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <thread>

namespace arssyut::windows {

struct PointerSnapshot {
    LONG screen_x = 0;
    LONG screen_y = 0;
    HCURSOR cursor_handle = nullptr;
    bool valid = false;
    bool cursor_visible = false;
    arssyut::core::TimePoint last_activity{};
};

struct MouseClickEvent {
    arssyut::presentation::ClickKind kind =
        arssyut::presentation::ClickKind::None;
    LONG screen_x = 0;
    LONG screen_y = 0;
    arssyut::core::TimePoint time{};
};

struct ShortcutEvent {
    arssyut::presentation::ShortcutChord chord{};
    arssyut::core::TimePoint time{};
};

class PresentationInputWorker final {
public:
    PresentationInputWorker() = default;
    ~PresentationInputWorker();

    PresentationInputWorker(const PresentationInputWorker &) = delete;
    PresentationInputWorker &operator=(const PresentationInputWorker &) = delete;

    [[nodiscard]] arssyut::core::Status start() noexcept;
    void stop() noexcept;

    [[nodiscard]] PointerSnapshot pointer() const noexcept;

    [[nodiscard]] bool try_pop_click(
        MouseClickEvent &event) noexcept
    {
        return click_events_.try_pop(event);
    }

    [[nodiscard]] bool try_pop_shortcut(
        ShortcutEvent &event) noexcept
    {
        return shortcut_events_.try_pop(event);
    }

    [[nodiscard]] std::uint64_t dropped_events() const noexcept
    {
        return dropped_events_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] bool system_shortcut_hook_active() const noexcept
    {
        return system_shortcut_hook_active_.load(
            std::memory_order_acquire);
    }

private:
    static LRESULT CALLBACK window_proc(
        HWND window,
        UINT message,
        WPARAM wparam,
        LPARAM lparam);

    static LRESULT CALLBACK keyboard_hook_proc(
        int code,
        WPARAM wparam,
        LPARAM lparam);

    void thread_main() noexcept;
    void handle_raw_input(HRAWINPUT input) noexcept;
    void handle_mouse(const RAWMOUSE &mouse) noexcept;
    void handle_keyboard(const RAWKEYBOARD &keyboard) noexcept;
    void handle_windows_key_hook(
        WPARAM message,
        const KBDLLHOOKSTRUCT &keyboard) noexcept;

    [[nodiscard]] std::uint8_t modifier_mask() const noexcept;
    void publish_shortcut(
        arssyut::presentation::ShortcutChord chord,
        arssyut::core::TimePoint time) noexcept;

    void publish_pointer_activity() noexcept;

    std::thread worker_;
    std::atomic<DWORD> thread_id_{0};
    HANDLE ready_event_ = nullptr;
    std::atomic<std::uint32_t> startup_status_{0};
    std::atomic<std::uint32_t> startup_detail_{0};

    std::atomic<LONG> pointer_x_{0};
    std::atomic<LONG> pointer_y_{0};
    std::atomic<bool> pointer_valid_{false};
    std::atomic<std::uintptr_t> cursor_handle_{0};
    std::atomic<bool> cursor_visible_{false};
    std::atomic<std::int64_t> pointer_activity_ticks_{0};

    std::array<std::atomic<bool>, 256> pressed_{};

    HHOOK keyboard_hook_ = nullptr;
    std::array<bool, 256> hook_pressed_{};
    bool hook_left_win_down_ = false;
    bool hook_right_win_down_ = false;
    bool hook_ctrl_down_ = false;
    bool hook_shift_down_ = false;
    bool hook_alt_down_ = false;

    static thread_local PresentationInputWorker *hook_owner_;

    arssyut::core::SpscRing<MouseClickEvent, 32> click_events_;
    arssyut::core::SpscRing<ShortcutEvent, 64> shortcut_events_;
    std::atomic<std::uint64_t> dropped_events_{0};
    std::atomic<bool> system_shortcut_hook_active_{false};
};

} // namespace arssyut::windows

#endif
