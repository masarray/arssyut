#include "app/region_geometry.hpp"

#ifdef _WIN32

#include <iostream>

namespace {

struct TestContext {
    int checks = 0;
    int failures = 0;

    void expect(
        bool condition,
        const char *message)
    {
        ++checks;
        if (!condition) {
            ++failures;
            std::cerr
                << "FAIL: "
                << message
                << '\n';
        }
    }
};

void test_negative_origin_mapping(
    TestContext &test)
{
    const RECT monitor{
        -1920,
        0,
        0,
        1080};
    const RECT selection{
        -1600,
        100,
        -800,
        700};

    arssyut::app::RegionCropMapping mapping;
    const bool ok =
        arssyut::app::
            map_region_to_crop(
                monitor,
                selection,
                mapping);

    test.expect(
        ok,
        "negative-origin Region maps");

    test.expect(
        mapping.crop.left == 320 &&
            mapping.crop.top == 100 &&
            mapping.crop.right == 1120 &&
            mapping.crop.bottom == 700,
        "negative-origin mapping is source-relative");

    test.expect(
        mapping.output_size.width == 800 &&
            mapping.output_size.height == 600,
        "Region output keeps native selected size");
}

void test_even_nv12_alignment(
    TestContext &test)
{
    const RECT monitor{
        0,
        0,
        1920,
        1080};
    const RECT selection{
        101,
        51,
        902,
        652};

    arssyut::app::RegionCropMapping mapping;
    const bool ok =
        arssyut::app::
            map_region_to_crop(
                monitor,
                selection,
                mapping);

    test.expect(
        ok,
        "odd Region maps");

    test.expect(
        mapping.output_size.width == 800 &&
            mapping.output_size.height == 600,
        "NV12 alignment trims only right/bottom");

    test.expect(
        mapping.screen_rect.left == 101 &&
            mapping.screen_rect.top == 51 &&
            mapping.screen_rect.right == 901 &&
            mapping.screen_rect.bottom == 651,
        "alignment preserves Region origin");
}

void test_camera_viewport(
    TestContext &test)
{
    const RECT region{
        100,
        200,
        1700,
        1100};

    const RECT centered =
        arssyut::app::
            camera_viewport_rect(
                region,
                0.5f,
                0.5f,
                2.0f);

    test.expect(
        centered.right -
                centered.left ==
            800 &&
            centered.bottom -
                centered.top ==
            450,
        "2x Smart Zoom boundary halves Region dimensions");

    test.expect(
        centered.left == 500 &&
            centered.top == 425,
        "centered Smart Zoom boundary preserves camera center");

    const RECT edge =
        arssyut::app::
            camera_viewport_rect(
                region,
                0.0f,
                0.0f,
                2.0f);

    test.expect(
        edge.left == region.left &&
            edge.top == region.top,
        "Smart Zoom boundary clamps at Region top-left");

    test.expect(
        edge.right <= region.right &&
            edge.bottom <= region.bottom,
        "Smart Zoom boundary never escapes Region");
}

void test_clamp_and_default(
    TestContext &test)
{
    const RECT monitor{
        100,
        200,
        1700,
        1100};

    const RECT clamped =
        arssyut::app::
            clamp_region_rect(
                RECT{
                    -500,
                    -500,
                    200,
                    100},
                monitor);

    test.expect(
        clamped.left == 100 &&
            clamped.top == 200,
        "Region clamps to monitor origin");

    test.expect(
        clamped.right >
            clamped.left &&
            clamped.bottom >
                clamped.top,
        "clamped Region remains valid");

    const RECT preset =
        arssyut::app::
            default_region_rect(
                monitor);

    test.expect(
        preset.left >= monitor.left &&
            preset.top >= monitor.top &&
            preset.right <= monitor.right &&
            preset.bottom <= monitor.bottom,
        "default Region stays inside monitor");

    test.expect(
        preset.right - preset.left >= 320 &&
            preset.bottom - preset.top >= 180,
        "default Region respects minimum size");
}

} // namespace

int main()
{
    TestContext test;

    test_negative_origin_mapping(test);
    test_even_nv12_alignment(test);
    test_camera_viewport(test);
    test_clamp_and_default(test);

    std::cout
        << "Region geometry checks: "
        << test.checks
        << ", failures: "
        << test.failures
        << '\n';

    return test.failures == 0
        ? 0
        : 1;
}

#endif
