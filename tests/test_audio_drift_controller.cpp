#include "core/audio/audio_drift_controller.hpp"

#include <cmath>
#include <iostream>

namespace {

using namespace arssyut::core::audio;

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

void test_sign_and_slew(Test &test)
{
    AudioDriftController controller({
        .maximum_absolute_correction_ppm = 500.0,
        .smoothing_time_constant_seconds = 2.0,
        .maximum_slew_ppm_per_second = 50.0,
        .deadband_ppm = 0.25,
    });

    DriftEstimate fast{
        .rate_error_ppm = +100.0,
        .valid = true,
    };

    const double first =
        controller.update(
            fast,
            1'000'000); // 100 ms

    test.expect(
        first < 0.0,
        "fast source clock requests negative resampler correction");
    test.expect(
        std::abs(first) <= 5.000001,
        "100 ms update cannot slew faster than 5 ppm");

    for (int i = 0; i < 400; ++i)
        (void)controller.update(
            fast,
            1'000'000);

    const auto settled =
        controller.snapshot();

    test.expect(
        settled.requested_resampler_ppm < -95.0 &&
        settled.requested_resampler_ppm > -105.0,
        "stable +100 ppm source converges near -100 ppm correction");
}

void test_untrusted_and_discontinuity(Test &test)
{
    AudioDriftController controller;

    DriftEstimate invalid{
        .rate_error_ppm = 400.0,
        .valid = false,
    };

    const double before =
        controller.update(
            invalid,
            1'000'000);

    test.expect(
        before == 0.0,
        "invalid timing cannot move correction");

    DriftEstimate valid{
        .rate_error_ppm = -200.0,
        .valid = true,
    };
    (void)controller.update(
        valid,
        10'000'000);

    test.expect(
        controller.snapshot().
            requested_resampler_ppm > 0.0,
        "slow source requests positive correction");

    controller.reset_for_discontinuity();

    const auto reset =
        controller.snapshot();

    test.expect(
        reset.requested_resampler_ppm == 0.0 &&
        reset.discontinuity_resets == 1,
        "discontinuity clears correction and smoothing state");
}

void test_clamp_deadband(Test &test)
{
    AudioDriftController controller({
        .maximum_absolute_correction_ppm = 300.0,
        .smoothing_time_constant_seconds = 1.0,
        .maximum_slew_ppm_per_second = 1'000.0,
        .deadband_ppm = 0.5,
    });

    DriftEstimate huge{
        .rate_error_ppm = 5'000.0,
        .valid = true,
    };

    (void)controller.update(
        huge,
        10'000'000);

    const auto clamped =
        controller.snapshot();

    test.expect(
        clamped.target_correction_ppm == -300.0 &&
        clamped.clamped_updates == 1,
        "pathological drift is hard-clamped");

    controller.reset();

    DriftEstimate tiny{
        .rate_error_ppm = 0.2,
        .valid = true,
    };

    (void)controller.update(
        tiny,
        10'000'000);

    test.expect(
        controller.snapshot().
            target_correction_ppm == 0.0,
        "sub-deadband clock noise requests no correction");
}

} // namespace

int main()
{
    Test test;
    test_sign_and_slew(test);
    test_untrusted_and_discontinuity(test);
    test_clamp_deadband(test);

    if (test.failures != 0)
        return 1;

    std::cout
        << "PASS: " << test.checks
        << " drift controller checks\n";
    return 0;
}
