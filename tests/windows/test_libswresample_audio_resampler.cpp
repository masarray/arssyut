#include "platform/windows/audio/libswresample_audio_resampler.hpp"

#include "core/audio/audio_format.hpp"

extern "C" {
#include <libavutil/mathematics.h>
}

#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {

using arssyut::core::audio::AudioResampleStatus;
using arssyut::core::audio::AudioResamplerConfig;
using arssyut::platform::windows::audio::
    LibSwResampleAudioResampler;

struct Test {
    int checks = 0;
    int failures = 0;

    void expect(bool value, const char *message)
    {
        ++checks;
        if (!value) {
            ++failures;
            std::cerr << "FAIL: " << message << '\n';
        }
    }
};

AudioResamplerConfig config_44k1()
{
    return {
        .input_sample_rate = 44'100,
        .input_channels = 2,
        .input_channel_mask =
            arssyut::core::audio::
                kStereoChannelMask,
    };
}

void test_config_and_rate_state(Test &test)
{
    LibSwResampleAudioResampler resampler;

    test.expect(
        resampler.configure(config_44k1()) ==
            AudioResampleStatus::Ok,
        "44.1 kHz stereo config is accepted");

    const auto initial =
        resampler.current_rate_state();

    test.expect(
        initial.observable(),
        "configured backend exposes observable rate state");

    std::vector<float> input(
        4'410 * 2,
        0.0F);
    std::vector<float> output(
        5'200 * 2,
        0.0F);

    const auto result =
        resampler.process(
            input,
            4'410,
            output,
            +100.0);

    test.expect(
        result.ok(),
        "+100 ppm process succeeds");

    const auto adjusted =
        resampler.current_rate_state();

    test.expect(
        adjusted.requested_rate_adjustment_ppm ==
            100.0,
        "requested ppm remains observable");
    test.expect(
        std::abs(
            adjusted.applied_rate_adjustment_ppm -
            100.0) <= 0.25,
        "applied ppm is close to requested compensation");
    constexpr std::int64_t phase_count = 1LL << 10;
    int expected_src_incr = 0;
    int expected_dst_incr = 0;
    test.expect(
        av_reduce(
            &expected_src_incr,
            &expected_dst_incr,
            48'000,
            44'100LL * phase_count,
            INT32_MAX / 2) != 0,
        "test reproduces pinned FFmpeg nominal lattice");

    while (expected_dst_incr < (1 << 20) &&
           expected_src_incr < (1 << 20)) {
        expected_dst_incr *= 2;
        expected_src_incr *= 2;
    }

    constexpr std::int64_t distance = 4'800'000;
    constexpr std::int64_t sample_delta = 480;
    const std::int64_t quantized_dst =
        expected_dst_incr -
        (static_cast<std::int64_t>(
             expected_dst_incr) *
         sample_delta) /
            distance;

    const double expected_ratio =
        static_cast<double>(
            expected_src_incr) *
        static_cast<double>(
            phase_count) /
        static_cast<double>(
            quantized_dst);

    const double nominal =
        48'000.0 / 44'100.0;
    const double expected_applied_ppm =
        (expected_ratio / nominal - 1.0) *
        1'000'000.0;

    test.expect(
        std::abs(
            adjusted.effective_output_per_input_ratio -
            expected_ratio) <
            1.0e-15,
        "effective ratio matches pinned FFmpeg integer dst increment");
    test.expect(
        std::abs(
            adjusted.applied_rate_adjustment_ppm -
            expected_applied_ppm) <
            1.0e-9,
        "applied ppm reports backend-quantized rate, not requested ideal");
    const std::uint64_t expected_phase_denominator =
        static_cast<std::uint64_t>(
            expected_src_incr) *
        static_cast<std::uint64_t>(
            phase_count);

    const std::uint64_t expected_phase_one =
        (static_cast<std::uint64_t>(
             result.output_frames_produced) *
         static_cast<std::uint64_t>(
             quantized_dst)) %
        expected_phase_denominator;

    test.expect(
        adjusted.phase_remainder_denominator ==
            expected_phase_denominator &&
        adjusted.phase_remainder_numerator ==
            expected_phase_one,
        "phase numerator exactly matches first compensated output advance");
    test.expect(
        adjusted.observable(),
        "fractional phase remains observable after processing");

    std::fill(
        output.begin(),
        output.end(),
        0.0F);

    const auto second =
        resampler.process(
            input,
            4'410,
            output,
            +100.0);

    test.expect(
        second.ok(),
        "second compensated process succeeds");

    const auto second_state =
        resampler.current_rate_state();

    const std::uint64_t expected_phase_two =
        (expected_phase_one +
         (static_cast<std::uint64_t>(
              second.output_frames_produced) *
          static_cast<std::uint64_t>(
              quantized_dst)) %
             expected_phase_denominator) %
        expected_phase_denominator;

    test.expect(
        second_state.phase_remainder_numerator ==
            expected_phase_two &&
        second_state.phase_remainder_denominator ==
            expected_phase_denominator,
        "phase evolves exactly across consecutive compensated calls");
}

void test_tone_quality(Test &test)
{
    LibSwResampleAudioResampler resampler;
    test.expect(
        resampler.configure(config_44k1()) ==
            AudioResampleStatus::Ok,
        "tone fixture config succeeds");

    constexpr std::uint32_t frames = 44'100;
    constexpr double tone_hz = 1'000.0;
    constexpr double pi = 3.14159265358979323846;

    std::vector<float> input(
        static_cast<std::size_t>(frames) * 2);

    for (std::uint32_t frame = 0;
         frame < frames;
         ++frame) {
        const float value =
            static_cast<float>(
                0.25 *
                std::sin(
                    2.0 * pi * tone_hz *
                    static_cast<double>(frame) /
                    44'100.0));
        input[
            static_cast<std::size_t>(frame) * 2] =
            value;
        input[
            static_cast<std::size_t>(frame) * 2 + 1] =
            value;
    }

    std::vector<float> output(
        50'000 * 2,
        0.0F);

    const auto result =
        resampler.process(
            input,
            frames,
            output,
            0.0);

    test.expect(
        result.ok() &&
        result.output_frames_produced > 47'000 &&
        result.output_frames_produced < 49'000,
        "fixed phase lattice preserves plausible one-second output count");

    const std::size_t produced =
        result.output_frames_produced;
    std::uint64_t crossings = 0;
    for (std::size_t frame = 1;
         frame < produced;
         ++frame) {
        const float previous =
            output[(frame - 1) * 2];
        const float current =
            output[frame * 2];
        if (previous <= 0.0F &&
            current > 0.0F)
            ++crossings;
    }

    test.expect(
        crossings >= 995 &&
        crossings <= 1'005,
        "fixed lattice preserves 1 kHz tone pitch");
}

void test_output_pressure(Test &test)
{
    LibSwResampleAudioResampler resampler;
    test.expect(
        resampler.configure(config_44k1()) ==
            AudioResampleStatus::Ok,
        "pressure fixture config succeeds");

    std::vector<float> input(
        4'410 * 2,
        0.0F);
    std::vector<float> too_small(
        32 * 2,
        0.0F);

    const auto result =
        resampler.process(
            input,
            4'410,
            too_small,
            0.0);

    test.expect(
        result.status ==
            AudioResampleStatus::OutputFull,
        "insufficient output capacity fails before conversion");
    test.expect(
        result.input_frames_consumed == 0 &&
        result.output_frames_produced == 0,
        "output pressure consumes no source frames");
}

void test_drain_reset(Test &test)
{
    LibSwResampleAudioResampler resampler;
    test.expect(
        resampler.configure(config_44k1()) ==
            AudioResampleStatus::Ok,
        "drain fixture config succeeds");

    std::vector<float> input(
        44'100 * 2,
        0.0F);
    std::vector<float> output(
        50'000 * 2,
        0.0F);

    const auto result =
        resampler.process(
            input,
            44'100,
            output,
            -100.0);

    test.expect(
        result.ok(),
        "negative ppm conversion succeeds");
    test.expect(
        resampler.maximum_drain_frames() ==
            8'192,
        "drain capacity is explicitly bounded");

    std::vector<float> drain(
        8'192 * 2,
        0.0F);

    bool complete = false;
    for (int iteration = 0;
         iteration < 16 && !complete;
         ++iteration) {
        const auto drained =
            resampler.drain(drain);
        test.expect(
            drained.ok(),
            "drain call succeeds");
        complete = drained.complete;
    }

    test.expect(
        complete,
        "bounded drain reaches completion");

    const auto again =
        resampler.drain(drain);

    test.expect(
        again.ok() &&
        again.complete &&
        again.output_frames_produced == 0,
        "drain completion is idempotent");

    resampler.reset();

    test.expect(
        resampler.current_delay_100ns() == 0,
        "reset clears retained delay");
}

void test_invalid_config(Test &test)
{
    LibSwResampleAudioResampler resampler;

    AudioResamplerConfig invalid{};
    test.expect(
        resampler.configure(invalid) ==
            AudioResampleStatus::InvalidArgument,
        "invalid config fails closed");
}

} // namespace

int main()
{
    Test test;
    test_config_and_rate_state(test);
    test_tone_quality(test);
    test_output_pressure(test);
    test_drain_reset(test);
    test_invalid_config(test);

    if (test.failures != 0)
        return 1;

    std::cout
        << "PASS: " << test.checks
        << " libswresample adapter checks\n";
    return 0;
}
