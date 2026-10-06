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


} // namespace

void PresentationController::reset() noexcept
{
    camera_.reset();
    clicks_ = {};
    click_generation_ = 0;

    zoom_until_ = {};
    keyboard_started_ = {};
    keyboard_until_ = {};
    last_shortcut_time_ = {};
    keyboard_ = {};
    last_shortcut_ = {};
    keyboard_generation_ = 0;
    have_last_shortcut_ = false;
    emphasis_pending_ = false;
    manual_zoom_latched_ = false;
    hold_zoom_active_ = false;
    overview_requested_ = false;
    overview_.reset();
    spotlight_focus_valid_ = false;
    spotlight_focus_x_ = 0.5f;
    spotlight_focus_y_ = 0.5f;
    spotlight_click_anchor_valid_ = false;
    spotlight_click_anchor_x_ = 0.5f;
    spotlight_click_anchor_y_ = 0.5f;
    spotlight_zoom_was_requested_ = false;
    spotlight_cinematic_.reset();
    spotlight_zoom_resize_.reset();
    spotlight_runtime_was_requested_ = false;
    spotlight_close_armed_ = false;
    last_camera_center_x_ = 0.5f;
    last_camera_center_y_ = 0.5f;
    last_camera_zoom_ = 1.0f;
    runtime_zoom_ =
        std::clamp(
            settings_.zoom,
            1.10f,
            4.00f);
}

void PresentationController::set_settings(
    PresentationSettings settings) noexcept
{
    settings.zoom = std::clamp(settings.zoom, 1.10f, 4.00f);

    // Normalize only safety ranges here. Visual tuning stays owned by the
    // pinned upstream contract and later recorded-output acceptance.
    settings.spotlight.area_scale_percent =
        std::clamp(
            std::isfinite(settings.spotlight.area_scale_percent)
                ? settings.spotlight.area_scale_percent
                : 100.0f,
            50.0f,
            200.0f);
    settings.spotlight.feather_short_edge_fraction =
        std::clamp(
            std::isfinite(settings.spotlight.feather_short_edge_fraction)
                ? settings.spotlight.feather_short_edge_fraction
                : 0.12f,
            0.0f,
            0.50f);
    settings.spotlight.dim_strength =
        std::clamp(
            std::isfinite(settings.spotlight.dim_strength)
                ? settings.spotlight.dim_strength
                : 0.38f,
            0.0f,
            0.75f);

    const bool reset_spotlight_focus =
        !settings.spotlight.enabled ||
        settings.spotlight.mode != settings_.spotlight.mode;

    settings_ = settings;
    runtime_zoom_ = settings.zoom;

    if (!settings_.presenter_controls) {
        manual_zoom_latched_ = false;
        hold_zoom_active_ = false;
        overview_requested_ = false;
        overview_.reset();
    }

    if (reset_spotlight_focus) {
        spotlight_focus_valid_ = false;
        spotlight_click_anchor_valid_ = false;
        spotlight_zoom_was_requested_ = false;
        spotlight_cinematic_.reset();
        spotlight_zoom_resize_.reset();
        spotlight_runtime_was_requested_ = false;
        spotlight_close_armed_ = false;
    }
}

void PresentationController::push_click(
    ClickKind kind,
    float content_x,
    float content_y) noexcept
{
    if (kind == ClickKind::None)
        return;

    const float safe_x =
        std::clamp(content_x, 0.0f, 1.0f);
    const float safe_y =
        std::clamp(content_y, 0.0f, 1.0f);

    // A fast repeated click at essentially the same target should recharge
    // one luminous pulse instead of drawing concentric geometry. This keeps
    // double-click feedback energetic but visually clean.
    constexpr float kRetriggerAgeSeconds = 0.12f;
    constexpr float kRetriggerRadiusSquared = 0.000225f;

    for (auto &pulse : clicks_) {
        if (!pulse.active() ||
            pulse.kind != kind ||
            pulse.age_seconds > kRetriggerAgeSeconds) {
            continue;
        }

        const float dx =
            pulse.content_x - safe_x;
        const float dy =
            pulse.content_y - safe_y;

        if (dx * dx + dy * dy >
            kRetriggerRadiusSquared) {
            continue;
        }

        ++click_generation_;
        if (click_generation_ == 0)
            click_generation_ = 1;

        pulse.content_x = safe_x;
        pulse.content_y = safe_y;
        pulse.age_seconds = 0.0f;
        pulse.generation = click_generation_;
        return;
    }

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
    pulse.content_x = safe_x;
    pulse.content_y = safe_y;
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

    // Click Spotlight consumes the same validated content-space event as click
    // feedback. One anchor replaces one anchor: bounded O(1), no history.
    if (kind != ClickKind::None &&
        settings_.spotlight.enabled &&
        settings_.spotlight.mode == SpotlightMode::Click) {
        spotlight_click_anchor_x_ =
            std::clamp(content_x, 0.0f, 1.0f);
        spotlight_click_anchor_y_ =
            std::clamp(content_y, 0.0f, 1.0f);
        spotlight_click_anchor_valid_ = true;
        spotlight_focus_x_ = spotlight_click_anchor_x_;
        spotlight_focus_y_ = spotlight_click_anchor_y_;
        spotlight_focus_valid_ = true;
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

void PresentationController::toggle_manual_zoom() noexcept
{
    if (!settings_.presenter_controls)
        return;

    manual_zoom_latched_ =
        !manual_zoom_latched_;

    // Match ArZoom Toggle Zoom intent: activation uses the same accepted
    // camera and current pointer focus. Turning it OFF owns the close action,
    // so any still-running Smart Zoom tail is cancelled for this activation.
    if (manual_zoom_latched_) {
        emphasis_pending_ = true;
    } else {
        zoom_until_ = {};
        emphasis_pending_ = false;
    }
}

void PresentationController::adjust_manual_zoom(
    float delta) noexcept
{
    if (!settings_.presenter_controls)
        return;

    // ArZoom Presenter Controls use 0.25x steps and clamp the configured
    // framing range to 1.10x..4.00x. The caller supplies the step so this
    // controller stays reusable and deterministic.
    runtime_zoom_ =
        std::clamp(
            runtime_zoom_ + delta,
            1.10f,
            4.00f);
}

void PresentationController::reset_full_frame() noexcept
{
    if (!settings_.presenter_controls)
        return;

    // ArZoom Reset / Full Frame clears every active presenter zoom intent but
    // deliberately preserves the configured zoom amount for the next
    // activation. If Overview Peek is in flight, step() uses its upstream
    // cancel-to-overview path instead of snapping the render transform.
    manual_zoom_latched_ = false;
    hold_zoom_active_ = false;
    overview_requested_ = false;
    zoom_until_ = {};
    emphasis_pending_ = false;

    // Reset / Full Frame also clears transient Spotlight focus. This does not
    // add render/camera authority; it only invalidates the bounded read-only
    // focus snapshot so the next activation must prove a fresh target.
    spotlight_focus_valid_ = false;
    spotlight_click_anchor_valid_ = false;
    spotlight_zoom_was_requested_ = false;
    spotlight_cinematic_.set_target(
        false,
        arzoom::CinematicFocusSpeed::Balanced);
    spotlight_zoom_resize_.reset();
    spotlight_runtime_was_requested_ = false;
    spotlight_close_armed_ = false;
}

void PresentationController::set_hold_zoom(
    bool active) noexcept
{
    if (!settings_.presenter_controls) {
        hold_zoom_active_ = false;
        return;
    }

    if (active && !hold_zoom_active_)
        emphasis_pending_ = true;

    hold_zoom_active_ = active;
}

void PresentationController::set_overview_peek(
    bool active) noexcept
{
    if (!settings_.presenter_controls) {
        overview_requested_ = false;
        return;
    }

    overview_requested_ = active;
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

    const bool smart_zoom_requested =
        settings_.smart_zoom &&
        now.ticks_100ns <
            zoom_until_.ticks_100ns;
    const bool wants_zoom =
        manual_zoom_latched_ ||
        hold_zoom_active_ ||
        smart_zoom_requested;

    const bool spotlight_zoom_rising =
        wants_zoom &&
        !spotlight_zoom_was_requested_;

    ArZoomCameraIntent intent;
    intent.dt = std::clamp(dt, 0.0f, 0.10f);
    intent.cursor = {
        std::clamp(cursor_x, 0.0f, 1.0f),
        std::clamp(cursor_y, 0.0f, 1.0f)
    };
    intent.cursor_valid = cursor_valid;
    intent.zoom_requested = wants_zoom;
    intent.configured_zoom = runtime_zoom_;
    intent.emphasis_event = emphasis_pending_;
    emphasis_pending_ = false;

    if (settings_.spotlight.enabled) {
        switch (settings_.spotlight.mode) {
        case SpotlightMode::Cursor:
            // Cursor mode follows only the canonical mapped pointer. Missing
            // mapping holds the last proven coordinate; it never guesses.
            if (intent.cursor_valid) {
                spotlight_focus_x_ = intent.cursor.x;
                spotlight_focus_y_ = intent.cursor.y;
                spotlight_focus_valid_ = true;
            }
            break;

        case SpotlightMode::Click:
            if (spotlight_click_anchor_valid_) {
                spotlight_focus_x_ = spotlight_click_anchor_x_;
                spotlight_focus_y_ = spotlight_click_anchor_y_;
                spotlight_focus_valid_ = true;
            }
            break;

        case SpotlightMode::SmartFocus:
        default:
            // Until P6UI.6D-F proves a read-only semantic-focus seam, capture
            // the same canonical pointer used to activate SmartCamera once per
            // zoom session. Local pointer motion cannot become a second planner.
            if (intent.cursor_valid &&
                (spotlight_zoom_rising ||
                 !spotlight_focus_valid_)) {
                spotlight_focus_x_ = intent.cursor.x;
                spotlight_focus_y_ = intent.cursor.y;
                spotlight_focus_valid_ = true;
            }
            break;
        }
    }

    const arzoom::Vec2 visible_center{
        last_camera_center_x_,
        last_camera_center_y_
    };
    const float visible_zoom =
        last_camera_zoom_;

    if (overview_requested_ &&
        !overview_.active() &&
        wants_zoom &&
        visible_zoom > 1.0005f) {
        (void)overview_.begin(
            visible_center,
            visible_zoom);
    }

    float camera_center_x = 0.5f;
    float camera_center_y = 0.5f;
    float camera_zoom = 1.0f;

    if (overview_.active()) {
        const auto phase =
            overview_.phase();

        if (!wants_zoom &&
            phase !=
                arzoom::OverviewPhase::CancelToOverview) {
            overview_.cancel_to_overview(
                visible_center,
                visible_zoom);
        } else if (!overview_requested_ &&
                   phase !=
                       arzoom::OverviewPhase::ToShot) {
            overview_.release(
                visible_center,
                visible_zoom);
        }

        const auto profile =
            arzoom::camera_profile(
                arzoom::CameraMotionStyle::Cinematic);
        const float out_seconds =
            std::clamp(
                profile.zoom_out_seconds * 0.62f,
                0.24f,
                0.42f);
        const float back_seconds =
            std::clamp(
                profile.zoom_in_seconds * 0.72f,
                0.24f,
                0.40f);

        const auto overview_output =
            overview_.step(
                intent.dt,
                out_seconds,
                back_seconds);

        camera_center_x =
            overview_output.center.x;
        camera_center_y =
            overview_output.center.y;
        camera_zoom =
            overview_output.zoom;

        if (overview_output.cancelled) {
            camera_.reset();
            camera_center_x = 0.5f;
            camera_center_y = 0.5f;
            camera_zoom = 1.0f;
        }
    } else {
        const auto camera =
            camera_.step(intent);
        camera_center_x =
            camera.center.x;
        camera_center_y =
            camera.center.y;
        camera_zoom =
            camera.zoom;
    }

    last_camera_center_x_ =
        camera_center_x;
    last_camera_center_y_ =
        camera_center_y;
    last_camera_zoom_ =
        camera_zoom;

    // P6UI.6D-C: Spotlight choreography observes the one authoritative camera.
    // Zoom starts first. The aperture is not allowed to close until a rendered
    // camera frame has actually departed full-frame; this avoids inventing a
    // second timing authority while preserving the requested framing-first cue.
    const bool spotlight_requested =
        settings_.spotlight.enabled &&
        settings_.spotlight.link_to_zoom &&
        wants_zoom &&
        spotlight_focus_valid_ &&
        !overview_.active();

    arzoom::CinematicFocusSpeed spotlight_speed =
        arzoom::CinematicFocusSpeed::Balanced;
    switch (settings_.spotlight.cinematic_speed) {
    case SpotlightCinematicSpeed::Smooth:
        spotlight_speed = arzoom::CinematicFocusSpeed::Smooth;
        break;
    case SpotlightCinematicSpeed::Snappy:
        spotlight_speed = arzoom::CinematicFocusSpeed::Snappy;
        break;
    case SpotlightCinematicSpeed::Balanced:
    default:
        break;
    }

    if (!spotlight_requested) {
        spotlight_close_armed_ = false;
        spotlight_cinematic_.set_target(false, spotlight_speed);
    } else {
        if (!spotlight_runtime_was_requested_)
            spotlight_close_armed_ = false;

        if (spotlight_close_armed_) {
            spotlight_cinematic_.set_target(true, spotlight_speed);
        } else if (camera_zoom > 1.0005f) {
            // Arm only after this already-renderable frame proves that the
            // camera has begun framing. Closing begins on the following tick.
            spotlight_close_armed_ = true;
        }
    }

    spotlight_cinematic_.step(intent.dt);

    // Zoom +/- is resize-only. It follows live camera zoom from the same
    // session and cannot replay or alter the cinematic activation state.
    spotlight_zoom_resize_.observe(
        spotlight_requested,
        runtime_zoom_,
        camera_zoom);
    spotlight_zoom_resize_.step(camera_zoom);

    spotlight_runtime_was_requested_ =
        spotlight_requested;

    constexpr float kLeftClickLifetime = 0.88f;
    constexpr float kRightClickLifetime = 0.90f;
    constexpr float kMiddleClickLifetime = 0.84f;

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

    PresentationFrameState result;
    result.camera_center_x = camera_center_x;
    result.camera_center_y = camera_center_y;
    result.camera_zoom = camera_zoom;

    result.spotlight.enabled =
        settings_.spotlight.enabled;
    result.spotlight.link_to_zoom =
        settings_.spotlight.link_to_zoom;
    result.spotlight.mode =
        settings_.spotlight.mode;
    result.spotlight.size =
        settings_.spotlight.size;
    result.spotlight.shape =
        settings_.spotlight.shape;
    result.spotlight.cinematic_speed =
        settings_.spotlight.cinematic_speed;
    result.spotlight.area_scale_percent =
        settings_.spotlight.area_scale_percent;
    result.spotlight.feather_short_edge_fraction =
        settings_.spotlight.feather_short_edge_fraction;
    result.spotlight.dim_strength =
        settings_.spotlight.dim_strength;
    result.spotlight.focus_valid =
        spotlight_focus_valid_;
    result.spotlight.content_x =
        spotlight_focus_x_;
    result.spotlight.content_y =
        spotlight_focus_y_;

    result.spotlight.focus_mix =
        spotlight_cinematic_.value;
    result.spotlight.dim_mix =
        arzoom::cinematic_dim_mix(
            spotlight_cinematic_.value);
    result.spotlight.zoom_resize_scale =
        spotlight_zoom_resize_.scale;

    // Keep the existing compositor alive while an opening transition is still
    // visually non-zero. This prevents Zoom-off/reversal from becoming a hard
    // cut while still returning exact pass-through at the endpoint.
    result.spotlight.runtime_requested =
        settings_.spotlight.enabled &&
        spotlight_focus_valid_ &&
        (spotlight_requested ||
         spotlight_cinematic_.visually_active());

    spotlight_zoom_was_requested_ =
        wants_zoom;

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