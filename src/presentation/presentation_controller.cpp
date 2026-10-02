#include "presentation/presentation_controller.hpp"

#include <algorithm>
#include "presentation/shortcut_visualizer.hpp"

#include <cmath>
#include <limits>

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

[[nodiscard]] float smooth_segment(
    float from,
    float to,
    float progress) noexcept
{
    return from +
        (to - from) *
        minimum_jerk(progress);
}

[[nodiscard]] float ballistic_cursor_scale(
    float age_seconds) noexcept
{
    const float age =
        std::max(age_seconds, 0.0f);

    if (age <= 0.040f) {
        return smooth_segment(
            1.00f,
            1.17f,
            age / 0.040f);
    }

    if (age <= 0.105f) {
        return smooth_segment(
            1.17f,
            0.95f,
            (age - 0.040f) / 0.065f);
    }

    if (age <= 0.180f) {
        return smooth_segment(
            0.95f,
            1.025f,
            (age - 0.105f) / 0.075f);
    }

    if (age <= 0.230f) {
        return smooth_segment(
            1.025f,
            1.00f,
            (age - 0.180f) / 0.050f);
    }

    return 1.0f;
}

} // namespace

void PresentationController::reset() noexcept
{
    camera_.reset();
    clicks_ = {};
    click_generation_ = 0;

    cursor_impact_age_seconds_ = 0.0f;
    cursor_velocity_boost_ = 0.0f;
    previous_cursor_x_ = 0.5f;
    previous_cursor_y_ = 0.5f;
    cursor_impact_active_ = false;
    have_previous_cursor_ = false;

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

void PresentationController::push_click(
    ClickKind kind,
    float content_x,
    float content_y) noexcept
{
    if (kind == ClickKind::None)
        return;

    std::size_t target = clicks_.size();
    std::uint32_t oldest_generation =
        std::numeric_limits<std::uint32_t>::max();

    for (std::size_t i = 0; i < clicks_.size(); ++i) {
        if (!clicks_[i].active()) {
            target = i;
            break;
        }

        if (clicks_[i].generation < oldest_generation) {
            oldest_generation = clicks_[i].generation;
            target = i;
        }
    }

    if (target >= clicks_.size())
        target = 0;

    ++click_generation_;
    if (click_generation_ == 0)
        click_generation_ = 1;

    auto &pulse = clicks_[target];
    pulse.kind = kind;
    pulse.content_x =
        std::clamp(content_x, 0.0f, 1.0f);
    pulse.content_y =
        std::clamp(content_y, 0.0f, 1.0f);
    pulse.age_seconds = 0.0f;
    pulse.generation = click_generation_;
}

void PresentationController::on_click(
    ClickKind kind,
    float content_x,
    float content_y,
    arssyut::core::TimePoint time) noexcept
{
    if (settings_.click_visual) {
        push_click(
            kind,
            content_x,
            content_y);
    }

    if (kind != ClickKind::None) {
        cursor_impact_age_seconds_ = 0.0f;
        cursor_impact_active_ = true;
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

    constexpr float kLeftClickLifetime = 0.70f;
    constexpr float kRightClickLifetime = 0.72f;
    constexpr float kMiddleClickLifetime = 0.68f;

    for (auto &pulse : clicks_) {
        if (!pulse.active())
            continue;

        pulse.age_seconds +=
            std::max(dt, 0.0f);

        float lifetime = kLeftClickLifetime;
        if (pulse.kind == ClickKind::Right)
            lifetime = kRightClickLifetime;
        else if (pulse.kind == ClickKind::Middle)
            lifetime = kMiddleClickLifetime;

        if (pulse.age_seconds >= lifetime)
            pulse = {};
    }

    const float safe_dt =
        std::clamp(dt, 0.0f, 0.10f);

    float velocity_target = 0.0f;
    if (cursor_valid &&
        have_previous_cursor_ &&
        safe_dt > 0.0001f) {
        const float dx =
            cursor_x - previous_cursor_x_;
        const float dy =
            cursor_y - previous_cursor_y_;
        const float normalized_speed =
            std::sqrt(dx * dx + dy * dy) /
            safe_dt;

        velocity_target =
            std::clamp(
                (normalized_speed - 0.75f) * 0.0030f,
                0.0f,
                0.050f);
    }

    const float velocity_response =
        std::clamp(
            safe_dt * 18.0f,
            0.0f,
            1.0f);
    cursor_velocity_boost_ +=
        (velocity_target - cursor_velocity_boost_) *
        velocity_response;

    if (cursor_valid) {
        previous_cursor_x_ =
            std::clamp(cursor_x, 0.0f, 1.0f);
        previous_cursor_y_ =
            std::clamp(cursor_y, 0.0f, 1.0f);
        have_previous_cursor_ = true;
    } else {
        have_previous_cursor_ = false;
        cursor_velocity_boost_ *=
            std::max(
                0.0f,
                1.0f - safe_dt * 18.0f);
    }

    if (cursor_impact_active_) {
        cursor_impact_age_seconds_ +=
            std::max(dt, 0.0f);
        if (cursor_impact_age_seconds_ >= 0.230f) {
            cursor_impact_age_seconds_ = 0.230f;
            cursor_impact_active_ = false;
        }
    }

    const float impact_scale =
        ballistic_cursor_scale(
            cursor_impact_age_seconds_);
    const float velocity_weight =
        cursor_impact_active_ ? 0.35f : 1.0f;

    PresentationFrameState result;
    result.camera_center_x = camera.center.x;
    result.camera_center_y = camera.center.y;
    result.camera_zoom = camera.zoom;
    result.cursor.content_x =
        std::clamp(cursor_x, 0.0f, 1.0f);
    result.cursor.content_y =
        std::clamp(cursor_y, 0.0f, 1.0f);
    result.cursor.scale =
        std::clamp(
            impact_scale +
                cursor_velocity_boost_ *
                    velocity_weight,
            0.94f,
            1.18f);
    result.cursor.opacity =
        cursor_valid ? 1.0f : 0.0f;

    for (std::size_t i = 0;
         i < clicks_.size();
         ++i) {
        const auto &pulse = clicks_[i];
        if (!pulse.active())
            continue;

        auto &out = result.clicks[i];
        out.content_x = pulse.content_x;
        out.content_y = pulse.content_y;
        out.age_seconds = pulse.age_seconds;
        out.kind = pulse.kind;

        if (pulse.kind == ClickKind::Right)
            out.lifetime_seconds = kRightClickLifetime;
        else if (pulse.kind == ClickKind::Middle)
            out.lifetime_seconds = kMiddleClickLifetime;
        else
            out.lifetime_seconds = kLeftClickLifetime;
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
