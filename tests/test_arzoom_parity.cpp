#include "presentation/arzoom_camera_adapter.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>

namespace {

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

bool nearly(float a, float b, float epsilon = 1.0e-6f)
{
    return std::fabs(a - b) <= epsilon;
}

bool same_output(
    const arzoom::CameraOutput &a,
    const arzoom::CameraOutput &b)
{
    return
        nearly(a.center.x, b.center.x) &&
        nearly(a.center.y, b.center.y) &&
        nearly(a.zoom, b.zoom) &&
        nearly(a.velocity.x, b.velocity.x) &&
        nearly(a.velocity.y, b.velocity.y) &&
        nearly(a.acceleration.x, b.acceleration.x) &&
        nearly(a.acceleration.y, b.acceleration.y) &&
        a.state == b.state &&
        nearly(a.intent_confidence, b.intent_confidence) &&
        nearly(a.urgency, b.urgency);
}

arzoom::Vec2 cursor_at(float seconds)
{
    if (seconds < 1.0f)
        return {0.50f, 0.50f};

    if (seconds < 2.4f) {
        const float t = (seconds - 1.0f) / 1.4f;
        return {
            0.20f + 0.58f * t,
            0.32f + 0.05f * std::sin(t * 6.2831853f)
        };
    }

    if (seconds < 3.8f) {
        const float t = seconds - 2.4f;
        return {
            0.78f + 0.035f * std::cos(t * 5.0f),
            0.37f + 0.028f * std::sin(t * 5.0f)
        };
    }

    if (seconds < 5.2f) {
        const float t = (seconds - 3.8f) / 1.4f;
        return {
            0.78f - 0.55f * t,
            0.37f + 0.38f * t
        };
    }

    return {0.23f, 0.75f};
}

bool zoom_requested_at(float seconds)
{
    return seconds >= 1.0f && seconds < 6.0f;
}

bool emphasis_at(int frame, int fps)
{
    const int first = fps;
    const int second = static_cast<int>(3.8f * static_cast<float>(fps));
    return frame == first || frame == second;
}

arzoom::CameraOutput run_adapter(int fps, float stop_seconds)
{
    arssyut::presentation::ArZoomCameraAdapter adapter;
    const int frames =
        static_cast<int>(stop_seconds * static_cast<float>(fps));

    arzoom::CameraOutput output{};
    for (int frame = 0; frame < frames; ++frame) {
        const float seconds =
            static_cast<float>(frame) / static_cast<float>(fps);

        arssyut::presentation::ArZoomCameraIntent intent;
        intent.dt = 1.0f / static_cast<float>(fps);
        intent.cursor = cursor_at(seconds);
        intent.cursor_valid = true;
        intent.zoom_requested = zoom_requested_at(seconds);
        intent.configured_zoom = 2.0f;
        intent.emphasis_event = emphasis_at(frame, fps);

        output = adapter.step(intent);
    }
    return output;
}

void test_exact_upstream_mapping(TestContext &test)
{
    arssyut::presentation::ArZoomCameraAdapter adapter;

    arzoom::PresenterAwareSmartCamera upstream;
    upstream.reset();
    upstream.set_scene_context(false);

    bool exact = true;
    bool cinematic_differs_from_balanced = false;

    arzoom::PresenterAwareSmartCamera balanced;
    balanced.reset();
    balanced.set_scene_context(false);

    constexpr int fps = 60;
    constexpr int frames = 480;

    for (int frame = 0; frame < frames; ++frame) {
        const float seconds =
            static_cast<float>(frame) / static_cast<float>(fps);
        const auto cursor = cursor_at(seconds);
        const bool zoom = zoom_requested_at(seconds);
        const bool emphasis = emphasis_at(frame, fps);

        arssyut::presentation::ArZoomCameraIntent intent;
        intent.dt = 1.0f / static_cast<float>(fps);
        intent.cursor = cursor;
        intent.cursor_valid = true;
        intent.zoom_requested = zoom;
        intent.configured_zoom = 2.0f;
        intent.emphasis_event = emphasis;

        arzoom::CameraInput canonical;
        canonical.dt = intent.dt;
        canonical.cursor = intent.cursor;
        canonical.cursor_valid = intent.cursor_valid;
        canonical.zoom_requested = intent.zoom_requested;
        canonical.configured_zoom = intent.configured_zoom;
        canonical.anchor = {0.5f, 0.45f};
        canonical.safe_zone = 0.28f;
        canonical.follow_policy = arzoom::CameraFollowPolicy::Smart;
        canonical.motion_style = arzoom::CameraMotionStyle::Cinematic;
        canonical.emphasis_event = intent.emphasis_event;

        const auto adapter_output = adapter.step(intent);
        const auto upstream_output = upstream.step(canonical);

        if (!same_output(adapter_output, upstream_output)) {
            exact = false;
            break;
        }

        arzoom::CameraInput balanced_input = canonical;
        balanced_input.motion_style = arzoom::CameraMotionStyle::Balanced;
        const auto balanced_output = balanced.step(balanced_input);

        if (std::fabs(adapter_output.zoom - balanced_output.zoom) > 0.002f ||
            std::fabs(adapter_output.center.x - balanced_output.center.x) > 0.002f ||
            std::fabs(adapter_output.center.y - balanced_output.center.y) > 0.002f) {
            cinematic_differs_from_balanced = true;
        }
    }

    test.expect(
        exact,
        "Arssyut adapter is frame-exact with pinned upstream per-source ArZoom");
    test.expect(
        cinematic_differs_from_balanced,
        "Arssyut parity profile is Cinematic rather than Balanced");
}

void test_frame_rate_stability(TestContext &test)
{
    const auto at_30 = run_adapter(30, 4.8f);
    const auto at_60 = run_adapter(60, 4.8f);
    const auto at_120 = run_adapter(120, 4.8f);

    const auto close_pair =
        [](const arzoom::CameraOutput &a,
           const arzoom::CameraOutput &b) {
            return
                std::fabs(a.center.x - b.center.x) < 0.020f &&
                std::fabs(a.center.y - b.center.y) < 0.020f &&
                std::fabs(a.zoom - b.zoom) < 0.010f;
        };

    test.expect(
        close_pair(at_30, at_60),
        "ArZoom camera remains materially equivalent at 30 vs 60 fps");
    test.expect(
        close_pair(at_60, at_120),
        "ArZoom camera remains materially equivalent at 60 vs 120 fps");
}

void test_return_to_full_frame(TestContext &test)
{
    const auto output = run_adapter(60, 7.5f);

    test.expect(
        std::fabs(output.zoom - 1.0f) < 0.002f,
        "ArZoom Cinematic return settles to full-frame zoom");
    test.expect(
        std::fabs(output.center.x - 0.5f) < 0.002f &&
        std::fabs(output.center.y - 0.5f) < 0.002f,
        "ArZoom Cinematic return settles to full-frame center");
}

} // namespace

int main()
{
    TestContext test;

    test_exact_upstream_mapping(test);
    test_frame_rate_stability(test);
    test_return_to_full_frame(test);

    if (test.failures != 0) {
        std::cerr
            << test.failures << " of "
            << test.checks
            << " ArZoom parity checks failed\n";
        return 1;
    }

    std::cout
        << "PASS: " << test.checks
        << " ArZoom parity checks\n";
    return 0;
}
