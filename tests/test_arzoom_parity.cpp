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


float output_speed(const arzoom::CameraOutput &output)
{
    return
        std::sqrt(
            output.velocity.x * output.velocity.x +
            output.velocity.y * output.velocity.y) *
        std::max(output.zoom, 1.0f);
}

float output_acceleration(const arzoom::CameraOutput &output)
{
    return
        std::sqrt(
            output.acceleration.x * output.acceleration.x +
            output.acceleration.y * output.acceleration.y) *
        std::max(output.zoom, 1.0f);
}

void settle_zoomed_camera(
    arssyut::presentation::ArZoomCameraAdapter &adapter,
    float cursor_x,
    float cursor_y,
    int fps = 60)
{
    const int frames =
        static_cast<int>(1.8f * static_cast<float>(fps));

    for (int frame = 0; frame < frames; ++frame) {
        arssyut::presentation::ArZoomCameraIntent intent;
        intent.dt = 1.0f / static_cast<float>(fps);
        intent.cursor = {cursor_x, cursor_y};
        intent.cursor_valid = true;
        intent.zoom_requested = true;
        intent.configured_zoom = 2.0f;
        intent.emphasis_event = frame == 0;
        (void)adapter.step(intent);
    }
}

void test_smart_zone_local_motion_reaches_exact_hold(TestContext &test)
{
    arssyut::presentation::ArZoomCameraAdapter adapter;
    settle_zoomed_camera(adapter, 0.67f, 0.43f);

    const auto stable = adapter.output();
    bool pixel_stable = true;
    bool zero_motion = true;

    constexpr int fps = 60;
    constexpr int frames = 180;

    for (int frame = 0; frame < frames; ++frame) {
        const float phase =
            static_cast<float>(frame) * 0.18f;

        arssyut::presentation::ArZoomCameraIntent intent;
        intent.dt = 1.0f / static_cast<float>(fps);
        intent.cursor = {
            0.67f + 0.018f * std::cos(phase),
            0.43f + 0.014f * std::sin(phase)
        };
        intent.cursor_valid = true;
        intent.zoom_requested = true;
        intent.configured_zoom = 2.0f;

        const auto output = adapter.step(intent);
        pixel_stable =
            pixel_stable &&
            nearly(output.center.x, stable.center.x, 1.0e-6f) &&
            nearly(output.center.y, stable.center.y, 1.0e-6f);
        zero_motion =
            zero_motion &&
            output_speed(output) < 1.0e-6f &&
            output_acceleration(output) < 1.0e-6f;
    }

    test.expect(
        pixel_stable,
        "Smart Zone local pointer gestures produce zero camera relocation");
    test.expect(
        zero_motion,
        "SmoothIdle reaches exact zero velocity/acceleration without drift");
}

void test_remote_move_uses_bounded_catchup_and_smooth_settle(
    TestContext &test)
{
    arssyut::presentation::ArZoomCameraAdapter adapter;
    settle_zoomed_camera(adapter, 0.24f, 0.36f);

    constexpr int fps = 60;
    constexpr float dt = 1.0f / static_cast<float>(fps);
    constexpr int frames = 240;

    const auto start = adapter.output();
    float max_speed = 0.0f;
    float max_acceleration = 0.0f;
    float max_jerk = 0.0f;
    float previous_acceleration_x =
        start.acceleration.x;
    float previous_acceleration_y =
        start.acceleration.y;
    bool saw_follow = false;
    bool saw_catchup = false;
    bool finite = true;
    int velocity_direction_reversals = 0;
    arzoom::Vec2 previous_velocity =
        start.velocity;

    arzoom::CameraOutput output = start;
    for (int frame = 0; frame < frames; ++frame) {
        arssyut::presentation::ArZoomCameraIntent intent;
        intent.dt = dt;
        intent.cursor = {0.86f, 0.72f};
        intent.cursor_valid = true;
        intent.zoom_requested = true;
        intent.configured_zoom = 2.0f;
        intent.emphasis_event = frame == 0;

        output = adapter.step(intent);

        const float speed =
            output_speed(output);
        const float acceleration =
            output_acceleration(output);
        const float jerk_x =
            (output.acceleration.x -
             previous_acceleration_x) /
            dt;
        const float jerk_y =
            (output.acceleration.y -
             previous_acceleration_y) /
            dt;
        const float jerk =
            std::sqrt(
                jerk_x * jerk_x +
                jerk_y * jerk_y) *
            std::max(output.zoom, 1.0f);

        max_speed =
            std::max(max_speed, speed);
        max_acceleration =
            std::max(max_acceleration, acceleration);
        max_jerk =
            std::max(max_jerk, jerk);

        finite =
            finite &&
            std::isfinite(output.center.x) &&
            std::isfinite(output.center.y) &&
            std::isfinite(output.zoom) &&
            std::isfinite(speed) &&
            std::isfinite(acceleration) &&
            std::isfinite(jerk);

        saw_follow =
            saw_follow ||
            output.state == arzoom::CameraState::Follow;
        saw_catchup =
            saw_catchup ||
            output.state == arzoom::CameraState::CatchUp;

        const float previous_len =
            std::sqrt(
                previous_velocity.x * previous_velocity.x +
                previous_velocity.y * previous_velocity.y);
        const float current_len =
            std::sqrt(
                output.velocity.x * output.velocity.x +
                output.velocity.y * output.velocity.y);
        if (previous_len > 0.001f &&
            current_len > 0.001f &&
            (previous_velocity.x * output.velocity.x +
             previous_velocity.y * output.velocity.y) < 0.0f) {
            ++velocity_direction_reversals;
        }

        previous_velocity = output.velocity;
        previous_acceleration_x =
            output.acceleration.x;
        previous_acceleration_y =
            output.acceleration.y;
    }

    const float relocation =
        std::sqrt(
            (output.center.x - start.center.x) *
                (output.center.x - start.center.x) +
            (output.center.y - start.center.y) *
                (output.center.y - start.center.y));

    test.expect(
        finite,
        "remote SmartCamera trace remains finite");
    test.expect(
        saw_follow || saw_catchup,
        "remote pointer move enters the existing follow/catch-up authority");
    test.expect(
        relocation > 0.08f,
        "remote pointer move relocates framing enough to remain catchable");
    test.expect(
        max_speed > 0.03f &&
        max_speed < 8.0f,
        "catch-up speed is nonzero and bounded");
    test.expect(
        max_acceleration < 160.0f &&
        max_jerk < 20000.0f,
        "camera acceleration and jerk remain bounded");
    test.expect(
        velocity_direction_reversals <= 1,
        "fixed remote target does not oscillate through repeated velocity reversals");
    test.expect(
        output.state == arzoom::CameraState::SmoothIdle &&
        output_speed(output) < 1.0e-5f &&
        output_acceleration(output) < 1.0e-5f,
        "fixed remote target settles to exact drift-free HOLD");
}

void test_retarget_preserves_motion_continuity(TestContext &test)
{
    arssyut::presentation::ArZoomCameraAdapter adapter;
    settle_zoomed_camera(adapter, 0.22f, 0.34f);

    constexpr int fps = 60;
    constexpr float dt = 1.0f / static_cast<float>(fps);

    arzoom::CameraOutput before{};
    for (int frame = 0; frame < 36; ++frame) {
        arssyut::presentation::ArZoomCameraIntent intent;
        intent.dt = dt;
        intent.cursor = {0.84f, 0.38f};
        intent.cursor_valid = true;
        intent.zoom_requested = true;
        intent.configured_zoom = 2.0f;
        intent.emphasis_event = frame == 0;
        before = adapter.step(intent);
    }

    arssyut::presentation::ArZoomCameraIntent retarget;
    retarget.dt = dt;
    retarget.cursor = {0.72f, 0.80f};
    retarget.cursor_valid = true;
    retarget.zoom_requested = true;
    retarget.configured_zoom = 2.0f;
    retarget.emphasis_event = true;

    const auto after =
        adapter.step(retarget);

    const float before_speed =
        output_speed(before);
    const float after_speed =
        output_speed(after);
    const float center_step =
        std::sqrt(
            (after.center.x - before.center.x) *
                (after.center.x - before.center.x) +
            (after.center.y - before.center.y) *
                (after.center.y - before.center.y));

    test.expect(
        before_speed > 0.001f,
        "retarget continuity trace starts while camera is genuinely moving");
    test.expect(
        after_speed > 0.0001f,
        "retarget does not stop/restart the camera at zero velocity");
    test.expect(
        center_step < 0.08f,
        "retarget cannot teleport the viewport");
}

void test_144fps_endpoint_consistency(TestContext &test)
{
    const auto at_60 =
        run_adapter(60, 4.8f);
    const auto at_144 =
        run_adapter(144, 4.8f);

    test.expect(
        std::fabs(at_60.center.x - at_144.center.x) < 0.020f &&
        std::fabs(at_60.center.y - at_144.center.y) < 0.020f &&
        std::fabs(at_60.zoom - at_144.zoom) < 0.010f,
        "ArZoom camera remains materially equivalent at 60 vs 144 fps");
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
    test_smart_zone_local_motion_reaches_exact_hold(test);
    test_remote_move_uses_bounded_catchup_and_smooth_settle(test);
    test_retarget_preserves_motion_continuity(test);
    test_144fps_endpoint_consistency(test);
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
