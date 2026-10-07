#include "presentation/presentation_controller.hpp"
#include "app/region_geometry.hpp"
#include "presentation/momentary_presenter_gate.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <type_traits>

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


arssyut::presentation::PresentationFrameState
step_frames_without_pointer_activity(
    PresentationController &controller,
    int frames,
    std::int64_t &ticks,
    float cursor_x = 0.72f,
    float cursor_y = 0.42f)
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
                true,
                now,
                TimePoint{});
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



void test_reset_while_momentary_key_remains_held(
    TestContext &test)
{
    arssyut::presentation::MomentaryReleaseGate gate;

    test.expect(
        gate.accept(true),
        "momentary gate accepts an initial physical key-down");

    gate.block_until_release();

    test.expect(
        !gate.accept(true) &&
        gate.blocked(),
        "Reset blocks a still-held momentary chord");

    test.expect(
        !gate.accept(true),
        "still-held chord cannot re-arm on later presentation ticks");

    test.expect(
        !gate.accept(false) &&
        !gate.blocked(),
        "physical key-up clears the Reset interlock");

    test.expect(
        gate.accept(true),
        "fresh key-down after release may activate again");
}

void test_toggle_hold_overlap_matrix(TestContext &test)
{
    PresentationController controller;
    PresentationSettings settings;
    settings.smart_zoom = false;
    settings.presenter_controls = true;
    settings.zoom = 2.0f;

    controller.reset();
    controller.set_settings(settings);

    std::int64_t ticks = 0;
    controller.toggle_manual_zoom();
    controller.set_hold_zoom(true);

    auto state =
        step_frames(controller, 120, ticks);

    test.expect(
        state.camera_zoom > 1.25f,
        "Toggle + Hold converge on the same camera authority");

    controller.set_hold_zoom(false);
    state =
        step_frames(controller, 45, ticks);

    test.expect(
        controller.manual_zoom_latched() &&
        state.camera_zoom > 1.20f,
        "releasing Hold does not clear Toggle Zoom intent");

    controller.set_hold_zoom(true);
    controller.toggle_manual_zoom();
    state =
        step_frames(controller, 45, ticks);

    test.expect(
        !controller.manual_zoom_latched() &&
        state.camera_zoom > 1.20f,
        "turning Toggle off does not clear an active Hold Zoom intent");

    controller.set_hold_zoom(false);
    state =
        step_frames(controller, 180, ticks);

    test.expect(
        std::fabs(state.camera_zoom - 1.0f) < 0.02f,
        "camera returns to full frame only after the final zoom owner releases");
}

void test_hold_smart_zoom_overlap(TestContext &test)
{
    PresentationController controller;
    PresentationSettings settings;
    settings.smart_zoom = true;
    settings.presenter_controls = true;
    settings.zoom = 2.0f;

    controller.reset();
    controller.set_settings(settings);

    std::int64_t ticks = 1;
    controller.on_click(
        arssyut::presentation::ClickKind::Left,
        0.75f,
        0.40f,
        TimePoint{ticks});
    controller.set_hold_zoom(true);

    auto state =
        step_frames_without_pointer_activity(
            controller,
            60,
            ticks);

    test.expect(
        state.camera_zoom > 1.20f,
        "Smart Zoom and Hold share the accepted ArZoom camera");

    controller.set_hold_zoom(false);
    state =
        step_frames_without_pointer_activity(
            controller,
            30,
            ticks);

    test.expect(
        state.camera_zoom > 1.10f,
        "releasing Hold preserves still-active Smart Zoom intent");

    state =
        step_frames_without_pointer_activity(
            controller,
            240,
            ticks);

    test.expect(
        std::fabs(state.camera_zoom - 1.0f) < 0.03f,
        "camera returns after Smart Zoom tail expires");
}


void test_spotlight_state_is_bounded_and_master_is_not_runtime(
    TestContext &test)
{
    using arssyut::presentation::SpotlightFrameState;

    static_assert(std::is_trivially_copyable_v<SpotlightFrameState>);
    static_assert(sizeof(SpotlightFrameState) <= 64);

    PresentationController controller;
    PresentationSettings settings;
    settings.smart_zoom = false;
    settings.presenter_controls = true;
    settings.spotlight.enabled = true;
    settings.spotlight.link_to_zoom = true;

    controller.reset();
    controller.set_settings(settings);

    std::int64_t ticks = 0;
    auto state = step_frames_at(
        controller, 1, ticks, 0.73f, 0.36f);

    test.expect(
        state.spotlight.enabled &&
        !state.spotlight.runtime_requested,
        "Spotlight master enable does not put the effect on-air by itself");
    test.expect(
        state.spotlight.focus_mix == 0.0f &&
        state.spotlight.dim_mix == 0.0f,
        "P6UI.6D-B remains exact visual pass-through before choreography");

    controller.toggle_manual_zoom();
    state = step_frames_at(
        controller, 1, ticks, 0.73f, 0.36f);

    test.expect(
        state.spotlight.runtime_requested &&
        state.spotlight.focus_valid,
        "zoom-linked Spotlight requests runtime only after zoom plus proven focus");
    test.expect(
        std::fabs(state.spotlight.content_x - 0.73f) < 1.0e-6f &&
        std::fabs(state.spotlight.content_y - 0.36f) < 1.0e-6f,
        "Spotlight stores canonical content-space focus without output remapping");
}


void test_spotlight_standalone_runtime_and_cursor_authority(
    TestContext &test)
{
    using arssyut::presentation::SpotlightMode;

    PresentationSettings settings;
    settings.smart_zoom = false;
    settings.click_visual = false;
    settings.shortcut_keys = false;
    settings.presenter_controls = false;
    settings.spotlight.link_to_zoom = false;
    settings.spotlight.mode = SpotlightMode::Cursor;

    test.expect(
        !settings.needs_presentation_frames(),
        "disabled effects require no presentation input work");

    settings.spotlight.enabled = true;
    test.expect(
        settings.needs_presentation_frames(),
        "standalone Spotlight alone enables existing presentation input work");

    PresentationController controller;
    controller.reset();
    controller.set_settings(settings);

    std::int64_t ticks = 0;
    auto frame =
        step_frames_at(
            controller,
            4,
            ticks,
            0.74f,
            0.31f,
            false);

    test.expect(
        !frame.spotlight.focus_valid &&
            !frame.spotlight.runtime_requested,
        "standalone Spotlight refuses to invent an invalid cursor focus");

    frame =
        step_frames_at(
            controller,
            65,
            ticks,
            0.74f,
            0.31f);

    test.expect(
        frame.spotlight.focus_valid &&
            frame.spotlight.runtime_requested &&
            frame.spotlight.focus_mix > 0.99f &&
            frame.spotlight.dim_mix > 0.99f,
        "standalone Spotlight closes the pinned cinematic aperture without Zoom");

    test.expect(
        std::fabs(frame.camera_zoom - 1.0f) < 0.0001f &&
            std::fabs(frame.camera_center_x - 0.5f) < 0.0001f &&
            std::fabs(frame.camera_center_y - 0.5f) < 0.0001f,
        "standalone Spotlight never activates or replaces the camera");

    frame =
        step_frames_at(
            controller,
            2,
            ticks,
            0.21f,
            0.83f);

    test.expect(
        std::fabs(frame.spotlight.content_x - 0.21f) < 0.00001f &&
            std::fabs(frame.spotlight.content_y - 0.83f) < 0.00001f,
        "standalone Spotlight uses existing canonical Cursor mode for focus");

    frame =
        step_frames_at(
            controller,
            2,
            ticks,
            0.90f,
            0.10f,
            false);

    test.expect(
        std::fabs(frame.spotlight.content_x - 0.21f) < 0.00001f &&
            std::fabs(frame.spotlight.content_y - 0.83f) < 0.00001f,
        "missing cursor mapping holds last proven standalone focus");

    // Linked mode must retain the existing no-zoom => no-effect contract.
    settings.spotlight.link_to_zoom = true;
    settings.spotlight.mode = SpotlightMode::SmartFocus;
    PresentationController linked;
    linked.reset();
    linked.set_settings(settings);
    std::int64_t linked_ticks = 0;
    const auto linked_frame =
        step_frames_at(
            linked,
            65,
            linked_ticks,
            0.74f,
            0.31f);

    test.expect(
        !linked_frame.spotlight.runtime_requested &&
            linked_frame.spotlight.dim_mix == 0.0f,
        "Zoom-linked Spotlight stays exact pass-through until Zoom is requested");
}

void test_spotlight_focus_size_controls_real_aperture(
    TestContext &test)
{
    using arssyut::presentation::SpotlightMode;
    using arssyut::presentation::SpotlightSize;

    const auto run_size =
        [](SpotlightSize size) {
            PresentationSettings settings;
            settings.smart_zoom = false;
            settings.click_visual = false;
            settings.shortcut_keys = false;
            settings.presenter_controls = false;
            settings.spotlight.enabled = true;
            settings.spotlight.link_to_zoom = false;
            settings.spotlight.mode = SpotlightMode::Cursor;
            settings.spotlight.size = size;

            PresentationController controller;
            controller.reset();
            controller.set_settings(settings);
            std::int64_t ticks = 0;
            return step_frames_at(
                controller,
                60,
                ticks,
                0.52f,
                0.50f);
        };

    const auto compact =
        run_size(SpotlightSize::Compact);
    const auto balanced =
        run_size(SpotlightSize::Balanced);
    const auto wide =
        run_size(SpotlightSize::Wide);

    test.expect(
        compact.spotlight.area_scale_percent <
            balanced.spotlight.area_scale_percent &&
            balanced.spotlight.area_scale_percent <
                wide.spotlight.area_scale_percent &&
            compact.spotlight.focus_mix > 0.99f &&
            balanced.spotlight.focus_mix > 0.99f &&
            wide.spotlight.focus_mix > 0.99f,
        "Focus Size Compact/Balanced/Wide changes the real native aperture scale");

    test.expect(
        std::fabs(compact.spotlight.area_scale_percent - 82.0f) <
                0.01f &&
            std::fabs(balanced.spotlight.area_scale_percent - 100.0f) <
                0.01f &&
            std::fabs(wide.spotlight.area_scale_percent - 148.0f) <
                0.01f,
        "product size presets preserve pinned upstream proportional sizing");

    test.expect(
        compact.camera_zoom == balanced.camera_zoom &&
            balanced.camera_zoom == wide.camera_zoom &&
            compact.camera_center_x == wide.camera_center_x &&
            compact.camera_center_y == wide.camera_center_y,
        "Focus Size changes only Spotlight aperture, never camera geometry");
}

void test_spotlight_cannot_change_camera_authority(
    TestContext &test)
{
    PresentationSettings baseline_settings;
    baseline_settings.smart_zoom = false;
    baseline_settings.presenter_controls = true;
    baseline_settings.zoom = 2.75f;

    PresentationSettings spotlight_settings = baseline_settings;
    spotlight_settings.spotlight.enabled = true;
    spotlight_settings.spotlight.link_to_zoom = true;

    PresentationController baseline;
    PresentationController spotlight;
    baseline.reset();
    spotlight.reset();
    baseline.set_settings(baseline_settings);
    spotlight.set_settings(spotlight_settings);
    baseline.toggle_manual_zoom();
    spotlight.toggle_manual_zoom();

    std::int64_t baseline_ticks = 0;
    std::int64_t spotlight_ticks = 0;
    bool identical = true;

    for (int frame = 0; frame < 240; ++frame) {
        const float x =
            frame < 100 ? 0.78f :
            (frame < 170 ? 0.22f : 0.64f);
        const float y =
            frame < 100 ? 0.35f :
            (frame < 170 ? 0.78f : 0.46f);

        const auto a = step_frames_at(
            baseline, 1, baseline_ticks, x, y);
        const auto b = step_frames_at(
            spotlight, 1, spotlight_ticks, x, y);

        identical =
            identical &&
            std::fabs(a.camera_center_x - b.camera_center_x) < 1.0e-7f &&
            std::fabs(a.camera_center_y - b.camera_center_y) < 1.0e-7f &&
            std::fabs(a.camera_zoom - b.camera_zoom) < 1.0e-7f;
    }

    test.expect(
        identical,
        "Spotlight state is read-only and cannot perturb SmartCamera output");
}

void test_spotlight_focus_modes_share_canonical_content_space(
    TestContext &test)
{
    PresentationController smart;
    PresentationSettings smart_settings;
    smart_settings.smart_zoom = false;
    smart_settings.presenter_controls = true;
    smart_settings.spotlight.enabled = true;
    smart_settings.spotlight.mode =
        arssyut::presentation::SpotlightMode::SmartFocus;
    smart.reset();
    smart.set_settings(smart_settings);
    smart.toggle_manual_zoom();

    std::int64_t smart_ticks = 0;
    auto smart_state = step_frames_at(
        smart, 1, smart_ticks, 0.76f, 0.34f);
    const float smart_x = smart_state.spotlight.content_x;
    const float smart_y = smart_state.spotlight.content_y;

    smart_state = step_frames_at(
        smart, 90, smart_ticks, 0.53f, 0.47f);

    test.expect(
        std::fabs(smart_state.spotlight.content_x - smart_x) < 1.0e-7f &&
        std::fabs(smart_state.spotlight.content_y - smart_y) < 1.0e-7f,
        "Smart Focus does not chase local pointer motion with an independent planner");

    PresentationController cursor;
    auto cursor_settings = smart_settings;
    cursor_settings.spotlight.mode =
        arssyut::presentation::SpotlightMode::Cursor;
    cursor.reset();
    cursor.set_settings(cursor_settings);
    cursor.toggle_manual_zoom();

    std::int64_t cursor_ticks = 0;
    auto cursor_state = step_frames_at(
        cursor, 1, cursor_ticks, 0.68f, 0.31f);
    cursor_state = step_frames_at(
        cursor, 1, cursor_ticks, 0.21f, 0.82f);

    test.expect(
        std::fabs(cursor_state.spotlight.content_x - 0.21f) < 1.0e-6f &&
        std::fabs(cursor_state.spotlight.content_y - 0.82f) < 1.0e-6f,
        "Cursor Spotlight consumes canonical mapped pointer coordinates directly");

    PresentationController invalid;
    invalid.reset();
    invalid.set_settings(smart_settings);
    invalid.toggle_manual_zoom();
    std::int64_t invalid_ticks = 0;
    auto invalid_state = step_frames_at(
        invalid, 1, invalid_ticks, 0.5f, 0.5f, false);

    test.expect(
        !invalid_state.spotlight.focus_valid &&
        !invalid_state.spotlight.runtime_requested,
        "Spotlight refuses to invent focus when pointer mapping is unavailable");

    invalid_state = step_frames_at(
        invalid, 1, invalid_ticks, 0.62f, 0.29f, true);
    test.expect(
        invalid_state.spotlight.focus_valid &&
        invalid_state.spotlight.runtime_requested &&
        std::fabs(invalid_state.spotlight.content_x - 0.62f) < 1.0e-6f,
        "Spotlight activates only after valid canonical focus is acquired");
}

void test_click_spotlight_owns_one_anchor_not_history(
    TestContext &test)
{
    PresentationController controller;
    PresentationSettings settings;
    settings.smart_zoom = false;
    settings.click_visual = false;
    settings.presenter_controls = true;
    settings.spotlight.enabled = true;
    settings.spotlight.mode =
        arssyut::presentation::SpotlightMode::Click;

    controller.reset();
    controller.set_settings(settings);
    controller.on_click(
        arssyut::presentation::ClickKind::Left,
        0.81f,
        0.27f,
        TimePoint{1});
    controller.toggle_manual_zoom();

    std::int64_t ticks = 0;
    auto state = step_frames_at(
        controller, 60, ticks, 0.15f, 0.88f);

    test.expect(
        state.spotlight.focus_valid &&
        std::fabs(state.spotlight.content_x - 0.81f) < 1.0e-6f &&
        std::fabs(state.spotlight.content_y - 0.27f) < 1.0e-6f,
        "Click Spotlight locks one canonical content anchor independent of click-ring rendering");

    controller.on_click(
        arssyut::presentation::ClickKind::Right,
        0.32f,
        0.64f,
        TimePoint{ticks + 1});
    state = step_frames_at(
        controller, 1, ticks, 0.95f, 0.05f);

    test.expect(
        std::fabs(state.spotlight.content_x - 0.32f) < 1.0e-6f &&
        std::fabs(state.spotlight.content_y - 0.64f) < 1.0e-6f,
        "next Click Spotlight focus replaces the single bounded anchor");
}


void test_spotlight_reset_clears_transient_focus(
    TestContext &test)
{
    PresentationController controller;
    PresentationSettings settings;
    settings.smart_zoom = false;
    settings.presenter_controls = true;
    settings.spotlight.enabled = true;
    settings.spotlight.mode =
        arssyut::presentation::SpotlightMode::Click;

    controller.reset();
    controller.set_settings(settings);
    controller.on_click(
        arssyut::presentation::ClickKind::Left,
        0.79f,
        0.24f,
        TimePoint{1});
    controller.toggle_manual_zoom();

    std::int64_t ticks = 0;
    auto state = step_frames_at(
        controller, 1, ticks, 0.2f, 0.8f);

    test.expect(
        state.spotlight.runtime_requested &&
        state.spotlight.focus_valid,
        "Click Spotlight has one active bounded focus before Reset");

    controller.reset_full_frame();
    state = step_frames_at(
        controller, 1, ticks, 0.2f, 0.8f);

    test.expect(
        !state.spotlight.runtime_requested &&
        !state.spotlight.focus_valid,
        "Reset / Full Frame clears transient Spotlight focus deterministically");
}



void test_freeze_camera_exact_hold_and_resume(
    TestContext &test)
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
    auto state = step_frames_at(
        controller, 42, ticks, 0.82f, 0.30f);

    controller.toggle_freeze_camera();
    const float frozen_x = state.camera_center_x;
    const float frozen_y = state.camera_center_y;
    const float frozen_zoom = state.camera_zoom;

    bool exact = true;
    for (int i = 0; i < 120; ++i) {
        state = step_frames_at(
            controller, 1, ticks, 0.08f, 0.92f);
        exact =
            exact &&
            state.camera_center_x == frozen_x &&
            state.camera_center_y == frozen_y &&
            state.camera_zoom == frozen_zoom;
    }

    test.expect(
        controller.camera_frozen() && exact,
        "Freeze holds the exact rendered center and zoom through remote pointer motion");

    controller.adjust_manual_zoom(0.50f);
    state = step_frames_at(
        controller, 45, ticks, 0.08f, 0.92f);

    test.expect(
        state.camera_center_x == frozen_x &&
        state.camera_center_y == frozen_y &&
        state.camera_zoom == frozen_zoom &&
        std::fabs(controller.configured_zoom() - 2.75f) < 1.0e-6f,
        "Zoom plus while frozen changes only the future configured target");

    controller.toggle_freeze_camera();
    const auto first_resumed = step_frames_at(
        controller, 1, ticks, 0.08f, 0.92f);

    test.expect(
        !controller.camera_frozen() &&
        std::fabs(first_resumed.camera_center_x - frozen_x) < 0.03f &&
        std::fabs(first_resumed.camera_center_y - frozen_y) < 0.03f &&
        std::fabs(first_resumed.camera_zoom - frozen_zoom) < 0.08f,
        "unfreeze resumes the existing camera continuously without a snap");

    state = step_frames_at(
        controller, 180, ticks, 0.08f, 0.92f);
    test.expect(
        state.camera_zoom > frozen_zoom &&
        state.camera_zoom <= 2.76f,
        "unfreeze converges through the same camera toward the queued zoom target");
}


void test_freeze_mid_activation_and_reset_priority(
    TestContext &test)
{
    PresentationController controller;
    PresentationSettings settings;
    settings.smart_zoom = false;
    settings.presenter_controls = true;
    settings.zoom = 2.50f;

    controller.reset();
    controller.set_settings(settings);
    controller.toggle_manual_zoom();

    std::int64_t ticks = 0;
    auto state = step_frames_at(
        controller, 8, ticks, 0.78f, 0.36f);

    controller.toggle_freeze_camera();
    const float frozen_x = state.camera_center_x;
    const float frozen_y = state.camera_center_y;
    const float frozen_zoom = state.camera_zoom;

    state = step_frames_at(
        controller, 75, ticks, 0.15f, 0.85f);

    test.expect(
        controller.camera_frozen() &&
        state.camera_center_x == frozen_x &&
        state.camera_center_y == frozen_y &&
        state.camera_zoom == frozen_zoom,
        "Freeze during activation pauses the exact in-flight camera state");

    controller.reset_full_frame();
    test.expect(
        !controller.camera_frozen(),
        "Reset Full Frame clears Freeze with priority");

    state = step_frames_at(
        controller, 180, ticks, 0.15f, 0.85f);

    test.expect(
        std::fabs(state.camera_zoom - 1.0f) < 0.02f &&
        std::fabs(state.camera_center_x - 0.5f) < 0.03f &&
        std::fabs(state.camera_center_y - 0.5f) < 0.03f,
        "Reset after Freeze returns through the accepted camera to full frame");
}


void test_zoom_off_clears_freeze(
    TestContext &test)
{
    PresentationController controller;
    PresentationSettings settings;
    settings.smart_zoom = false;
    settings.presenter_controls = true;
    settings.zoom = 2.0f;

    controller.reset();
    controller.set_settings(settings);
    controller.toggle_manual_zoom();

    std::int64_t ticks = 0;
    auto state = step_frames_at(
        controller, 120, ticks, 0.72f, 0.40f);

    controller.toggle_freeze_camera();
    test.expect(
        controller.camera_frozen(),
        "Freeze can lock a stable zoomed shot");

    controller.toggle_manual_zoom();
    test.expect(
        !controller.camera_frozen(),
        "Toggle Zoom off wins and clears Freeze");

    state = step_frames_at(
        controller, 180, ticks, 0.72f, 0.40f);

    test.expect(
        std::fabs(state.camera_zoom - 1.0f) < 0.02f,
        "Zoom off after Freeze still returns to full frame");
}



struct CinematicFrameRateTrace {
    bool framing_first = false;
    float focused_mix = 0.0f;
    float focused_dim = 0.0f;
    float plus_scale = 0.0f;
    float minus_scale = 0.0f;
    float after_off_mix = 0.0f;
    float opening_mix = 0.0f;
    float opening_dim = 0.0f;
    float final_mix = 0.0f;
    float final_dim = 0.0f;
    bool final_pass_through = false;
};

CinematicFrameRateTrace sample_cinematic_frame_rate(int fps)
{
    PresentationSettings settings;
    settings.smart_zoom = false;
    settings.presenter_controls = true;
    settings.zoom = 2.0f;
    settings.spotlight.enabled = true;
    settings.spotlight.link_to_zoom = true;

    PresentationController controller;
    controller.reset();
    controller.set_settings(settings);

    std::int64_t ticks = 0;
    const auto next = [&]() {
        ticks += MonotonicClock::ticks_per_second / fps;
        const TimePoint now{ticks};
        return controller.step(
            1.0f / static_cast<float>(fps),
            0.74f,
            0.36f,
            true,
            now,
            now);
    };

    const auto advance = [&](int frames) {
        arssyut::presentation::PresentationFrameState frame{};
        for (int i = 0; i < frames; ++i)
            frame = next();
        return frame;
    };

    CinematicFrameRateTrace trace{};

    controller.toggle_manual_zoom();
    const auto first = next();
    trace.framing_first =
        first.spotlight.runtime_requested &&
        first.spotlight.focus_valid &&
        first.spotlight.focus_mix == 0.0f &&
        first.spotlight.dim_mix == 0.0f;

    const auto focused = advance(fps * 2);
    trace.focused_mix = focused.spotlight.focus_mix;
    trace.focused_dim = focused.spotlight.dim_mix;

    // ArZoom contract: Zoom +/- resizes the aperture only. It must not
    // re-trigger the full-screen closing animation or alter camera ownership.
    controller.adjust_manual_zoom(0.50f);
    const auto zoomed_in = advance(fps * 2);
    trace.plus_scale = zoomed_in.spotlight.zoom_resize_scale;

    controller.adjust_manual_zoom(-0.25f);
    const auto zoomed_out_step = advance(fps * 2);
    trace.minus_scale = zoomed_out_step.spotlight.zoom_resize_scale;

    controller.toggle_manual_zoom();
    const auto just_off = next();
    trace.after_off_mix = just_off.spotlight.focus_mix;
    const auto opening = advance(fps / 6);
    trace.opening_mix = opening.spotlight.focus_mix;
    trace.opening_dim = opening.spotlight.dim_mix;

    const auto cleared = advance(fps * 2);
    trace.final_mix = cleared.spotlight.focus_mix;
    trace.final_dim = cleared.spotlight.dim_mix;
    trace.final_pass_through =
        !cleared.spotlight.runtime_requested &&
        cleared.spotlight.focus_mix == 0.0f &&
        cleared.spotlight.dim_mix == 0.0f;

    return trace;
}

void test_spotlight_cinematic_frame_rate_acceptance(
    TestContext &test)
{
    // Exercise the actual PresenterController camera/Spotlight handoff at
    // wall-clock equivalent intervals, not only the vendored state helper.
    const std::array<int, 4> rates{30, 60, 120, 144};
    const auto baseline = sample_cinematic_frame_rate(60);

    bool framing_first = true;
    bool reaches_focus = true;
    bool resize_only = true;
    bool reverse_opens = true;
    bool returns_to_pass_through = true;
    bool frame_rate_equivalent = true;

    for (const int fps : rates) {
        const auto trace = sample_cinematic_frame_rate(fps);

        framing_first =
            framing_first && trace.framing_first;
        reaches_focus =
            reaches_focus &&
            std::fabs(trace.focused_mix - 1.0f) < 1.0e-5f &&
            std::fabs(trace.focused_dim - 1.0f) < 1.0e-5f;
        resize_only =
            resize_only &&
            trace.plus_scale > 1.15f &&
            trace.minus_scale < trace.plus_scale &&
            trace.minus_scale > 1.02f;
        reverse_opens =
            reverse_opens &&
            trace.after_off_mix > 0.0f &&
            trace.after_off_mix <= 1.0f &&
            trace.opening_mix > 0.0f &&
            trace.opening_mix < trace.after_off_mix &&
            trace.opening_dim <= trace.opening_mix;
        returns_to_pass_through =
            returns_to_pass_through &&
            trace.final_pass_through;
        frame_rate_equivalent =
            frame_rate_equivalent &&
            std::fabs(
                trace.focused_mix -
                baseline.focused_mix) < 0.01f &&
            std::fabs(
                trace.plus_scale -
                baseline.plus_scale) < 0.08f &&
            std::fabs(
                trace.minus_scale -
                baseline.minus_scale) < 0.08f &&
            std::fabs(
                trace.opening_mix -
                baseline.opening_mix) < 0.16f &&
            std::fabs(
                trace.final_mix -
                baseline.final_mix) < 1.0e-5f;

        std::cout
            << "P6UI.6D-J1 cinematic rate " << fps
            << " focused=" << trace.focused_mix
            << " zoom_plus_scale=" << trace.plus_scale
            << " zoom_minus_scale=" << trace.minus_scale
            << " opening=" << trace.opening_mix
            << " final=" << trace.final_mix
            << '\n';
    }

    test.expect(
        framing_first,
        "30/60/120/144 fps: Zoom begins with Spotlight full-frame and undimmed");
    test.expect(
        reaches_focus,
        "30/60/120/144 fps: cinematic close reaches exact focus and dim endpoints");
    test.expect(
        resize_only,
        "30/60/120/144 fps: Zoom plus/minus resizes focus in-session");
    test.expect(
        reverse_opens,
        "30/60/120/144 fps: Toggle Zoom off expands focus smoothly before pass-through");
    test.expect(
        returns_to_pass_through,
        "30/60/120/144 fps: Zoom off settles to exact Spotlight pass-through");
    test.expect(
        frame_rate_equivalent,
        "30/60/120/144 fps: cinematic outcomes stay wall-time equivalent");
}

void test_spotlight_cinematic_choreography_and_resize(
    TestContext &test)
{
    PresentationController controller;
    PresentationSettings settings;
    settings.smart_zoom = false;
    settings.presenter_controls = true;
    settings.zoom = 2.0f;
    settings.spotlight.enabled = true;
    settings.spotlight.link_to_zoom = true;
    settings.spotlight.cinematic_speed =
        arssyut::presentation::SpotlightCinematicSpeed::Balanced;

    controller.reset();
    controller.set_settings(settings);
    controller.toggle_manual_zoom();

    std::int64_t ticks = 0;

    const auto first = step_frames_at(
        controller, 1, ticks, 0.76f, 0.34f);
    test.expect(
        first.spotlight.runtime_requested &&
        first.spotlight.focus_mix == 0.0f &&
        first.spotlight.dim_mix == 0.0f,
        "Spotlight stays full-frame while the authoritative camera starts framing");

    auto state = step_frames_at(
        controller, 24, ticks, 0.76f, 0.34f);
    test.expect(
        state.spotlight.focus_mix > 0.0f &&
        state.spotlight.focus_mix <= 1.0f,
        "Spotlight closes only after camera framing has begun");
    test.expect(
        state.spotlight.dim_mix <= state.spotlight.focus_mix,
        "Spotlight dimming trails aperture closure");

    state = step_frames_at(
        controller, 90, ticks, 0.76f, 0.34f);
    test.expect(
        std::fabs(state.spotlight.focus_mix - 1.0f) < 0.01f &&
        std::fabs(state.spotlight.dim_mix - 1.0f) < 0.01f,
        "Spotlight reaches the exact focused endpoint");

    const float focused_before_resize =
        state.spotlight.focus_mix;
    controller.adjust_manual_zoom(0.50f);

    float previous_scale =
        state.spotlight.zoom_resize_scale;
    bool monotonic_resize = true;
    for (int i = 0; i < 90; ++i) {
        state = step_frames_at(
            controller, 1, ticks, 0.76f, 0.34f);
        monotonic_resize =
            monotonic_resize &&
            state.spotlight.zoom_resize_scale + 1.0e-5f >=
                previous_scale;
        previous_scale =
            state.spotlight.zoom_resize_scale;
    }

    test.expect(
        monotonic_resize &&
        state.spotlight.zoom_resize_scale > 1.20f,
        "Zoom plus resizes Spotlight monotonically from live camera progress");
    test.expect(
        std::fabs(state.spotlight.focus_mix - focused_before_resize) < 0.02f,
        "Zoom plus does not replay Spotlight activation");

    controller.toggle_manual_zoom();
    const float close_value =
        state.spotlight.focus_mix;
    state = step_frames_at(
        controller, 1, ticks, 0.76f, 0.34f);

    test.expect(
        state.spotlight.runtime_requested &&
        state.spotlight.focus_mix <= close_value &&
        state.spotlight.focus_mix > 0.0f,
        "Zoom off opens Spotlight smoothly instead of hard-cutting the renderer");

    controller.toggle_manual_zoom();
    const float before_reversal =
        state.spotlight.focus_mix;
    state = step_frames_at(
        controller, 2, ticks, 0.76f, 0.34f);

    test.expect(
        std::fabs(state.spotlight.focus_mix - before_reversal) < 0.20f,
        "mid-transition Spotlight reversal starts from current visual state");

    controller.toggle_manual_zoom();
    state = step_frames_at(
        controller, 120, ticks, 0.76f, 0.34f);

    test.expect(
        !state.spotlight.runtime_requested &&
        std::fabs(state.spotlight.focus_mix) < 1.0e-5f &&
        std::fabs(state.spotlight.dim_mix) < 1.0e-5f,
        "Spotlight returns to exact pass-through after opening completes");
}


void test_region_boundary_tracks_hold_and_overview(
    TestContext &test)
{
    PresentationController controller;
    PresentationSettings settings;
    settings.smart_zoom = false;
    settings.presenter_controls = true;
    settings.zoom = 2.25f;

    controller.reset();
    controller.set_settings(settings);
    controller.set_hold_zoom(true);

    std::int64_t ticks = 0;
    auto state =
        step_frames_at(
            controller,
            160,
            ticks,
            0.78f,
            0.38f);

    const RECT region{
        -1400,
        120,
        200,
        1020};

    const RECT saved_boundary =
        arssyut::app::camera_viewport_rect(
            region,
            state.camera_center_x,
            state.camera_center_y,
            state.camera_zoom);

    test.expect(
        saved_boundary.left >= region.left &&
        saved_boundary.top >= region.top &&
        saved_boundary.right <= region.right &&
        saved_boundary.bottom <= region.bottom &&
        (saved_boundary.right - saved_boundary.left) <
            (region.right - region.left),
        "Hold Zoom boundary contracts inside the selected Region");

    controller.set_overview_peek(true);
    state =
        step_frames_at(
            controller,
            60,
            ticks,
            0.10f,
            0.90f);

    const RECT overview_boundary =
        arssyut::app::camera_viewport_rect(
            region,
            state.camera_center_x,
            state.camera_center_y,
            state.camera_zoom);

    test.expect(
        overview_boundary.left == region.left &&
        overview_boundary.top == region.top &&
        overview_boundary.right == region.right &&
        overview_boundary.bottom == region.bottom,
        "Overview Peek expands the native Region boundary to exact full frame");

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

    const RECT restored_boundary =
        arssyut::app::camera_viewport_rect(
            region,
            state.camera_center_x,
            state.camera_center_y,
            state.camera_zoom);

    test.expect(
        restored_boundary.left == saved_boundary.left &&
        restored_boundary.top == saved_boundary.top &&
        restored_boundary.right == saved_boundary.right &&
        restored_boundary.bottom == saved_boundary.bottom,
        "Overview Peek restores the exact saved Region viewport after pointer movement");
}

} // namespace

int main()
{
    TestContext test;

    test_toggle_and_reset(test);
    test_zoom_step_and_bounds(test);
    test_hold_zoom_press_release(test);
    test_reset_while_momentary_key_remains_held(test);
    test_toggle_hold_overlap_matrix(test);
    test_hold_smart_zoom_overlap(test);
    test_spotlight_state_is_bounded_and_master_is_not_runtime(test);
    test_spotlight_standalone_runtime_and_cursor_authority(test);
    test_spotlight_focus_size_controls_real_aperture(test);
    test_spotlight_cannot_change_camera_authority(test);
    test_spotlight_focus_modes_share_canonical_content_space(test);
    test_click_spotlight_owns_one_anchor_not_history(test);
    test_spotlight_reset_clears_transient_focus(test);
    test_freeze_camera_exact_hold_and_resume(test);
    test_freeze_mid_activation_and_reset_priority(test);
    test_zoom_off_clears_freeze(test);
    test_spotlight_cinematic_choreography_and_resize(test);
    test_spotlight_cinematic_frame_rate_acceptance(test);
    test_overview_peek_saved_shot(test);
    test_overview_cancel_when_zoom_intent_ends(test);
    test_region_boundary_tracks_hold_and_overview(test);

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