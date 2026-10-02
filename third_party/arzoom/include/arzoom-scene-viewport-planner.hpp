#pragma once

#include "arzoom-scene-motion-synthesizer.hpp"
#include "arzoom-smart-zone-camera.hpp"

#include <algorithm>
#include <cmath>

namespace arzoom {

/*
 * Deterministic Scene Viewport Planner + Kinematic Camera
 * =======================================================
 *
 * SceneViewportPlanner owns WHERE the camera should frame. SceneKinematicMotion
 * owns HOW the camera gets there. There is still exactly one Scene Camera
 * authority.
 *
 * Normal/local work:
 *   OBSERVE -> SETTLE -> one final target -> KINEMATIC SETTLE -> EXACT HOLD
 *
 * Far movement / visibility risk:
 *   OBSERVE -> TRACK (latched) -> pointer settles -> one final target
 *           -> same kinematic state SETTLES -> EXACT HOLD
 *
 * TRACK is latched with hysteresis. It cannot chatter ON/OFF around a threshold,
 * and leaving TRACK never zeroes velocity. The same position/velocity/
 * acceleration state continues into final settling. Pressure is continuous and
 * affects motion limits only; it never moves the desired framing corridor.
 *
 * State/work remain O(1). No pointer history, image analysis, frame readback,
 * hidden camera synchronization or scene mutation is introduced.
 */

struct SceneViewportPlan {
    bool active = false;
    Vec2 target_center{0.5f, 0.5f};
    Vec2 target_pointer_output{0.5f, 0.5f};
    float travel_output = 0.0f;
};

inline float scene_zoom_pressure(float zoom)
{
    return std::clamp((std::max(zoom, 1.0f) - 2.60f) / 1.40f,
                      0.0f, 1.0f);
}

inline float scene_pointer_settle_seconds(CameraMotionStyle style, float zoom)
{
    float base = 0.15f;
    switch (style) {
    case CameraMotionStyle::Responsive:
        base = 0.09f;
        break;
    case CameraMotionStyle::Balanced:
        base = 0.12f;
        break;
    case CameraMotionStyle::Cinematic:
    default:
        base = 0.15f;
        break;
    }

    const float pressure = scene_zoom_pressure(zoom);
    return std::max(0.055f, base * (1.0f - 0.45f * pressure));
}

inline float scene_context_wake_half(float safe_zone, float zoom)
{
    const float base = std::clamp(safe_zone * 0.41f, 0.105f, 0.165f);
    const float pressure = scene_zoom_pressure(zoom);
    return std::clamp(base * (1.0f - 0.28f * pressure),
                      0.078f, 0.165f);
}

inline float scene_context_landing_half(float wake_half, float zoom)
{
    const float pressure = scene_zoom_pressure(zoom);
    const float ratio = 0.66f - 0.16f * pressure;
    return std::clamp(wake_half * ratio, 0.040f, 0.105f);
}

inline Vec2 scene_context_anchor(const CameraInput &input)
{
    return {
        std::clamp(input.anchor.x, 0.20f, 0.80f),
        std::clamp(input.anchor.y, 0.20f, 0.80f),
    };
}

inline float scene_hard_visibility_margin(float zoom)
{
    return 0.060f + 0.080f * scene_zoom_pressure(zoom);
}

inline float scene_axis_follow_pressure(float value, float anchor,
                                        float wake_half, float hard_margin)
{
    const float distance = std::fabs(value - anchor);
    if (distance <= wake_half)
        return 0.0f;

    const bool low_side = value < anchor;
    const float hard_distance = low_side
        ? std::max(anchor - hard_margin, wake_half + 0.01f)
        : std::max((1.0f - hard_margin) - anchor, wake_half + 0.01f);
    const float normalized = std::clamp(
        (distance - wake_half) / std::max(hard_distance - wake_half, 0.01f),
        0.0f, 1.0f);
    return scene_smoothstep01(normalized);
}

inline float scene_follow_pressure(const CameraInput &input,
                                   Vec2 current_center, float zoom)
{
    if (!input.cursor_valid || input.follow_policy == CameraFollowPolicy::Fixed)
        return 0.0f;

    const float safe_zoom = std::max(zoom, 1.0f);
    const Vec2 pointer_output = cursor_output_position(
        input.cursor, current_center, safe_zoom);
    if (pointer_output.x < 0.0f || pointer_output.x > 1.0f ||
        pointer_output.y < 0.0f || pointer_output.y > 1.0f)
        return 1.0f;

    const Vec2 anchor = scene_context_anchor(input);
    const float wake_half = scene_context_wake_half(input.safe_zone, safe_zoom);
    const float hard_margin = scene_hard_visibility_margin(safe_zoom);
    return std::max(
        scene_axis_follow_pressure(pointer_output.x, anchor.x,
                                   wake_half, hard_margin),
        scene_axis_follow_pressure(pointer_output.y, anchor.y,
                                   wake_half, hard_margin));
}

inline float scene_tracking_enter_pressure(float zoom)
{
    /* Start earlier instead of driving the camera harder. At high zoom the
     * visible viewport is tiny, so smooth tracking needs more runway before the
     * pointer reaches the physical edge. */
    return 0.16f - 0.08f * scene_zoom_pressure(zoom);
}

inline float scene_tracking_exit_pressure(float zoom)
{
    return 0.10f - 0.025f * scene_zoom_pressure(zoom);
}

inline bool scene_adaptive_guard_needed(const CameraInput &input,
                                        Vec2 current_center, float zoom,
                                        float pointer_motion_output)
{
    if (!input.cursor_valid || input.follow_policy == CameraFollowPolicy::Fixed ||
        pointer_motion_output <= 0.0012f)
        return false;
    return scene_follow_pressure(input, current_center, zoom) >=
           scene_tracking_enter_pressure(zoom);
}

inline float scene_high_zoom_guard_margin(float zoom)
{
    return 0.055f + 0.145f * scene_zoom_pressure(zoom);
}

inline bool scene_high_zoom_guard_needed(const CameraInput &input,
                                         Vec2 current_center, float zoom)
{
    if (scene_zoom_pressure(zoom) < 0.18f || !input.cursor_valid ||
        input.follow_policy == CameraFollowPolicy::Fixed)
        return false;
    const Vec2 pointer_output = cursor_output_position(
        input.cursor, current_center, std::max(zoom, 1.0f));
    const float margin = scene_high_zoom_guard_margin(zoom);
    return pointer_output.x < margin || pointer_output.x > 1.0f - margin ||
           pointer_output.y < margin || pointer_output.y > 1.0f - margin;
}

inline float scene_high_zoom_retarget_threshold(float zoom)
{
    return 0.070f - 0.025f * scene_zoom_pressure(zoom);
}

inline float scene_guard_follow_time(float zoom)
{
    return 0.19f - 0.065f * scene_zoom_pressure(zoom);
}

inline float scene_guard_follow_max_output_speed(float zoom)
{
    return 2.45f + 1.35f * scene_zoom_pressure(zoom);
}

inline float scene_adaptive_follow_time(float zoom, float pressure)
{
    const float z = scene_zoom_pressure(zoom);
    return std::clamp(0.205f - 0.080f * pressure - 0.050f * z,
                      0.075f, 0.205f);
}

inline float scene_adaptive_follow_max_output_speed(float zoom, float pressure)
{
    const float z = scene_zoom_pressure(zoom);
    return 2.70f + 2.00f * pressure + 1.10f * z;
}

inline float scene_pointer_lead_seconds(float zoom, float pressure)
{
    const float z = scene_zoom_pressure(zoom);
    return std::clamp(0.015f + 0.022f * pressure + 0.012f * z,
                      0.0f, 0.050f);
}

inline float scene_tracking_landing_half(float zoom, float /*pressure*/)
{
    const float z = scene_zoom_pressure(zoom);
    return std::clamp(0.125f - 0.050f * z, 0.070f, 0.125f);
}

inline SceneViewportPlan scene_tracking_plan(const CameraInput &input,
                                             Vec2 tracking_cursor,
                                             Vec2 current_center,
                                             float zoom,
                                             float pressure)
{
    SceneViewportPlan plan;
    if (!input.cursor_valid || input.follow_policy == CameraFollowPolicy::Fixed)
        return plan;

    const float safe_zoom = std::max(zoom, 1.0f);
    const Vec2 pointer_output = cursor_output_position(
        tracking_cursor, current_center, safe_zoom);
    const Vec2 anchor = scene_context_anchor(input);
    const float landing = scene_tracking_landing_half(safe_zoom, pressure);

    Vec2 desired_output = pointer_output;
    bool needs_move = false;
    if (pointer_output.x < anchor.x - landing) {
        desired_output.x = anchor.x - landing;
        needs_move = true;
    } else if (pointer_output.x > anchor.x + landing) {
        desired_output.x = anchor.x + landing;
        needs_move = true;
    }
    if (pointer_output.y < anchor.y - landing) {
        desired_output.y = anchor.y - landing;
        needs_move = true;
    } else if (pointer_output.y > anchor.y + landing) {
        desired_output.y = anchor.y + landing;
        needs_move = true;
    }

    if (!needs_move)
        return plan;

    Vec2 target_center{
        current_center.x + (pointer_output.x - desired_output.x) / safe_zoom,
        current_center.y + (pointer_output.y - desired_output.y) / safe_zoom,
    };
    target_center = clamp_center(target_center, safe_zoom);
    const float travel_output =
        length(sub(target_center, current_center)) * safe_zoom;
    if (travel_output <= 0.0025f)
        return plan;

    plan.active = true;
    plan.target_center = target_center;
    plan.target_pointer_output = cursor_output_position(
        tracking_cursor, target_center, safe_zoom);
    plan.travel_output = travel_output;
    return plan;
}

inline SceneViewportPlan scene_context_plan(const CameraInput &input,
                                            Vec2 current_center,
                                            float zoom)
{
    SceneViewportPlan plan;
    if (!input.cursor_valid || input.follow_policy == CameraFollowPolicy::Fixed)
        return plan;

    const float safe_zoom = std::max(zoom, 1.0f);
    const Vec2 pointer_output = cursor_output_position(
        input.cursor, current_center, safe_zoom);
    Vec2 desired_output = pointer_output;
    bool needs_move = false;

    if (input.follow_policy == CameraFollowPolicy::Centered) {
        const Vec2 anchor{
            std::clamp(input.anchor.x, 0.12f, 0.88f),
            std::clamp(input.anchor.y, 0.12f, 0.88f),
        };
        if (length(sub(pointer_output, anchor)) <= 0.030f)
            return plan;
        desired_output = anchor;
        needs_move = true;
    } else {
        const float wake_half = scene_context_wake_half(input.safe_zone, safe_zoom);
        const float landing_half = scene_context_landing_half(wake_half, safe_zoom);
        const Vec2 anchor = scene_context_anchor(input);
        if (pointer_output.x < anchor.x - wake_half) {
            desired_output.x = anchor.x - landing_half;
            needs_move = true;
        } else if (pointer_output.x > anchor.x + wake_half) {
            desired_output.x = anchor.x + landing_half;
            needs_move = true;
        }
        if (pointer_output.y < anchor.y - wake_half) {
            desired_output.y = anchor.y - landing_half;
            needs_move = true;
        } else if (pointer_output.y > anchor.y + wake_half) {
            desired_output.y = anchor.y + landing_half;
            needs_move = true;
        }
    }

    if (!needs_move)
        return plan;

    Vec2 target_center{
        current_center.x + (pointer_output.x - desired_output.x) / safe_zoom,
        current_center.y + (pointer_output.y - desired_output.y) / safe_zoom,
    };
    target_center = clamp_center(target_center, safe_zoom);
    const float travel_output =
        length(sub(target_center, current_center)) * safe_zoom;
    if (travel_output <= 0.0075f)
        return plan;

    plan.active = true;
    plan.target_center = target_center;
    plan.target_pointer_output = cursor_output_position(
        input.cursor, target_center, safe_zoom);
    plan.travel_output = travel_output;
    return plan;
}

inline float scene_context_shot_seconds(const CameraProfile &profile,
                                        float zoom, float travel_output)
{
    const float base = std::clamp(
        profile.camera_filter_seconds + 0.055f, 0.26f, 0.44f);
    const float z = scene_zoom_pressure(zoom);
    const float distance = std::clamp((travel_output - 0.035f) / 0.42f,
                                      0.0f, 1.0f);
    return std::clamp(base * (1.0f - 0.24f * z - 0.34f * distance),
                      0.18f, 0.44f);
}

enum class SceneShotReason {
    None,
    Activation,
    ZoomStep,
    Return,
};

class SceneViewportPlanner {
public:
    SceneViewportPlanner() { reset(); }

    void reset()
    {
        output_ = {};
        output_.center = {0.5f, 0.5f};
        output_.zoom = 1.0f;
        output_.state = CameraState::Rest;
        motion_.reset(output_.center);
        initialized_ = false;
        previous_zoom_requested_ = false;
        requested_zoom_target_ = 2.0f;
        pointer_tracker_valid_ = false;
        tracked_cursor_ = {0.5f, 0.5f};
        pointer_still_elapsed_ = 0.0f;
        pointer_motion_output_ = 0.0f;
        pointer_velocity_scene_ = {0.0f, 0.0f};
        pointer_prediction_confidence_ = 0.0f;
        tracking_active_ = false;
        settling_active_ = false;
        settling_target_ = output_.center;
        shot_active_ = false;
        shot_reason_ = SceneShotReason::None;
        shot_elapsed_ = 0.0f;
        shot_duration_ = 0.40f;
        shot_start_ = screen_transform({0.5f, 0.5f}, 1.0f);
        shot_target_ = shot_start_;
        committed_cursor_valid_ = false;
        committed_cursor_ = {0.5f, 0.5f};
        generation_ = 0;
        tracking_entries_ = 0;
    }

    CameraOutput output() const { return output_; }
    unsigned long long generation() const { return generation_; }
    bool shot_active() const { return shot_active_; }
    bool guard_follow_active() const { return tracking_active_; }
    unsigned long long tracking_entries() const { return tracking_entries_; }

    CameraOutput step(const CameraInput &source_input)
    {
        CameraInput input = source_input;
        const float dt = std::clamp(input.dt, 0.0f, 0.10f);
        const float desired_zoom =
            std::clamp(input.configured_zoom, 1.10f, 4.00f);
        const CameraProfile profile = camera_profile(input.motion_style);
        update_pointer_tracker(input, dt);

        if (!initialized_) {
            initialized_ = true;
            requested_zoom_target_ = desired_zoom;
        }

        const bool rising = input.zoom_requested && !previous_zoom_requested_;
        const bool falling = !input.zoom_requested && previous_zoom_requested_;
        const bool zoom_step = input.zoom_requested && previous_zoom_requested_ &&
            std::fabs(desired_zoom - requested_zoom_target_) > 0.0005f;

        if (falling) {
            cancel_live_motion();
            commit_return(profile);
        } else if (rising) {
            cancel_live_motion();
            commit_zoom_shot(input, desired_zoom, profile,
                             SceneShotReason::Activation);
        } else if (zoom_step) {
            cancel_live_motion();
            commit_zoom_shot(input, desired_zoom, profile,
                             SceneShotReason::ZoomStep);
        }

        requested_zoom_target_ = desired_zoom;
        previous_zoom_requested_ = input.zoom_requested;

        if (!input.zoom_requested) {
            if (!shot_active_ &&
                (output_.zoom > 1.0005f ||
                 !nearly_equal(output_.center, {0.5f, 0.5f}, 0.0005f)))
                commit_return(profile);
            if (shot_active_)
                return step_explicit_shot(dt);
            lock_full_frame();
            return output_;
        }

        if (shot_active_) {
            maybe_supersede_zoom_pointer(input, profile);
            return step_explicit_shot(dt);
        }

        const bool settled_pointer = pointer_has_settled(input.motion_style);
        const float pressure = scene_follow_pressure(
            input, output_.center, output_.zoom);
        const bool moving = pointer_motion_output_ > 0.0012f;

        if (!tracking_active_ && moving &&
            pressure >= scene_tracking_enter_pressure(output_.zoom)) {
            tracking_active_ = true;
            settling_active_ = false;
            ++tracking_entries_;
            ++generation_;
        }

        if (tracking_active_) {
            if (settled_pointer) {
                tracking_active_ = false;
                commit_final_pointer_target(input);
                return step_live_motion(input.motion_style, pressure, dt,
                                        CameraState::Settle);
            }
            return step_tracking(input, pressure, dt);
        }

        if (settling_active_) {
            if (moving) {
                settling_active_ = false;
                settling_target_ = output_.center;
            } else {
                return step_live_motion(input.motion_style, pressure, dt,
                                        CameraState::Settle);
            }
        }

        if (settled_pointer) {
            const SceneViewportPlan plan = scene_context_plan(
                input, output_.center, output_.zoom);
            if (plan.active) {
                settling_target_ = plan.target_center;
                settling_active_ = true;
                ++generation_;
                return step_live_motion(input.motion_style, pressure, dt,
                                        CameraState::Settle);
            }
        }

        if (length(output_.velocity) * output_.zoom > 0.0008f ||
            length(output_.acceleration) * output_.zoom > 0.02f) {
            settling_target_ = output_.center;
            settling_active_ = true;
            return step_live_motion(input.motion_style, pressure, dt,
                                    CameraState::SmoothIdle);
        }

        exact_hold();
        return output_;
    }

private:
    bool pointer_has_settled(CameraMotionStyle style) const
    {
        return pointer_tracker_valid_ &&
               pointer_still_elapsed_ >=
                   scene_pointer_settle_seconds(style, output_.zoom);
    }

    void update_pointer_tracker(const CameraInput &input, float dt)
    {
        if (!input.cursor_valid) {
            pointer_tracker_valid_ = false;
            pointer_still_elapsed_ = 0.0f;
            pointer_motion_output_ = 0.0f;
            pointer_velocity_scene_ = {0.0f, 0.0f};
            pointer_prediction_confidence_ = 0.0f;
            return;
        }

        if (!pointer_tracker_valid_) {
            pointer_tracker_valid_ = true;
            tracked_cursor_ = input.cursor;
            pointer_still_elapsed_ = 0.0f;
            pointer_motion_output_ = 0.0f;
            pointer_velocity_scene_ = {0.0f, 0.0f};
            pointer_prediction_confidence_ = 0.0f;
            return;
        }

        const float zoom = std::max(output_.zoom, 1.0f);
        const Vec2 delta = sub(input.cursor, tracked_cursor_);
        pointer_motion_output_ = length(delta) * zoom;

        if (dt > 1.0e-6f) {
            const Vec2 instant_velocity = mul(delta, 1.0f / dt);
            const float instant_speed = length(instant_velocity);
            const float filtered_speed = length(pointer_velocity_scene_);
            const bool reversing = filtered_speed > 0.02f && instant_speed > 0.02f &&
                dot(instant_velocity, pointer_velocity_scene_) < 0.0f;

            if (reversing) {
                pointer_prediction_confidence_ = 0.0f;
                pointer_velocity_scene_ = mul(pointer_velocity_scene_, 0.30f);
            } else {
                const float velocity_alpha = exponential_alpha(dt, 0.095f);
                pointer_velocity_scene_ = add(
                    mul(pointer_velocity_scene_, 1.0f - velocity_alpha),
                    mul(instant_velocity, velocity_alpha));
                const float confidence_alpha = exponential_alpha(dt, 0.11f);
                pointer_prediction_confidence_ +=
                    (1.0f - pointer_prediction_confidence_) * confidence_alpha;
            }
        }

        if (pointer_motion_output_ <= 0.0012f) {
            pointer_still_elapsed_ = std::min(pointer_still_elapsed_ + dt, 2.0f);
            pointer_prediction_confidence_ *= std::exp(-12.0f * dt);
            pointer_velocity_scene_ = mul(pointer_velocity_scene_,
                                          std::exp(-8.0f * dt));
        } else {
            pointer_still_elapsed_ = 0.0f;
        }

        tracked_cursor_ = input.cursor;
    }

    Vec2 predicted_tracking_cursor(const CameraInput &input,
                                   float pressure) const
    {
        const float confidence = std::clamp(pointer_prediction_confidence_,
                                            0.0f, 1.0f);
        const float lead = scene_pointer_lead_seconds(output_.zoom, pressure) *
                           confidence;
        Vec2 displacement = mul(pointer_velocity_scene_, lead);
        const float max_displacement_scene = 0.055f / std::max(output_.zoom, 1.0f);
        displacement = scene_clamp_magnitude(displacement,
                                             max_displacement_scene);
        Vec2 predicted = add(input.cursor, displacement);
        predicted.x = std::clamp(predicted.x, 0.0f, 1.0f);
        predicted.y = std::clamp(predicted.y, 0.0f, 1.0f);
        return predicted;
    }

    Vec2 zoom_target_center(const CameraInput &input, float target_zoom) const
    {
        if (input.follow_policy == CameraFollowPolicy::Fixed || !input.cursor_valid)
            return clamp_center(output_.center, target_zoom);
        const Vec2 anchor{
            std::clamp(input.anchor.x, 0.18f, 0.82f),
            std::clamp(input.anchor.y, 0.18f, 0.82f),
        };
        return centered_target(input.cursor, anchor, target_zoom);
    }

    void cancel_live_motion()
    {
        tracking_active_ = false;
        settling_active_ = false;
    }

    void commit_explicit_shot(Vec2 target_center, float target_zoom,
                              float duration, SceneShotReason reason,
                              const CameraInput *input)
    {
        shot_start_ = screen_transform(output_.center,
                                       std::max(output_.zoom, 1.0f));
        shot_target_ = screen_transform(
            clamp_center(target_center, target_zoom), target_zoom);
        shot_elapsed_ = 0.0f;
        shot_duration_ = std::max(duration, 0.08f);
        shot_reason_ = reason;
        shot_active_ = true;
        ++generation_;
        committed_cursor_valid_ = input && input->cursor_valid;
        committed_cursor_ = committed_cursor_valid_ ? input->cursor
                                                     : output_.center;
    }

    void commit_zoom_shot(const CameraInput &input, float target_zoom,
                          const CameraProfile &profile,
                          SceneShotReason reason)
    {
        const bool zooming_in = target_zoom >= output_.zoom;
        const float duration = zooming_in
            ? std::clamp(profile.zoom_in_seconds, 0.30f, 0.58f)
            : std::clamp(profile.zoom_out_seconds, 0.34f, 0.66f);
        commit_explicit_shot(zoom_target_center(input, target_zoom),
                             target_zoom, duration, reason, &input);
    }

    void commit_return(const CameraProfile &profile)
    {
        commit_explicit_shot({0.5f, 0.5f}, 1.0f,
                             std::clamp(profile.zoom_out_seconds, 0.34f, 0.70f),
                             SceneShotReason::Return, nullptr);
    }

    void commit_final_pointer_target(const CameraInput &input)
    {
        const SceneViewportPlan plan = scene_context_plan(
            input, output_.center, output_.zoom);
        settling_target_ = plan.active ? plan.target_center : output_.center;
        settling_active_ = true;
        ++generation_;
    }

    CameraOutput step_tracking(const CameraInput &input, float pressure, float dt)
    {
        const Vec2 predicted = predicted_tracking_cursor(input, pressure);
        const SceneViewportPlan plan = scene_tracking_plan(
            input, predicted, output_.center, output_.zoom, pressure);
        const Vec2 target = plan.active ? plan.target_center : output_.center;
        const float distance_output =
            length(sub(target, output_.center)) * output_.zoom;
        const SceneMotionLimits limits = scene_motion_limits(
            input.motion_style, output_.zoom, pressure, distance_output);
        const SceneMotionSample motion = motion_.step(
            target, output_.zoom, dt, limits);
        apply_motion_sample(motion, CameraState::CatchUp, pressure);
        return output_;
    }

    CameraOutput step_live_motion(CameraMotionStyle style, float pressure,
                                  float dt, CameraState state)
    {
        const float distance_output =
            length(sub(settling_target_, output_.center)) * output_.zoom;
        const SceneMotionLimits limits = scene_motion_limits(
            style, output_.zoom, pressure, distance_output);
        const SceneMotionSample motion = motion_.step(
            settling_target_, output_.zoom, dt, limits);
        apply_motion_sample(motion, state, pressure);
        if (motion.settled) {
            settling_active_ = false;
            exact_hold();
        }
        return output_;
    }

    void apply_motion_sample(const SceneMotionSample &motion,
                             CameraState state, float pressure)
    {
        output_.center = motion.center;
        output_.velocity = motion.velocity;
        output_.acceleration = motion.acceleration;
        output_.state = state;
        output_.intent_confidence = 1.0f;
        output_.urgency = pressure;
    }

    void maybe_supersede_zoom_pointer(const CameraInput &input,
                                      const CameraProfile &profile)
    {
        if (!shot_active_ || shot_reason_ == SceneShotReason::Return ||
            !input.cursor_valid || !committed_cursor_valid_ ||
            !pointer_has_settled(input.motion_style))
            return;
        const float moved_output =
            length(sub(input.cursor, committed_cursor_)) *
            std::max(output_.zoom, 1.0f);
        if (moved_output <= scene_high_zoom_retarget_threshold(output_.zoom))
            return;
        commit_zoom_shot(input, requested_zoom_target_, profile,
                         SceneShotReason::ZoomStep);
    }

    CameraOutput step_explicit_shot(float dt)
    {
        if (!shot_active_)
            return output_;

        const CameraOutput previous = output_;
        shot_elapsed_ += dt;
        const float normalized = std::clamp(
            shot_elapsed_ / std::max(shot_duration_, 0.05f), 0.0f, 1.0f);
        const ScreenTransform transform = lerp_transform(
            shot_start_, shot_target_, minimum_jerk01(normalized));
        output_.zoom = std::max(transform.scale, 1.0f);
        output_.center = clamp_center(transform_center(transform), output_.zoom);
        output_.intent_confidence = 1.0f;
        output_.urgency = 0.0f;
        output_.state = shot_reason_ == SceneShotReason::Return
            ? CameraState::Returning
            : (shot_reason_ == SceneShotReason::Activation
                   ? CameraState::Activating
                   : CameraState::Settle);

        if (dt > 1.0e-6f) {
            const Vec2 next_velocity =
                mul(sub(output_.center, previous.center), 1.0f / dt);
            output_.acceleration =
                mul(sub(next_velocity, previous.velocity), 1.0f / dt);
            output_.velocity = next_velocity;
        }

        if (normalized >= 1.0f) {
            output_.zoom = std::max(shot_target_.scale, 1.0f);
            output_.center = clamp_center(transform_center(shot_target_), output_.zoom);
            output_.velocity = {0.0f, 0.0f};
            output_.acceleration = {0.0f, 0.0f};
            const bool returning = shot_reason_ == SceneShotReason::Return;
            shot_active_ = false;
            shot_reason_ = SceneShotReason::None;
            motion_.reset(output_.center);
            if (returning)
                lock_full_frame();
            else
                output_.state = CameraState::SmoothIdle;
        }
        return output_;
    }

    void exact_hold()
    {
        tracking_active_ = false;
        settling_active_ = false;
        output_.center = clamp_center(output_.center, output_.zoom);
        output_.velocity = {0.0f, 0.0f};
        output_.acceleration = {0.0f, 0.0f};
        output_.state = CameraState::SmoothIdle;
        output_.intent_confidence = 1.0f;
        output_.urgency = 0.0f;
        motion_.reset(output_.center);
    }

    void lock_full_frame()
    {
        tracking_active_ = false;
        settling_active_ = false;
        output_.center = {0.5f, 0.5f};
        output_.zoom = 1.0f;
        output_.velocity = {0.0f, 0.0f};
        output_.acceleration = {0.0f, 0.0f};
        output_.state = CameraState::Rest;
        output_.intent_confidence = 0.0f;
        output_.urgency = 0.0f;
        shot_active_ = false;
        shot_reason_ = SceneShotReason::None;
        motion_.reset(output_.center);
    }

    CameraOutput output_{};
    SceneKinematicMotion motion_{};
    bool initialized_ = false;
    bool previous_zoom_requested_ = false;
    float requested_zoom_target_ = 2.0f;

    bool pointer_tracker_valid_ = false;
    Vec2 tracked_cursor_{0.5f, 0.5f};
    float pointer_still_elapsed_ = 0.0f;
    float pointer_motion_output_ = 0.0f;
    Vec2 pointer_velocity_scene_{0.0f, 0.0f};
    float pointer_prediction_confidence_ = 0.0f;

    bool tracking_active_ = false;
    bool settling_active_ = false;
    Vec2 settling_target_{0.5f, 0.5f};

    bool shot_active_ = false;
    SceneShotReason shot_reason_ = SceneShotReason::None;
    float shot_elapsed_ = 0.0f;
    float shot_duration_ = 0.40f;
    ScreenTransform shot_start_{};
    ScreenTransform shot_target_{};
    bool committed_cursor_valid_ = false;
    Vec2 committed_cursor_{0.5f, 0.5f};

    unsigned long long generation_ = 0;
    unsigned long long tracking_entries_ = 0;
};

} // namespace arzoom
