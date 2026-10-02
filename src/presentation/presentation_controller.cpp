#include "presentation/presentation_controller.hpp"

#include <Windows.h>

#include <algorithm>
#include <iterator>
#include <cwchar>

namespace arssyut::presentation {

namespace {

constexpr std::int64_t kZoomClickHoldTicks =
    arssyut::core::MonotonicClock::ticks_per_second * 3;
constexpr std::int64_t kZoomMotionTailTicks =
    arssyut::core::MonotonicClock::ticks_per_second * 3 / 4;
constexpr std::int64_t kKeyboardHoldTicks =
    arssyut::core::MonotonicClock::ticks_per_second * 6 / 5;

constexpr std::uint8_t kModCtrl = 1u << 0;
constexpr std::uint8_t kModShift = 1u << 1;
constexpr std::uint8_t kModAlt = 1u << 2;
constexpr std::uint8_t kModWin = 1u << 3;

void append_token(
    wchar_t *buffer,
    std::size_t capacity,
    const wchar_t *token)
{
    if (!buffer || capacity == 0 || !token)
        return;

    const std::size_t used = std::wcslen(buffer);
    if (used >= capacity - 1)
        return;

    if (used != 0) {
        const wchar_t plus[] = L"  +  ";
        wcsncat_s(
            buffer,
            capacity,
            plus,
            _TRUNCATE);
    }

    wcsncat_s(
        buffer,
        capacity,
        token,
        _TRUNCATE);
}

void key_name(
    std::uint16_t key,
    wchar_t *buffer,
    std::size_t capacity)
{
    if (!buffer || capacity == 0)
        return;

    buffer[0] = L'\0';

    if (key >= L'A' && key <= L'Z') {
        buffer[0] = static_cast<wchar_t>(key);
        if (capacity > 1)
            buffer[1] = L'\0';
        return;
    }

    if (key >= L'0' && key <= L'9') {
        buffer[0] = static_cast<wchar_t>(key);
        if (capacity > 1)
            buffer[1] = L'\0';
        return;
    }

    if (key >= VK_F1 && key <= VK_F12) {
        swprintf_s(
            buffer,
            capacity,
            L"F%u",
            static_cast<unsigned>(key - VK_F1 + 1));
        return;
    }

    const wchar_t *name = nullptr;
    switch (key) {
    case VK_TAB:
        name = L"Tab";
        break;
    case VK_ESCAPE:
        name = L"Esc";
        break;
    case VK_RETURN:
        name = L"Enter";
        break;
    case VK_SPACE:
        name = L"Space";
        break;
    case VK_BACK:
        name = L"Backspace";
        break;
    case VK_DELETE:
        name = L"Delete";
        break;
    case VK_HOME:
        name = L"Home";
        break;
    case VK_END:
        name = L"End";
        break;
    case VK_PRIOR:
        name = L"PgUp";
        break;
    case VK_NEXT:
        name = L"PgDn";
        break;
    case VK_LEFT:
        name = L"Left";
        break;
    case VK_RIGHT:
        name = L"Right";
        break;
    case VK_UP:
        name = L"Up";
        break;
    case VK_DOWN:
        name = L"Down";
        break;
    case VK_SNAPSHOT:
        name = L"PrtSc";
        break;
    default:
        name = L"Key";
        break;
    }

    wcsncpy_s(buffer, capacity, name, _TRUNCATE);
}

} // namespace

void PresentationController::reset() noexcept
{
    camera_.reset();
    clicks_.clear();

    zoom_until_ = {};
    keyboard_until_ = {};
    keyboard_ = {};
    keyboard_generation_ = 0;
    emphasis_pending_ = false;
}

void PresentationController::set_settings(
    PresentationSettings settings) noexcept
{
    settings.zoom = std::clamp(settings.zoom, 1.10f, 4.00f);
    settings_ = settings;
}

void PresentationController::on_click(
    ClickKind kind,
    float content_x,
    float content_y,
    arssyut::core::TimePoint time) noexcept
{
    if (settings_.click_visual) {
        arzoom::ClickType type = arzoom::ClickType::None;
        switch (kind) {
        case ClickKind::Left:
            type = arzoom::ClickType::Left;
            break;
        case ClickKind::Right:
            type = arzoom::ClickType::Right;
            break;
        case ClickKind::Middle:
            type = arzoom::ClickType::Middle;
            break;
        case ClickKind::None:
        default:
            break;
        }

        clicks_.push(
            type,
            {content_x, content_y});
    }

    if (settings_.smart_zoom) {
        zoom_until_.ticks_100ns =
            std::max(
                zoom_until_.ticks_100ns,
                time.ticks_100ns + kZoomClickHoldTicks);
        emphasis_pending_ = true;
    }
}

void PresentationController::on_shortcut(
    ShortcutChord chord,
    arssyut::core::TimePoint time) noexcept
{
    if (!settings_.shortcut_keys)
        return;

    update_keyboard(chord);
    keyboard_until_.ticks_100ns =
        time.ticks_100ns + kKeyboardHoldTicks;
}

void PresentationController::update_keyboard(
    ShortcutChord chord) noexcept
{
    keyboard_.text.fill(L'\0');

    if (chord.modifiers & kModCtrl)
        append_token(
            keyboard_.text.data(),
            keyboard_.text.size(),
            L"Ctrl");
    if (chord.modifiers & kModShift)
        append_token(
            keyboard_.text.data(),
            keyboard_.text.size(),
            L"Shift");
    if (chord.modifiers & kModAlt)
        append_token(
            keyboard_.text.data(),
            keyboard_.text.size(),
            L"Alt");
    if (chord.modifiers & kModWin)
        append_token(
            keyboard_.text.data(),
            keyboard_.text.size(),
            L"Win");

    wchar_t key[24]{};
    key_name(
        chord.key,
        key,
        std::size(key));

    append_token(
        keyboard_.text.data(),
        keyboard_.text.size(),
        key);

    ++keyboard_generation_;
    if (keyboard_generation_ == 0)
        keyboard_generation_ = 1;

    keyboard_.generation = keyboard_generation_;
    keyboard_.opacity = 1.0f;
}

PresentationFrameState PresentationController::step(
    float dt,
    float cursor_x,
    float cursor_y,
    bool cursor_valid,
    arssyut::core::TimePoint now,
    arssyut::core::TimePoint last_pointer_activity) noexcept
{
    if (settings_.smart_zoom &&
        zoom_until_.ticks_100ns > now.ticks_100ns &&
        last_pointer_activity.ticks_100ns > 0 &&
        now.ticks_100ns - last_pointer_activity.ticks_100ns <=
            kZoomMotionTailTicks) {
        zoom_until_.ticks_100ns =
            std::max(
                zoom_until_.ticks_100ns,
                now.ticks_100ns + kZoomMotionTailTicks);
    }

    ArZoomCameraIntent intent;
    intent.dt = std::clamp(dt, 0.0f, 0.10f);
    intent.cursor = {
        std::clamp(cursor_x, 0.0f, 1.0f),
        std::clamp(cursor_y, 0.0f, 1.0f)
    };
    intent.cursor_valid = cursor_valid;
    intent.zoom_requested =
        settings_.smart_zoom &&
        now.ticks_100ns < zoom_until_.ticks_100ns;
    intent.configured_zoom = settings_.zoom;
    intent.emphasis_event = emphasis_pending_;
    emphasis_pending_ = false;

    const auto camera = camera_.step(intent);

    clicks_.advance(dt);

    PresentationFrameState result;
    result.camera_center_x = camera.center.x;
    result.camera_center_y = camera.center.y;
    result.camera_zoom = camera.zoom;

    for (std::size_t i = 0;
         i < arzoom::ClickVisualState::kSlotCount;
         ++i) {
        const auto &event = clicks_.slot(i);
        if (!event.active())
            continue;

        auto &out = result.clicks[i];
        out.content_x = event.content_position.x;
        out.content_y = event.content_position.y;
        out.age_seconds = event.age_seconds;
        out.lifetime_seconds =
            arzoom::click_lifetime_seconds(event.type);

        switch (event.type) {
        case arzoom::ClickType::Left:
            out.kind = ClickKind::Left;
            break;
        case arzoom::ClickType::Right:
            out.kind = ClickKind::Right;
            break;
        case arzoom::ClickType::Middle:
            out.kind = ClickKind::Middle;
            break;
        case arzoom::ClickType::None:
        default:
            out.kind = ClickKind::None;
            break;
        }
    }

    if (settings_.shortcut_keys &&
        keyboard_.generation != 0 &&
        now.ticks_100ns < keyboard_until_.ticks_100ns) {
        const auto remaining =
            keyboard_until_.ticks_100ns -
            now.ticks_100ns;
        const float fraction = std::clamp(
            static_cast<float>(remaining) /
                static_cast<float>(kKeyboardHoldTicks),
            0.0f,
            1.0f);

        result.keyboard = keyboard_;
        result.keyboard.opacity =
            std::min(
                1.0f,
                fraction * 3.0f);
    }

    return result;
}

} // namespace arssyut::presentation
