#include "arzoom-spotlight.hpp"

#include <cassert>
#include <cmath>
#include <iostream>

namespace {

bool near(float a, float b, float eps = 1.0e-4f)
{
    return std::fabs(a - b) <= eps;
}

} // namespace

int main()
{
    using namespace arzoom;

    /* Master enable is configuration, not on-air state. */
    assert(!spotlight_runtime_requested(true, false, false, false));
    assert(spotlight_runtime_requested(true, true, false, false));
    assert(spotlight_runtime_requested(true, false, true, false));
    assert(spotlight_runtime_requested(true, false, false, true));
    assert(!spotlight_runtime_requested(false, true, true, true));

    /* Any Spotlight pass shares the cursor sampler ABI and therefore requires
     * fallback protection until a real cursor atlas is completely ready. */
    assert(spotlight_shared_pass_needs_cursor_fallback(true, false));
    assert(!spotlight_shared_pass_needs_cursor_fallback(true, true));
    assert(!spotlight_shared_pass_needs_cursor_fallback(false, false));

    /* P5.2 render ownership: the minimal solo pass exists only when Spotlight
     * is the sole visible presentation effect.  Existing camera/click/cursor
     * activity must stay owned by the accepted P4.1 shared renderer. */
    assert(spotlight_needs_solo_pass(true, false, false, false));
    assert(!spotlight_needs_solo_pass(true, true, false, false));
    assert(!spotlight_needs_solo_pass(true, false, true, false));
    assert(!spotlight_needs_solo_pass(true, false, false, true));
    assert(!spotlight_needs_solo_pass(false, false, false, false));

    const auto compact = spotlight_half_size_px(
        SpotlightSize::Compact, 1920.0f, 1080.0f);
    const auto balanced = spotlight_half_size_px(
        SpotlightSize::Balanced, 1920.0f, 1080.0f);
    const auto wide = spotlight_half_size_px(
        SpotlightSize::Wide, 1920.0f, 1080.0f);
    assert(compact.x < balanced.x && balanced.x < wide.x);
    assert(compact.y < balanced.y && balanced.y < wide.y);

    /* Circle is the P5.1 default visual and must remain exactly round even on
     * non-square output canvases. */
    const auto circle_100 = spotlight_focus_half_size_px(
        SpotlightShape::Circle, 100.0f, 1920.0f, 1080.0f);
    const auto circle_50 = spotlight_focus_half_size_px(
        SpotlightShape::Circle, 50.0f, 1920.0f, 1080.0f);
    const auto circle_200 = spotlight_focus_half_size_px(
        SpotlightShape::Circle, 200.0f, 1920.0f, 1080.0f);
    assert(near(circle_100.x, circle_100.y));
    assert(circle_50.x < circle_100.x && circle_100.x < circle_200.x);
    assert(near(circle_200.x / circle_100.x, 2.0f));

    const float circle_center = spotlight_circle_signed_distance_px(
        {0.0f, 0.0f}, circle_100.x);
    const float circle_edge = spotlight_circle_signed_distance_px(
        {circle_100.x, 0.0f}, circle_100.x);
    const float circle_outside = spotlight_circle_signed_distance_px(
        {circle_100.x + 80.0f, 0.0f}, circle_100.x);
    assert(circle_center < 0.0f);
    assert(near(circle_edge, 0.0f));
    assert(circle_outside > 0.0f);

    const SpotlightVec2 radius{300.0f, 180.0f};
    const float ellipse_center = spotlight_ellipse_signed_distance_px(
        {0.0f, 0.0f}, radius);
    const float ellipse_edge = spotlight_ellipse_signed_distance_px(
        {300.0f, 0.0f}, radius);
    const float ellipse_outside = spotlight_ellipse_signed_distance_px(
        {450.0f, 0.0f}, radius);
    assert(ellipse_center < 0.0f);
    assert(near(ellipse_edge, 0.0f));
    assert(ellipse_outside > 0.0f);

    const float rect_center = spotlight_rounded_rect_signed_distance_px(
        {0.0f, 0.0f}, {300.0f, 180.0f}, 40.0f);
    const float rect_outside = spotlight_rounded_rect_signed_distance_px(
        {420.0f, 260.0f}, {300.0f, 180.0f}, 40.0f);
    assert(rect_center < 0.0f);
    assert(rect_outside > 0.0f);

    assert(near(spotlight_scene_multiplier(200.0f, 90.0f, 0.38f, false),
                1.0f));
    assert(near(spotlight_scene_multiplier(-20.0f, 90.0f, 0.38f, true),
                1.0f));
    assert(near(spotlight_scene_multiplier(200.0f, 90.0f, 0.38f, true),
                0.62f));

    const float feather_near = spotlight_scene_multiplier(
        15.0f, 90.0f, 0.38f, true);
    const float feather_mid = spotlight_scene_multiplier(
        45.0f, 90.0f, 0.38f, true);
    const float feather_far = spotlight_scene_multiplier(
        75.0f, 90.0f, 0.38f, true);
    assert(feather_near > feather_mid);
    assert(feather_mid > feather_far);
    assert(feather_near < 1.0f && feather_far > 0.62f);

    const auto at_1080 = spotlight_focus_half_size_px(
        SpotlightShape::Circle, 100.0f, 1920.0f, 1080.0f);
    const auto at_4k = spotlight_focus_half_size_px(
        SpotlightShape::Circle, 100.0f, 3840.0f, 2160.0f);
    assert(near(at_4k.x / at_1080.x, 2.0f));
    assert(near(at_4k.y / at_1080.y, 2.0f));

    std::cout << "P5.2 Spotlight activation/geometry/render-routing invariants passed\n";
    return 0;
}
