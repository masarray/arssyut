#pragma once

#include "arzoom-math.hpp"

#include <algorithm>
#include <cmath>

namespace arzoom {

enum class CameraState {
    Rest,
    Activating,
    Observe,
    Follow,
    CatchUp,
    Coast,
    SmoothIdle,
    Settle,
    Returning,
};

enum class CameraFollowPolicy {
    Smart,
    Centered,
    Fixed,
};

enum class CameraMotionStyle {
    Cinematic,
    Balanced,
    Responsive,
};

/*
 * Smart Zone / Smooth Idle camera
 * -------------------------------
 * ArZoom follows presenter AREA changes, not pointer motion.
 *
 * - Zoom transitions interpolate one affine screen transform with quintic
 *   minimum-jerk progress, keeping screen-space motion straight.
 * - Long relocation uses the existing cascaded gimbal destination filters.
 * - Once the cursor has been reacquired inside a useful presentation region,
 *   live-pointer influence fades through COAST instead of snapping to rest.
 * - SMOOTH_IDLE locks the viewport while the presenter circles/points locally.
 *   Only a sustained exit from the outer zone starts another follow shot.
 */
struct CameraProfile {
    float zoom_in_seconds;
    float zoom_out_seconds;
    float observe_seconds;
    float destination_filter_seconds;
    float camera_filter_seconds;
    float urgency_filter_seconds;
    float urgency_response_gain;
    float cursor_velocity_filter_seconds;
    float settle_position_output;
    float settle_velocity_output;
    float explanation_radius_output;
    float explanation_orbit_radius_output;
    float explanation_coherence_limit;
    float coast_seconds;
    float idle_zone_radius_output;
    float idle_exit_radius_output;
    float idle_exit_observe_seconds;
};

inline CameraProfile camera_profile(CameraMotionStyle style)
{
    switch (style) {
    case CameraMotionStyle::Responsive:
        return {0.34f, 0.42f, 0.060f, 0.125f, 0.205f, 0.095f,
                0.85f, 0.085f, 0.0055f, 0.018f,
                0.125f, 0.180f, 0.42f,
                0.30f, 0.180f, 0.290f, 0.055f};
    case CameraMotionStyle::Cinematic:
        return {0.52f, 0.64f, 0.145f, 0.235f, 0.360f, 0.150f,
                0.55f, 0.130f, 0.0070f, 0.012f,
                0.175f, 0.245f, 0.48f,
                0.48f, 0.245f, 0.390f, 0.110f};
    case CameraMotionStyle::Balanced:
    default:
        return {0.44f, 0.56f, 0.105f, 0.195f, 0.315f, 0.125f,
                0.68f, 0.105f, 0.0060f, 0.015f,
                0.155f, 0.220f, 0.45f,
                0.40f, 0.220f, 0.345f, 0.080f};
    }
}

struct CameraInput {
    float dt = 1.0f / 60.0f;
    Vec2 cursor{0.5f, 0.5f};
    bool cursor_valid = false;
    bool zoom_requested = false;
    float configured_zoom = 2.0f;
    Vec2 anchor{0.5f, 0.45f};
    float safe_zone = 0.28f;
    CameraFollowPolicy follow_policy = CameraFollowPolicy::Smart;
    CameraMotionStyle motion_style = CameraMotionStyle::Balanced;
    bool emphasis_event = false;
};

struct CameraOutput {
    Vec2 center{0.5f, 0.5f};
    float zoom = 1.0f;
    Vec2 velocity{0.0f, 0.0f};
    Vec2 acceleration{0.0f, 0.0f};
    CameraState state = CameraState::Rest;
    float intent_confidence = 0.0f;
    float urgency = 0.0f;
};

inline float dot(Vec2 a, Vec2 b)
{
    return a.x * b.x + a.y * b.y;
}

inline Vec2 lerp(Vec2 a, Vec2 b, float t)
{
    return add(a, mul(sub(b, a), t));
}

inline Vec2 normalized(Vec2 value)
{
    const float len = length(value);
    return len > 1.0e-6f ? mul(value, 1.0f / len)
                          : Vec2{0.0f, 0.0f};
}

inline float gimbal_alpha(float dt, float time_constant)
{
    const float safe_dt = std::clamp(dt, 0.0f, 0.10f);
    const float tau = std::max(time_constant, 0.001f);
    return 1.0f - std::exp(-safe_dt / tau);
}

inline Vec2 gimbal_lowpass(Vec2 current, Vec2 target, float dt,
                           float time_constant)
{
    return lerp(current, target, gimbal_alpha(dt, time_constant));
}

inline float gimbal_lowpass(float current, float target, float dt,
                            float time_constant)
{
    return current + (target - current) *
                         gimbal_alpha(dt, time_constant);
}

inline float minimum_jerk01(float t)
{
    t = std::clamp(t, 0.0f, 1.0f);
    const float t2 = t * t;
    const float t3 = t2 * t;
    return t3 * (10.0f + t * (-15.0f + 6.0f * t));
}

struct ScreenTransform {
    float scale = 1.0f;
    Vec2 offset{0.0f, 0.0f};
};

inline ScreenTransform screen_transform(Vec2 center, float zoom)
{
    const float scale = std::max(zoom, 1.0f);
    return {scale,
            {0.5f - scale * center.x,
             0.5f - scale * center.y}};
}

inline Vec2 transform_center(const ScreenTransform &transform)
{
    const float scale = std::max(transform.scale, 1.0e-6f);
    return {(0.5f - transform.offset.x) / scale,
            (0.5f - transform.offset.y) / scale};
}

inline ScreenTransform lerp_transform(const ScreenTransform &a,
                                      const ScreenTransform &b, float t)
{
    return {a.scale + (b.scale - a.scale) * t,
            lerp(a.offset, b.offset, t)};
}

class SmartCamera {
public:
    CameraOutput output() const
    {
        return {center_, zoom_, velocity_, acceleration_, state_,
                intent_confidence_, urgency_};
    }

    void reset()
    {
        center_ = {0.5f, 0.5f};
        zoom_ = 1.0f;
        velocity_ = {0.0f, 0.0f};
        acceleration_ = {0.0f, 0.0f};
        previous_zoom_requested_ = false;
        state_ = CameraState::Rest;

        observe_seconds_ = 0.0f;
        intent_confidence_ = 0.0f;
        urgency_ = 0.0f;

        reset_servo(center_);
        clear_zone();

        have_cursor_ = false;
        previous_cursor_ = {0.5f, 0.5f};
        filtered_cursor_velocity_ = {0.0f, 0.0f};
        previous_cursor_direction_ = {0.0f, 0.0f};
        direction_persistence_ = 0.0f;

        activation_elapsed_ = 0.0f;
        return_elapsed_ = 0.0f;
    }

    CameraOutput step(const CameraInput &input)
    {
        const float dt = std::clamp(input.dt, 0.0f, 0.10f);
        const float configured_zoom =
            std::clamp(input.configured_zoom, 1.10f, 4.00f);
        const float safe_zone =
            std::clamp(input.safe_zone, 0.10f, 0.60f);
        const CameraProfile profile = camera_profile(input.motion_style);

        const Vec2 previous_center = center_;
        const Vec2 previous_velocity = velocity_;

        update_cursor_model(input.cursor, input.cursor_valid, dt, profile);

        const bool rising = input.zoom_requested && !previous_zoom_requested_;
        const bool falling = !input.zoom_requested && previous_zoom_requested_;
        previous_zoom_requested_ = input.zoom_requested;

        if (rising)
            begin_activation(input, configured_zoom, safe_zone);
        else if (falling)
            begin_return();

        if (!input.zoom_requested) {
            if (state_ == CameraState::Returning)
                step_return(dt, profile);
            else
                lock_full_frame();
        } else if (state_ == CameraState::Activating) {
            step_activation(input, configured_zoom, safe_zone, dt, profile);
        } else {
            update_active_zoom(configured_zoom, dt, profile);
            step_active_follow(input, safe_zone, dt, profile);
        }

        update_motion_diagnostics(previous_center, previous_velocity, dt);
        return output();
    }

private:
    void update_motion_diagnostics(Vec2 previous_center,
                                   Vec2 previous_velocity, float dt)
    {
        if (dt <= 1.0e-6f || state_ == CameraState::Rest ||
            state_ == CameraState::SmoothIdle) {
            velocity_ = {0.0f, 0.0f};
            acceleration_ = {0.0f, 0.0f};
            return;
        }

        const Vec2 measured_velocity =
            mul(sub(center_, previous_center), 1.0f / dt);
        velocity_ = measured_velocity;
        acceleration_ =
            mul(sub(measured_velocity, previous_velocity), 1.0f / dt);
    }

    void update_cursor_model(Vec2 cursor, bool valid, float dt,
                             const CameraProfile &profile)
    {
        if (!valid) {
            filtered_cursor_velocity_ = gimbal_lowpass(
                filtered_cursor_velocity_, {0.0f, 0.0f}, dt,
                profile.cursor_velocity_filter_seconds);
            direction_persistence_ =
                std::max(0.0f, direction_persistence_ - 3.0f * dt);
            return;
        }

        if (!have_cursor_ || dt <= 1.0e-6f) {
            previous_cursor_ = cursor;
            filtered_cursor_velocity_ = {0.0f, 0.0f};
            previous_cursor_direction_ = {0.0f, 0.0f};
            direction_persistence_ = 0.0f;
            have_cursor_ = true;
            return;
        }

        const Vec2 delta = sub(cursor, previous_cursor_);
        if (zone_valid_)
            zone_path_source_ += length(delta);

        const Vec2 raw_velocity = mul(delta, 1.0f / dt);
        filtered_cursor_velocity_ = gimbal_lowpass(
            filtered_cursor_velocity_, raw_velocity, dt,
            profile.cursor_velocity_filter_seconds);

        const Vec2 direction = normalized(raw_velocity);
        if (length(direction) > 0.0f) {
            if (length(previous_cursor_direction_) > 0.0f) {
                const float alignment = dot(direction,
                                            previous_cursor_direction_);
                direction_persistence_ = std::clamp(
                    direction_persistence_ +
                        (alignment > 0.72f ? 2.8f : -6.5f) * dt,
                    0.0f, 1.0f);
            } else {
                direction_persistence_ =
                    std::min(1.0f, direction_persistence_ + 1.8f * dt);
            }
            previous_cursor_direction_ = normalized(
                lerp(previous_cursor_direction_, direction, 0.25f));
        } else {
            direction_persistence_ =
                std::max(0.0f, direction_persistence_ - 2.5f * dt);
        }

        previous_cursor_ = cursor;
    }

    void reset_servo(Vec2 center)
    {
        follow_active_ = false;
        coast_active_ = false;
        coast_elapsed_ = 0.0f;
        follow_gain_ = 0.0f;
        follow_reference_center_ = center;
        desired_destination_ = center;
        destination_stage1_ = center;
        destination_stage2_ = center;
        coast_anchor_center_ = center;
        coast_zone_cursor_ = {0.5f, 0.5f};
    }

    void prime_follow_servo()
    {
        follow_active_ = true;
        coast_active_ = false;
        coast_elapsed_ = 0.0f;
        follow_gain_ = 1.0f;
        follow_reference_center_ = center_;
        desired_destination_ = center_;
        destination_stage1_ = center_;
        destination_stage2_ = center_;
    }

    void clear_zone()
    {
        zone_valid_ = false;
        zone_anchor_cursor_ = {0.5f, 0.5f};
        zone_anchor_center_ = center_;
        zone_path_source_ = 0.0f;
        idle_exit_seconds_ = 0.0f;
    }

    void arm_zone(Vec2 cursor, Vec2 center)
    {
        zone_valid_ = true;
        zone_anchor_cursor_ = cursor;
        zone_anchor_center_ = clamp_center(center, zoom_);
        zone_path_source_ = 0.0f;
        idle_exit_seconds_ = 0.0f;
    }

    float edge_risk(Vec2 cursor) const
    {
        const Vec2 output = cursor_output_position(cursor, center_, zoom_);
        const float edge_distance = std::max(
            std::fabs(output.x - 0.5f),
            std::fabs(output.y - 0.5f));
        return std::clamp((edge_distance - 0.39f) / 0.16f,
                          0.0f, 1.0f);
    }

    float zone_net_output(Vec2 cursor) const
    {
        if (!zone_valid_)
            return 1000.0f;
        return length(sub(cursor, zone_anchor_cursor_)) *
               std::max(zoom_, 1.0f);
    }

    float zone_coherence(Vec2 cursor) const
    {
        const float net = zone_net_output(cursor);
        const float path = zone_path_source_ * std::max(zoom_, 1.0f);
        return path > 0.001f ? net / path : 1.0f;
    }

    bool same_presentation_zone(const CameraInput &input,
                                const CameraProfile &profile,
                                float risk) const
    {
        if (!zone_valid_ || input.emphasis_event || risk >= 0.82f)
            return false;

        const float net = zone_net_output(input.cursor);
        const float path = zone_path_source_ * std::max(zoom_, 1.0f);
        const bool inside_outer = net <= profile.idle_exit_radius_output;
        const bool orbiting =
            net <= profile.idle_exit_radius_output * 1.08f &&
            path >= profile.idle_zone_radius_output * 0.75f &&
            zone_coherence(input.cursor) <=
                profile.explanation_coherence_limit;
        return inside_outer || orbiting;
    }

    Vec2 final_activation_center(const CameraInput &input,
                                 float target_zoom,
                                 float safe_zone) const
    {
        if (input.follow_policy == CameraFollowPolicy::Centered)
            return centered_target(activation_focus_, input.anchor,
                                   target_zoom);
        if (input.follow_policy == CameraFollowPolicy::Smart) {
            return smart_follow_target(
                activation_focus_, activation_start_center_, input.anchor,
                safe_zone, target_zoom);
        }
        return clamp_center({0.5f, 0.5f}, target_zoom);
    }

    void begin_activation(const CameraInput &input,
                          float configured_zoom, float safe_zone)
    {
        state_ = CameraState::Activating;
        activation_elapsed_ = 0.0f;
        activation_start_center_ = center_;
        activation_start_zoom_ = zoom_;
        activation_target_zoom_ = configured_zoom;
        activation_focus_ = input.cursor_valid ? input.cursor : center_;
        activation_start_transform_ =
            screen_transform(activation_start_center_, activation_start_zoom_);
        const Vec2 final_center =
            final_activation_center(input, configured_zoom, safe_zone);
        activation_target_transform_ =
            screen_transform(final_center, configured_zoom);
        observe_seconds_ = 0.0f;
        intent_confidence_ = 1.0f;
        urgency_ = 0.0f;
        reset_servo(center_);
        clear_zone();
    }

    void step_activation(const CameraInput &input,
                         float configured_zoom, float safe_zone,
                         float dt, const CameraProfile &profile)
    {
        if (std::fabs(configured_zoom - activation_target_zoom_) > 0.0001f) {
            activation_target_zoom_ = configured_zoom;
            const Vec2 final_center =
                final_activation_center(input, configured_zoom, safe_zone);
            activation_target_transform_ =
                screen_transform(final_center, configured_zoom);
        }

        activation_elapsed_ += dt;
        const float duration = std::max(profile.zoom_in_seconds, 0.05f);
        const float normalized_time =
            std::clamp(activation_elapsed_ / duration, 0.0f, 1.0f);
        const float progress = minimum_jerk01(normalized_time);
        const ScreenTransform transform = lerp_transform(
            activation_start_transform_, activation_target_transform_, progress);
        zoom_ = transform.scale;
        center_ = clamp_center(transform_center(transform), zoom_);

        if (normalized_time >= 1.0f) {
            zoom_ = activation_target_transform_.scale;
            center_ = clamp_center(
                transform_center(activation_target_transform_), zoom_);
            reset_servo(center_);
            observe_seconds_ = 0.0f;
            intent_confidence_ = 0.0f;
            urgency_ = 0.0f;
            if (input.cursor_valid)
                arm_zone(input.cursor, center_);
            state_ = CameraState::SmoothIdle;
        }
    }

    void begin_return()
    {
        state_ = CameraState::Returning;
        return_elapsed_ = 0.0f;
        return_start_transform_ = screen_transform(center_, zoom_);
        return_target_transform_ = screen_transform({0.5f, 0.5f}, 1.0f);
        observe_seconds_ = 0.0f;
        intent_confidence_ = 0.0f;
        urgency_ = 0.0f;
        reset_servo(center_);
        clear_zone();
    }

    void step_return(float dt, const CameraProfile &profile)
    {
        state_ = CameraState::Returning;
        return_elapsed_ += dt;
        const float duration = std::max(profile.zoom_out_seconds, 0.05f);
        const float normalized_time =
            std::clamp(return_elapsed_ / duration, 0.0f, 1.0f);
        const float progress = minimum_jerk01(normalized_time);
        const ScreenTransform transform = lerp_transform(
            return_start_transform_, return_target_transform_, progress);
        zoom_ = transform.scale;
        center_ = clamp_center(transform_center(transform), zoom_);
        if (normalized_time >= 1.0f)
            lock_full_frame();
    }

    void lock_full_frame()
    {
        center_ = {0.5f, 0.5f};
        zoom_ = 1.0f;
        velocity_ = {0.0f, 0.0f};
        acceleration_ = {0.0f, 0.0f};
        state_ = CameraState::Rest;
        observe_seconds_ = 0.0f;
        intent_confidence_ = 0.0f;
        urgency_ = 0.0f;
        reset_servo(center_);
        clear_zone();
    }

    void update_active_zoom(float configured_zoom, float dt,
                            const CameraProfile &profile)
    {
        if (std::fabs(configured_zoom - zoom_) < 0.0001f)
            return;
        const float tau = configured_zoom > zoom_
                              ? profile.zoom_in_seconds * 0.45f
                              : profile.zoom_out_seconds * 0.45f;
        float next_zoom = gimbal_lowpass(zoom_, configured_zoom, dt, tau);
        if (next_zoom < zoom_)
            next_zoom = std::max(next_zoom,
                                 minimum_zoom_for_center(center_));
        zoom_ = std::clamp(next_zoom, 1.0f, 4.0f);
        center_ = clamp_center(center_, zoom_);
        destination_stage1_ = clamp_center(destination_stage1_, zoom_);
        destination_stage2_ = clamp_center(destination_stage2_, zoom_);
        desired_destination_ = clamp_center(desired_destination_, zoom_);
        follow_reference_center_ = clamp_center(follow_reference_center_, zoom_);
        zone_anchor_center_ = clamp_center(zone_anchor_center_, zoom_);
        coast_anchor_center_ = clamp_center(coast_anchor_center_, zoom_);
    }

    void step_active_follow(const CameraInput &input,
                            float safe_zone, float dt,
                            const CameraProfile &profile)
    {
        if (input.follow_policy == CameraFollowPolicy::Fixed) {
            clear_zone();
            state_ = CameraState::Follow;
            step_gimbal_to(clamp_center({0.5f, 0.5f}, zoom_),
                           dt, profile, 0.0f);
            return;
        }

        if (!input.cursor_valid) {
            observe_seconds_ = 0.0f;
            intent_confidence_ = 0.0f;
            urgency_ = gimbal_lowpass(
                urgency_, 0.0f, dt, profile.urgency_filter_seconds);
            reset_servo(center_);
            clear_zone();
            state_ = CameraState::Rest;
            return;
        }

        if (input.follow_policy == CameraFollowPolicy::Centered) {
            clear_zone();
            follow_active_ = true;
            const Vec2 target = centered_target(input.cursor,
                                                input.anchor, zoom_);
            const float distance_output =
                length(sub(target, center_)) * zoom_;
            const float raw_urgency =
                std::clamp(distance_output / 0.32f, 0.0f, 1.0f);
            urgency_ = gimbal_lowpass(
                urgency_, raw_urgency, dt,
                profile.urgency_filter_seconds);
            intent_confidence_ = 1.0f;
            state_ = CameraState::Follow;
            step_gimbal_to(target, dt, profile, urgency_);
            return;
        }

        step_smart_zone_follow(input, safe_zone, dt, profile);
    }

    void start_follow_shot()
    {
        prime_follow_servo();
        clear_zone();
        observe_seconds_ = 0.0f;
        intent_confidence_ = 1.0f;
        state_ = CameraState::Follow;
    }

    void begin_coast(Vec2 cursor)
    {
        if (coast_active_)
            return;
        coast_active_ = true;
        coast_elapsed_ = 0.0f;
        follow_gain_ = 1.0f;
        coast_anchor_center_ = clamp_center(destination_stage2_, zoom_);
        coast_zone_cursor_ = cursor;
        state_ = CameraState::Coast;
    }

    bool coast_zone_broken(const CameraInput &input,
                           const CameraProfile &profile,
                           float risk) const
    {
        const float net = length(sub(input.cursor, coast_zone_cursor_)) *
                          std::max(zoom_, 1.0f);
        return input.emphasis_event || risk >= 0.88f ||
               net > profile.idle_exit_radius_output * 1.12f;
    }

    void cancel_coast()
    {
        coast_active_ = false;
        coast_elapsed_ = 0.0f;
        follow_gain_ = 1.0f;
        state_ = CameraState::Follow;
    }

    void finish_smooth_idle(Vec2 zone_cursor)
    {
        center_ = clamp_center(coast_anchor_center_, zoom_);
        reset_servo(center_);
        arm_zone(zone_cursor, center_);
        observe_seconds_ = 0.0f;
        intent_confidence_ = 0.0f;
        urgency_ = 0.0f;
        state_ = CameraState::SmoothIdle;
    }

    void step_smooth_idle(const CameraInput &input,
                          const CameraProfile &profile,
                          float risk, float dt)
    {
        /* Fast idle path: no destination cascade while cursor stays local. */
        if (same_presentation_zone(input, profile, risk)) {
            idle_exit_seconds_ = 0.0f;
            urgency_ = gimbal_lowpass(
                urgency_, 0.0f, dt, profile.urgency_filter_seconds);
            center_ = zone_anchor_center_;
            state_ = CameraState::SmoothIdle;
            return;
        }

        const float net = zone_net_output(input.cursor);
        const float overshoot = std::max(
            0.0f, net - profile.idle_exit_radius_output);
        const float signal = std::clamp(
            overshoot /
                std::max(profile.idle_exit_radius_output * 0.55f, 0.05f),
            0.0f, 1.0f);

        idle_exit_seconds_ += dt * (0.65f + 0.35f * direction_persistence_);
        intent_confidence_ = std::clamp(
            0.52f * signal + 0.30f * direction_persistence_ +
                0.34f * risk + (input.emphasis_event ? 0.75f : 0.0f),
            0.0f, 1.0f);

        const float dwell = std::max(
            0.025f,
            profile.idle_exit_observe_seconds * (1.0f - 0.65f * risk));
        const bool confirmed = input.emphasis_event || risk > 0.90f ||
            (idle_exit_seconds_ >= dwell && intent_confidence_ >= 0.48f);

        if (!confirmed) {
            center_ = zone_anchor_center_;
            state_ = CameraState::Observe;
            return;
        }

        start_follow_shot();
    }

    void step_smart_zone_follow(const CameraInput &input,
                                float safe_zone, float dt,
                                const CameraProfile &profile)
    {
        const float risk = edge_risk(input.cursor);
        const Vec2 broad_target = smart_follow_target(
            input.cursor, center_, input.anchor, safe_zone, zoom_);
        const float broad_distance_output =
            length(sub(broad_target, center_)) * zoom_;
        const float distance_urgency =
            std::clamp(broad_distance_output / 0.30f, 0.0f, 1.0f);
        const float raw_urgency = std::max(risk, distance_urgency);
        urgency_ = gimbal_lowpass(
            urgency_, raw_urgency, dt, profile.urgency_filter_seconds);

        if (!follow_active_ && zone_valid_) {
            step_smooth_idle(input, profile, risk, dt);
            if (!follow_active_)
                return;
        }

        if (!follow_active_) {
            /* Initial/no-zone fallback. Build a local zone first. */
            arm_zone(input.cursor, center_);
            state_ = CameraState::SmoothIdle;
            return;
        }

        const float settle_zone =
            std::clamp(safe_zone * 0.62f, 0.08f, safe_zone);
        const Vec2 live_target = smart_follow_target(
            input.cursor, follow_reference_center_, input.anchor,
            settle_zone, zoom_);

        if (!coast_active_ && broad_distance_output <= 0.0035f)
            begin_coast(input.cursor);

        if (coast_active_ && coast_zone_broken(input, profile, risk))
            cancel_coast();

        if (!coast_active_) {
            state_ = urgency_ > 0.72f
                         ? CameraState::CatchUp
                         : CameraState::Follow;
            follow_gain_ = 1.0f;
            step_gimbal_to(live_target, dt, profile, urgency_);
            return;
        }

        state_ = CameraState::Coast;
        coast_elapsed_ += dt;
        const float normalized_time = std::clamp(
            coast_elapsed_ / std::max(profile.coast_seconds, 0.10f),
            0.0f, 1.0f);
        follow_gain_ = 1.0f - minimum_jerk01(normalized_time);

        const Vec2 effective_target = lerp(
            coast_anchor_center_, live_target, follow_gain_);
        const float coast_urgency = urgency_ * follow_gain_;
        step_gimbal_to(effective_target, dt, profile, coast_urgency);

        const float error_output =
            length(sub(coast_anchor_center_, center_)) * zoom_;
        const float speed_output = length(velocity_) * zoom_;
        const float hard_position = std::min(
            profile.settle_position_output * 0.22f, 0.0014f);
        const float hard_velocity = std::min(
            profile.settle_velocity_output * 0.24f, 0.0040f);

        if (normalized_time >= 1.0f &&
            error_output <= hard_position &&
            speed_output <= hard_velocity) {
            finish_smooth_idle(coast_zone_cursor_);
        }
    }

    void step_gimbal_to(Vec2 target, float dt,
                        const CameraProfile &profile,
                        float urgency)
    {
        desired_destination_ = clamp_center(target, zoom_);
        const float smooth_urgency = std::clamp(urgency, 0.0f, 1.0f);
        const float response_scale =
            1.0f + profile.urgency_response_gain * smooth_urgency;
        const float destination_tau =
            profile.destination_filter_seconds / response_scale;
        const float camera_tau =
            profile.camera_filter_seconds /
            (1.0f + 0.60f * profile.urgency_response_gain *
                         smooth_urgency);

        destination_stage1_ = gimbal_lowpass(
            destination_stage1_, desired_destination_, dt,
            destination_tau);
        destination_stage2_ = gimbal_lowpass(
            destination_stage2_, destination_stage1_, dt,
            destination_tau * 0.72f);
        center_ = clamp_center(
            gimbal_lowpass(center_, destination_stage2_, dt,
                           camera_tau),
            zoom_);
    }

    Vec2 center_{0.5f, 0.5f};
    float zoom_ = 1.0f;
    Vec2 velocity_{0.0f, 0.0f};
    Vec2 acceleration_{0.0f, 0.0f};
    bool previous_zoom_requested_ = false;
    CameraState state_ = CameraState::Rest;

    float observe_seconds_ = 0.0f;
    float intent_confidence_ = 0.0f;
    float urgency_ = 0.0f;

    bool follow_active_ = false;
    bool coast_active_ = false;
    float coast_elapsed_ = 0.0f;
    float follow_gain_ = 0.0f;
    Vec2 follow_reference_center_{0.5f, 0.5f};
    Vec2 desired_destination_{0.5f, 0.5f};
    Vec2 destination_stage1_{0.5f, 0.5f};
    Vec2 destination_stage2_{0.5f, 0.5f};
    Vec2 coast_anchor_center_{0.5f, 0.5f};
    Vec2 coast_zone_cursor_{0.5f, 0.5f};

    bool zone_valid_ = false;
    Vec2 zone_anchor_cursor_{0.5f, 0.5f};
    Vec2 zone_anchor_center_{0.5f, 0.5f};
    float zone_path_source_ = 0.0f;
    float idle_exit_seconds_ = 0.0f;

    bool have_cursor_ = false;
    Vec2 previous_cursor_{0.5f, 0.5f};
    Vec2 filtered_cursor_velocity_{0.0f, 0.0f};
    Vec2 previous_cursor_direction_{0.0f, 0.0f};
    float direction_persistence_ = 0.0f;

    Vec2 activation_focus_{0.5f, 0.5f};
    Vec2 activation_start_center_{0.5f, 0.5f};
    float activation_start_zoom_ = 1.0f;
    float activation_target_zoom_ = 2.0f;
    float activation_elapsed_ = 0.0f;
    ScreenTransform activation_start_transform_{};
    ScreenTransform activation_target_transform_{};

    float return_elapsed_ = 0.0f;
    ScreenTransform return_start_transform_{};
    ScreenTransform return_target_transform_{};
};

} // namespace arzoom
