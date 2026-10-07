#include "arzoom-cinematic-spotlight.hpp"

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
    using namespace arzoom;

    assert(near(cinematic_minimum_jerk(0.0f), 0.0f));
    assert(near(cinematic_minimum_jerk(1.0f), 1.0f));
    assert(cinematic_minimum_jerk(0.25f) < cinematic_minimum_jerk(0.50f));
    assert(cinematic_minimum_jerk(0.50f) < cinematic_minimum_jerk(0.75f));

    /* Actual-center radius must cover every corner, including edge focus. */
    const float centered = cinematic_full_radius_px(
        1920.0f, 1080.0f, 0.5f, 0.5f, 40.0f);
    const float corner = cinematic_full_radius_px(
        1920.0f, 1080.0f, 0.02f, 0.02f, 40.0f);
    assert(centered > 1100.0f);
    assert(corner > centered);

    const float full_percent = cinematic_full_area_percent(162.0f, centered);
    assert(full_percent > 600.0f);
    assert(full_percent < 2000.0f);

    assert(near(cinematic_dim_mix(0.0f), 0.0f));
    assert(near(cinematic_dim_mix(0.12f), 0.0f));
    assert(cinematic_dim_mix(0.50f) > 0.0f);
    assert(near(cinematic_dim_mix(1.0f), 1.0f));

    CinematicSpotlightState state;
    state.reset();
    state.set_target(true, CinematicFocusSpeed::Balanced);
    float previous = state.value;
    for (int i = 0; i < 60; ++i) {
        state.step(1.0f / 60.0f);
        assert(state.value + 1.0e-5f >= previous);
        previous = state.value;
    }
    assert(near(state.value, 1.0f));

    /* Mid-close reversal must start from current state, never jump to an end. */
    state.reset();
    state.set_target(true, CinematicFocusSpeed::Balanced);
    for (int i = 0; i < 8; ++i)
        state.step(1.0f / 60.0f);
    const float before_reverse = state.value;
    assert(before_reverse > 0.0f && before_reverse < 1.0f);
    state.set_target(false, CinematicFocusSpeed::Balanced);
    assert(near(state.value, before_reverse));
    state.step(1.0f / 60.0f);
    assert(state.value <= before_reverse + 1.0e-5f);

    /* Frame-rate independence: equal wall time converges to same endpoint. */
    CinematicSpotlightState at30;
    CinematicSpotlightState at144;
    at30.set_target(true, CinematicFocusSpeed::Balanced);
    at144.set_target(true, CinematicFocusSpeed::Balanced);
    for (int i = 0; i < 30; ++i)
        at30.step(1.0f / 30.0f);
    for (int i = 0; i < 144; ++i)
        at144.step(1.0f / 144.0f);
    assert(near(at30.value, 1.0f));
    assert(near(at144.value, 1.0f));

    assert(cinematic_close_duration(CinematicFocusSpeed::Smooth) >
           cinematic_close_duration(CinematicFocusSpeed::Balanced));
    assert(cinematic_close_duration(CinematicFocusSpeed::Balanced) >
           cinematic_close_duration(CinematicFocusSpeed::Snappy));

    std::cout << "P5 cinematic Spotlight choreography invariants passed\n";
    return 0;
}
