#include "presentation/presentation_controller.hpp"

#include <algorithm>
#include "presentation/shortcut_visualizer.hpp"

#include <cmath>

namespace arssyut::presentation {

namespace {

constexpr std::int64_t kZoomClickHoldTicks =
    arssyut::core::MonotonicClock::ticks_per_second * 3;
constexpr std::int64_t kZoomMotionTailTicks =
    arssyut::core::MonotonicClock::ticks_per_second * 3 / 4;
constexpr std::int64_t kKeyboardHoldTicks =
    arssyut::core::MonotonicClock::ticks_per_second * 6 / 5;
constexpr std::int64_t kKeyboardFadeInTicks =
    arssyut::core::MonotonicClock::ticks_per_second * 7 / 100;
constexpr std::int64_t kKeyboardFadeOutTicks =
    arssyut::core::MonotonicClock::ticks_per_second * 22 / 100;
constexpr std::int64_t kShortcutCoalesceTicks =
    arssyut::core::MonotonicClock::ticks_per_second * 8 / 100;

[[nodiscard]] float minimum_jerk(float value) noexcept
{
    const float t = std::clamp(value, 0.0f, 1.0f);
    const float t2 = t * t;
    const float t3 = t2 * t;
    return t3 * (10.0f + t * (-15.0f + 6.0f * t));
}

} // namespace

void PresentationController::reset() noexcept
{
    camera_.reset();
    clicks_.clear();

    zoom_until_ = {};
    keyboard_started_ = {};
    keyboard_until_ = {};
    last_shortcut_time_ = {};
    keyboard_ = {};
    last_shortcut_ = {};
    keyboard_generation_ = 0;
    have_last_shortcut_ = false;
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
    if (!settings_.shortcut_keys ||
        !should_visualize_shortcut(chord)) {
        return;
    }

    const std::int64_t since_last =
        have_last_shortcut_
            ? arssyut::core::MonotonicClock::duration_ticks(
                  last_shortcut_time_,
                  time)
            : kShortcutCoalesceTicks + 1;

    if (have_last_shortcut_ &&
        same_shortcut(chord, last_shortcut_) &&
        since_last >= 0 &&
        since_last <= kShortcutCoalesceTicks) {
        last_shortcut_time_ = time;
        keyboard_until_.ticks_100ns =
            time.ticks_100ns + kKeyboardHoldTicks;
        return;
    }

    last_shortcut_ = chord;
    last_shortcut_time_ = time;
    have_last_shortcut_ = true;

    update_keyboard(chord);
    keyboard_started_ = time;
    keyboard_until_.ticks_100ns =
        time.ticks_100ns + kKeyboardHoldTicks;
}

void PresentationController::update_keyboard(
    ShortcutChord chord) noexcept
{
    ++keyboard_generation_;
    if (keyboard_generation_ == 0)
        keyboard_generation_ = 1;

    keyboard_ = build_keyboard_overlay(
        chord,
        keyboard_generation_);
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
        const std::int64_t elapsed = std::max<std::int64_t>(
            0,
            arssyut::core::MonotonicClock::duration_ticks(
                keyboard_started_,
                now));
        const std::int64_t remaining = std::max<std::int64_t>(
            0,
            keyboard_until_.ticks_100ns -
                now.ticks_100ns);

        const float fade_in = minimum_jerk(
            static_cast<float>(elapsed) /
            static_cast<float>(kKeyboardFadeInTicks));
        const float fade_out = minimum_jerk(
            static_cast<float>(remaining) /
            static_cast<float>(kKeyboardFadeOutTicks));

        result.keyboard = keyboard_;
        result.keyboard.opacity =
            std::min(fade_in, fade_out);
    }

    return result;
}

} // namespace arssyut::presentation
