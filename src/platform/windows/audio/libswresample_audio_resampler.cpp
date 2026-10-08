#include "platform/windows/audio/libswresample_audio_resampler.hpp"

extern "C" {
#include <libavutil/channel_layout.h>
#include <libavutil/mathematics.h>
#include <libavutil/opt.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

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
constexpr int kPhaseShift = 10;
constexpr std::int64_t kPhaseCount =
    1LL << kPhaseShift;

[[nodiscard]] std::uint64_t add_mul_mod(
    std::uint64_t current,
    std::uint64_t count,
    std::uint64_t step,
    std::uint64_t modulus) noexcept
{
    if (modulus == 0)
        return 0;

    current %= modulus;
    step %= modulus;

    while (count != 0) {
        if ((count & 1U) != 0) {
            if (current >= modulus - step)
                current -= modulus - step;
            else
                current += step;
        }

        count >>= 1U;
        if (count == 0)
            break;

        if (step >= modulus - step)
            step -= modulus - step;
        else
            step += step;
    }

    return current;
}

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

    /*
     * Freeze the internal resampling lattice using public AVOptions. With
     * exact_rational disabled, phase_count remains exactly 2^phase_shift and
     * soft compensation does not rebuild the filter bank onto a different
     * phase lattice. This lets the wrapper reproduce FFmpeg 9.0.2's integer
     * src/dst increments exactly using the same public av_reduce() primitive.
     */
    if (av_opt_set_int(
            created,
            "phase_shift",
            kPhaseShift,
            0) < 0 ||
        av_opt_set_int(
            created,
            "exact_rational",
            0,
            0) < 0) {
        swr_free(&created);
        return AudioResampleStatus::Failed;
    }

    if (swr_init(created) < 0) {
        swr_free(&created);
        return AudioResampleStatus::Failed;
    }

    context_ = created;
    config_ = config;

    int src_incr = 0;
    int dst_incr = 0;
    if (!av_reduce(
            &src_incr,
            &dst_incr,
            AudioResamplerConfig::
                kOutputSampleRate,
            static_cast<std::int64_t>(
                config.input_sample_rate) *
                kPhaseCount,
            INT32_MAX / 2)) {
        release();
        return AudioResampleStatus::Failed;
    }

    while (dst_incr < (1 << 20) &&
           src_incr < (1 << 20)) {
        dst_incr *= 2;
        src_incr *= 2;
    }

    nominal_src_incr_ = src_incr;
    nominal_dst_incr_ = dst_incr;
    active_dst_incr_ = nominal_dst_incr_;

    const std::uint64_t denominator =
        static_cast<std::uint64_t>(
            nominal_src_incr_) *
        static_cast<std::uint64_t>(
            kPhaseCount);

    if (denominator == 0) {
        release();
        return AudioResampleStatus::Failed;
    }

    phase_denominator_ = denominator;
    phase_remainder_ = 0;

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
    phase_remainder_ = 0;
    active_dst_incr_ = nominal_dst_incr_;
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

    const int required_output_frames =
        swr_get_out_samples(
            context_,
            static_cast<int>(
                input_frames));

    if (required_output_frames < 0) {
        result.status =
            AudioResampleStatus::Failed;
        return result;
    }

    if (output_frames_capacity <
        static_cast<std::size_t>(
            required_output_frames)) {
        result.status =
            AudioResampleStatus::OutputFull;
        result.algorithmic_delay_100ns =
            current_delay_100ns();
        refresh_rate_state(
            rate_state_.
                requested_rate_adjustment_ppm,
            rate_state_.
                applied_rate_adjustment_ppm);
        return result;
    }

    /*
     * Keep one process() call inside one compensation epoch. apply_rate_adjustment()
     * refreshes at/below the half-horizon mark, so bounding one call to that
     * half horizon guarantees FFmpeg cannot switch back to nominal dst_incr in
     * the middle of the call while the mirrored phase tracker still uses the
     * compensated step.
     */
    if (required_output_frames >
        kCompensationRefreshFrames) {
        result.status =
            AudioResampleStatus::InvalidArgument;
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

    phase_remainder_ =
        add_mul_mod(
            phase_remainder_,
            static_cast<std::uint64_t>(
                produced),
            static_cast<std::uint64_t>(
                active_dst_incr_),
            phase_denominator_);
    result.algorithmic_delay_100ns =
        current_delay_100ns();

    compensation_frames_remaining_ =
        std::max<std::int64_t>(
            0,
            compensation_frames_remaining_ -
                produced);

    drain_complete_ = false;
    result.status =
        AudioResampleStatus::Ok;

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

    if (!apply_rate_adjustment(
            rate_state_.
                requested_rate_adjustment_ppm)) {
        result.status =
            AudioResampleStatus::Failed;
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

    phase_remainder_ =
        add_mul_mod(
            phase_remainder_,
            static_cast<std::uint64_t>(
                produced),
            static_cast<std::uint64_t>(
                active_dst_incr_),
            phase_denominator_);

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

    const std::int64_t compensated_dst =
        nominal_dst_incr_ -
        (nominal_dst_incr_ *
         static_cast<std::int64_t>(
             sample_delta)) /
            kCompensationHorizonFrames;

    if (compensated_dst <= 0)
        return false;

    active_dst_incr_ =
        compensated_dst;

    const double effective_ratio =
        static_cast<double>(
            nominal_src_incr_) *
        static_cast<double>(
            kPhaseCount) /
        static_cast<double>(
            active_dst_incr_);

    const double nominal_ratio =
        static_cast<double>(
            AudioResamplerConfig::
                kOutputSampleRate) /
        static_cast<double>(
            config_.input_sample_rate);

    const double applied_ppm =
        (effective_ratio / nominal_ratio -
         1.0) *
        1'000'000.0;

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

    if (!configured_ ||
        config_.input_sample_rate == 0 ||
        active_dst_incr_ <= 0 ||
        nominal_src_incr_ <= 0 ||
        phase_denominator_ == 0) {
        rate_state_.
            effective_output_per_input_ratio = 0.0;
        rate_state_.
            phase_remainder_numerator = 0;
        rate_state_.
            phase_remainder_denominator = 1;
        return;
    }

    rate_state_.
        effective_output_per_input_ratio =
        static_cast<double>(
            nominal_src_incr_) *
        static_cast<double>(
            kPhaseCount) /
        static_cast<double>(
            active_dst_incr_);

    rate_state_.
        phase_remainder_numerator =
        phase_remainder_;
    rate_state_.
        phase_remainder_denominator =
        phase_denominator_;
}

void LibSwResampleAudioResampler::release() noexcept
{
    if (context_ != nullptr)
        swr_free(&context_);

    configured_ = false;
    config_ = {};
    rate_state_ = {};
    phase_remainder_ = 0;
    phase_denominator_ = 1;
    nominal_src_incr_ = 0;
    nominal_dst_incr_ = 0;
    active_dst_incr_ = 0;
    compensation_frames_remaining_ = 0;
    drain_complete_ = false;
}

} // namespace arssyut::platform::windows::audio
