#include "platform/windows/graphics/d3d11_compositor.hpp"

#ifdef _WIN32

#include "core/time/monotonic_clock.hpp"
#include "platform/windows/graphics/d3d11_arvisual_scene_analyzer.hpp"

#include <d3dcompiler.h>

#include <algorithm>
#include <cwchar>
#include <limits>
#include <new>
#include <utility>

namespace arssyut::windows {

namespace {

using arssyut::core::CropRect;
using arssyut::core::FrameSize;
using arssyut::core::MonotonicClock;
using arssyut::core::Result;
using arssyut::core::Status;
using arssyut::core::StatusCode;
using arssyut::core::TimePoint;

constexpr char shader_source[] = R"(
cbuffer PresentationConstants : register(b0)
{
    float4 uv_rect;
    float4 camera_keyboard;
    float4 output_info;
    float4 keyboard_rect;
    float4 arvisual0;
    float4 arvisual1;
    float4 arvisual2;
    float4 arvisual3;
    float4 arvisual4;
    float4 arvisual5;
    float4 arvisual6;
    float4 click0;
    float4 click1;
    float4 click2;
    float4 click3;
};

Texture2D source_texture : register(t0);
Texture2D keyboard_texture : register(t1);
SamplerState source_sampler : register(s0);

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

VertexOutput vs_main(uint vertex_id : SV_VertexID)
{
    VertexOutput output;
    float2 uv = float2((vertex_id << 1) & 2, vertex_id & 2);
    output.uv = uv;
    output.position = float4(
        uv.x * 2.0f - 1.0f,
        1.0f - uv.y * 2.0f,
        0.0f,
        1.0f);
    return output;
}

/* ArVisual v0.5.9 portable shader behavior adapted from
 * masarray/arvisual-obs@d0a3f405447446e88dc56f4a50535a17257fcccf.
 * OBS plumbing and scene-readback code are intentionally excluded in P5A.
 * Adaptive inputs are neutral until P5B owns asynchronous analysis. */
float arvisual_luminance(float3 color)
{
    return dot(color, float3(0.2126, 0.7152, 0.0722));
}

float3 arvisual_rgb2hsv(float3 c)
{
    float4 K = float4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
    float4 p = c.g < c.b ? float4(c.bg, K.wz) : float4(c.gb, K.xy);
    float4 q = c.r < p.x ? float4(p.xyw, c.r) : float4(c.r, p.yzx);
    float d = q.x - min(q.w, q.y);
    float e = 1.0e-10;
    return float3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}

float3 arvisual_hsv2rgb(float3 c)
{
    float4 K = float4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    float3 p = abs(frac(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * lerp(K.xxx, saturate(p - K.xxx), saturate(c.y));
}

float arvisual_hue_mask(float hue, float center, float width)
{
    float d = abs(hue - center);
    d = min(d, 1.0 - d);
    return saturate(1.0 - d / width);
}

float arvisual_hue_lerp_wrap(float h, float target, float t)
{
    float d = target - h;
    d -= floor(d + 0.5);
    return frac(h + d * t + 1.0);
}

/* Move chroma toward a target luminance, then shrink chroma just enough for
   every channel to fit a safe inner gamut. Pure source black/white can remain
   pure, but a creative operation cannot manufacture a digital 0/1 plateau.
   Because chroma is measured around its own luma, this preserves target luma
   and hue without hard per-channel clipping. */
float3 arvisual_fit_gamut_preserve_luma(float3 color, float target_luma)
{
    float y = saturate(target_luma);
    float source_y = arvisual_luminance(color);
    float3 chroma = color - float3(source_y, source_y, source_y);
    float low_bound = min(y, 0.008);
    float high_bound = max(y, 0.992);
    float scale = 1.0;

    if (chroma.r > 0.0)
        scale = min(scale, (high_bound - y) / max(chroma.r, 0.000001));
    else if (chroma.r < 0.0)
        scale = min(scale, (y - low_bound) / max(-chroma.r, 0.000001));

    if (chroma.g > 0.0)
        scale = min(scale, (high_bound - y) / max(chroma.g, 0.000001));
    else if (chroma.g < 0.0)
        scale = min(scale, (y - low_bound) / max(-chroma.g, 0.000001));

    if (chroma.b > 0.0)
        scale = min(scale, (high_bound - y) / max(chroma.b, 0.000001));
    else if (chroma.b < 0.0)
        scale = min(scale, (y - low_bound) / max(-chroma.b, 0.000001));

    return saturate(float3(y, y, y) + chroma * saturate(scale));
}

float arvisual_soft_luma_shoulder(float y, float knee, float pressure)
{
    float over = max(y - knee, 0.0);
    float rolled = y - over + over / (1.0 + over * (2.6 + pressure * 3.0));
    return min(rolled, 0.985);
}

/* Alpha-correct neighbor tap. */
float3 arvisual_tap(float2 uv)
{
    float4 t = source_texture.Sample(source_sampler, uv);
    float inv_a = (t.a > 0.0) ? (1.0 / t.a) : 0.0;
    return saturate(t.rgb * inv_a);
}

float4 apply_arvisual(float4 px, float2 uv)
{
    if (arvisual0.x < 0.5)
        return px;

    const float master = arvisual0.y;
    const float enhance = arvisual0.z;
    const float color_pop = arvisual0.w;
    const float clean_white = arvisual1.x;
    const float clarity = arvisual1.y;
    const float skin_protect = arvisual1.z;
    const float skin_beauty = arvisual1.w;
    const float healthy_tone = arvisual2.x;
    const float toy_gloss = arvisual2.y;
    const float depth_pop = arvisual2.z;
    const float highlight_guard = arvisual2.w;
    const float performance = arvisual3.x;
    const float smart_exposure = arvisual3.y;
    const float smart_pop = arvisual3.z;
    const float smart_highlight = arvisual3.w;
    const float smart_shadow = arvisual4.x;
    const float smart_strength = arvisual4.y;
    const float smart_chroma_limit = arvisual4.z;
    const float smart_clean = arvisual4.w;
    const float smart_separation = arvisual5.x;
    const float2 texel_size = max(arvisual5.yz, float2(0.0000001, 0.0000001));
    const float text_legibility = saturate(arvisual5.w);
    const float ui_structure = saturate(arvisual6.x);

    float alpha = px.a;
    float3 src = saturate(px.rgb * ((alpha > 0.0) ? (1.0 / alpha) : 0.0));
    float3 src_hsv = arvisual_rgb2hsv(src);
    float src_y = arvisual_luminance(src);

    /* Smart Auto may lower the complete creative dose on risky scenes. User
       controls still work normally; safety controls are never weakened. */
    float k = max(master, 0.0) * saturate(smart_strength);
    float f_enhance = saturate(enhance * k);
    float f_pop = saturate(color_pop * k * smart_pop);
    float f_clarity = saturate(clarity * k);
    float f_beauty = saturate(skin_beauty * k);
    float f_healthy = saturate(healthy_tone * k);
    float f_gloss = saturate(toy_gloss * k);
    float f_depth = saturate(depth_pop * k);

    /* Neutral and skin classification is based on the unmodified source. */
    float low_sat = 1.0 - smoothstep(0.030, 0.18, src_hsv.y);
    float neutral_luma = smoothstep(0.10, 0.36, src_y) * (1.0 - smoothstep(0.965, 1.0, src_y));
    float neutral_mask = saturate(low_sat * neutral_luma);
    float neutral_highlight = (1.0 - smoothstep(0.025, 0.12, src_hsv.y)) *
                              smoothstep(0.72, 0.92, src_y);
    float color_confidence = smoothstep(0.10, 0.34, src_hsv.y) * smoothstep(0.065, 0.20, src_y) *
                             (1.0 - smoothstep(0.95, 1.0, src_y));

    float skin_hue = max(max(arvisual_hue_mask(src_hsv.x, 0.055, 0.060), arvisual_hue_mask(src_hsv.x, 0.092, 0.070)),
                         arvisual_hue_mask(src_hsv.x, 0.125, 0.050));
    float skin_luma = smoothstep(0.070, 0.26, src_y) * (1.0 - smoothstep(0.90, 1.0, src_y));
    float skin_sat = smoothstep(0.055, 0.28, src_hsv.y) * (1.0 - smoothstep(0.86, 1.0, src_hsv.y));
    float brown_skin = smoothstep(0.08, 0.38, src_y) * smoothstep(0.16, 0.52, src_hsv.y) *
                       arvisual_hue_mask(src_hsv.x, 0.085, 0.085);
    float skin_mask = saturate((skin_hue * skin_luma * skin_sat + brown_skin * 0.45) * skin_protect);

    float3 color = src;

    /* Neutral cleanup. It can only remove a detected cast. */
    float neutral_clean = neutral_mask * clean_white * (0.62 + smart_clean * 0.18);
    color = lerp(color, float3(src_y, src_y, src_y), neutral_clean);
    float green_cast = saturate((color.g - max(color.r, color.b)) * 4.0);
    float cyan_cast = saturate((min(color.g, color.b) - color.r) * 3.0);
    float cast_guard = neutral_mask * clean_white * (0.24 + smart_clean * 0.12) * saturate(green_cast + cyan_cast);
    color.g = lerp(color.g, (color.r + color.b) * 0.5, cast_guard);
    color = arvisual_fit_gamut_preserve_luma(color, arvisual_luminance(color));

    /* Bounded tone shaping. Vivid highlights get almost no lift. */
    float y = arvisual_luminance(color);
    float mid = smoothstep(0.12, 0.38, y) * (1.0 - smoothstep(0.76, 0.94, y));
    float upper_mid = smoothstep(0.34, 0.62, y) * (1.0 - smoothstep(0.84, 0.98, y));
    float deep_shadow = 1.0 - smoothstep(0.035, 0.18, y);
    float highlight = smoothstep(0.72, 0.98, y);
    float vivid_bright = smoothstep(0.48, 0.78, src_hsv.y) * smoothstep(0.50, 0.82, y);
    float lift_scale = 1.0 - vivid_bright * 0.90;
    float expose_band = smoothstep(0.06, 0.23, y) * (1.0 - smoothstep(0.68, 0.90, y));

    float target_y = y;
    target_y += smart_exposure * expose_band;
    target_y += f_enhance * 0.032 * mid * lift_scale;
    target_y += f_enhance * f_depth * 0.022 * upper_mid * lift_scale;

    float dark_vivid = smoothstep(0.30, 0.62, src_hsv.y) * smoothstep(0.09, 0.20, y) *
                       (1.0 - smoothstep(0.30, 0.48, y));
    target_y += f_pop * 0.012 * dark_vivid;
    target_y -= f_depth * 0.010 * deep_shadow * (1.0 - smart_shadow * 0.78);
    target_y += smart_shadow * 0.007 * deep_shadow;
    float diffuse_white_guard = 1.0 - neutral_highlight * 0.88;
    target_y -= (highlight_guard * 0.006 + smart_highlight * 0.008) * highlight * diffuse_white_guard;

    /* Positive tone lift consumes real channel headroom. This prevents warm
       skin highlights (high red, moderate luma) from landing on digital 1. */
    float tone_delta = target_y - y;
    float channel_lift_room = 1.0 - smoothstep(0.82, 0.985, max(max(color.r, color.g), color.b));
    float skin_lift_guard = 1.0 - skin_mask * 0.42;
    float neutral_lift_guard = 1.0 - neutral_mask * (0.35 + smart_clean * 0.15);
    target_y = y + min(tone_delta, 0.0) + max(tone_delta, 0.0) * channel_lift_room * skin_lift_guard *
               neutral_lift_guard;

    /* Toe/shoulder protected contrast: dimensional midtones without crushing
       a deliberately low-key background or dulling clean diffuse whites. */
    float contrast = (f_enhance * 0.042 + f_depth * 0.030) * (1.0 - vivid_bright * 0.75);
    float contrast_target = (target_y - 0.5) * (1.0 + contrast) + 0.5;
    float contrast_band = smoothstep(0.075, 0.24, target_y) * (1.0 - smoothstep(0.80, 0.94, target_y));
    target_y = lerp(target_y, contrast_target, contrast_band * neutral_lift_guard);
    float tone_knee = lerp(0.84 - smart_highlight * 0.035, 0.972, neutral_highlight);
    target_y = arvisual_soft_luma_shoulder(target_y, tone_knee, smart_highlight);
    color = arvisual_fit_gamut_preserve_luma(color, target_y);

    /* Very small warm/cool depth split, immediately gamut-mapped. */
    y = arvisual_luminance(color);
    float shadow_chroma_confidence = smoothstep(0.040, 0.16, src_hsv.y);
    float cool_shadow = (1.0 - smoothstep(0.18, 0.44, y)) * (1.0 - neutral_mask * 0.90) *
                        shadow_chroma_confidence;
    float warm_light = smoothstep(0.42, 0.76, y) * (1.0 - smoothstep(0.90, 1.0, y)) *
                       (1.0 - neutral_mask * 0.96);
    color += f_depth * 0.006 * cool_shadow * float3(-0.10, 0.01, 0.15);
    color += f_depth * 0.005 * warm_light * float3(0.14, 0.035, -0.05);
    color = arvisual_fit_gamut_preserve_luma(color, arvisual_luminance(color));

    /* Headroom-based saturation. No path can make HSV saturation negative or
       greater than the adaptive ceiling. Colors already above that ceiling
       are left alone instead of being destructively desaturated. */
    float3 hsv = arvisual_rgb2hsv(color);
    y = arvisual_luminance(color);
    float luma_safe = smoothstep(0.055, 0.20, y) * (1.0 - smoothstep(0.86, 0.98, y));
    float muted_colored = smoothstep(0.045, 0.18, hsv.y) * (1.0 - smoothstep(0.84, 0.98, hsv.y));
    float vibrance = f_pop * muted_colored * color_confidence * luma_safe *
                     (1.0 - neutral_mask * 0.98) * (1.0 - skin_mask * 0.90);

    float red = arvisual_hue_mask(hsv.x, 0.000, 0.050);
    float orange = arvisual_hue_mask(hsv.x, 0.080, 0.055);
    float yellow = arvisual_hue_mask(hsv.x, 0.155, 0.052);
    float green = arvisual_hue_mask(hsv.x, 0.330, 0.055);
    float cyan = arvisual_hue_mask(hsv.x, 0.500, 0.050);
    float blue = arvisual_hue_mask(hsv.x, 0.620, 0.064);
    float magenta = arvisual_hue_mask(hsv.x, 0.835, 0.050);
    float dopamine_zone = saturate(red + orange * 0.75 + yellow * 0.78 + green * 0.62 +
                                    cyan * 0.55 + blue + magenta * 0.72);
    float object_pop = dopamine_zone * color_confidence * (1.0 - neutral_mask) * (1.0 - skin_mask * 0.88);

    float sat_ceiling = max(hsv.y, saturate(smart_chroma_limit));
    float sat_room = max(sat_ceiling - hsv.y, 0.0);
    float sat_request = saturate(vibrance * 0.38 + object_pop * f_pop * (0.18 + f_depth * 0.04));
    float blue_safety = arvisual_hue_mask(hsv.x, 0.600, 0.120);
    float blue_sat_guard = 1.0 - blue_safety * (0.34 + smart_separation * 0.28);
    sat_request *= blue_sat_guard;
    float sat_y = y;
    float max_sat_gain = lerp(0.075, 0.110, saturate(smart_strength));
    float green_teal_safety = saturate(arvisual_hue_mask(hsv.x, 0.300, 0.160) * 1.35 +
                                        arvisual_hue_mask(hsv.x, 0.500, 0.140));
    float violet_safety = saturate(arvisual_hue_mask(hsv.x, 0.730, 0.140) * 1.10);
    float secondary_safety = saturate(green_teal_safety + violet_safety + blue_safety * 0.80);
    float scene_chroma_risk = saturate((0.985 - smart_chroma_limit) * (1.0 / 0.085));
    float secondary_gain_cap = lerp(0.058, 0.046, scene_chroma_risk);
    max_sat_gain = lerp(max_sat_gain, secondary_gain_cap, secondary_safety);
    hsv.y += min(sat_room * sat_request, max_sat_gain);

    float weak_yellow_green = saturate((yellow + green + cyan) * (1.0 - color_confidence));
    hsv.y *= 1.0 - weak_yellow_green * clean_white * 0.16;
    color = arvisual_fit_gamut_preserve_luma(arvisual_hsv2rgb(hsv), sat_y);

    /* Pleasure comes from separation, not neon saturation: warm hero colors
       receive a tiny clean midtone lift, cyan/green a smaller lift, and deep
       blue a tiny density anchor. Skin and neutrals are excluded by object_pop. */
    y = arvisual_luminance(color);
    float warm_hero = saturate(red * 0.55 + orange + yellow * 0.35 + magenta * 0.18);
    float fresh_hero = saturate(cyan * 0.55 + green * 0.30);
    float blue_hero = blue * (1.0 - cyan * 0.65);
    float hero_band = smoothstep(0.10, 0.28, y) * (1.0 - smoothstep(0.72, 0.90, y));
    float warm_separation = 0.012 + smart_separation * 0.012;
    float fresh_separation = 0.005 + smart_separation * 0.004;
    float blue_density = 0.005 + smart_separation * 0.005;
    float hero_delta = object_pop * f_pop * hero_band *
                       (warm_hero * warm_separation + fresh_hero * fresh_separation - blue_hero * blue_density);
    float3 hero_hsv_before = arvisual_rgb2hsv(color);
    color = arvisual_fit_gamut_preserve_luma(color, y + hero_delta);

    /* A luma lift lowers relative HSV saturation even when absolute chroma is
       unchanged. Restore only that loss for classified warm objects; skin is
       already excluded by object_pop and no new saturation is manufactured. */
    float hero_y = arvisual_luminance(color);
    float3 hero_hsv_after = arvisual_rgb2hsv(color);
    float synthetic_warm_object = color_confidence * smoothstep(0.68, 0.82, src_hsv.y) *
                                  (1.0 - neutral_mask);
    float warm_chroma_object = max(object_pop, synthetic_warm_object);
    float warm_chroma_retention = saturate(warm_hero * warm_chroma_object * (1.25 + smart_separation * 0.35));
    float warm_sat_floor = min(src_hsv.y, smart_chroma_limit);
    hero_hsv_after.y += max(warm_sat_floor - hero_hsv_after.y, 0.0) * warm_chroma_retention;
    color = arvisual_fit_gamut_preserve_luma(arvisual_hsv2rgb(hero_hsv_after), hero_y);

    /* Symmetric neighborhood used for skin smoothing and luma clarity. */
    float3 smooth_src = src;
    float detail = 0.0;
    float neighbor_luma = src_y;
    if (performance >= 0.25) {
        float3 nl = arvisual_tap(uv - float2(texel_size.x, 0.0));
        float3 nr = arvisual_tap(uv + float2(texel_size.x, 0.0));
        float3 nu = arvisual_tap(uv - float2(0.0, texel_size.y));
        float3 nd = arvisual_tap(uv + float2(0.0, texel_size.y));
        float3 cross_sum = nl + nr + nu + nd;

        if (performance >= 0.75) {
            float3 d1 = arvisual_tap(uv + float2( texel_size.x,  texel_size.y));
            float3 d2 = arvisual_tap(uv + float2(-texel_size.x,  texel_size.y));
            float3 d3 = arvisual_tap(uv + float2( texel_size.x, -texel_size.y));
            float3 d4 = arvisual_tap(uv + float2(-texel_size.x, -texel_size.y));
            smooth_src = (src * 4.0 + cross_sum * 2.0 + d1 + d2 + d3 + d4) * (1.0 / 16.0);
        } else {
            smooth_src = (src * 4.0 + cross_sum) * 0.125;
        }

        neighbor_luma = (arvisual_luminance(nl) + arvisual_luminance(nr) + arvisual_luminance(nu) + arvisual_luminance(nd)) * 0.25;
        detail = src_y - neighbor_luma;
    }

    /* Skin treatment follows local detail without replacing the grade with an
       ungraded blur. Hue/saturation moves are bounded and headroom-based. */
    float smooth_y = arvisual_luminance(smooth_src);
    float skin_detail = abs(src_y - smooth_y);
    float skin_smooth_gate = 1.0 - smoothstep(0.012, 0.070, skin_detail);
    float beauty_amount = skin_mask * f_beauty * skin_smooth_gate * lerp(0.06, 0.14, skin_protect);
    float3 beauty = color + (smooth_src - src) * beauty_amount;
    float beauty_y = arvisual_luminance(beauty);
    float brown_lift = smoothstep(0.07, 0.34, src_y) * (1.0 - smoothstep(0.62, 0.92, src_y));
    float skin_lift = skin_mask * f_beauty * (0.010 + brown_lift * 0.018) *
                      (1.0 - smoothstep(0.76, 0.94, beauty_y));
    float skin_channel_room = 1.0 - smoothstep(0.80, 0.98, max(max(beauty.r, beauty.g), beauty.b));
    skin_lift *= skin_channel_room;
    beauty = arvisual_fit_gamut_preserve_luma(beauty, arvisual_soft_luma_shoulder(beauty_y + skin_lift, 0.86, smart_highlight));

    float3 beauty_hsv = arvisual_rgb2hsv(beauty);
    beauty_hsv.x = arvisual_hue_lerp_wrap(beauty_hsv.x, 0.055, skin_mask * f_healthy * f_beauty * 0.030);
    float skin_sat_ceiling = max(beauty_hsv.y, min(smart_chroma_limit, 0.92));
    float skin_sat_room = max(skin_sat_ceiling - beauty_hsv.y, 0.0);
    beauty_hsv.y += min(skin_sat_room * skin_mask * f_healthy * f_beauty * 0.060, 0.012);
    beauty = arvisual_fit_gamut_preserve_luma(arvisual_hsv2rgb(beauty_hsv), arvisual_luminance(beauty));
    color = lerp(color, beauty, skin_mask * f_beauty * 0.72);
    color = arvisual_fit_gamut_preserve_luma(color, arvisual_luminance(color));

    /* Post-neutral cleanup. */
    y = arvisual_luminance(color);
    float3 post_hsv = arvisual_rgb2hsv(color);
    float post_neutral = (1.0 - smoothstep(0.05, 0.20, post_hsv.y)) * smoothstep(0.18, 0.92, y);
    color = lerp(color, float3(y, y, y), post_neutral * clean_white * 0.14);

    /* Anti-halo clarity changes luma through the same gamut-safe path. */
    y = arvisual_luminance(color);
    float edge_gate = smoothstep(0.010, 0.045, abs(detail));
    float big_edge = smoothstep(0.080, 0.18, abs(detail));
    float halo_guard = 1.0 - big_edge * 0.92;
    float hi_zone = smoothstep(0.70, 0.93, y);
    float pos_detail = max(detail, 0.0) * (1.0 - hi_zone * (0.70 + smart_highlight * 0.25));
    float guarded_detail = pos_detail + min(detail, 0.0);
    float d_soft = guarded_detail / (1.0 + abs(guarded_detail) * 8.0);
    float clarity_mask = edge_gate * halo_guard * (1.0 - skin_mask * 0.92) * smoothstep(0.07, 0.26, y);
    float clarity_delta = d_soft * f_clarity * 0.10 * lerp(0.72, 1.0, performance) * clarity_mask;
    clarity_delta += d_soft * f_depth * 0.018 * object_pop * halo_guard;
    /* Do not shoulder the source luma when clarity contributes nothing. The
       old unconditional 0.84 knee turned a flat diffuse white into gray. The
       final scene-aware shoulder still constrains the actual target. */
    color = arvisual_fit_gamut_preserve_luma(color, y + clarity_delta);

    /*
     * P5D screen-text legibility.
     *
     * Reuse the already-computed symmetric source-neighborhood detail rather
     * than adding another sharpen pass. Reinforce only luminance micro-edges;
     * chroma is never sharpened. Neutral UI/text receives the full effect,
     * saturated edges receive a deliberately reduced dose, and skin is nearly
     * excluded. A small scale-aware boost helps 4K->1080/minified desktop
     * capture survive later player scaling without creating one-pixel halos.
     */
    y = arvisual_luminance(color);
    float text_detail_abs = abs(detail);
    float text_edge =
        smoothstep(0.006, 0.030, text_detail_abs) *
        (1.0 - smoothstep(0.16, 0.30, text_detail_abs));
    float text_neutral =
        1.0 - smoothstep(0.10, 0.34, src_hsv.y);
    float text_chroma_weight =
        lerp(0.24, 1.0, text_neutral);

    float2 crop_pixels =
        abs(uv_rect.zw - uv_rect.xy) /
        max(texel_size, float2(0.0000001, 0.0000001));
    float safe_camera_zoom =
        max(camera_keyboard.z, 1.0);
    float2 source_per_output =
        crop_pixels /
        max(
            output_info.xy * safe_camera_zoom,
            float2(1.0, 1.0));
    float minification =
        max(source_per_output.x, source_per_output.y);
    float scale_resilience =
        lerp(
            1.0,
            1.22,
            smoothstep(1.0, 2.2, minification));

    float text_soft_detail =
        detail /
        (1.0 + text_detail_abs * 12.0);

    /*
     * P5D.5 bright-background text calibration.
     *
     * The first real P5D triad showed that dark/gray text on white browser UI
     * gained much less perceived weight after player downscale than white text
     * on dark UI. Do not raise global sharpening: classify only a dark
     * micro-stroke whose immediate neighborhood is bright and neutral.
     *
     * This is intentionally directional. Positive detail (light text on dark
     * UI) receives the original P5D gain. Only negative detail can receive the
     * additional 1.45x maximum reinforcement and slightly wider negative luma
     * headroom. Saturated neighborhoods, skin and flat regions remain gated
     * by the existing P5D masks.
     */
    float3 neighborhood_hsv =
        arvisual_rgb2hsv(smooth_src);
    /*
     * A one-pixel vertical glyph edge has bright neighbors left/right but
     * same-stroke neighbors above/below, so the cardinal average is lower
     * than the actual page background. Calibrate the confidence ramp for that
     * real raster topology rather than requiring an isolated dark pixel.
     */
    float bright_neutral_neighborhood =
        smoothstep(0.64, 0.88, neighbor_luma) *
        (1.0 - smoothstep(0.08, 0.24, neighborhood_hsv.y));
    float dark_stroke_confidence =
        smoothstep(0.012, 0.060, -detail);
    float dark_on_bright =
        bright_neutral_neighborhood *
        dark_stroke_confidence *
        text_neutral;

    float directional_gain =
        text_soft_detail < 0.0
            ? lerp(1.0, 1.45, dark_on_bright)
            : 1.0;

    float signed_headroom =
        text_soft_detail >= 0.0
            ? (1.0 - smoothstep(0.90, 0.995, y))
            : smoothstep(0.005, 0.080, y);

    float text_delta =
        text_soft_detail *
        text_legibility *
        0.30 *
        scale_resilience *
        text_edge *
        text_chroma_weight *
        directional_gain *
        (1.0 - skin_mask * 0.98) *
        signed_headroom;

    float negative_limit =
        lerp(-0.014, -0.018, dark_on_bright);
    text_delta =
        clamp(text_delta, negative_limit, 0.014);

    color = arvisual_fit_gamut_preserve_luma(
        color,
        y + text_delta);

    /* Perceptual punch and gloss consume luma headroom instead of adding RGB. */
    y = arvisual_luminance(color);
    float perceptual_punch = object_pop * f_pop * f_depth * smoothstep(0.18, 0.74, y) *
                             (1.0 - smoothstep(0.84, 0.97, y));
    float creative_headroom = 1.0 - smoothstep(0.80, 0.985, max(max(color.r, color.g), color.b));
    target_y = y + perceptual_punch * 0.012 * creative_headroom;
    float gloss_mask = smoothstep(0.58, 0.86, y) * object_pop * (1.0 - skin_mask * 0.88);
    target_y += gloss_mask * f_gloss * 0.008 * (1.0 - smoothstep(0.82, 0.97, y)) * creative_headroom;

    /* Final shoulder + gamut map is a mathematical invariant, not a rescue
       clamp: target luma is bounded and chroma is scaled before any channel
       can leave display gamut. */
    float final_knee_base = 0.84 - smart_highlight * 0.045 - highlight_guard * 0.010;
    float final_knee = lerp(final_knee_base, 0.972, neutral_highlight);
    target_y = arvisual_soft_luma_shoulder(target_y, final_knee, smart_highlight + highlight_guard * 0.35);
    color = arvisual_fit_gamut_preserve_luma(color, target_y);

    /*
     * P5D.6 low-contrast UI structure preservation.
     *
     * Real matched capture showed neutral 1px card borders/separators fading
     * into their background after grading + H.264/player scaling. This is a
     * different topology from text: shallow neutral detail only. Preserve it
     * late in the pipeline so earlier tone shaping cannot flatten it again.
     *
     * Bright UI: only a slightly darker neutral line may be reinforced.
     * Dark UI: only a slightly lighter neutral line may be reinforced.
     * Strong edges/text, saturated edges, skin and flat regions are excluded.
     */
    y = arvisual_luminance(color);
    float structure_detail_abs = abs(detail);
    float structure_edge =
        smoothstep(0.0025, 0.010, structure_detail_abs) *
        (1.0 - smoothstep(0.055, 0.105, structure_detail_abs));

    float structure_src_neutral =
        1.0 - smoothstep(0.06, 0.22, src_hsv.y);
    float structure_neighbor_neutral =
        1.0 - smoothstep(0.06, 0.20, neighborhood_hsv.y);

    float bright_structure_context =
        smoothstep(0.68, 0.92, neighbor_luma) *
        smoothstep(0.0025, 0.025, -detail);
    float dark_structure_context =
        (1.0 - smoothstep(0.20, 0.48, neighbor_luma)) *
        smoothstep(0.0025, 0.025, detail);

    float structure_context =
        max(
            bright_structure_context,
            dark_structure_context * 0.72);

    float structure_soft_detail =
        detail /
        (1.0 + structure_detail_abs * 18.0);

    float structure_delta =
        structure_soft_detail *
        ui_structure *
        0.42 *
        structure_edge *
        structure_src_neutral *
        structure_neighbor_neutral *
        structure_context *
        (1.0 - skin_mask * 0.99);

    structure_delta =
        clamp(structure_delta, -0.010, 0.007);

    color = arvisual_fit_gamut_preserve_luma(
        color,
        y + structure_delta);

    return float4(saturate(color) * alpha, alpha);
}


/* Arssyut presentation click skin.
 * Camera/content anchoring remains the P3R ArZoom parity path, but click
 * appearance intentionally uses a single larger satisfying ring requested
 * during direct visual validation. */
float smooth_out(float t)
{
    t = saturate(t);
    const float inv = 1.0 - t;
    return 1.0 - inv * inv * inv;
}

float vector_ring(
    float distance_px,
    float radius_px,
    float half_width_px)
{
    const float edge =
        abs(distance_px - radius_px);
    return 1.0 - smoothstep(
        half_width_px,
        half_width_px + 1.25,
        edge);
}

float2 project_content(
    float2 content_position,
    float2 camera_center,
    float safe_zoom)
{
    return float2(0.5, 0.5) +
           (content_position - camera_center) * safe_zoom;
}

float2 event_delta_px(
    float2 output_uv,
    float4 event_data,
    float2 camera_center,
    float safe_zoom,
    float2 safe_viewport)
{
    const float2 center = project_content(
        event_data.xy,
        camera_center,
        safe_zoom);
    return (output_uv - center) * safe_viewport;
}

float3 click_color(float type)
{
    if (type < 1.5)
        return float3(0.196, 0.722, 1.000); // #32B8FF
    if (type < 2.5)
        return float3(1.000, 0.361, 0.541); // #FF5C8A
    return float3(1.000, 0.784, 0.341);     // #FFC857
}

float3 composite_single_ring(
    float3 base,
    float2 delta_px,
    float progress,
    float type,
    float2 safe_viewport)
{
    progress = saturate(progress);

    const float scale = clamp(
        min(safe_viewport.x, safe_viewport.y) / 1080.0,
        0.85,
        1.60);

    const float expansion =
        smooth_out(progress);
    const float radius_px =
        lerp(10.5, 64.0, expansion) * scale;
    const float half_width_px =
        lerp(3.90, 2.25, progress) * scale;
    const float distance_px =
        length(delta_px);
    const float edge =
        abs(distance_px - radius_px);

    // Fast ignition, then a deliberately long luminous tail. The core fades
    // before the halo so the pulse dissolves instead of simply disappearing.
    const float ignition =
        smoothstep(0.0, 0.035, progress);
    const float core_fade =
        1.0 - smoothstep(0.48, 0.94, progress);
    const float bloom_fade =
        1.0 - smoothstep(0.58, 1.00, progress);

    const float core =
        vector_ring(
            distance_px,
            radius_px,
            half_width_px) *
        ignition *
        core_fade;

    // Both bloom zones are distance falloffs from the SAME analytic ring.
    // They are not secondary circles and never fill the ring center.
    const float glow_distance =
        max(edge - half_width_px, 0.0);
    const float near_spread_px =
        lerp(9.0, 16.0, expansion) * scale;
    const float halo_spread_px =
        lerp(18.0, 30.0, expansion) * scale;

    const float near_shape =
        saturate(
            1.0 -
            glow_distance /
                max(near_spread_px, 0.001));
    const float halo_shape =
        saturate(
            1.0 -
            glow_distance /
                max(halo_spread_px, 0.001));

    // Keep a definite empty center even during the small-radius
    // ignition phase. Bloom may surround the ring, but it must never become
    // a filled click disc or center dot.
    const float center_guard =
        smoothstep(
            radius_px * 0.28,
            radius_px * 0.62,
            distance_px);

    const float near_bloom =
        near_shape *
        near_shape *
        center_guard *
        ignition *
        bloom_fade;
    const float diffuse_halo =
        halo_shape *
        halo_shape *
        center_guard *
        ignition *
        bloom_fade;

    const float support =
        vector_ring(
            distance_px,
            radius_px,
            half_width_px + 2.6 * scale) *
        ignition *
        core_fade;

    const float3 tint =
        click_color(type);

    const float luminance =
        dot(
            base,
            float3(0.2126, 0.7152, 0.0722));
    const float bright_surface =
        smoothstep(0.62, 0.92, luminance);

    // White surfaces cannot become physically brighter. On bright content,
    // use a restrained deeper chromatic support around the same ring so the
    // eye still reads a luminous edge instead of a thin washed-out outline.
    const float3 core_color =
        lerp(
            saturate(tint * 1.10),
            tint * 0.80,
            bright_surface);
    const float3 support_color =
        lerp(
            tint * 0.28,
            tint * 0.50,
            bright_surface);
    const float3 halo_color =
        lerp(
            tint,
            tint * 0.62,
            bright_surface);

    base = lerp(
        base,
        support_color,
        saturate(
            support *
            (0.08 + 0.24 * bright_surface)));

    base = lerp(
        base,
        halo_color,
        saturate(
            near_bloom *
                (0.20 + 0.08 * bright_surface) +
            diffuse_halo *
                (0.075 + 0.055 * bright_surface)));

    base = lerp(
        base,
        core_color,
        saturate(core * 0.995));

    base +=
        tint *
        (near_bloom * 0.30 +
         diffuse_halo * 0.14) *
        (1.0 - bright_surface * 0.78);

    return saturate(base);
}

float3 apply_click(
    float3 base,
    float2 output_uv,
    float4 event_data,
    float2 camera_center,
    float safe_zoom,
    float2 safe_viewport)
{
    const float type = event_data.w;
    if (type < 0.5)
        return base;

    const float2 delta_px =
        event_delta_px(
            output_uv,
            event_data,
            camera_center,
            safe_zoom,
            safe_viewport);

    return composite_single_ring(
        base,
        delta_px,
        event_data.z,
        type,
        safe_viewport);
}


float4 ps_main(VertexOutput input) : SV_Target
{
    const float2 camera_center = camera_keyboard.xy;
    const float zoom = max(camera_keyboard.z, 1.0f);

    const float2 camera_uv =
        camera_center + (input.uv - 0.5f) / zoom;
    const float2 uv =
        lerp(uv_rect.xy, uv_rect.zw, camera_uv);

    float4 color =
        source_texture.Sample(source_sampler, uv);

    color = apply_arvisual(
        color,
        uv);

    const float2 safe_viewport =
        max(output_info.xy, float2(1.0f, 1.0f));

    color.rgb = apply_click(
        color.rgb,
        input.uv,
        click0,
        camera_center,
        zoom,
        safe_viewport);
    color.rgb = apply_click(
        color.rgb,
        input.uv,
        click1,
        camera_center,
        zoom,
        safe_viewport);
    color.rgb = apply_click(
        color.rgb,
        input.uv,
        click2,
        camera_center,
        zoom,
        safe_viewport);
    color.rgb = apply_click(
        color.rgb,
        input.uv,
        click3,
        camera_center,
        zoom,
        safe_viewport);

    const float keyboard_opacity =
        saturate(camera_keyboard.w);

    if (keyboard_opacity > 0.001f &&
        input.uv.x >= keyboard_rect.x &&
        input.uv.x <= keyboard_rect.z &&
        input.uv.y >= keyboard_rect.y &&
        input.uv.y <= keyboard_rect.w) {
        float2 keyboard_uv =
            (input.uv - keyboard_rect.xy) /
            max(
                keyboard_rect.zw - keyboard_rect.xy,
                0.0001f);
        keyboard_uv *=
            max(output_info.zw, float2(0.0001f, 0.0001f));

        const float4 overlay =
            keyboard_texture.Sample(
                source_sampler,
                keyboard_uv);

        const float alpha =
            saturate(overlay.a * keyboard_opacity);

        color.rgb =
            lerp(color.rgb, overlay.rgb, alpha);
    }

    return color;
}
)";

struct PresentationConstants {
    float uv_left;
    float uv_top;
    float uv_right;
    float uv_bottom;

    float camera_center_x;
    float camera_center_y;
    float camera_zoom;
    float keyboard_opacity;

    float output_width;
    float output_height;
    float keyboard_uv_scale_x;
    float keyboard_uv_scale_y;

    float keyboard_left;
    float keyboard_top;
    float keyboard_right;
    float keyboard_bottom;

    float arvisual_enabled;
    float arvisual_master;
    float arvisual_enhance;
    float arvisual_color_pop;

    float arvisual_clean_white;
    float arvisual_clarity;
    float arvisual_skin_protect;
    float arvisual_skin_beauty;

    float arvisual_healthy_tone;
    float arvisual_toy_gloss;
    float arvisual_depth_pop;
    float arvisual_highlight_guard;

    float arvisual_performance;
    float arvisual_smart_exposure;
    float arvisual_smart_pop;
    float arvisual_smart_highlight;

    float arvisual_smart_shadow;
    float arvisual_smart_strength;
    float arvisual_smart_chroma_limit;
    float arvisual_smart_clean;

    float arvisual_smart_separation;
    float arvisual_texel_x;
    float arvisual_texel_y;
    float arvisual_text_legibility;

    float arvisual_ui_structure;
    float arvisual_reserved6_y;
    float arvisual_reserved6_z;
    float arvisual_reserved6_w;

    float clicks[16]{};
};

constexpr UINT kKeyboardWidth = 768;
constexpr UINT kKeyboardHeight = 128;

[[nodiscard]] std::uint32_t hresult_detail(HRESULT hr) noexcept
{
    return static_cast<std::uint32_t>(hr);
}

[[nodiscard]] Status d3d_failure(HRESULT hr) noexcept
{
    return Status::failure(
        StatusCode::PlatformFailure,
        hresult_detail(hr));
}

[[nodiscard]] Result<Microsoft::WRL::ComPtr<ID3DBlob>>
compile_shader(const char *entry, const char *target) noexcept
{
    Microsoft::WRL::ComPtr<ID3DBlob> bytecode;
    Microsoft::WRL::ComPtr<ID3DBlob> errors;

    const UINT flags =
#ifdef _DEBUG
        D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
        D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

    const HRESULT hr = D3DCompile(
        shader_source,
        sizeof(shader_source) - 1,
        "arssyut-p1-compositor",
        nullptr,
        nullptr,
        entry,
        target,
        flags,
        0,
        bytecode.GetAddressOf(),
        errors.GetAddressOf());

    if (FAILED(hr) || !bytecode) {
        return Result<Microsoft::WRL::ComPtr<ID3DBlob>>::failure(
            d3d_failure(hr));
    }

    return Result<Microsoft::WRL::ComPtr<ID3DBlob>>::success(
        std::move(bytecode));
}

[[nodiscard]] std::uint32_t elapsed_microseconds(
    TimePoint start,
    TimePoint end) noexcept
{
    const std::int64_t ticks = std::max<std::int64_t>(
        0,
        MonotonicClock::duration_ticks(start, end));

    return static_cast<std::uint32_t>(
        std::min<std::uint64_t>(
            static_cast<std::uint64_t>(ticks) / 10ULL,
            std::numeric_limits<std::uint32_t>::max()));
}

} // namespace

D3D11Compositor::~D3D11Compositor()
{
    if (keyboard_dc_) {
        if (keyboard_old_bitmap_)
            SelectObject(keyboard_dc_, keyboard_old_bitmap_);
        DeleteDC(keyboard_dc_);
        keyboard_dc_ = nullptr;
    }

    if (keyboard_font_) {
        DeleteObject(keyboard_font_);
        keyboard_font_ = nullptr;
    }
    if (keyboard_light_pen_) {
        DeleteObject(keyboard_light_pen_);
        keyboard_light_pen_ = nullptr;
    }
    if (keyboard_shadow_brush_) {
        DeleteObject(keyboard_shadow_brush_);
        keyboard_shadow_brush_ = nullptr;
    }
    if (keyboard_light_brush_) {
        DeleteObject(keyboard_light_brush_);
        keyboard_light_brush_ = nullptr;
    }

    if (keyboard_bitmap_) {
        DeleteObject(keyboard_bitmap_);
        keyboard_bitmap_ = nullptr;
    }

    keyboard_old_bitmap_ = nullptr;
    keyboard_bits_ = nullptr;
}

Result<std::unique_ptr<D3D11Compositor>>
D3D11Compositor::create(ID3D11Device *device) noexcept
{
    if (!device) {
        return Result<std::unique_ptr<D3D11Compositor>>::failure(
            Status::failure(StatusCode::InvalidArgument));
    }

    std::unique_ptr<D3D11Compositor> compositor(
        new (std::nothrow) D3D11Compositor{});
    if (!compositor) {
        return Result<std::unique_ptr<D3D11Compositor>>::failure(
            Status::failure(StatusCode::InternalError));
    }

    const Status status = compositor->initialize(device);
    if (!status.ok()) {
        return Result<std::unique_ptr<D3D11Compositor>>::failure(status);
    }

    return Result<std::unique_ptr<D3D11Compositor>>::success(
        std::move(compositor));
}

Status D3D11Compositor::initialize(ID3D11Device *device) noexcept
{
    device_ = device;

    auto vs = compile_shader("vs_main", "vs_5_0");
    if (!vs)
        return vs.status();

    auto ps = compile_shader("ps_main", "ps_5_0");
    if (!ps)
        return ps.status();

    HRESULT hr = device_->CreateVertexShader(
        vs.value()->GetBufferPointer(),
        vs.value()->GetBufferSize(),
        nullptr,
        vertex_shader_.GetAddressOf());
    if (FAILED(hr))
        return d3d_failure(hr);

    hr = device_->CreatePixelShader(
        ps.value()->GetBufferPointer(),
        ps.value()->GetBufferSize(),
        nullptr,
        pixel_shader_.GetAddressOf());
    if (FAILED(hr))
        return d3d_failure(hr);

    D3D11_SAMPLER_DESC sampler_desc{};
    sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;

    hr = device_->CreateSamplerState(
        &sampler_desc,
        sampler_.GetAddressOf());
    if (FAILED(hr))
        return d3d_failure(hr);

    D3D11_BUFFER_DESC constant_desc{};
    constant_desc.ByteWidth = sizeof(PresentationConstants);
    constant_desc.Usage = D3D11_USAGE_DEFAULT;
    constant_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

    hr = device_->CreateBuffer(
        &constant_desc,
        nullptr,
        crop_constant_buffer_.GetAddressOf());
    if (FAILED(hr))
        return d3d_failure(hr);

    const Status keyboard_status =
        initialize_keyboard_overlay();
    if (!keyboard_status.ok())
        return keyboard_status;

    // P5B analysis is optional infrastructure. If its resources cannot be
    // created, the P5A static grade remains fully usable with neutral
    // adaptive inputs; recording must not fail because Smart Auto is absent.
    auto analyzer_result =
        D3D11ArVisualSceneAnalyzer::create(
            device_.Get());
    if (analyzer_result) {
        scene_analyzer_ =
            std::move(analyzer_result).value();
    }

    for (auto &slot : gpu_queries_) {
        D3D11_QUERY_DESC disjoint_desc{};
        disjoint_desc.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;

        hr = device_->CreateQuery(
            &disjoint_desc,
            slot.disjoint.GetAddressOf());
        if (FAILED(hr))
            return d3d_failure(hr);

        D3D11_QUERY_DESC timestamp_desc{};
        timestamp_desc.Query = D3D11_QUERY_TIMESTAMP;

        hr = device_->CreateQuery(
            &timestamp_desc,
            slot.start.GetAddressOf());
        if (FAILED(hr))
            return d3d_failure(hr);

        hr = device_->CreateQuery(
            &timestamp_desc,
            slot.end.GetAddressOf());
        if (FAILED(hr))
            return d3d_failure(hr);
    }

    return Status::success();
}

Status D3D11Compositor::initialize_keyboard_overlay() noexcept
{
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = kKeyboardWidth;
    desc.Height = kKeyboardHeight;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    HRESULT hr = device_->CreateTexture2D(
        &desc,
        nullptr,
        keyboard_texture_.GetAddressOf());
    if (FAILED(hr))
        return d3d_failure(hr);

    hr = device_->CreateShaderResourceView(
        keyboard_texture_.Get(),
        nullptr,
        keyboard_srv_.GetAddressOf());
    if (FAILED(hr))
        return d3d_failure(hr);

    keyboard_dc_ = CreateCompatibleDC(nullptr);
    if (!keyboard_dc_) {
        return Status::failure(
            StatusCode::PlatformFailure,
            GetLastError());
    }

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth =
        static_cast<LONG>(kKeyboardWidth);
    info.bmiHeader.biHeight =
        -static_cast<LONG>(kKeyboardHeight);
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    keyboard_bitmap_ = CreateDIBSection(
        keyboard_dc_,
        &info,
        DIB_RGB_COLORS,
        &keyboard_bits_,
        nullptr,
        0);
    if (!keyboard_bitmap_ || !keyboard_bits_) {
        return Status::failure(
            StatusCode::PlatformFailure,
            GetLastError());
    }

    keyboard_old_bitmap_ =
        SelectObject(
            keyboard_dc_,
            keyboard_bitmap_);

    keyboard_font_ = CreateFontW(
        -30,
        0,
        0,
        0,
        FW_SEMIBOLD,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        ANTIALIASED_QUALITY,
        DEFAULT_PITCH,
        L"Segoe UI");
    if (!keyboard_font_) {
        return Status::failure(
            StatusCode::PlatformFailure,
            GetLastError());
    }

    keyboard_shadow_brush_ =
        CreateSolidBrush(RGB(16, 19, 24));
    keyboard_light_brush_ =
        CreateSolidBrush(RGB(246, 247, 245));
    keyboard_light_pen_ =
        CreatePen(PS_SOLID, 2, RGB(198, 202, 207));

    if (!keyboard_shadow_brush_ ||
        !keyboard_light_brush_ ||
        !keyboard_light_pen_) {
        return Status::failure(
            StatusCode::PlatformFailure,
            GetLastError());
    }

    return Status::success();
}

void D3D11Compositor::rasterize_keyboard_keycaps(
    const arssyut::presentation::KeyboardOverlayFrame &keyboard) noexcept
{
    if (!keyboard_dc_ ||
        !keyboard_bits_ ||
        !keyboard_font_ ||
        !keyboard_shadow_brush_ ||
        !keyboard_light_brush_ ||
        !keyboard_light_pen_) {
        return;
    }

    auto *pixels =
        static_cast<std::uint32_t *>(keyboard_bits_);
    std::fill(
        pixels,
        pixels +
            static_cast<std::size_t>(kKeyboardWidth) *
                static_cast<std::size_t>(kKeyboardHeight),
        0u);

    keyboard_content_width_ = 1;
    keyboard_content_height_ = 1;

    const std::size_t count =
        std::min(
            keyboard.keycap_count,
            keyboard.keycaps.size());
    if (count == 0)
        return;

    HGDIOBJ old_font =
        SelectObject(
            keyboard_dc_,
            keyboard_font_);
    HGDIOBJ old_brush =
        GetCurrentObject(
            keyboard_dc_,
            OBJ_BRUSH);
    HGDIOBJ old_pen =
        GetCurrentObject(
            keyboard_dc_,
            OBJ_PEN);

    SetBkMode(
        keyboard_dc_,
        TRANSPARENT);

    constexpr int kPadding = 8;
    constexpr int kGap = 10;
    constexpr int kTop = 12;
    constexpr int kFaceHeight = 82;
    constexpr int kDepth = 6;
    constexpr int kCorner = 13;

    int x = kPadding;

    for (std::size_t i = 0; i < count; ++i) {
        const auto &keycap =
            keyboard.keycaps[i];

        const bool windows_logo =
            keycap.glyph ==
            arssyut::presentation::KeycapGlyph::WindowsLogo;

        if (!windows_logo &&
            keycap.label[0] == L'\0') {
            continue;
        }

        int width = kFaceHeight;
        switch (keycap.size) {
        case arssyut::presentation::KeycapSize::Unit125:
            width = 103;
            break;
        case arssyut::presentation::KeycapSize::Unit150:
            width = 123;
            break;
        case arssyut::presentation::KeycapSize::Unit200:
            width = 164;
            break;
        case arssyut::presentation::KeycapSize::Unit350:
            width = 287;
            break;
        case arssyut::presentation::KeycapSize::Unit1:
        default:
            width = kFaceHeight;
            break;
        }

        RECT shadow{
            x + 2,
            kTop + kDepth,
            x + width + 2,
            kTop + kFaceHeight + kDepth
        };

        SelectObject(
            keyboard_dc_,
            keyboard_shadow_brush_);
        SelectObject(
            keyboard_dc_,
            GetStockObject(NULL_PEN));
        RoundRect(
            keyboard_dc_,
            shadow.left,
            shadow.top,
            shadow.right,
            shadow.bottom,
            kCorner,
            kCorner);

        SelectObject(
            keyboard_dc_,
            keyboard_light_brush_);
        SelectObject(
            keyboard_dc_,
            keyboard_light_pen_);

        RECT face{
            x,
            kTop,
            x + width,
            kTop + kFaceHeight
        };

        RoundRect(
            keyboard_dc_,
            face.left,
            face.top,
            face.right,
            face.bottom,
            kCorner,
            kCorner);

        SetTextColor(
            keyboard_dc_,
            RGB(31, 34, 39));

        if (windows_logo) {
            const int cx =
                (face.left + face.right) / 2;
            const int cy =
                (face.top + face.bottom) / 2;
            constexpr int pane_w = 11;
            constexpr int pane_h = 13;
            constexpr int gap = 3;

            HGDIOBJ old_logo_brush =
                SelectObject(
                    keyboard_dc_,
                    GetStockObject(DC_BRUSH));
            HGDIOBJ old_logo_pen =
                SelectObject(
                    keyboard_dc_,
                    GetStockObject(NULL_PEN));
            SetDCBrushColor(
                keyboard_dc_,
                RGB(31, 34, 39));

            const int left =
                cx - pane_w - gap / 2;
            const int right =
                cx + gap / 2;
            const int top =
                cy - pane_h - gap / 2;
            const int bottom =
                cy + gap / 2;

            Rectangle(
                keyboard_dc_,
                left,
                top,
                left + pane_w,
                top + pane_h);
            Rectangle(
                keyboard_dc_,
                right,
                top - 1,
                right + pane_w + 1,
                top + pane_h);
            Rectangle(
                keyboard_dc_,
                left,
                bottom,
                left + pane_w,
                bottom + pane_h);
            Rectangle(
                keyboard_dc_,
                right,
                bottom,
                right + pane_w + 1,
                bottom + pane_h + 1);

            SelectObject(
                keyboard_dc_,
                old_logo_pen);
            SelectObject(
                keyboard_dc_,
                old_logo_brush);
        } else {
            RECT text_rect = face;
            text_rect.top -= 1;

            DrawTextW(
                keyboard_dc_,
                keycap.label.data(),
                -1,
                &text_rect,
                DT_CENTER |
                    DT_VCENTER |
                    DT_SINGLELINE |
                    DT_NOPREFIX);
        }

        x = face.right + kGap;
    }

    SelectObject(
        keyboard_dc_,
        old_pen);
    SelectObject(
        keyboard_dc_,
        old_brush);
    SelectObject(
        keyboard_dc_,
        old_font);

    keyboard_content_width_ =
        static_cast<std::uint32_t>(
            std::clamp(
                x - kGap + kPadding,
                1,
                static_cast<int>(kKeyboardWidth)));
    keyboard_content_height_ =
        static_cast<std::uint32_t>(
            std::min(
                kTop + kFaceHeight + kDepth + 8,
                static_cast<int>(kKeyboardHeight)));

    // GDI draws RGB but does not preserve alpha in the 32-bit DIB. Promote
    // only painted pixels. The shader applies the temporal overlay opacity.
    for (std::size_t i = 0;
         i <
         static_cast<std::size_t>(kKeyboardWidth) *
             static_cast<std::size_t>(kKeyboardHeight);
         ++i) {
        if ((pixels[i] & 0x00FFFFFFu) != 0)
            pixels[i] |= 0xFF000000u;
    }
}

Status D3D11Compositor::update_keyboard_overlay(
    ID3D11DeviceContext *context,
    const arssyut::presentation::KeyboardOverlayFrame &keyboard) noexcept
{
    if (!context ||
        !keyboard_texture_)
        return Status::failure(
            StatusCode::InvalidArgument);

    if (keyboard.generation == 0 ||
        keyboard.generation == keyboard_generation_) {
        return Status::success();
    }

    rasterize_keyboard_keycaps(
        keyboard);

    context->UpdateSubresource(
        keyboard_texture_.Get(),
        0,
        nullptr,
        keyboard_bits_,
        kKeyboardWidth * 4,
        0);

    keyboard_generation_ =
        keyboard.generation;

    return Status::success();
}

void D3D11Compositor::resolve_gpu_queries(
    ID3D11DeviceContext *context) noexcept
{
    if (!context)
        return;

    for (auto &slot : gpu_queries_) {
        if (!slot.pending)
            continue;

        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint{};
        UINT64 start = 0;
        UINT64 end = 0;

        const HRESULT disjoint_hr = context->GetData(
            slot.disjoint.Get(),
            &disjoint,
            sizeof(disjoint),
            D3D11_ASYNC_GETDATA_DONOTFLUSH);
        if (disjoint_hr != S_OK)
            continue;

        const HRESULT start_hr = context->GetData(
            slot.start.Get(),
            &start,
            sizeof(start),
            D3D11_ASYNC_GETDATA_DONOTFLUSH);
        if (start_hr != S_OK)
            continue;

        const HRESULT end_hr = context->GetData(
            slot.end.Get(),
            &end,
            sizeof(end),
            D3D11_ASYNC_GETDATA_DONOTFLUSH);
        if (end_hr != S_OK)
            continue;

        if (!disjoint.Disjoint &&
            disjoint.Frequency > 0 &&
            end >= start) {
            const std::uint64_t elapsed =
                end - start;
            const std::uint64_t microseconds =
                (elapsed * 1'000'000ULL) /
                disjoint.Frequency;

            gpu_latency_.observe(
                static_cast<std::uint32_t>(
                    std::min<std::uint64_t>(
                        microseconds,
                        std::numeric_limits<std::uint32_t>::max())));
        }

        slot.pending = false;
    }
}

std::size_t D3D11Compositor::begin_gpu_query(
    ID3D11DeviceContext *context) noexcept
{
    if (!context)
        return invalid_query_slot;

    for (std::size_t i = 0; i < gpu_queries_.size(); ++i) {
        auto &slot = gpu_queries_[i];
        if (slot.pending)
            continue;

        context->Begin(slot.disjoint.Get());
        context->End(slot.start.Get());
        return i;
    }

    return invalid_query_slot;
}

void D3D11Compositor::end_gpu_query(
    ID3D11DeviceContext *context,
    std::size_t index) noexcept
{
    if (!context ||
        index == invalid_query_slot ||
        index >= gpu_queries_.size()) {
        return;
    }

    auto &slot = gpu_queries_[index];
    context->End(slot.end.Get());
    context->End(slot.disjoint.Get());
    slot.pending = true;
}

Status D3D11Compositor::ensure_input(
    ID3D11Texture2D *source) noexcept
{
    if (!source)
        return Status::failure(StatusCode::InvalidArgument);

    D3D11_TEXTURE2D_DESC desc{};
    source->GetDesc(&desc);

    if (desc.Width == 0 || desc.Height == 0)
        return Status::failure(StatusCode::InvalidArgument);

    const bool same =
        input_copy_ &&
        desc.Width == input_desc_.Width &&
        desc.Height == input_desc_.Height &&
        desc.Format == input_desc_.Format &&
        desc.SampleDesc.Count == input_desc_.SampleDesc.Count;

    if (same)
        return Status::success();

    input_srv_.Reset();
    input_copy_.Reset();

    D3D11_TEXTURE2D_DESC copy_desc = desc;
    copy_desc.Usage = D3D11_USAGE_DEFAULT;
    copy_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    copy_desc.CPUAccessFlags = 0;
    copy_desc.MiscFlags = 0;
    copy_desc.MipLevels = 1;
    copy_desc.ArraySize = 1;

    HRESULT hr = device_->CreateTexture2D(
        &copy_desc,
        nullptr,
        input_copy_.GetAddressOf());
    if (FAILED(hr))
        return d3d_failure(hr);

    D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc{};
    srv_desc.Format = copy_desc.Format;
    srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Texture2D.MostDetailedMip = 0;
    srv_desc.Texture2D.MipLevels = 1;

    hr = device_->CreateShaderResourceView(
        input_copy_.Get(),
        &srv_desc,
        input_srv_.GetAddressOf());
    if (FAILED(hr)) {
        input_copy_.Reset();
        return d3d_failure(hr);
    }

    input_desc_ = desc;
    ++resource_generation_;
    return Status::success();
}

Status D3D11Compositor::ensure_output(FrameSize output_size) noexcept
{
    if (!output_size.valid())
        return Status::failure(StatusCode::InvalidArgument);

    if (output_texture_ && output_size == output_size_)
        return Status::success();

    output_rtv_.Reset();
    output_texture_.Reset();

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = output_size.width;
    desc.Height = output_size.height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags =
        D3D11_BIND_RENDER_TARGET |
        D3D11_BIND_SHADER_RESOURCE;

    HRESULT hr = device_->CreateTexture2D(
        &desc,
        nullptr,
        output_texture_.GetAddressOf());
    if (FAILED(hr))
        return d3d_failure(hr);

    hr = device_->CreateRenderTargetView(
        output_texture_.Get(),
        nullptr,
        output_rtv_.GetAddressOf());
    if (FAILED(hr)) {
        output_texture_.Reset();
        return d3d_failure(hr);
    }

    output_size_ = output_size;
    ++resource_generation_;
    return Status::success();
}

Status D3D11Compositor::submit_scene_analysis(
    ID3D11DeviceContext *context,
    TimePoint now,
    const arssyut::visual::ArVisualGradeSettings *visual) noexcept
{
    if (!context) {
        return Status::failure(
            StatusCode::InvalidArgument);
    }

    if (!visual ||
        !visual->enabled ||
        !visual->smart_auto ||
        !scene_analyzer_ ||
        !input_srv_) {
        return Status::success();
    }

    return scene_analyzer_->submit_if_due(
        context,
        input_srv_.Get(),
        now);
}

Status D3D11Compositor::update_source(
    ID3D11DeviceContext *context,
    ID3D11Texture2D *source) noexcept
{
    if (!context || !source)
        return Status::failure(StatusCode::InvalidArgument);

    const Status input_status = ensure_input(source);
    if (!input_status.ok())
        return input_status;

    context->CopyResource(input_copy_.Get(), source);
    return Status::success();
}

Status D3D11Compositor::render_retained(
    ID3D11DeviceContext *context,
    CropRect crop,
    FrameSize output_size,
    const arssyut::presentation::PresentationFrameState *presentation,
    const arssyut::visual::ArVisualGradeSettings *visual) noexcept
{
    if (!context || !has_source())
        return Status::failure(StatusCode::InvalidArgument);

    const TimePoint started = MonotonicClock::now();

    resolve_gpu_queries(context);

    const Status output_status = ensure_output(output_size);
    if (!output_status.ok())
        return output_status;

    const FrameSize source_size{
        input_desc_.Width,
        input_desc_.Height
    };
    crop = arssyut::core::clamp_crop(crop, source_size);

    const std::size_t gpu_query =
        begin_gpu_query(context);

    arssyut::presentation::PresentationFrameState neutral{};
    const auto &state =
        presentation ? *presentation : neutral;

    auto grade =
        arssyut::visual::sanitize(
            visual
                ? *visual
                : arssyut::visual::ArVisualGradeSettings{});

    if (grade.enabled &&
        grade.smart_auto &&
        scene_analyzer_) {
        scene_analyzer_->poll_nonblocking(
            context);
        (void)scene_analyzer_->apply_latest(
            grade);
    }

    const Status keyboard_status =
        update_keyboard_overlay(
            context,
            state.keyboard);
    if (!keyboard_status.ok())
        return keyboard_status;

    PresentationConstants constants{};
    constants.uv_left =
        static_cast<float>(crop.left) /
        static_cast<float>(source_size.width);
    constants.uv_top =
        static_cast<float>(crop.top) /
        static_cast<float>(source_size.height);
    constants.uv_right =
        static_cast<float>(crop.right) /
        static_cast<float>(source_size.width);
    constants.uv_bottom =
        static_cast<float>(crop.bottom) /
        static_cast<float>(source_size.height);

    constants.camera_center_x =
        std::clamp(
            state.camera_center_x,
            0.0f,
            1.0f);
    constants.camera_center_y =
        std::clamp(
            state.camera_center_y,
            0.0f,
            1.0f);
    constants.camera_zoom =
        std::clamp(
            state.camera_zoom,
            1.0f,
            4.0f);
    constants.keyboard_opacity =
        std::clamp(
            state.keyboard.opacity,
            0.0f,
            1.0f);

    constants.output_width =
        static_cast<float>(output_size.width);
    constants.output_height =
        static_cast<float>(output_size.height);

    constants.keyboard_uv_scale_x =
        static_cast<float>(keyboard_content_width_) /
        static_cast<float>(kKeyboardWidth);
    constants.keyboard_uv_scale_y =
        static_cast<float>(keyboard_content_height_) /
        static_cast<float>(kKeyboardHeight);

    const float content_aspect =
        static_cast<float>(keyboard_content_width_) /
        std::max(
            static_cast<float>(keyboard_content_height_),
            1.0f);

    float display_height_px = std::clamp(
        constants.output_height * 0.090f,
        72.0f,
        132.0f);
    float display_width_px =
        display_height_px * content_aspect;

    const float max_width_px =
        constants.output_width * 0.78f;
    if (display_width_px > max_width_px &&
        display_width_px > 0.0f) {
        const float scale =
            max_width_px / display_width_px;
        display_width_px *= scale;
        display_height_px *= scale;
    }

    const float bottom_margin_px = std::clamp(
        constants.output_height * 0.035f,
        18.0f,
        54.0f);

    constants.keyboard_left =
        (constants.output_width - display_width_px) *
        0.5f /
        constants.output_width;
    constants.keyboard_right =
        (constants.output_width + display_width_px) *
        0.5f /
        constants.output_width;
    constants.keyboard_bottom =
        (constants.output_height - bottom_margin_px) /
        constants.output_height;
    constants.keyboard_top =
        (constants.output_height -
         bottom_margin_px -
         display_height_px) /
        constants.output_height;

    constants.arvisual_enabled =
        grade.enabled ? 1.0f : 0.0f;
    constants.arvisual_master =
        grade.master;
    constants.arvisual_enhance =
        grade.enhance;
    constants.arvisual_color_pop =
        grade.color_pop;

    constants.arvisual_clean_white =
        grade.clean_white;
    constants.arvisual_clarity =
        grade.clarity;
    constants.arvisual_skin_protect =
        grade.skin_protect;
    constants.arvisual_skin_beauty =
        grade.skin_beauty;

    constants.arvisual_healthy_tone =
        grade.healthy_tone;
    constants.arvisual_toy_gloss =
        grade.toy_gloss;
    constants.arvisual_depth_pop =
        grade.depth_pop;
    constants.arvisual_highlight_guard =
        grade.highlight_guard;

    constants.arvisual_performance =
        grade.performance;
    constants.arvisual_smart_exposure =
        grade.smart_exposure;
    constants.arvisual_smart_pop =
        grade.smart_pop;
    constants.arvisual_smart_highlight =
        grade.smart_highlight;

    constants.arvisual_smart_shadow =
        grade.smart_shadow;
    constants.arvisual_smart_strength =
        grade.smart_strength;
    constants.arvisual_smart_chroma_limit =
        grade.smart_chroma_limit;
    constants.arvisual_smart_clean =
        grade.smart_clean;

    constants.arvisual_smart_separation =
        grade.smart_separation;
    constants.arvisual_texel_x =
        1.0f /
        std::max(
            static_cast<float>(source_size.width),
            1.0f);
    constants.arvisual_texel_y =
        1.0f /
        std::max(
            static_cast<float>(source_size.height),
            1.0f);
    constants.arvisual_text_legibility =
        grade.text_legibility;
    constants.arvisual_ui_structure =
        grade.ui_structure;

    for (std::size_t i = 0;
         i < state.clicks.size();
         ++i) {
        const auto &click =
            state.clicks[i];
        const std::size_t base = i * 4;

        constants.clicks[base + 0] =
            click.content_x;
        constants.clicks[base + 1] =
            click.content_y;
        const float lifetime =
            std::max(
                click.lifetime_seconds,
                0.0001f);
        constants.clicks[base + 2] =
            std::clamp(
                click.age_seconds / lifetime,
                0.0f,
                1.0f);
        constants.clicks[base + 3] =
            static_cast<float>(
                static_cast<std::uint8_t>(
                    click.kind));
    }

    context->UpdateSubresource(
        crop_constant_buffer_.Get(),
        0,
        nullptr,
        &constants,
        0,
        0);

    ID3D11RenderTargetView *rtv = output_rtv_.Get();
    context->OMSetRenderTargets(1, &rtv, nullptr);

    D3D11_VIEWPORT viewport{};
    viewport.Width = static_cast<float>(output_size.width);
    viewport.Height = static_cast<float>(output_size.height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    context->RSSetViewports(1, &viewport);

    context->IASetInputLayout(nullptr);
    context->IASetPrimitiveTopology(
        D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    context->VSSetShader(vertex_shader_.Get(), nullptr, 0);
    context->PSSetShader(pixel_shader_.Get(), nullptr, 0);

    ID3D11Buffer *constant_buffer = crop_constant_buffer_.Get();
    context->PSSetConstantBuffers(0, 1, &constant_buffer);

    ID3D11SamplerState *sampler = sampler_.Get();
    context->PSSetSamplers(0, 1, &sampler);

    ID3D11ShaderResourceView *srvs[2] = {
        input_srv_.Get(),
        keyboard_srv_.Get()
    };
    context->PSSetShaderResources(0, 2, srvs);

    context->Draw(3, 0);

    ID3D11ShaderResourceView *null_srvs[2] = {
        nullptr,
        nullptr
    };
    context->PSSetShaderResources(0, 2, null_srvs);

    ID3D11RenderTargetView *null_rtv = nullptr;
    context->OMSetRenderTargets(1, &null_rtv, nullptr);

    end_gpu_query(context, gpu_query);

    cpu_latency_.observe(
        elapsed_microseconds(started, MonotonicClock::now()));

    return Status::success();
}

bool D3D11Compositor::scene_analysis_available() const noexcept
{
    return scene_analyzer_ != nullptr;
}

std::uint64_t D3D11Compositor::scene_analysis_submitted() const noexcept
{
    return scene_analyzer_
        ? scene_analyzer_->submitted()
        : 0;
}

std::uint64_t D3D11Compositor::scene_analysis_completed() const noexcept
{
    return scene_analyzer_
        ? scene_analyzer_->completed()
        : 0;
}

std::uint64_t D3D11Compositor::scene_analysis_busy_skips() const noexcept
{
    return scene_analyzer_
        ? scene_analyzer_->busy_skips()
        : 0;
}

std::uint64_t D3D11Compositor::scene_analysis_map_failures() const noexcept
{
    return scene_analyzer_
        ? scene_analyzer_->map_failures()
        : 0;
}

bool D3D11Compositor::scene_analysis_primed() const noexcept
{
    return scene_analyzer_
        ? scene_analyzer_->primed()
        : false;
}

arssyut::visual::ArVisualSceneStats
D3D11Compositor::scene_analysis_stats() const noexcept
{
    return scene_analyzer_
        ? scene_analyzer_->latest_stats()
        : arssyut::visual::ArVisualSceneStats{};
}

arssyut::visual::ArVisualAdaptiveState
D3D11Compositor::scene_analysis_adaptive() const noexcept
{
    return scene_analyzer_
        ? scene_analyzer_->latest_adaptive()
        : arssyut::visual::ArVisualAdaptiveState{};
}

Status D3D11Compositor::render(
    ID3D11DeviceContext *context,
    ID3D11Texture2D *source,
    CropRect crop,
    FrameSize output_size,
    const arssyut::presentation::PresentationFrameState *presentation,
    const arssyut::visual::ArVisualGradeSettings *visual) noexcept
{
    const Status update_status =
        update_source(context, source);
    if (!update_status.ok())
        return update_status;

    return render_retained(
        context,
        crop,
        output_size,
        presentation,
        visual);
}

} // namespace arssyut::windows

#endif
