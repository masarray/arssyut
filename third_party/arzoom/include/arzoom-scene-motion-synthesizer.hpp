#pragma once

#include "arzoom-smart-zone-camera.hpp"

#include <algorithm>
#include <cmath>

namespace arzoom {

/*
 * Scene Camera kinematic motion synthesizer
 * =========================================
 *
 * This layer owns HOW an already-selected viewport target is reached. It has no
 * pointer semantics, scene mapping or presentation-zone decisions.
 *
 * Motion state is explicitly position + velocity + acceleration. A target that
 * is itself moving frame-to-frame is treated as live tracking and may use
 * decisive cruise authority. Once the planner's target becomes immutable, the
 * same state automatically switches to conservative precision braking. No
 * position/velocity/acceleration reset is needed at TRACK -> SETTLE handoff.
 *
 * Acceleration is jerk-limited every substep. Limits are output-space values and
 * integration uses bounded fixed substeps for stable 30/60/120/144 fps behavior.
 * State/work remain O(1).
 */

struct SceneMotionLimits {
    float position_gain = 4.2f;
    float velocity_response_seconds = 0.10f;
    float max_speed_output = 3.0f;
    float max_acceleration_output = 18.0f;
    float max_jerk_output = 120.0f;
    float settle_position_output = 0.0018f;
    float settle_velocity_output = 0.012f;
    float settle_acceleration_output = 0.20f;
};

inline Vec2 scene_clamp_magnitude(Vec2 value, float maximum)
{
    const float limit = std::max(maximum, 0.0f);
    const float magnitude = length(value);
    if (magnitude <= limit || magnitude <= 1.0e-8f)
        return value;
    return mul(value, limit / magnitude);
}

inline float scene_smoothstep01(float value)
{
    const float t = std::clamp(value, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

inline SceneMotionLimits scene_motion_limits(CameraMotionStyle style,
                                              float zoom,
                                              float pressure,
                                              float distance_output)
{
    const float p = scene_smoothstep01(pressure);
    const float z = std::clamp((std::max(zoom, 1.0f) - 2.60f) / 1.40f,
                               0.0f, 1.0f);
    const float d = scene_smoothstep01(
        std::clamp((distance_output - 0.04f) / 0.42f, 0.0f, 1.0f));
    const float urgency = std::clamp(std::max(p, 0.72f * d) + 0.18f * z,
                                     0.0f, 1.0f);

    SceneMotionLimits limits;
    switch (style) {
    case CameraMotionStyle::Responsive:
        limits.position_gain = 4.7f;
        limits.velocity_response_seconds = 0.090f - 0.018f * urgency;
        limits.max_speed_output = 3.15f + 2.25f * urgency;
        limits.max_acceleration_output = 19.0f + 13.0f * urgency;
        limits.max_jerk_output = 125.0f + 95.0f * urgency;
        break;
    case CameraMotionStyle::Cinematic:
        limits.position_gain = 3.5f;
        limits.velocity_response_seconds = 0.132f - 0.021f * urgency;
        limits.max_speed_output = 2.15f + 1.75f * urgency;
        limits.max_acceleration_output = 12.5f + 9.5f * urgency;
        limits.max_jerk_output = 78.0f + 70.0f * urgency;
        break;
    case CameraMotionStyle::Balanced:
    default:
        limits.position_gain = 4.2f;
        limits.velocity_response_seconds = 0.105f - 0.022f * urgency;
        limits.max_speed_output = 2.65f + 2.05f * urgency;
        limits.max_acceleration_output = 15.5f + 11.0f * urgency;
        limits.max_jerk_output = 95.0f + 82.0f * urgency;
        break;
    }

    limits.max_speed_output *= 1.0f + 0.08f * z;
    limits.max_acceleration_output *= 1.0f + 0.06f * z;
    return limits;
}

inline float scene_jerk_stopping_speed(float distance_output,
                                       float max_acceleration_output,
                                       float max_jerk_output,
                                       bool moving_target)
{
    const float distance = std::max(distance_output, 0.0f);
    const float acceleration = std::max(max_acceleration_output, 0.1f);
    const float jerk = std::max(max_jerk_output, 1.0f);

    const float jerk_safety = moving_target ? 0.78f : 0.24f;
    const float accel_safety = moving_target ? 0.82f : 0.52f;
    const float jerk_speed = std::pow(
        std::max(distance * std::sqrt(jerk) * jerk_safety, 0.0f),
        2.0f / 3.0f);
    const float acceleration_speed = accel_safety * std::sqrt(
        std::max(0.0f, 2.0f * acceleration * distance));
    return std::min(jerk_speed, acceleration_speed);
}

struct SceneMotionSample {
    Vec2 center{0.5f, 0.5f};
    Vec2 velocity{0.0f, 0.0f};
    Vec2 acceleration{0.0f, 0.0f};
    bool settled = true;
};

class SceneKinematicMotion {
public:
    void reset(Vec2 center = {0.5f, 0.5f})
    {
        center_ = center;
        velocity_ = {0.0f, 0.0f};
        acceleration_ = {0.0f, 0.0f};
        target_initialized_ = false;
        previous_target_ = center;
    }

    void adopt(Vec2 center, Vec2 velocity, Vec2 acceleration, float zoom)
    {
        center_ = clamp_center(center, std::max(zoom, 1.0f));
        velocity_ = velocity;
        acceleration_ = acceleration;
        target_initialized_ = false;
        previous_target_ = center_;
    }

    SceneMotionSample sample() const
    {
        return {center_, velocity_, acceleration_, false};
    }

    SceneMotionSample step(Vec2 target_center, float zoom, float dt,
                           const SceneMotionLimits &limits,
                           bool force_live_tracking = false)
    {
        const float safe_zoom = std::max(zoom, 1.0f);
        target_center = clamp_center(target_center, safe_zoom);
        const float target_motion_output = target_initialized_
            ? length(sub(target_center, previous_target_)) * safe_zoom
            : 0.0f;
        const bool moving_target = force_live_tracking ||
                                   target_motion_output > 0.0010f;
        previous_target_ = target_center;
        target_initialized_ = true;

        const float total_dt = std::clamp(dt, 0.0f, 0.10f);
        if (total_dt <= 1.0e-7f)
            return current_sample(target_center, safe_zoom, limits);

        constexpr float max_substep = 1.0f / 120.0f;
        const int steps = std::clamp(
            static_cast<int>(std::ceil(total_dt / max_substep)), 1, 12);
        const float h = total_dt / static_cast<float>(steps);
        for (int i = 0; i < steps; ++i)
            integrate_substep(target_center, safe_zoom, h, limits,
                              moving_target);

        SceneMotionSample result = current_sample(
            target_center, safe_zoom, limits);
        if (result.settled) {
            center_ = target_center;
            velocity_ = {0.0f, 0.0f};
            acceleration_ = {0.0f, 0.0f};
            result.center = center_;
            result.velocity = velocity_;
            result.acceleration = acceleration_;
        }
        return result;
    }

private:
    void integrate_substep(Vec2 target_center, float zoom, float dt,
                           const SceneMotionLimits &limits,
                           bool moving_target)
    {
        const Vec2 error_output = mul(sub(target_center, center_), zoom);
        const float distance_output = length(error_output);
        Vec2 velocity_output = mul(velocity_, zoom);
        Vec2 acceleration_output = mul(acceleration_, zoom);

        const Vec2 direction = distance_output > 1.0e-8f
            ? mul(error_output, 1.0f / distance_output)
            : Vec2{0.0f, 0.0f};

        const float proportional_speed =
            std::max(limits.position_gain, 0.1f) * distance_output;
        const float cruise_mix = scene_smoothstep01(
            std::clamp((distance_output - 0.045f) / 0.20f, 0.0f, 1.0f));
        const float cruise_fraction = moving_target ? 0.70f : 0.30f;
        const float cruise_speed =
            limits.max_speed_output * (cruise_fraction * cruise_mix);
        const float requested_speed = std::max(
            proportional_speed, cruise_speed);

        const float stopping_speed = scene_jerk_stopping_speed(
            distance_output, limits.max_acceleration_output,
            limits.max_jerk_output, moving_target);
        const float desired_speed = std::min(
            limits.max_speed_output,
            std::min(requested_speed, stopping_speed));
        const Vec2 desired_velocity = mul(direction, desired_speed);

        const float response =
            std::max(limits.velocity_response_seconds, 0.025f);
        Vec2 desired_acceleration = mul(
            sub(desired_velocity, velocity_output), 1.0f / response);
        desired_acceleration = scene_clamp_magnitude(
            desired_acceleration, limits.max_acceleration_output);

        const Vec2 acceleration_delta = sub(
            desired_acceleration, acceleration_output);
        acceleration_output = add(
            acceleration_output,
            scene_clamp_magnitude(
                acceleration_delta,
                std::max(limits.max_jerk_output, 1.0f) * dt));
        acceleration_output = scene_clamp_magnitude(
            acceleration_output, limits.max_acceleration_output);

        velocity_output = add(velocity_output,
                              mul(acceleration_output, dt));
        velocity_output = scene_clamp_magnitude(
            velocity_output, limits.max_speed_output);

        const Vec2 proposed = add(
            center_, mul(velocity_output, dt / zoom));
        const Vec2 clamped = clamp_center(proposed, zoom);
        if (std::fabs(clamped.x - proposed.x) > 1.0e-7f) {
            velocity_output.x = 0.0f;
            acceleration_output.x = 0.0f;
        }
        if (std::fabs(clamped.y - proposed.y) > 1.0e-7f) {
            velocity_output.y = 0.0f;
            acceleration_output.y = 0.0f;
        }

        center_ = clamped;
        velocity_ = mul(velocity_output, 1.0f / zoom);
        acceleration_ = mul(acceleration_output, 1.0f / zoom);
    }

    SceneMotionSample current_sample(Vec2 target_center, float zoom,
                                     const SceneMotionLimits &limits) const
    {
        const float position_error = length(sub(target_center, center_)) * zoom;
        const float speed = length(velocity_) * zoom;
        const float acceleration = length(acceleration_) * zoom;
        const bool settled =
            position_error <= limits.settle_position_output &&
            speed <= limits.settle_velocity_output &&
            acceleration <= limits.settle_acceleration_output;
        return {center_, velocity_, acceleration_, settled};
    }

    Vec2 center_{0.5f, 0.5f};
    Vec2 velocity_{0.0f, 0.0f};
    Vec2 acceleration_{0.0f, 0.0f};
    bool target_initialized_ = false;
    Vec2 previous_target_{0.5f, 0.5f};
};

} // namespace arzoom
