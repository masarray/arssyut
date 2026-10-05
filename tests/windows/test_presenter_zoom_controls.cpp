#include "presentation/presentation_controller.hpp"

#include <cmath>
#include <iostream>

namespace {

using arssyut::core::MonotonicClock;
using arssyut::core::TimePoint;
using arssyut::presentation::PresentationController;
using arssyut::presentation::PresentationSettings;

struct TestContext {
    int checks = 0;
    int failures = 0;

    void expect(bool condition, const char *message)
    {
        ++checks;
        if (!condition) {
            ++failures;
            std::cerr << "FAIL: " << message << '\n';
        }
    }
};

arssyut::presentation::PresentationFrameState
step_frames_at(
    PresentationController &controller,
    int frames,
    std::int64_t &ticks,
    float cursor_x,
    float cursor_y,
    bool cursor_valid = true)
{
    arssyut::presentation::PresentationFrameState state{};

    for (int i = 0; i < frames; ++i) {
        ticks +=
            MonotonicClock::ticks_per_second / 60;

        const TimePoint now{ticks};
        state =
            controller.step(
                1.0f / 60.0f,
                cursor_x,
                cursor_y,
                cursor_valid,
                now,
                now);
    }

    return state;
}

arssyut::presentation::PresentationFrameState
step_frames(
    PresentationController &controller,
    int frames,
    std::int64_t &ticks)
{
    return step_frames_at(
        controller,
        frames,
        ticks,
        0.72f,
        0.42f);
}

void test_toggle_and_reset(TestContext &test)
{
    PresentationController controller;
    PresentationSettings settings;
    settings.smart_zoom = false;
    settings.presenter_controls = true;
    settings.zoom = 2.0f;

    controller.reset();
    controller.set_settings(settings);

    std::int64_t ticks = 0;
    auto state =
        step_frames(controller, 30, ticks);

    test.expect(
        std::fabs(state.camera_zoom - 1.0f) < 0.01f,
        "manual presenter controls begin at full frame");

    controller.toggle_manual_zoom();
    state =
        step_frames(controller, 120, ticks);

    test.expect(
        controller.manual_zoom_latched(),
        "Toggle Zoom latches presenter zoom");
    test.expect(
        state.camera_zoom > 1.25f,
        "manual zoom works even when Smart Zoom is disabled");

    controller.reset_full_frame();
    state =
        step_frames(controller, 180, ticks);

    test.expect(
        !controller.manual_zoom_latched(),
        "Reset / Full Frame clears manual zoom latch");
    test.expect(
        std::fabs(state.camera_zoom - 1.0f) < 0.02f,
        "Reset / Full Frame returns camera to full frame");
}

void test_zoom_step_and_bounds(TestContext &test)
{
    PresentationController controller;
    PresentationSettings settings;
    settings.smart_zoom = false;
    settings.presenter_controls = true;
    settings.zoom = 2.0f;

    controller.reset();
    controller.set_settings(settings);

    controller.adjust_manual_zoom(0.25f);
    controller.adjust_manual_zoom(0.25f);

    test.expect(
        std::fabs(controller.configured_zoom() - 2.5f) < 0.0001f,
        "ArZoom Zoom In uses 0.25x presenter steps");

    for (int i = 0; i < 20; ++i)
        controller.adjust_manual_zoom(0.25f);

    test.expect(
        std::fabs(controller.configured_zoom() - 4.0f) < 0.0001f,
        "presenter zoom clamps at ArZoom 4.00x maximum");

    for (int i = 0; i < 40; ++i)
        controller.adjust_manual_zoom(-0.25f);

    test.expect(
        std::fabs(controller.configured_zoom() - 1.10f) < 0.0001f,
        "presenter zoom clamps at ArZoom 1.10x minimum");

    controller.adjust_manual_zoom(0.90f);
    const float remembered =
        controller.configured_zoom();

    controller.toggle_manual_zoom();
    controller.reset_full_frame();

    test.expect(
        std::fabs(controller.configured_zoom() - remembered) < 0.0001f,
        "Reset / Full Frame preserves configured zoom amount");
}


void test_hold_zoom_press_release(TestContext &test)
{
    PresentationController controller;
    PresentationSettings settings;
    settings.smart_zoom = false;
    settings.presenter_controls = true;
    settings.zoom = 2.0f;

    controller.reset();
    controller.set_settings(settings);

    std::int64_t ticks = 0;
    controller.set_hold_zoom(true);

    auto state =
        step_frames(controller, 120, ticks);

    test.expect(
        state.camera_zoom > 1.25f,
        "Hold Zoom key-down requests the existing ArZoom camera");

    controller.set_hold_zoom(false);
    state =
        step_frames(controller, 180, ticks);

    test.expect(
        std::fabs(state.camera_zoom - 1.0f) < 0.02f,
        "Hold Zoom key-up returns smoothly to full frame");
}

void test_overview_peek_saved_shot(TestContext &test)
{
    PresentationController controller;
    PresentationSettings settings;
    settings.smart_zoom = false;
    settings.presenter_controls = true;
    settings.zoom = 2.25f;

    controller.reset();
    controller.set_settings(settings);
    controller.toggle_manual_zoom();

    std::int64_t ticks = 0;
    auto state =
        step_frames_at(
            controller,
            160,
            ticks,
            0.78f,
            0.38f);

    const float saved_x =
        state.camera_center_x;
    const float saved_y =
        state.camera_center_y;
    const float saved_zoom =
        state.camera_zoom;

    test.expect(
        saved_zoom > 1.5f,
        "Overview Peek test begins from a real zoomed shot");

    controller.set_overview_peek(true);
    state =
        step_frames_at(
            controller,
            60,
            ticks,
            0.10f,
            0.90f);

    test.expect(
        controller.overview_active(),
        "Overview Peek remains active while its chord is held");
    test.expect(
        std::fabs(state.camera_zoom - 1.0f) < 0.02f &&
        std::fabs(state.camera_center_x - 0.5f) < 0.02f &&
        std::fabs(state.camera_center_y - 0.5f) < 0.02f,
        "Overview Peek holds a centered full-frame overview");

    // Move the pointer while peeking. The underlying camera must stay paused,
    // so release restores the saved shot rather than the new cursor target.
    controller.set_overview_peek(false);

    for (int i = 0;
         i < 120 && controller.overview_active();
         ++i) {
        state =
            step_frames_at(
                controller,
                1,
                ticks,
                0.12f,
                0.88f);
    }

    test.expect(
        !controller.overview_active(),
        "Overview Peek key-up completes the return transition");
    test.expect(
        std::fabs(state.camera_zoom - saved_zoom) < 0.03f &&
        std::fabs(state.camera_center_x - saved_x) < 0.03f &&
        std::fabs(state.camera_center_y - saved_y) < 0.03f,
        "Overview Peek restores the saved shot without cursor retargeting");
}

void test_overview_cancel_when_zoom_intent_ends(TestContext &test)
{
    PresentationController controller;
    PresentationSettings settings;
    settings.smart_zoom = false;
    settings.presenter_controls = true;
    settings.zoom = 2.0f;

    controller.reset();
    controller.set_settings(settings);

    std::int64_t ticks = 0;
    controller.set_hold_zoom(true);
    auto state =
        step_frames(controller, 120, ticks);

    controller.set_overview_peek(true);
    state =
        step_frames(controller, 24, ticks);

    test.expect(
        controller.overview_active(),
        "Overview Peek begins while Hold Zoom owns zoom intent");

    // Releasing Hold while still holding Peek matches upstream ArZoom's
    // cancel-to-overview path: return to full frame rather than restoring a
    // shot whose zoom intent no longer exists.
    controller.set_hold_zoom(false);

    for (int i = 0;
         i < 120 && controller.overview_active();
         ++i) {
        state =
            step_frames(controller, 1, ticks);
    }

    test.expect(
        !controller.overview_active(),
        "Overview Peek cancels when underlying zoom intent ends");
    test.expect(
        std::fabs(state.camera_zoom - 1.0f) < 0.02f,
        "cancelled Overview Peek ends at full frame");
}

} // namespace

int main()
{
    TestContext test;

    test_toggle_and_reset(test);
    test_zoom_step_and_bounds(test);
    test_hold_zoom_press_release(test);
    test_overview_peek_saved_shot(test);
    test_overview_cancel_when_zoom_intent_ends(test);

    if (test.failures != 0) {
        std::cerr
            << test.failures << " of "
            << test.checks
            << " presenter zoom checks failed\n";
        return 1;
    }

    std::cout
        << "PASS: " << test.checks
        << " presenter zoom checks\n";
    return 0;
}
