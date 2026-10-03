#include "visual/arvisual_scene_analysis.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace arssyut::visual {

namespace {

[[nodiscard]] float percentile(
    const std::array<std::uint32_t, 256> &bins,
    std::uint32_t total,
    float q) noexcept
{
    const std::uint32_t wanted =
        std::max(
            1u,
            static_cast<std::uint32_t>(
                std::ceil(
                    q *
                    static_cast<float>(total))));

    std::uint32_t cumulative = 0;
    for (std::uint32_t i = 0;
         i < bins.size();
         ++i) {
        cumulative += bins[i];
        if (cumulative >= wanted) {
            return static_cast<float>(i) *
                (1.0f / 255.0f);
        }
    }

    return 1.0f;
}

} // namespace

void ArVisualSceneModel::reset() noexcept
{
    stats_ = {};
    adaptive_ = {};
}

bool ArVisualSceneModel::observe_bgra8(
    const std::uint8_t *pixels,
    std::size_t row_pitch,
    std::uint32_t width,
    std::uint32_t height,
    float dt_seconds) noexcept
{
    if (!pixels ||
        width == 0 ||
        height == 0 ||
        row_pitch <
            static_cast<std::size_t>(width) * 4u) {
        return false;
    }

    std::array<std::uint32_t, 256> histogram{};
    std::array<std::uint32_t, 256> saturation_histogram{};

    double sum_saturation = 0.0;
    std::uint32_t active = 0;
    std::uint32_t saturation_samples = 0;
    std::uint32_t shadows = 0;
    std::uint32_t near_clip = 0;
    std::uint32_t vivid = 0;
    std::uint32_t hot_vivid = 0;
    std::uint32_t neutral = 0;
    std::uint32_t colored = 0;

    for (std::uint32_t row = 0;
         row < height;
         ++row) {
        const std::uint8_t *px =
            pixels +
            static_cast<std::size_t>(row) *
                row_pitch;

        for (std::uint32_t col = 0;
             col < width;
             ++col, px += 4) {
            // Analysis target is DXGI_FORMAT_B8G8R8A8_UNORM.
            const float b =
                px[0] * (1.0f / 255.0f);
            const float g =
                px[1] * (1.0f / 255.0f);
            const float r =
                px[2] * (1.0f / 255.0f);
            const float a =
                px[3] * (1.0f / 255.0f);

            const float y =
                0.2126f * r +
                0.7152f * g +
                0.0722f * b;
            const float max_c =
                std::max({r, g, b});

            if (a < 0.035f ||
                (y < 0.020f &&
                 max_c < 0.035f)) {
                continue;
            }

            const float min_c =
                std::min({r, g, b});
            const float saturation =
                max_c > 0.001f
                    ? (max_c - min_c) / max_c
                    : 0.0f;

            const std::uint32_t bin =
                std::min(
                    static_cast<std::uint32_t>(
                        y * 255.0f + 0.5f),
                    255u);
            histogram[bin]++;
            active++;

            if (y >= 0.070f) {
                sum_saturation += saturation;
                const std::uint32_t saturation_bin =
                    std::min(
                        static_cast<std::uint32_t>(
                            saturation * 255.0f +
                            0.5f),
                        255u);
                saturation_histogram[saturation_bin]++;
                saturation_samples++;
            }

            if (y < 0.095f)
                shadows++;
            if (max_c >= 0.975f)
                near_clip++;
            if (saturation >= 0.78f)
                vivid++;
            if (saturation >= 0.70f &&
                max_c >= 0.92f) {
                hot_vivid++;
            }
            if (y >= 0.15f &&
                saturation < 0.12f) {
                neutral++;
            }
            if (y >= 0.08f &&
                saturation > 0.35f) {
                colored++;
            }
        }
    }

    /*
     * P5E screen-topology pass.
     *
     * The production analyzer is a fixed 64x36 texture, so a second pass over
     * ~2304 pixels is cheap and avoids any heap/storage growth. We measure
     * authored flat neutral surfaces and local edge density rather than trying
     * to recognize text/windows semantically.
     */
    std::uint32_t topology_samples = 0;
    std::uint32_t flat = 0;
    std::uint32_t neutral_flat = 0;
    std::uint32_t bright_neutral_flat = 0;
    std::uint32_t dark_neutral_flat = 0;
    std::uint32_t edges = 0;

    const auto sample_y_sat =
        [](const std::uint8_t *sample,
           float &y,
           float &saturation) noexcept -> bool {
            const float b =
                sample[0] * (1.0f / 255.0f);
            const float g =
                sample[1] * (1.0f / 255.0f);
            const float r =
                sample[2] * (1.0f / 255.0f);
            const float a =
                sample[3] * (1.0f / 255.0f);

            y =
                0.2126f * r +
                0.7152f * g +
                0.0722f * b;

            const float max_c =
                std::max({r, g, b});
            if (a < 0.035f ||
                (y < 0.020f &&
                 max_c < 0.035f)) {
                saturation = 0.0f;
                return false;
            }

            const float min_c =
                std::min({r, g, b});
            saturation =
                max_c > 0.001f
                    ? (max_c - min_c) / max_c
                    : 0.0f;
            return true;
        };

    for (std::uint32_t row = 0;
         row < height;
         ++row) {
        const std::uint8_t *row_px =
            pixels +
            static_cast<std::size_t>(row) *
                row_pitch;

        for (std::uint32_t col = 0;
             col < width;
             ++col) {
            const std::uint8_t *px =
                row_px +
                static_cast<std::size_t>(col) *
                    4u;

            float y = 0.0f;
            float saturation = 0.0f;
            if (!sample_y_sat(
                    px,
                    y,
                    saturation)) {
                continue;
            }

            float max_delta = 0.0f;
            bool have_neighbor = false;

            if (col + 1 < width) {
                float neighbor_y = 0.0f;
                float neighbor_sat = 0.0f;
                if (sample_y_sat(
                        px + 4,
                        neighbor_y,
                        neighbor_sat)) {
                    max_delta =
                        std::max(
                            max_delta,
                            std::abs(
                                y -
                                neighbor_y));
                    have_neighbor = true;
                }
            }

            if (row + 1 < height) {
                const std::uint8_t *below =
                    pixels +
                    static_cast<std::size_t>(
                        row + 1) *
                        row_pitch +
                    static_cast<std::size_t>(col) *
                        4u;

                float neighbor_y = 0.0f;
                float neighbor_sat = 0.0f;
                if (sample_y_sat(
                        below,
                        neighbor_y,
                        neighbor_sat)) {
                    max_delta =
                        std::max(
                            max_delta,
                            std::abs(
                                y -
                                neighbor_y));
                    have_neighbor = true;
                }
            }

            if (!have_neighbor)
                continue;

            ++topology_samples;

            if (max_delta <= 0.018f) {
                ++flat;

                if (saturation < 0.12f) {
                    ++neutral_flat;

                    if (y >= 0.72f)
                        ++bright_neutral_flat;
                    if (y <= 0.24f)
                        ++dark_neutral_flat;
                }
            }

            if (max_delta >= 0.045f)
                ++edges;
        }
    }

    if (active < 8)
        return false;

    const float p10 =
        percentile(histogram, active, 0.10f);
    const float p50 =
        percentile(histogram, active, 0.50f);
    const float p90 =
        percentile(histogram, active, 0.90f);
    const float p98 =
        percentile(histogram, active, 0.98f);

    const float mean_sat =
        saturation_samples
            ? static_cast<float>(
                  sum_saturation /
                  static_cast<double>(
                      saturation_samples))
            : 0.0f;

    const float p90_sat =
        saturation_samples
            ? percentile(
                  saturation_histogram,
                  saturation_samples,
                  0.90f)
            : 0.0f;

    const float inv_active =
        1.0f /
        static_cast<float>(active);

    const float frac_shadow =
        static_cast<float>(shadows) *
        inv_active;
    const float frac_near_clip =
        static_cast<float>(near_clip) *
        inv_active;
    const float frac_vivid =
        static_cast<float>(vivid) *
        inv_active;
    const float frac_hot_vivid =
        static_cast<float>(hot_vivid) *
        inv_active;
    const float frac_neutral =
        static_cast<float>(neutral) *
        inv_active;
    const float frac_colored =
        static_cast<float>(colored) *
        inv_active;

    const float inv_topology =
        topology_samples > 0
            ? 1.0f /
                static_cast<float>(
                    topology_samples)
            : 0.0f;

    const float frac_flat =
        static_cast<float>(flat) *
        inv_topology;
    const float frac_neutral_flat =
        static_cast<float>(neutral_flat) *
        inv_topology;
    const float frac_bright_neutral_flat =
        static_cast<float>(
            bright_neutral_flat) *
        inv_topology;
    const float frac_dark_neutral_flat =
        static_cast<float>(
            dark_neutral_flat) *
        inv_topology;
    const float frac_edges =
        static_cast<float>(edges) *
        inv_topology;

    auto &s = stats_;

    if (!s.primed) {
        s.p10_luma = p10;
        s.median_luma = p50;
        s.p90_luma = p90;
        s.p98_luma = p98;
        s.mean_saturation = mean_sat;
        s.p90_saturation = p90_sat;
        s.shadow_frac = frac_shadow;
        s.near_clip_frac = frac_near_clip;
        s.vivid_frac = frac_vivid;
        s.hot_vivid_frac = frac_hot_vivid;
        s.neutral_frac = frac_neutral;
        s.colored_frac = frac_colored;
        s.flat_frac = frac_flat;
        s.neutral_flat_frac =
            frac_neutral_flat;
        s.bright_neutral_flat_frac =
            frac_bright_neutral_flat;
        s.dark_neutral_flat_frac =
            frac_dark_neutral_flat;
        s.edge_frac = frac_edges;
        s.primed = true;
    } else {
        const float dt =
            std::clamp(
                dt_seconds,
                0.001f,
                1.0f);
        const float alpha =
            1.0f -
            std::exp(-dt / 0.65f);

        s.p10_luma +=
            (p10 - s.p10_luma) * alpha;
        s.median_luma +=
            (p50 - s.median_luma) * alpha;
        s.p90_luma +=
            (p90 - s.p90_luma) * alpha;
        s.p98_luma +=
            (p98 - s.p98_luma) * alpha;
        s.mean_saturation +=
            (mean_sat - s.mean_saturation) * alpha;
        s.p90_saturation +=
            (p90_sat - s.p90_saturation) * alpha;
        s.shadow_frac +=
            (frac_shadow - s.shadow_frac) * alpha;
        s.near_clip_frac +=
            (frac_near_clip - s.near_clip_frac) * alpha;
        s.vivid_frac +=
            (frac_vivid - s.vivid_frac) * alpha;
        s.hot_vivid_frac +=
            (frac_hot_vivid - s.hot_vivid_frac) * alpha;
        s.neutral_frac +=
            (frac_neutral - s.neutral_frac) * alpha;
        s.colored_frac +=
            (frac_colored - s.colored_frac) * alpha;
        s.flat_frac +=
            (frac_flat - s.flat_frac) * alpha;
        s.neutral_flat_frac +=
            (frac_neutral_flat -
             s.neutral_flat_frac) *
            alpha;
        s.bright_neutral_flat_frac +=
            (frac_bright_neutral_flat -
             s.bright_neutral_flat_frac) *
            alpha;
        s.dark_neutral_flat_frac +=
            (frac_dark_neutral_flat -
             s.dark_neutral_flat_frac) *
            alpha;
        s.edge_frac +=
            (frac_edges - s.edge_frac) * alpha;
    }

    update_adaptive();
    return true;
}

void ArVisualSceneModel::update_adaptive() noexcept
{
    const auto &s = stats_;
    if (!s.primed) {
        adaptive_ = {};
        return;
    }

    /*
     * Screen-capture calibration:
     *
     * A browser/document can legitimately contain an almost full-screen
     * neutral white canvas. Treating that as the same thing as clipped,
     * colorful highlights caused P5C calibration captures to enter
     * highlight=1 / exposure=-0.025 on otherwise healthy white UI. That state
     * then bled into the next colorful scene through the 0.65 s EMA.
     *
     * Detect only the very specific "bright + overwhelmingly neutral +
     * low-chroma" topology. Photos, colorful highlights, dark IDEs and vivid
     * scenes do not satisfy all three gates and retain the pinned P5B logic.
     */
    const float bright_neutral_ui =
        std::clamp(
            (s.neutral_frac - 0.70f) / 0.25f,
            0.0f,
            1.0f) *
        (1.0f -
         std::clamp(
             (s.mean_saturation - 0.04f) / 0.12f,
             0.0f,
             1.0f)) *
        std::clamp(
            (s.median_luma - 0.82f) / 0.14f,
            0.0f,
            1.0f);

    adaptive_.white_ui =
        bright_neutral_ui;

    /*
     * P5E screen-native confidence.
     *
     * Screen UI is characterized by authored flat neutral surfaces much more
     * than by a camera histogram. This intentionally uses only local luma /
     * saturation topology from the fixed 64x36 sample: no OCR, window
     * detection or application-specific heuristics.
     */
    const float low_chroma_ui =
        1.0f -
        std::clamp(
            (s.mean_saturation - 0.10f) / 0.20f,
            0.0f,
            1.0f);
    const float neutral_flat_score =
        std::clamp(
            (s.neutral_flat_frac - 0.24f) / 0.56f,
            0.0f,
            1.0f);
    const float flat_score =
        std::clamp(
            (s.flat_frac - 0.38f) / 0.48f,
            0.0f,
            1.0f);

    const float screen_ui =
        std::max(
            bright_neutral_ui,
            neutral_flat_score *
                low_chroma_ui *
                (0.72f + flat_score * 0.28f));

    const float bright_surface =
        std::clamp(
            (s.bright_neutral_flat_frac - 0.04f) /
                0.20f,
            0.0f,
            1.0f);
    const float dark_surface =
        std::clamp(
            (s.dark_neutral_flat_frac - 0.04f) /
                0.24f,
            0.0f,
            1.0f);

    const float mixed_ui =
        std::min(
            bright_surface,
            dark_surface) *
        screen_ui;

    adaptive_.screen_ui =
        std::clamp(
            std::max(
                screen_ui,
                mixed_ui),
            0.0f,
            1.0f);
    adaptive_.mixed_ui =
        std::clamp(
            mixed_ui,
            0.0f,
            1.0f);

    const float upper_key =
        s.p90_luma * 0.72f +
        s.p98_luma * 0.28f;

    const float raw_exposure =
        std::clamp(
            (0.64f - upper_key) * 0.09f,
            -0.025f,
            0.012f);

    const float neutral_exposure_guard =
        1.0f - bright_neutral_ui * 0.88f;

    adaptive_.exposure =
        raw_exposure < 0.0f
            ? raw_exposure *
                neutral_exposure_guard
            : raw_exposure;

    const float p90_pressure =
        std::clamp(
            (s.p90_luma - 0.80f) / 0.16f,
            0.0f,
            1.0f);
    const float p98_pressure =
        std::clamp(
            (s.p98_luma - 0.91f) / 0.075f,
            0.0f,
            1.0f);
    const float clip_pressure =
        std::clamp(
            (s.near_clip_frac - 0.012f) * 10.0f,
            0.0f,
            1.0f);
    const float hot_pressure =
        std::clamp(
            (s.hot_vivid_frac - 0.008f) * 12.0f,
            0.0f,
            1.0f);

    const float neutral_highlight_guard =
        1.0f - bright_neutral_ui * 0.88f;

    const float neutral_luma_pressure =
        std::max({
            p90_pressure * 0.70f,
            p98_pressure,
            clip_pressure
        }) *
        neutral_highlight_guard;

    adaptive_.highlight =
        std::max(
            neutral_luma_pressure,
            hot_pressure);

    const float sat_pressure =
        std::clamp(
            (s.mean_saturation - 0.32f) / 0.34f,
            0.0f,
            1.0f);
    const float tail_sat_pressure =
        std::clamp(
            (s.p90_saturation - 0.68f) / 0.28f,
            0.0f,
            1.0f);
    const float vivid_pressure =
        std::clamp(
            (s.vivid_frac - 0.16f) / 0.40f,
            0.0f,
            1.0f);
    const float color_risk =
        std::max({
            sat_pressure,
            tail_sat_pressure,
            vivid_pressure,
            hot_pressure
        });

    adaptive_.color_risk =
        color_risk;
    adaptive_.hot_risk =
        hot_pressure;

    const float muted_lift =
        std::clamp(
            (0.18f - s.mean_saturation) * 0.16f,
            0.0f,
            0.025f) *
        (1.0f - bright_neutral_ui);

    adaptive_.pop =
        std::clamp(
            1.0f +
                muted_lift -
                color_risk * 0.48f,
            0.52f,
            1.025f);

    adaptive_.strength =
        std::clamp(
            1.0f -
                color_risk * 0.34f -
                adaptive_.highlight * 0.16f,
            0.58f,
            1.0f);

    adaptive_.chroma_limit =
        std::clamp(
            0.985f -
                color_risk * 0.060f -
                adaptive_.highlight * 0.025f,
            0.90f,
            0.985f);

    const float neutral_dominance =
        std::clamp(
            (s.neutral_frac - 0.30f) / 0.45f,
            0.0f,
            1.0f);
    const float colored_presence =
        std::clamp(
            (s.colored_frac - 0.04f) / 0.22f,
            0.0f,
            1.0f);
    const float colorful_scene =
        std::clamp(
            (s.mean_saturation - 0.24f) / 0.25f,
            0.0f,
            1.0f);

    adaptive_.clean =
        neutral_dominance *
        (1.0f - colorful_scene);

    adaptive_.separation =
        neutral_dominance *
        colored_presence *
        (1.0f -
         adaptive_.highlight * 0.45f);

    const float low_percentile_pressure =
        std::clamp(
            (0.085f - s.p10_luma) / 0.075f,
            0.0f,
            1.0f);
    const float shadow_area_pressure =
        std::clamp(
            (s.shadow_frac - 0.24f) * 2.2f,
            0.0f,
            1.0f);

    adaptive_.shadow =
        std::max(
            low_percentile_pressure,
            shadow_area_pressure);
}

void apply_adaptive(
    ArVisualGradeSettings &grade,
    const ArVisualAdaptiveState &adaptive) noexcept
{
    /*
     * P5E keeps the scene model capable of expressing camera-style pressure,
     * then converts that evidence according to the selected product mode.
     * screen_native therefore changes policy, not measurement.
     *
     * For screen UI, global exposure/shadow/neutral-highlight normalization is
     * strongly suppressed. Color/hot-vivid risk remains authoritative and may
     * still reduce pop, strength and chroma ceiling.
     */
    const float screen_confidence =
        std::clamp(
            std::max(
                adaptive.screen_ui,
                adaptive.mixed_ui),
            0.0f,
            1.0f);

    const float screen_weight =
        std::clamp(
            grade.screen_native *
                screen_confidence,
            0.0f,
            1.0f);

    const auto blend =
        [screen_weight](
            float camera_value,
            float screen_value) noexcept {
            return camera_value +
                (screen_value - camera_value) *
                    screen_weight;
        };

    const float screen_exposure =
        adaptive.exposure * 0.05f;
    const float screen_highlight =
        std::max(
            adaptive.hot_risk,
            adaptive.highlight * 0.10f);
    const float screen_shadow =
        adaptive.shadow * 0.08f;
    const float screen_pop =
        std::min(
            adaptive.pop,
            1.0f);
    const float screen_strength =
        std::clamp(
            1.0f -
                adaptive.color_risk * 0.34f -
                adaptive.hot_risk * 0.08f,
            0.66f,
            1.0f);
    const float screen_chroma_limit =
        std::clamp(
            0.985f -
                adaptive.color_risk * 0.060f -
                adaptive.hot_risk * 0.015f,
            0.91f,
            0.985f);

    grade.smart_exposure =
        blend(
            adaptive.exposure,
            screen_exposure);
    grade.smart_pop =
        blend(
            adaptive.pop,
            screen_pop);
    grade.smart_highlight =
        blend(
            adaptive.highlight,
            screen_highlight);
    grade.smart_shadow =
        blend(
            adaptive.shadow,
            screen_shadow);
    grade.smart_strength =
        blend(
            adaptive.strength,
            screen_strength);
    grade.smart_chroma_limit =
        blend(
            adaptive.chroma_limit,
            screen_chroma_limit);
    grade.smart_clean =
        adaptive.clean;
    grade.smart_separation =
        adaptive.separation;
    grade.smart_screen_ui =
        screen_weight;
}

} // namespace arssyut::visual
