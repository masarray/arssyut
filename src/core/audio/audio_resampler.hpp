#pragma once

#include "core/audio/audio_format.hpp"

#include <cmath>
#include <cstdint>
#include <span>

namespace arssyut::core::audio {

struct AudioResamplerConfig {
    static constexpr std::uint32_t kOutputSampleRate = 48'000;
    static constexpr std::uint16_t kOutputChannels = 2;

    std::uint32_t input_sample_rate = 0;
    std::uint16_t input_channels = 0;
    std::uint32_t input_channel_mask = 0;

    [[nodiscard]] bool valid() const noexcept
    {
        if (input_sample_rate == 0 || input_channels == 0)
            return false;

        if (!channel_mask_matches_count(input_channels, input_channel_mask))
            return false;

        // Legacy mono/stereo formats may omit a channel mask because their
        // interleaving is canonical. Multichannel downmixing may not guess.
        if (input_channels > 2 && input_channel_mask == 0)
            return false;

        return true;
    }
};

enum class AudioResampleStatus : std::uint8_t {
    Ok = 0,
    InvalidArgument,
    NotConfigured,
    OutputFull,
    Failed,
};

/*
 * Observable rate state after the latest process() call.
 *
 * effective_output_per_input_ratio is the actual output-frame/input-frame
 * ratio applied by the implementation, including the nominal sample-rate
 * conversion and any quantized/clamped drift correction. The phase remainder
 * is the exact fractional input-frame phase retained across calls.
 *
 * P7A5 diagnostics must use the applied/effective values rather than assuming
 * the requested ppm was accepted unchanged by the selected backend.
 */
struct AudioResamplerRateState {
    double requested_rate_adjustment_ppm = 0.0;
    double applied_rate_adjustment_ppm = 0.0;
    double effective_output_per_input_ratio = 0.0;
    std::uint64_t phase_remainder_numerator = 0;
    std::uint64_t phase_remainder_denominator = 0;

    [[nodiscard]] bool observable() const noexcept
    {
        return std::isfinite(requested_rate_adjustment_ppm) &&
               std::isfinite(applied_rate_adjustment_ppm) &&
               std::isfinite(effective_output_per_input_ratio) &&
               effective_output_per_input_ratio > 0.0 &&
               phase_remainder_denominator != 0 &&
               phase_remainder_numerator < phase_remainder_denominator;
    }
};

struct AudioResampleResult {
    AudioResampleStatus status = AudioResampleStatus::NotConfigured;
    std::uint32_t input_frames_consumed = 0;
    std::uint32_t output_frames_produced = 0;
    std::uint64_t algorithmic_delay_100ns = 0;

    [[nodiscard]] bool ok() const noexcept
    {
        return status == AudioResampleStatus::Ok;
    }
};

struct AudioResampleDrainResult {
    AudioResampleStatus status = AudioResampleStatus::NotConfigured;
    std::uint32_t output_frames_produced = 0;
    std::uint64_t remaining_delay_100ns = 0;
    bool complete = false;

    [[nodiscard]] bool ok() const noexcept
    {
        return status == AudioResampleStatus::Ok;
    }
};

/*
 * P7A1 contract only.
 *
 * Implementations consume normalized float32 interleaved frames. Input channel
 * layout remains explicit so surround endpoints can be downmixed without
 * guessing. Output is structurally fixed to the canonical 48 kHz stereo bus.
 *
 * Resamplers are explicitly stateful. process() reports consumed/produced
 * frames and current algorithmic/group delay directly in the RecorderSession
 * 100 ns media-time domain. current_rate_state() exposes requested versus
 * actually applied drift correction, the resulting effective output/input
 * ratio and the exact retained fractional phase. Output PTS accounting must
 * include group delay and must never infer effective ratio from one call's
 * integer consumed/produced counts.
 *
 * At logical end-of-stream, drain() releases retained filter state before
 * writer shutdown. maximum_drain_frames() is a conservative hard upper bound
 * for one drain sequence after the final process() call and must not grow with
 * recording duration. reset() discards retained state without emitting media
 * and returns current_delay_100ns() to zero.
 *
 * P7A1R (#72) selects the production implementation; this interface does not
 * choose FFmpeg, Media Foundation or another candidate.
 */
class IAudioResampler {
public:
    virtual ~IAudioResampler() = default;

    [[nodiscard]] virtual AudioResampleStatus configure(
        AudioResamplerConfig config) noexcept = 0;

    virtual void reset() noexcept = 0;

    [[nodiscard]] virtual AudioResampleResult process(
        std::span<const float> input_interleaved,
        std::uint32_t input_frames,
        std::span<float> output_interleaved,
        double rate_adjustment_ppm) noexcept = 0;

    [[nodiscard]] virtual std::uint64_t current_delay_100ns() const noexcept = 0;

    [[nodiscard]] virtual AudioResamplerRateState current_rate_state() const noexcept = 0;

    [[nodiscard]] virtual std::uint32_t maximum_drain_frames() const noexcept = 0;

    [[nodiscard]] virtual AudioResampleDrainResult drain(
        std::span<float> output_interleaved) noexcept = 0;
};

} // namespace arssyut::core::audio
