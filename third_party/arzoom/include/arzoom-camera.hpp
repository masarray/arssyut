#pragma once

#include "arzoom-smart-zone-camera.hpp"
#include "arzoom-scene-viewport-planner.hpp"

namespace arzoom {

/*
 * Camera facade
 * =============
 *
 * Per-source ArZoom keeps the accepted P1 SmartCamera unchanged.
 * Managed Scene Camera opts into the deterministic SceneViewportPlanner.
 * Only one engine is active for a given filter instance, so there is no hidden
 * synchronization tail and no competing camera authority.
 */
class PresenterAwareSmartCamera {
public:
    PresenterAwareSmartCamera() { reset(); }

    CameraOutput output() const
    {
        return scene_context_enabled_ ? scene_.output() : legacy_.output();
    }

    void set_scene_context(bool enabled)
    {
        if (scene_context_enabled_ == enabled)
            return;
        scene_context_enabled_ = enabled;
        if (enabled)
            scene_.reset();
        else
            legacy_.reset();
    }

    void reset()
    {
        legacy_.reset();
        scene_.reset();
        scene_context_enabled_ = false;
    }

    CameraOutput step(const CameraInput &input)
    {
        return scene_context_enabled_ ? scene_.step(input)
                                      : legacy_.step(input);
    }

private:
    SmartCamera legacy_;
    SceneViewportPlanner scene_;
    bool scene_context_enabled_ = false;
};

} // namespace arzoom

/* arzoom-filter-v2.cpp historically names its field `arzoom::SmartCamera`.
 * Keep that source layout stable while routing managed Scene Camera through the
 * facade above. The macro is intentionally defined only after the real P1
 * SmartCamera has been used as a member of PresenterAwareSmartCamera. */
#define SmartCamera PresenterAwareSmartCamera
