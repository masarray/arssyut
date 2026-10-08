#include "platform/windows/audio/libswresample_audio_resampler.hpp"

extern "C" {
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>

namespace arssyut::platform::windows::audio {

namespace {

using core::audio::AudioResampleDrainResult;
using core::audio::AudioResampleResult;
using core::audio::AudioResampleStatus;
using core::audio::AudioResamplerConfig;

constexpr double kMaximumAbsolutePpm = 1'000.0;
constexpr std::int64_t kCompensationHorizonFrames = 4'800'000; // 100 s at 48 kHz
constexpr std::int64_t kCompensationRefreshFrames =
    kCompensationHorizonFrames / 2;
constexpr std::uint32_t kMaximumDrainFrames = 8'192;

[[nodiscard]] bool make_input_layout(
    const AudioResamplerConfig &config,
    AVChannelLayout &layout) noexcept
{
    if (config.input_channel_mask != 0) {
        const int result =
            av_channel_layout_from_mask(
                &layout,
                static_cast<std::uint64_t>(
                    config.input_channel_mask));
        return result >= 0 &&
               layout.nb_channels ==
                   config.input_channels;
    }

    av_channel_layout_default(
        &layout,
        config.input_channels);

    return layout.nb_channels ==
           config.input_channels;
}

[[nodiscard]] std::uint64_t safe_lcm(
    std::uint32_t first,
    std::uint32_t second) noexcept
{
    if (first == 0 || second == 0)
        return 0;

    const auto divisor =
        std::gcd(first, second);
    const std::uint64_t reduced =
        first / divisor;

    if (reduced >
        std::numeric_limits<std::uint64_t>::max() /
            second)
        return 0;

    return reduced * second;
}

} // namespace

LibSwResampleAudioResampler::~LibSwResampleAudioResampler()
{
    release();
}

AudioResampleStatus LibSwResampleAudioResampler::configure(
    AudioResamplerConfig config) noexcept
{
    release();

    if (!config.valid())
        return AudioResampleStatus::InvalidArgument;

    AVChannelLayout input_layout{};
    AVChannelLayout output_layout =
        AV_CHANNEL_LAYOUT_STEREO;

    if (!make_input_layout(
            config,
            input_layout)) {
        av_channel_layout_uninit(
            &input_layout);
        return AudioResampleStatus::InvalidArgument;
    }

    SwrContext *created = nullptr;
    const int allocation_result =
        swr_alloc_set_opts2(
            &created,
            &output_layout,
            AV_SAMPLE_FMT_FLT,
            AudioResamplerConfig::kOutputSampleRate,
            &input_layout,
            AV_SAMPLE_FMT_FLT,
            static_cast<int>(
                config.input_sample_rate),
            0,
            nullptr);

    av_channel_layout_uninit(
        &input_layout);

    if (allocation_result < 0 ||
        created == nullptr) {
        if (created != nullptr)
            swr_free(&created);
        return AudioResampleStatus::Failed;
    }

    if (swr_init(created) < 0) {
        swr_free(&created);
        return AudioResampleStatus::Failed;
    }

    context_ = created;
    config_ = config;
    phase_base_ =
        safe_lcm(
            config.input_sample_rate,
            AudioResamplerConfig::
                kOutputSampleRate);

    if (phase_base_ == 0 ||
        phase_base_ >
            static_cast<std::uint64_t>(
                std::numeric_limits<
                    std::int64_t>::max())) {
        release();
        return AudioResampleStatus::Failed;
    }

    configured_ = true;
    drain_complete_ = false;
    compensation_frames_remaining_ = 0;
    refresh_rate_state(0.0, 0.0);
    return AudioResampleStatus::Ok;
}

void LibSwResampleAudioResampler::reset() noexcept
{
    if (!configured_ ||
        context_ == nullptr)
        return;

    swr_close(context_);
    if (swr_init(context_) < 0) {
        release();
        return;
    }

    compensation_frames_remaining_ = 0;
    drain_complete_ = false;
    refresh_rate_state(0.0, 0.0);
}

AudioResampleResult LibSwResampleAudioResampler::process(
    std::span<const float> input_interleaved,
    std::uint32_t input_frames,
    std::span<float> output_interleaved,
    double rate_adjustment_ppm) noexcept
{
    AudioResampleResult result;

    if (!configured_ ||
        context_ == nullptr) {
        result.status =
            AudioResampleStatus::NotConfigured;
        return result;
    }

    if (input_frames == 0 ||
        !std::isfinite(rate_adjustment_ppm) ||
        input_interleaved.size() !=
            static_cast<std::size_t>(
                input_frames) *
                config_.input_channels ||
        output_interleaved.size() <
            AudioResamplerConfig::
                kOutputChannels) {
        result.status =
            AudioResampleStatus::InvalidArgument;
        return result;
    }

    const auto output_frames_capacity =
        output_interleaved.size() /
        AudioResamplerConfig::
            kOutputChannels;

    if (output_frames_capacity >
        static_cast<std::size_t>(
            std::numeric_limits<int>::max()) ||
        input_frames >
        static_cast<std::uint32_t>(
            std::numeric_limits<int>::max())) {
        result.status =
            AudioResampleStatus::InvalidArgument;
        return result;
    }

    if (!apply_rate_adjustment(
            rate_adjustment_ppm)) {
        result.status =
            AudioResampleStatus::Failed;
        return result;
    }

    const std::uint8_t *input_planes[1]{
        reinterpret_cast<const std::uint8_t *>(
            input_interleaved.data())};
    std::uint8_t *output_planes[1]{
        reinterpret_cast<std::uint8_t *>(
            output_interleaved.data())};

    const int produced =
        swr_convert(
            context_,
            output_planes,
            static_cast<int>(
                output_frames_capacity),
            input_planes,
            static_cast<int>(
                input_frames));

    if (produced < 0) {
        result.status =
            AudioResampleStatus::Failed;
        return result;
    }

    result.input_frames_consumed =
        input_frames;
    result.output_frames_produced =
        static_cast<std::uint32_t>(
            produced);
    result.algorithmic_delay_100ns =
        current_delay_100ns();

    compensation_frames_remaining_ =
        std::max<std::int64_t>(
            0,
            compensation_frames_remaining_ -
                produced);

    drain_complete_ = false;
    result.status =
        output_frames_capacity == 0
            ? AudioResampleStatus::OutputFull
            : AudioResampleStatus::Ok;

    refresh_rate_state(
        rate_state_.requested_rate_adjustment_ppm,
        rate_state_.applied_rate_adjustment_ppm);

    return result;
}

std::uint64_t
LibSwResampleAudioResampler::current_delay_100ns() const noexcept
{
    if (!configured_ ||
        context_ == nullptr)
        return 0;

    const auto delay =
        swr_get_delay(
            context_,
            static_cast<std::int64_t>(
                core::audio::
                    kMediaTicksPerSecond));

    return delay > 0
        ? static_cast<std::uint64_t>(
              delay)
        : 0;
}

core::audio::AudioResamplerRateState
LibSwResampleAudioResampler::current_rate_state() const noexcept
{
    return rate_state_;
}

std::uint32_t
LibSwResampleAudioResampler::maximum_drain_frames() const noexcept
{
    return kMaximumDrainFrames;
}

AudioResampleDrainResult LibSwResampleAudioResampler::drain(
    std::span<float> output_interleaved) noexcept
{
    AudioResampleDrainResult result;

    if (!configured_ ||
        context_ == nullptr) {
        result.status =
            AudioResampleStatus::NotConfigured;
        return result;
    }

    if (drain_complete_) {
        result.status =
            AudioResampleStatus::Ok;
        result.complete = true;
        return result;
    }

    const auto output_frames_capacity =
        output_interleaved.size() /
        AudioResamplerConfig::
            kOutputChannels;

    if (output_frames_capacity == 0 ||
        output_frames_capacity >
            static_cast<std::size_t>(
                std::numeric_limits<int>::max())) {
        result.status =
            AudioResampleStatus::InvalidArgument;
        return result;
    }

    std::uint8_t *output_planes[1]{
        reinterpret_cast<std::uint8_t *>(
            output_interleaved.data())};

    const int produced =
        swr_convert(
            context_,
            output_planes,
            static_cast<int>(
                output_frames_capacity),
            nullptr,
            0);

    if (produced < 0) {
        result.status =
            AudioResampleStatus::Failed;
        return result;
    }

    result.output_frames_produced =
        static_cast<std::uint32_t>(
            produced);
    result.remaining_delay_100ns =
        current_delay_100ns();
    result.complete =
        produced == 0;

    if (result.complete)
        drain_complete_ = true;

    result.status =
        AudioResampleStatus::Ok;

    refresh_rate_state(
        rate_state_.requested_rate_adjustment_ppm,
        rate_state_.applied_rate_adjustment_ppm);

    return result;
}

bool LibSwResampleAudioResampler::apply_rate_adjustment(
    double requested_ppm) noexcept
{
    requested_ppm =
        std::clamp(
            requested_ppm,
            -kMaximumAbsolutePpm,
            kMaximumAbsolutePpm);

    const bool changed =
        std::abs(
            requested_ppm -
            rate_state_.
                requested_rate_adjustment_ppm) >
        0.01;

    if (!changed &&
        compensation_frames_remaining_ >
            kCompensationRefreshFrames)
        return true;

    const auto sample_delta =
        static_cast<int>(
            std::llround(
                static_cast<double>(
                    kCompensationHorizonFrames) *
                requested_ppm /
                1'000'000.0));

    if (swr_set_compensation(
            context_,
            sample_delta,
            static_cast<int>(
                kCompensationHorizonFrames)) < 0)
        return false;

    const double applied_ppm =
        static_cast<double>(
            sample_delta) *
        1'000'000.0 /
        static_cast<double>(
            kCompensationHorizonFrames);

    compensation_frames_remaining_ =
        kCompensationHorizonFrames;

    refresh_rate_state(
        requested_ppm,
        applied_ppm);
    return true;
}

void LibSwResampleAudioResampler::refresh_rate_state(
    double requested_ppm,
    double applied_ppm) noexcept
{
    rate_state_.
        requested_rate_adjustment_ppm =
        requested_ppm;
    rate_state_.
        applied_rate_adjustment_ppm =
        applied_ppm;

    const double nominal_ratio =
        static_cast<double>(
            AudioResamplerConfig::
                kOutputSampleRate) /
        static_cast<double>(
            config_.input_sample_rate);

    rate_state_.
        effective_output_per_input_ratio =
        configured_
            ? nominal_ratio *
                  (1.0 +
                   applied_ppm /
                       1'000'000.0)
            : 0.0;

    if (!configured_ ||
        context_ == nullptr ||
        phase_base_ == 0) {
        rate_state_.
            phase_remainder_numerator = 0;
        rate_state_.
            phase_remainder_denominator = 1;
        return;
    }

    const auto delay_units =
        swr_get_delay(
            context_,
            static_cast<std::int64_t>(
                phase_base_));

    const auto units_per_input_frame =
        phase_base_ /
        config_.input_sample_rate;

    if (delay_units < 0 ||
        units_per_input_frame == 0) {
        rate_state_.
            phase_remainder_numerator = 0;
        rate_state_.
            phase_remainder_denominator = 1;
        return;
    }

    rate_state_.
        phase_remainder_denominator =
        units_per_input_frame;
    rate_state_.
        phase_remainder_numerator =
        static_cast<std::uint64_t>(
            delay_units) %
        units_per_input_frame;
}

void LibSwResampleAudioResampler::release() noexcept
{
    if (context_ != nullptr)
        swr_free(&context_);

    configured_ = false;
    config_ = {};
    rate_state_ = {};
    phase_base_ = 0;
    compensation_frames_remaining_ = 0;
    drain_complete_ = false;
}

} // namespace arssyut::platform::windows::audio
