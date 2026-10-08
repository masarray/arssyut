#include "platform/windows/audio/libswresample_audio_resampler.hpp"

#include "core/audio/audio_format.hpp"

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
    const double nominal =
        48'000.0 / 44'100.0;
    constexpr double distance = 4'800'000.0;
    constexpr double delta = 480.0;
    const double expected_multiplier =
        distance / (distance - delta);

    test.expect(
        std::abs(
            adjusted.effective_output_per_input_ratio -
            nominal * expected_multiplier) <
            1.0e-12,
        "effective ratio reports exact compensation multiplier");
    test.expect(
        adjusted.phase_remainder_denominator ==
            48'000 &&
        adjusted.phase_remainder_numerator <
            adjusted.phase_remainder_denominator,
        "phase is exposed in the backend common timestamp lattice");
    test.expect(
        adjusted.observable(),
        "fractional phase remains observable after processing");
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
