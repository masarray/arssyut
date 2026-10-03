#pragma once

#include "visual/arvisual_grade.hpp"

#include <cstdint>

namespace arssyut::visual {

enum class ArVisualProductMode : std::uint8_t {
    PixelAccurate = 0,
    CleanScreen = 1,
    VividPresentation = 2,
};

[[nodiscard]] inline const char *product_mode_name(
    ArVisualProductMode mode) noexcept
{
    switch (mode) {
    case ArVisualProductMode::CleanScreen:
        return "clean_screen";
    case ArVisualProductMode::VividPresentation:
        return "vivid_presentation";
    case ArVisualProductMode::PixelAccurate:
    default:
        return "pixel_accurate";
    }
}

[[nodiscard]] inline ArVisualGradeSettings grade_for_mode(
    ArVisualProductMode mode) noexcept
{
    ArVisualGradeSettings grade;

    switch (mode) {
    case ArVisualProductMode::CleanScreen:
        /*
         * Conservative UI/document polish.
         *
         * Emphasize neutral cleanup, highlight safety and modest local clarity
         * while deliberately suppressing saturation, beauty, gloss and depth
         * effects that can make engineering/UI capture look synthetic.
         */
        grade.enabled = true;
        grade.smart_auto = true;
        grade.master = 0.84f;
        grade.enhance = 0.52f;
        grade.color_pop = 0.28f;
        grade.clean_white = 0.90f;
        grade.clarity = 0.46f;
        grade.skin_protect = 1.00f;
        grade.skin_beauty = 0.18f;
        grade.healthy_tone = 0.12f;
        grade.toy_gloss = 0.08f;
        grade.depth_pop = 0.30f;
        grade.highlight_guard = 0.98f;
        grade.performance = 1.00f;
        grade.text_legibility = 0.56f;
        grade.ui_structure = 0.72f;
        return sanitize(grade);

    case ArVisualProductMode::VividPresentation:
        /*
         * Presentation-first mode.
         *
         * Retain the pinned P5A v0.5.9 creative defaults and let P5B Smart
         * Auto reduce the dose on risky/highlight-heavy scenes.
         */
        grade.enabled = true;
        grade.smart_auto = true;
        grade.master = 1.00f;
        grade.enhance = 0.78f;
        grade.color_pop = 0.86f;
        grade.clean_white = 0.72f;
        grade.clarity = 0.68f;
        grade.skin_protect = 1.00f;
        grade.skin_beauty = 0.72f;
        grade.healthy_tone = 0.62f;
        grade.toy_gloss = 0.48f;
        grade.depth_pop = 0.76f;
        grade.highlight_guard = 0.94f;
        grade.performance = 1.00f;
        grade.text_legibility = 0.34f;
        grade.ui_structure = 0.38f;
        return sanitize(grade);

    case ArVisualProductMode::PixelAccurate:
    default:
        /*
         * True bypass. Smart Auto is disabled too, so Pixel Accurate does not
         * schedule any P5B scene-analysis work.
         */
        grade.enabled = false;
        grade.smart_auto = false;
        return sanitize(grade);
    }
}

} // namespace arssyut::visual
