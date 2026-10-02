#include "arzoom-camera.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void require(bool condition, const std::string &message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::abort();
    }
}

float edge_violation(const arzoom::CameraOutput &output)
{
    const float half = 0.5f / std::max(output.zoom, 1.0f);
    float violation = 0.0f;
    violation = std::max(violation, -(output.center.x - half));
    violation = std::max(violation, -(output.center.y - half));
    violation = std::max(violation, output.center.x + half - 1.0f);
    violation = std::max(violation, output.center.y + half - 1.0f);
    return std::max(0.0f, violation);
}

arzoom::CameraInput sample(float dt, arzoom::Vec2 cursor,
                           bool zoom_requested = true,
                           float zoom = 2.0f)
{
    arzoom::CameraInput input;
    input.dt = dt;
    input.cursor = cursor;
    input.cursor_valid = true;
    input.zoom_requested = zoom_requested;
    input.configured_zoom = zoom;
    input.follow_policy = arzoom::CameraFollowPolicy::Smart;
    input.motion_style = arzoom::CameraMotionStyle::Balanced;
    return input;
}

arzoom::Vec2 screen_pos(arzoom::Vec2 source,
                        const arzoom::CameraOutput &camera)
{
    return arzoom::cursor_output_position(source, camera.center, camera.zoom);
}

float distance_from_line(arzoom::Vec2 p, arzoom::Vec2 a, arzoom::Vec2 b)
{
    const arzoom::Vec2 ab = arzoom::sub(b, a);
    const float len = arzoom::length(ab);
    if (len < 1.0e-7f)
        return arzoom::length(arzoom::sub(p, a));
    const arzoom::Vec2 ap = arzoom::sub(p, a);
    return std::fabs(ap.x * ab.y - ap.y * ab.x) / len;
}

float line_progress(arzoom::Vec2 p, arzoom::Vec2 a, arzoom::Vec2 b)
{
    const arzoom::Vec2 ab = arzoom::sub(b, a);
    const float denom = ab.x * ab.x + ab.y * ab.y;
    if (denom < 1.0e-9f)
        return 1.0f;
    const arzoom::Vec2 ap = arzoom::sub(p, a);
    return (ap.x * ab.x + ap.y * ab.y) / denom;
}

void warm_center(arzoom::SmartCamera &camera, int fps = 60,
                 float zoom = 2.0f)
{
    camera.step(sample(1.0f / fps, {0.5f, 0.5f}, false, zoom));
    for (int i = 0; i < fps; ++i)
        camera.step(sample(1.0f / fps, {0.5f, 0.5f}, true, zoom));
}

void straight_zoom_in_screen_path()
{
    using namespace arzoom;
    const Vec2 focus{0.92f, 0.22f};
    const Vec2 probes[] = {{0.15f, 0.20f}, {0.72f, 0.66f}, {0.94f, 0.12f}};
    const float target_zoom = 3.0f;
    const Vec2 final_center = smart_follow_target(
        focus, {0.5f, 0.5f}, {0.5f, 0.45f}, 0.28f, target_zoom);

    SmartCamera camera;
    camera.step(sample(1.0f / 60.0f, focus, false, target_zoom));
    float previous_progress[3] = {-0.001f, -0.001f, -0.001f};

    for (int frame = 0; frame < 90; ++frame) {
        const CameraOutput out = camera.step(
            sample(1.0f / 60.0f, focus, true, target_zoom));
        require(edge_violation(out) <= 2.0e-6f,
                "zoom-in exposed invalid source pixels");

        for (int i = 0; i < 3; ++i) {
            const Vec2 start = probes[i];
            const Vec2 finish = cursor_output_position(
                probes[i], final_center, target_zoom);
            const Vec2 current = screen_pos(probes[i], out);
            require(distance_from_line(current, start, finish) < 2.0e-4f,
                    "zoom-in pixel trajectory curved in screen space");
            const float p = line_progress(current, start, finish);
            require(p + 2.0e-4f >= previous_progress[i],
                    "zoom-in reversed direction before target");
            previous_progress[i] = p;
        }
    }
}

void local_explanation_stays_in_smooth_idle()
{
    using namespace arzoom;
    SmartCamera camera;
    warm_center(camera);
    const Vec2 start_center = camera.output().center;
    float max_displacement = 0.0f;

    for (int i = 0; i < 300; ++i) {
        const float phase = 6.28318530718f * static_cast<float>(i) / 60.0f;
        const Vec2 cursor{
            0.5f + 0.075f * std::cos(phase),
            0.5f + 0.075f * std::sin(phase),
        };
        const CameraOutput out = camera.step(
            sample(1.0f / 60.0f, cursor, true, 2.0f));
        max_displacement = std::max(
            max_displacement, length(sub(out.center, start_center)));
        require(out.state == CameraState::SmoothIdle ||
                    out.state == CameraState::Observe,
                "local explanation unexpectedly started follow");
    }

    require(max_displacement < 1.0e-5f,
            "local explanation circle moved viewport");
}

void relocation_coasts_into_idle_while_mouse_keeps_explaining()
{
    using namespace arzoom;
    SmartCamera camera;
    warm_center(camera);

    const Vec2 area{0.82f, 0.48f};
    Vec2 previous_center = camera.output().center;
    float previous_speed_output = 0.0f;
    float speed_before_idle = 1000.0f;
    float max_step_output = 0.0f;
    bool saw_follow = false;
    bool saw_coast = false;
    bool circle_started = false;
    bool saw_idle = false;
    Vec2 idle_center{};
    float max_idle_displacement = 0.0f;
    int idle_frames = 0;

    for (int frame = 0; frame < 420; ++frame) {
        Vec2 cursor = area;
        if (circle_started) {
            const float phase = 6.28318530718f *
                                static_cast<float>(frame) / 54.0f;
            cursor = {area.x + 0.040f * std::cos(phase),
                      area.y + 0.035f * std::sin(phase)};
        }

        const CameraOutput out = camera.step(
            sample(1.0f / 60.0f, cursor, true, 2.0f));
        require(edge_violation(out) <= 2.0e-6f,
                "Smart Zone relocation exposed invalid source pixels");

        const float step_output =
            length(sub(out.center, previous_center)) * out.zoom;
        max_step_output = std::max(max_step_output, step_output);

        if (out.state == CameraState::Follow ||
            out.state == CameraState::CatchUp)
            saw_follow = true;
        if (out.state == CameraState::Coast) {
            saw_coast = true;
            circle_started = true;
        }
        if (!saw_idle && out.state == CameraState::SmoothIdle && saw_follow) {
            saw_idle = true;
            speed_before_idle = previous_speed_output;
            idle_center = out.center;
        }
        if (saw_idle) {
            max_idle_displacement = std::max(
                max_idle_displacement,
                length(sub(out.center, idle_center)));
            ++idle_frames;
        }

        previous_center = out.center;
        previous_speed_output = length(out.velocity) * out.zoom;
    }

    require(saw_follow, "large relocation never entered follow");
    require(saw_coast, "follow did not hand off through Coast");
    require(circle_started, "explanation motion never began during arrival");
    require(saw_idle, "camera never reached SmoothIdle while mouse moved");
    require(idle_frames > 90, "SmoothIdle did not remain stable");
    require(speed_before_idle < 0.012f,
            "follow snapped to steady while speed was visible");
    require(max_idle_displacement < 1.0e-5f,
            "new-area explanation moved SmoothIdle viewport");
    require(max_step_output < 0.030f,
            "relocation/coast contained a one-frame snap");
}

void leaving_idle_zone_has_soft_launch()
{
    using namespace arzoom;
    SmartCamera camera;
    warm_center(camera);

    const Vec2 first_area{0.82f, 0.48f};
    for (int frame = 0; frame < 300; ++frame)
        camera.step(sample(1.0f / 60.0f, first_area, true, 2.0f));

    require(camera.output().state == CameraState::SmoothIdle,
            "first relocation did not establish SmoothIdle");
    const Vec2 before = camera.output().center;

    const Vec2 second_area{0.20f, 0.72f};
    Vec2 previous = before;
    bool woke = false;
    bool saw_motion = false;
    float first_motion_step_output = 1000.0f;
    float max_cruise_step_output = 0.0f;

    for (int frame = 0; frame < 300; ++frame) {
        const CameraOutput out = camera.step(
            sample(1.0f / 60.0f, second_area, true, 2.0f));
        const float step_output =
            length(sub(out.center, previous)) * out.zoom;
        max_cruise_step_output = std::max(max_cruise_step_output, step_output);

        if (out.state == CameraState::Follow ||
            out.state == CameraState::CatchUp ||
            out.state == CameraState::Coast)
            woke = true;

        if (!saw_motion && step_output > 1.0e-6f) {
            saw_motion = true;
            first_motion_step_output = step_output;
        }
        previous = out.center;
    }

    require(woke, "leaving outer Smart Zone did not wake follow");
    require(saw_motion, "new follow shot never started moving");
    require(first_motion_step_output < 0.010f,
            "SmoothIdle wake launched with a visible first-frame snap");
    require(max_cruise_step_output < 0.070f,
            "new follow shot exceeded gimbal cruise step bound");
    require(length(sub(camera.output().center, before)) > 0.12f,
            "second area relocation did not move viewport");
}

void continuous_retarget_has_no_snap()
{
    using namespace arzoom;
    SmartCamera camera;
    warm_center(camera);

    float previous_x = camera.output().center.x;
    float max_step = 0.0f;
    for (int frame = 0; frame < 170; ++frame) {
        const Vec2 cursor = frame < 55 ? Vec2{0.90f, 0.50f}
                                      : Vec2{0.18f, 0.62f};
        const CameraOutput out = camera.step(
            sample(1.0f / 60.0f, cursor, true, 2.0f));
        max_step = std::max(max_step,
                            std::fabs(out.center.x - previous_x));
        previous_x = out.center.x;
    }
    require(max_step < 0.030f,
            "mid-flight retarget produced visible snap");
}

void straight_zoom_out_screen_path()
{
    using namespace arzoom;
    const Vec2 focus{0.92f, 0.20f};
    SmartCamera camera;
    camera.step(sample(1.0f / 60.0f, focus, false, 3.0f));
    for (int frame = 0; frame < 90; ++frame)
        camera.step(sample(1.0f / 60.0f, focus, true, 3.0f));

    const CameraOutput start_camera = camera.output();
    const Vec2 probes[] = {{0.10f, 0.18f}, {0.62f, 0.72f}, {0.95f, 0.10f}};
    Vec2 starts[3];
    float previous_progress[3] = {-0.001f, -0.001f, -0.001f};
    for (int i = 0; i < 3; ++i)
        starts[i] = screen_pos(probes[i], start_camera);

    float previous_zoom = start_camera.zoom + 0.001f;
    for (int frame = 0; frame < 120; ++frame) {
        const CameraOutput out = camera.step(
            sample(1.0f / 60.0f, focus, false, 3.0f));
        require(edge_violation(out) <= 2.0e-6f,
                "zoom-out exposed invalid source pixels");
        require(out.zoom <= previous_zoom + 2.0e-5f,
                "zoom-out reversed magnification direction");
        previous_zoom = out.zoom;

        for (int i = 0; i < 3; ++i) {
            const Vec2 finish = probes[i];
            const Vec2 current = screen_pos(probes[i], out);
            require(distance_from_line(current, starts[i], finish) < 2.5e-4f,
                    "zoom-out pixel trajectory curved in screen space");
            const float p = line_progress(current, starts[i], finish);
            require(p + 2.0e-4f >= previous_progress[i],
                    "zoom-out bent/reversed before final position");
            previous_progress[i] = p;
        }
    }

    const CameraOutput settled = camera.output();
    require(std::fabs(settled.zoom - 1.0f) < 1.0e-6f,
            "zoom-out did not exact-lock to 1x");
    require(length(sub(settled.center, Vec2{0.5f, 0.5f})) < 1.0e-6f,
            "zoom-out did not exact-lock to center");
}

void frame_rate_consistency()
{
    using namespace arzoom;
    const int fps_values[] = {30, 60, 120, 144};
    Vec2 centers[4];

    for (int index = 0; index < 4; ++index) {
        const int fps = fps_values[index];
        SmartCamera camera;
        camera.step(sample(1.0f / fps, {0.5f, 0.5f}, false, 2.0f));
        for (int frame = 0; frame < fps; ++frame)
            camera.step(sample(1.0f / fps, {0.5f, 0.5f}, true, 2.0f));
        for (int frame = 0; frame < 4 * fps; ++frame)
            camera.step(sample(1.0f / fps, {0.90f, 0.50f}, true, 2.0f));
        centers[index] = camera.output().center;
    }

    for (int index = 1; index < 4; ++index) {
        require(length(sub(centers[0], centers[index])) < 0.006f,
                "30/60/120/144 fps Smart Zone framing diverged");
    }
}

} // namespace

int main()
{
    straight_zoom_in_screen_path();
    local_explanation_stays_in_smooth_idle();
    relocation_coasts_into_idle_while_mouse_keeps_explaining();
    leaving_idle_zone_has_soft_launch();
    continuous_retarget_has_no_snap();
    straight_zoom_out_screen_path();
    frame_rate_consistency();
    std::cout << "ArZoom Smart Zone + Smooth Idle gates: PASS\n";
    return 0;
}
