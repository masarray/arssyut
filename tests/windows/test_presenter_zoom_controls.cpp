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
step_frames(
    PresentationController &controller,
    int frames,
    std::int64_t &ticks)
{
    arssyut::presentation::PresentationFrameState state{};

    for (int i = 0; i < frames; ++i) {
        ticks +=
            MonotonicClock::ticks_per_second / 60;

        const TimePoint now{ticks};
        state =
            controller.step(
                1.0f / 60.0f,
                0.72f,
                0.42f,
                true,
                now,
                now);
    }

    return state;
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

} // namespace

int main()
{
    TestContext test;

    test_toggle_and_reset(test);
    test_zoom_step_and_bounds(test);

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
