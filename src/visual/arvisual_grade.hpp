#pragma once

#include <algorithm>

namespace arssyut::visual {

/*
 * P5A standalone ArVisual grading contract.
 *
 * Defaults intentionally preserve the validated upstream v0.5.9 base tuning,
 * while enabled=false keeps Arssyut Pixel Accurate by default. P5B owns
 * asynchronous scene analysis; until then adaptive inputs stay at the exact
 * neutral values expected by the upstream shader.
 */
struct ArVisualGradeSettings {
    bool enabled = false;

    float master = 1.0f;
    float enhance = 0.78f;
    float color_pop = 0.86f;
    float clean_white = 0.72f;
    float clarity = 0.68f;
    float skin_protect = 1.0f;
    float skin_beauty = 0.72f;
    float healthy_tone = 0.62f;
    float toy_gloss = 0.48f;
    float depth_pop = 0.76f;
    float highlight_guard = 0.94f;
    float performance = 1.0f;

    // P5A neutral adaptive inputs. P5B will update these asynchronously.
    float smart_exposure = 0.0f;
    float smart_pop = 1.0f;
    float smart_highlight = 0.0f;
    float smart_shadow = 0.0f;
    float smart_strength = 1.0f;
    float smart_chroma_limit = 0.985f;
    float smart_clean = 0.0f;
    float smart_separation = 0.0f;
};

[[nodiscard]] inline ArVisualGradeSettings sanitize(
    ArVisualGradeSettings value) noexcept
{
    value.master =
        std::clamp(value.master, 0.0f, 2.0f);
    value.enhance =
        std::clamp(value.enhance, 0.0f, 1.0f);
    value.color_pop =
        std::clamp(value.color_pop, 0.0f, 1.0f);
    value.clean_white =
        std::clamp(value.clean_white, 0.0f, 1.0f);
    value.clarity =
        std::clamp(value.clarity, 0.0f, 1.0f);
    value.skin_protect =
        std::clamp(value.skin_protect, 0.0f, 1.0f);
    value.skin_beauty =
        std::clamp(value.skin_beauty, 0.0f, 1.0f);
    value.healthy_tone =
        std::clamp(value.healthy_tone, 0.0f, 1.0f);
    value.toy_gloss =
        std::clamp(value.toy_gloss, 0.0f, 1.0f);
    value.depth_pop =
        std::clamp(value.depth_pop, 0.0f, 1.0f);
    value.highlight_guard =
        std::clamp(value.highlight_guard, 0.0f, 1.0f);
    value.performance =
        std::clamp(value.performance, 0.0f, 1.0f);

    value.smart_exposure =
        std::clamp(value.smart_exposure, -0.025f, 0.018f);
    value.smart_pop =
        std::clamp(value.smart_pop, 0.52f, 1.025f);
    value.smart_highlight =
        std::clamp(value.smart_highlight, 0.0f, 1.0f);
    value.smart_shadow =
        std::clamp(value.smart_shadow, 0.0f, 1.0f);
    value.smart_strength =
        std::clamp(value.smart_strength, 0.58f, 1.0f);
    value.smart_chroma_limit =
        std::clamp(value.smart_chroma_limit, 0.90f, 0.985f);
    value.smart_clean =
        std::clamp(value.smart_clean, 0.0f, 1.0f);
    value.smart_separation =
        std::clamp(value.smart_separation, 0.0f, 1.0f);

    return value;
}

} // namespace arssyut::visual
