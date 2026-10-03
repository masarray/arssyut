#pragma once

#include "visual/arvisual_grade.hpp"

#include <cstddef>
#include <cstdint>

namespace arssyut::visual {

struct ArVisualSceneStats {
    float p10_luma = 0.10f;
    float median_luma = 0.40f;
    float p90_luma = 0.78f;
    float p98_luma = 0.92f;
    float mean_saturation = 0.30f;
    float p90_saturation = 0.72f;
    float shadow_frac = 0.0f;
    float near_clip_frac = 0.0f;
    float vivid_frac = 0.0f;
    float hot_vivid_frac = 0.0f;
    float neutral_frac = 0.30f;
    float colored_frac = 0.10f;

    // P5E screen topology evidence from the same fixed 64x36 analysis frame.
    // These are local-structure statistics, not semantic/OCR classification.
    float flat_frac = 0.0f;
    float neutral_flat_frac = 0.0f;
    float bright_neutral_flat_frac = 0.0f;
    float dark_neutral_flat_frac = 0.0f;
    float edge_frac = 0.0f;

    bool primed = false;
};

struct ArVisualAdaptiveState {
    float exposure = 0.0f;
    float pop = 1.0f;
    float highlight = 0.0f;
    float shadow = 0.0f;
    float strength = 1.0f;
    float chroma_limit = 0.985f;
    float clean = 0.0f;
    float separation = 0.0f;
    float white_ui = 0.0f;

    // P5E classifier/risk evidence. screen_ui is the broad screen-content
    // confidence; mixed_ui captures authored simultaneous dark + bright
    // neutral surfaces. color_risk/hot_risk remain true safety signals.
    float screen_ui = 0.0f;
    float mixed_ui = 0.0f;
    float color_risk = 0.0f;
    float hot_risk = 0.0f;
};

class ArVisualSceneModel final {
public:
    void reset() noexcept;

    [[nodiscard]] bool observe_bgra8(
        const std::uint8_t *pixels,
        std::size_t row_pitch,
        std::uint32_t width,
        std::uint32_t height,
        float dt_seconds) noexcept;

    [[nodiscard]] const ArVisualSceneStats &stats() const noexcept
    {
        return stats_;
    }

    [[nodiscard]] const ArVisualAdaptiveState &adaptive() const noexcept
    {
        return adaptive_;
    }

private:
    void update_adaptive() noexcept;

    ArVisualSceneStats stats_{};
    ArVisualAdaptiveState adaptive_{};
};

void apply_adaptive(
    ArVisualGradeSettings &grade,
    const ArVisualAdaptiveState &adaptive) noexcept;

} // namespace arssyut::visual
