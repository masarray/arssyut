#include "arzoom-spotlight-zoom-resize.hpp"

#include <cassert>
#include <cmath>
#include <iostream>

namespace {
bool near(float a, float b, float eps = 1.0e-3f)
{
    return std::fabs(a - b) <= eps;
}
}

int main()
{
    using arzoom::SpotlightZoomResizeState;

    SpotlightZoomResizeState state;

    /* Toggle Zoom begins a cinematic session but must not resize the configured
     * working Spotlight merely because the camera is still travelling to its
     * initial target. */
    state.observe(true, 2.0f, 1.0f);
    state.step(1.5f);
    assert(near(state.scale, 1.0f));
    assert(!state.resizing);

    /* Increase Zoom is resize-only: no reset/full-frame choreography. Scale
     * starts exactly where it already is and follows live camera progress. */
    state.observe(true, 2.5f, 2.0f);
    assert(state.resizing);
    assert(near(state.scale, 1.0f));
    state.step(2.25f);
    assert(state.scale > 1.0f && state.scale < 1.25f);
    const float midway_up = state.scale;
    state.step(2.5f);
    assert(near(state.scale, 1.25f));
    assert(!state.resizing);

    /* Decrease Zoom mirrors the same path and stays continuous. */
    state.observe(true, 1.5f, 2.5f);
    assert(state.resizing);
    assert(near(state.scale, 1.25f));
    state.step(2.0f);
    assert(state.scale < 1.25f && state.scale > 0.75f);
    assert(state.scale < midway_up + 0.30f);
    state.step(1.5f);
    assert(near(state.scale, 0.75f));

    /* Rapid retargeting must continue from the exact current visual scale. */
    state.observe(true, 2.25f, 1.5f);
    state.step(1.75f);
    const float before_retarget = state.scale;
    state.observe(true, 2.75f, 1.75f);
    assert(near(state.scale, before_retarget));
    state.step(2.75f);
    assert(near(state.scale, 1.375f));

    /* Toggle Zoom OFF ends the resize session and restores neutral scale. */
    state.observe(false, 2.75f, 2.75f);
    assert(!state.session_active);
    assert(!state.resizing);
    assert(near(state.scale, 1.0f));

    std::cout << "P5 Spotlight Zoom +/- resize-only invariants passed\n";
    return 0;
}
