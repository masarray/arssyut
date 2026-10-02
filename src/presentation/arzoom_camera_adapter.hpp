#pragma once

#include "arzoom-camera.hpp"
#ifdef SmartCamera
#undef SmartCamera
#endif

namespace arssyut::presentation {

struct ArZoomCameraIntent {
    float dt = 1.0f / 60.0f;
    arzoom::Vec2 cursor{0.5f, 0.5f};
    bool cursor_valid = false;
    bool zoom_requested = false;
    float configured_zoom = 2.0f;
    bool emphasis_event = false;
};

class ArZoomCameraAdapter final {
public:
    ArZoomCameraAdapter() noexcept
    {
        reset();
    }

    void reset() noexcept
    {
        camera_.reset();
        // Per-source ArZoom in OBS uses the accepted SmartCamera/gimbal path.
        // SceneViewportPlanner is reserved for managed Scene Camera.
        camera_.set_scene_context(false);
    }

    [[nodiscard]] arzoom::CameraOutput step(
        const ArZoomCameraIntent &intent) noexcept
    {
        arzoom::CameraInput input;
        input.dt = intent.dt;
        input.cursor = intent.cursor;
        input.cursor_valid = intent.cursor_valid;
        input.zoom_requested = intent.zoom_requested;
        input.configured_zoom = intent.configured_zoom;
        input.anchor = {0.5f, 0.45f};
        input.safe_zone = 0.28f;
        input.follow_policy = arzoom::CameraFollowPolicy::Smart;
        input.motion_style = arzoom::CameraMotionStyle::Cinematic;
        input.emphasis_event = intent.emphasis_event;
        return camera_.step(input);
    }

    [[nodiscard]] arzoom::CameraOutput output() const noexcept
    {
        return camera_.output();
    }

private:
    arzoom::PresenterAwareSmartCamera camera_;
};

} // namespace arssyut::presentation
