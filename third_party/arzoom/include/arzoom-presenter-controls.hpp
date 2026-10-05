#pragma once

#include "arzoom-smart-zone-camera.hpp"

#include <algorithm>

namespace arzoom {

inline bool presenter_zoom_requested(bool latched_zoom, bool hold_zoom)
{
    return latched_zoom || hold_zoom;
}

inline float presenter_zoom_step(float current_zoom, float delta)
{
    return std::clamp(current_zoom + delta, 1.10f, 4.00f);
}

enum class OverviewPhase {
    Inactive,
    ToOverview,
    Holding,
    ToShot,
    CancelToOverview,
};

struct OverviewOutput {
    Vec2 center{0.5f, 0.5f};
    float zoom = 1.0f;
    OverviewPhase phase = OverviewPhase::Inactive;
    bool active = false;
    bool restored = false;
    bool cancelled = false;
};

/*
 * Momentary presenter overview transition.
 *
 * The controller intentionally owns only an affine render transform. The
 * Smart Zone camera can be paused while this transition is visible, so cursor
 * movement during the peek cannot retarget the saved shot. Both directions
 * interpolate scale + screen offset with the same quintic minimum-jerk scalar,
 * preserving the straight screen-space motion contract established in P1.
 */
class OverviewPeekController {
public:
    bool begin(Vec2 center, float zoom)
    {
        if (active() || zoom <= 1.0005f)
            return false;

        saved_transform_ = screen_transform(center, zoom);
        transition_start_ = saved_transform_;
        transition_target_ = screen_transform({0.5f, 0.5f}, 1.0f);
        current_transform_ = saved_transform_;
        elapsed_ = 0.0f;
        phase_ = OverviewPhase::ToOverview;
        return true;
    }

    void release(Vec2 current_center, float current_zoom)
    {
        if (!active() || phase_ == OverviewPhase::ToShot ||
            phase_ == OverviewPhase::CancelToOverview)
            return;

        transition_start_ = screen_transform(current_center, current_zoom);
        transition_target_ = saved_transform_;
        current_transform_ = transition_start_;
        elapsed_ = 0.0f;
        phase_ = OverviewPhase::ToShot;
    }

    void cancel_to_overview(Vec2 current_center, float current_zoom)
    {
        if (!active())
            return;

        transition_start_ = screen_transform(current_center, current_zoom);
        transition_target_ = screen_transform({0.5f, 0.5f}, 1.0f);
        current_transform_ = transition_start_;
        elapsed_ = 0.0f;
        phase_ = OverviewPhase::CancelToOverview;
    }

    void reset()
    {
        phase_ = OverviewPhase::Inactive;
        elapsed_ = 0.0f;
        saved_transform_ = {};
        transition_start_ = {};
        transition_target_ = {};
        current_transform_ = {};
    }

    bool active() const { return phase_ != OverviewPhase::Inactive; }
    OverviewPhase phase() const { return phase_; }

    ScreenTransform saved_transform() const { return saved_transform_; }

    OverviewOutput step(float dt, float to_overview_seconds,
                        float to_shot_seconds)
    {
        OverviewOutput output;
        if (!active())
            return output;

        const float safe_dt = std::clamp(dt, 0.0f, 0.10f);
        bool restored = false;
        bool cancelled = false;

        if (phase_ == OverviewPhase::Holding) {
            current_transform_ = screen_transform({0.5f, 0.5f}, 1.0f);
        } else {
            elapsed_ += safe_dt;
            const bool toward_overview =
                phase_ == OverviewPhase::ToOverview ||
                phase_ == OverviewPhase::CancelToOverview;
            const float duration = std::max(
                toward_overview ? to_overview_seconds : to_shot_seconds,
                0.05f);
            const float normalized_time =
                std::clamp(elapsed_ / duration, 0.0f, 1.0f);
            const float progress = minimum_jerk01(normalized_time);
            current_transform_ = lerp_transform(
                transition_start_, transition_target_, progress);

            if (normalized_time >= 1.0f) {
                current_transform_ = transition_target_;
                if (phase_ == OverviewPhase::ToOverview) {
                    phase_ = OverviewPhase::Holding;
                } else if (phase_ == OverviewPhase::ToShot) {
                    phase_ = OverviewPhase::Inactive;
                    restored = true;
                } else if (phase_ == OverviewPhase::CancelToOverview) {
                    phase_ = OverviewPhase::Inactive;
                    cancelled = true;
                }
            }
        }

        output.zoom = std::max(current_transform_.scale, 1.0f);
        output.center = clamp_center(
            transform_center(current_transform_), output.zoom);
        output.phase = phase_;
        output.active = active();
        output.restored = restored;
        output.cancelled = cancelled;
        return output;
    }

private:
    OverviewPhase phase_ = OverviewPhase::Inactive;
    float elapsed_ = 0.0f;
    ScreenTransform saved_transform_{};
    ScreenTransform transition_start_{};
    ScreenTransform transition_target_{};
    ScreenTransform current_transform_{};
};

} // namespace arzoom
