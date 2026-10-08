#pragma once

#include "core/audio/audio_resampler.hpp"

#include <cstdint>

struct SwrContext;

namespace arssyut::platform::windows::audio {

class LibSwResampleAudioResampler final
    : public core::audio::IAudioResampler {
public:
    LibSwResampleAudioResampler() noexcept = default;
    ~LibSwResampleAudioResampler() override;

    LibSwResampleAudioResampler(
        const LibSwResampleAudioResampler &) = delete;
    LibSwResampleAudioResampler &operator=(
        const LibSwResampleAudioResampler &) = delete;

    [[nodiscard]] core::audio::AudioResampleStatus configure(
        core::audio::AudioResamplerConfig config) noexcept override;

    void reset() noexcept override;

    [[nodiscard]] core::audio::AudioResampleResult process(
        std::span<const float> input_interleaved,
        std::uint32_t input_frames,
        std::span<float> output_interleaved,
        double rate_adjustment_ppm) noexcept override;

    [[nodiscard]] std::uint64_t current_delay_100ns() const noexcept override;

    [[nodiscard]] core::audio::AudioResamplerRateState
    current_rate_state() const noexcept override;

    [[nodiscard]] std::uint32_t maximum_drain_frames() const noexcept override;

    [[nodiscard]] core::audio::AudioResampleDrainResult drain(
        std::span<float> output_interleaved) noexcept override;

private:
    [[nodiscard]] bool apply_rate_adjustment(
        double requested_ppm) noexcept;

    void refresh_rate_state(
        double requested_ppm,
        double applied_ppm) noexcept;

    void release() noexcept;

    SwrContext *context_ = nullptr;
    core::audio::AudioResamplerConfig config_{};
    core::audio::AudioResamplerRateState rate_state_{};
    std::int64_t next_input_pts_units_ = 0;
    double effective_compensation_multiplier_ = 1.0;
    std::int64_t compensation_frames_remaining_ = 0;
    bool configured_ = false;
    bool drain_complete_ = false;
};

} // namespace arssyut::platform::windows::audio
