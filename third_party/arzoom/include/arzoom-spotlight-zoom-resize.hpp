#pragma once

#include <algorithm>
#include <cmath>

namespace arzoom {

/*
 * Resize-only Spotlight coupling for Zoom +/-
 * ===========================================
 *
 * Toggle Zoom owns the cinematic full-frame <-> focus choreography. Changing
 * the configured zoom level while that Zoom session is already active is a
 * different presenter intent: the camera, Presentation Cursor and Spotlight
 * should only resize smoothly toward the new magnification.
 *
 * This state never changes camera intent. It observes configured/current zoom
 * and produces one bounded multiplicative Spotlight scale. The scale follows
 * actual live camera progress, so there is no second motion planner.
 */
struct SpotlightZoomResizeState {
    bool session_active = false;
    bool resizing = false;

    float baseline_configured_zoom = 1.0f;
    float observed_configured_zoom = 1.0f;

    float scale = 1.0f;
    float start_scale = 1.0f;
    float start_live_zoom = 1.0f;
    float target_live_zoom = 1.0f;
    float target_scale = 1.0f;

    void reset()
    {
        session_active = false;
        resizing = false;
        baseline_configured_zoom = 1.0f;
        observed_configured_zoom = 1.0f;
        scale = 1.0f;
        start_scale = 1.0f;
        start_live_zoom = 1.0f;
        target_live_zoom = 1.0f;
        target_scale = 1.0f;
    }

    void observe(bool zoom_requested, float configured_zoom, float live_zoom)
    {
        const float configured = std::max(configured_zoom, 1.0f);
        const float live = std::max(live_zoom, 1.0f);

        if (!zoom_requested) {
            reset();
            return;
        }

        if (!session_active) {
            session_active = true;
            baseline_configured_zoom = configured;
            observed_configured_zoom = configured;
            scale = 1.0f;
            resizing = false;
            return;
        }

        if (std::fabs(configured - observed_configured_zoom) <= 1.0e-4f)
            return;

        /* A new Zoom +/- command starts exactly from the current visual scale
         * and the current camera magnification. The final scale is always
         * relative to the Zoom level that began this Toggle-Zoom session. */
        start_scale = scale;
        start_live_zoom = live;
        target_live_zoom = configured;
        target_scale = std::clamp(
            configured / std::max(baseline_configured_zoom, 1.0f),
            0.35f, 4.0f);
        observed_configured_zoom = configured;
        resizing = true;
    }

    void step(float live_zoom)
    {
        if (!session_active || !resizing)
            return;

        const float live = std::max(live_zoom, 1.0f);
        const float delta = target_live_zoom - start_live_zoom;
        if (std::fabs(delta) <= 1.0e-4f) {
            scale = target_scale;
            resizing = false;
            return;
        }

        const float progress = std::clamp(
            (live - start_live_zoom) / delta, 0.0f, 1.0f);
        scale = start_scale + (target_scale - start_scale) * progress;

        if (progress >= 0.9995f) {
            scale = target_scale;
            resizing = false;
        }
    }
};

} // namespace arzoom
