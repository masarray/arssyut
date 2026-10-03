#include "visual/arvisual_grade.hpp"
#include "visual/arvisual_modes.hpp"
#include "visual/arvisual_scene_analysis.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

struct TestContext {
    int checks = 0;
    int failures = 0;

    void expect(bool condition, const char *message)
    {
        ++checks;
        if (!condition) {
            ++failures;
            std::cerr << "FAIL: " << message << "\n";
        }
    }
};

std::vector<std::uint8_t> solid_bgra(
    std::uint8_t b,
    std::uint8_t g,
    std::uint8_t r,
    std::uint8_t a = 255)
{
    constexpr std::uint32_t width = 64;
    constexpr std::uint32_t height = 36;

    std::vector<std::uint8_t> pixels(
        static_cast<std::size_t>(width) *
        static_cast<std::size_t>(height) *
        4u);

    for (std::size_t i = 0;
         i < pixels.size();
         i += 4) {
        pixels[i + 0] = b;
        pixels[i + 1] = g;
        pixels[i + 2] = r;
        pixels[i + 3] = a;
    }

    return pixels;
}

void test_neutral_scene(TestContext &test)
{
    auto pixels =
        solid_bgra(128, 128, 128);

    arssyut::visual::ArVisualSceneModel model;
    test.expect(
        model.observe_bgra8(
            pixels.data(),
            64u * 4u,
            64,
            36,
            0.20f),
        "Neutral scene produces an observation");

    const auto &stats = model.stats();
    const auto &adaptive = model.adaptive();

    test.expect(
        stats.primed,
        "Neutral scene primes scene model");
    test.expect(
        stats.neutral_frac > 0.99f,
        "Neutral scene is classified neutral");
    test.expect(
        stats.colored_frac < 0.01f,
        "Neutral scene is not classified colored");
    test.expect(
        adaptive.clean > 0.95f,
        "Neutral-dominant scene requests clean-white support");
    test.expect(
        adaptive.pop > 1.0f &&
            adaptive.pop <= 1.0251f,
        "Muted neutral scene only receives bounded pop lift");
    test.expect(
        adaptive.highlight < 0.01f,
        "Mid-gray scene does not create highlight pressure");
}

void test_bright_neutral_ui_scene(TestContext &test)
{
    auto pixels =
        solid_bgra(250, 250, 250);

    // Keep a small amount of saturated UI color so this models a real white
    // browser/document instead of a mathematically pure gray card.
    constexpr std::size_t colored_pixels = 24;
    for (std::size_t i = 0;
         i < colored_pixels;
         ++i) {
        const std::size_t offset = i * 4u;
        pixels[offset + 0] = 32;
        pixels[offset + 1] = 96;
        pixels[offset + 2] = 240;
        pixels[offset + 3] = 255;
    }

    arssyut::visual::ArVisualSceneModel model;
    test.expect(
        model.observe_bgra8(
            pixels.data(),
            64u * 4u,
            64,
            36,
            0.20f),
        "Bright neutral UI scene produces an observation");

    const auto &stats = model.stats();
    const auto &adaptive = model.adaptive();

    test.expect(
        stats.neutral_frac > 0.97f &&
            stats.mean_saturation < 0.03f &&
            stats.median_luma > 0.95f,
        "Bright browser-style scene is classified as neutral white UI");

    test.expect(
        adaptive.white_ui > 0.95f,
        "Neutral white UI classifier reaches high confidence");

    test.expect(
        adaptive.highlight < 0.20f,
        "Neutral white UI does not masquerade as clipped highlight risk");

    test.expect(
        adaptive.exposure > -0.005f,
        "Neutral white UI avoids global negative-exposure dimming");

    test.expect(
        adaptive.pop <= 1.001f &&
            adaptive.pop > 0.97f,
        "Neutral white UI never preloads a positive pop boost while small hot-color risk may still reduce pop");

    test.expect(
        adaptive.strength > 0.95f &&
            adaptive.chroma_limit > 0.98f,
        "Neutral white UI retains near-neutral creative safety state");

    test.expect(
        adaptive.clean > 0.95f,
        "Neutral white UI still requests clean-white support");
}

void test_hot_vivid_scene(TestContext &test)
{
    auto pixels =
        solid_bgra(8, 20, 255);

    arssyut::visual::ArVisualSceneModel model;
    test.expect(
        model.observe_bgra8(
            pixels.data(),
            64u * 4u,
            64,
            36,
            0.20f),
        "Hot vivid scene produces an observation");

    const auto &adaptive = model.adaptive();

    test.expect(
        adaptive.white_ui < 0.01f,
        "Hot vivid scene is never classified as neutral white UI");

    test.expect(
        adaptive.highlight > 0.95f,
        "Hot vivid scene drives highlight protection");
    test.expect(
        adaptive.pop <= 0.53f,
        "Hot vivid scene strongly reduces creative pop");
    test.expect(
        adaptive.strength <= 0.60f,
        "Hot vivid scene bounds total creative strength");
    test.expect(
        adaptive.chroma_limit <= 0.905f,
        "Hot vivid scene tightens chroma ceiling");
}

void test_dark_scene(TestContext &test)
{
    auto pixels =
        solid_bgra(20, 20, 20);

    arssyut::visual::ArVisualSceneModel model;
    test.expect(
        model.observe_bgra8(
            pixels.data(),
            64u * 4u,
            64,
            36,
            0.20f),
        "Dark active scene produces an observation");

    const auto &adaptive = model.adaptive();

    test.expect(
        adaptive.shadow > 0.95f,
        "Large dark area produces shadow pressure");
    test.expect(
        adaptive.exposure > 0.0f &&
            adaptive.exposure <= 0.0121f,
        "Dark scene receives only bounded positive exposure");
}

void test_time_based_ema(TestContext &test)
{
    auto neutral =
        solid_bgra(128, 128, 128);
    auto vivid =
        solid_bgra(8, 20, 255);

    arssyut::visual::ArVisualSceneModel model;
    (void)model.observe_bgra8(
        neutral.data(),
        64u * 4u,
        64,
        36,
        0.20f);

    const float initial_sat =
        model.stats().mean_saturation;

    (void)model.observe_bgra8(
        vivid.data(),
        64u * 4u,
        64,
        36,
        0.20f);

    const float after_one =
        model.stats().mean_saturation;

    test.expect(
        after_one > initial_sat &&
            after_one < 0.50f,
        "0.65-second EMA follows a cut without snapping");

    arssyut::visual::ArVisualGradeSettings grade;
    grade.enabled = true;

    arssyut::visual::apply_adaptive(
        grade,
        model.adaptive());

    test.expect(
        std::abs(
            grade.smart_pop -
            model.adaptive().pop) <
            0.00001f,
        "Adaptive output maps directly into P5A shader state");
}

void test_product_modes(TestContext &test)
{
    using arssyut::visual::ArVisualProductMode;
    using arssyut::visual::grade_for_mode;
    using arssyut::visual::product_mode_name;

    const auto pixel =
        grade_for_mode(
            ArVisualProductMode::PixelAccurate);
    const auto clean =
        grade_for_mode(
            ArVisualProductMode::CleanScreen);
    const auto vivid =
        grade_for_mode(
            ArVisualProductMode::VividPresentation);

    test.expect(
        !pixel.enabled &&
            !pixel.smart_auto,
        "Pixel Accurate is a true grade and analysis bypass");

    test.expect(
        clean.enabled &&
            clean.smart_auto,
        "Clean Screen enables bounded Smart Auto grading");

    test.expect(
        vivid.enabled &&
            vivid.smart_auto,
        "Vivid Presentation enables bounded Smart Auto grading");

    test.expect(
        clean.clean_white > vivid.clean_white &&
            clean.highlight_guard > vivid.highlight_guard,
        "Clean Screen prioritizes white and highlight safety");

    test.expect(
        clean.color_pop < vivid.color_pop &&
            clean.skin_beauty < vivid.skin_beauty &&
            clean.toy_gloss < vivid.toy_gloss &&
            clean.depth_pop < vivid.depth_pop,
        "Clean Screen keeps creative dose below Vivid Presentation");

    test.expect(
        std::abs(vivid.enhance - 0.78f) < 0.00001f &&
            std::abs(vivid.color_pop - 0.86f) < 0.00001f &&
            std::abs(vivid.clarity - 0.68f) < 0.00001f &&
            std::abs(vivid.depth_pop - 0.76f) < 0.00001f,
        "Vivid Presentation retains pinned P5A creative defaults");

    test.expect(
        std::string(
            product_mode_name(
                ArVisualProductMode::PixelAccurate)) ==
                "pixel_accurate" &&
            std::string(
                product_mode_name(
                    ArVisualProductMode::CleanScreen)) ==
                "clean_screen" &&
            std::string(
                product_mode_name(
                    ArVisualProductMode::VividPresentation)) ==
                "vivid_presentation",
        "Product mode diagnostic names are stable");
}

void test_ignored_pixels(TestContext &test)
{
    auto transparent =
        solid_bgra(255, 255, 255, 0);

    arssyut::visual::ArVisualSceneModel model;

    test.expect(
        !model.observe_bgra8(
            transparent.data(),
            64u * 4u,
            64,
            36,
            0.20f),
        "Transparent scene does not distort exposure statistics");

    auto black =
        solid_bgra(2, 2, 2, 255);

    test.expect(
        !model.observe_bgra8(
            black.data(),
            64u * 4u,
            64,
            36,
            0.20f),
        "Near-black letterbox scene is ignored");
}

} // namespace

int main()
{
    TestContext test;

    test_neutral_scene(test);
    test_bright_neutral_ui_scene(test);
    test_hot_vivid_scene(test);
    test_dark_scene(test);
    test_time_based_ema(test);
    test_product_modes(test);
    test_ignored_pixels(test);

    std::cout
        << "ArVisual scene analysis checks: "
        << test.checks
        << ", failures: "
        << test.failures
        << "\n";

    return test.failures == 0 ? 0 : 1;
}
