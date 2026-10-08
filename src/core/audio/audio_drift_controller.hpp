#pragma once

#include "core/audio/audio_drift.hpp"
#include "core/audio/audio_time.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace arssyut::core::audio {

struct AudioDriftControllerConfig {
    double maximum_absolute_correction_ppm = 500.0;
    double smoothing_time_constant_seconds = 2.0;
    double maximum_slew_ppm_per_second = 50.0;
    double deadband_ppm = 0.25;

    [[nodiscard]] bool valid() const noexcept
    {
        return std::isfinite(maximum_absolute_correction_ppm) &&
               maximum_absolute_correction_ppm > 0.0 &&
               std::isfinite(smoothing_time_constant_seconds) &&
               smoothing_time_constant_seconds > 0.0 &&
               std::isfinite(maximum_slew_ppm_per_second) &&
               maximum_slew_ppm_per_second > 0.0 &&
               std::isfinite(deadband_ppm) &&
               deadband_ppm >= 0.0 &&
               deadband_ppm <
                   maximum_absolute_correction_ppm;
    }
};

struct AudioDriftControllerSnapshot {
    double measured_source_error_ppm = 0.0;
    double target_correction_ppm = 0.0;
    double smoothed_correction_ppm = 0.0;
    double requested_resampler_ppm = 0.0;
    std::uint64_t accepted_updates = 0;
    std::uint64_t rejected_updates = 0;
    std::uint64_t clamped_updates = 0;
    std::uint64_t discontinuity_resets = 0;
};

/*
 * Fixed-state P7A5 drift policy.
 *
 * DriftEstimator measures the source device clock relative to the canonical
 * RecorderSession/QPC clock. A positive source error means the device produces
 * frames too quickly; the resampler therefore needs the opposite sign so those
 * extra source frames map into the fixed 48 kHz program timeline.
 *
 * Invalid/degraded timing never changes correction. Discontinuity explicitly
 * reanchors by returning correction to zero; no previous estimator state leaks
 * across a source clock reset.
 */
class AudioDriftController final {
public:
    explicit AudioDriftController(
        AudioDriftControllerConfig config = {}) noexcept
        : config_(
              config.valid()
                  ? config
                  : AudioDriftControllerConfig{})
    {
    }

    void reset() noexcept
    {
        snapshot_ = {};
        has_smoothed_value_ = false;
    }

    void reset_for_discontinuity() noexcept
    {
        const auto count =
            snapshot_.discontinuity_resets + 1;
        snapshot_ = {};
        snapshot_.discontinuity_resets =
            count;
        has_smoothed_value_ = false;
    }

    [[nodiscard]] double update(
        DriftEstimate estimate,
        std::uint64_t elapsed_100ns) noexcept
    {
        if (!estimate.valid ||
            !std::isfinite(
                estimate.rate_error_ppm) ||
            elapsed_100ns == 0) {
            ++snapshot_.rejected_updates;
            return snapshot_.
                requested_resampler_ppm;
        }

        ++snapshot_.accepted_updates;
        snapshot_.measured_source_error_ppm =
            estimate.rate_error_ppm;

        double target =
            -estimate.rate_error_ppm;

        const auto unclamped = target;
        target =
            std::clamp(
                target,
                -config_.
                    maximum_absolute_correction_ppm,
                config_.
                    maximum_absolute_correction_ppm);

        if (target != unclamped)
            ++snapshot_.clamped_updates;

        if (std::abs(target) <=
            config_.deadband_ppm)
            target = 0.0;

        snapshot_.target_correction_ppm =
            target;

        const double seconds =
            static_cast<double>(
                elapsed_100ns) /
            static_cast<double>(
                kMediaTicksPerSecond);

        if (!has_smoothed_value_) {
            snapshot_.smoothed_correction_ppm =
                target;
            has_smoothed_value_ = true;
        }
        else {
            const double alpha =
                1.0 -
                std::exp(
                    -seconds /
                    config_.
                        smoothing_time_constant_seconds);

            snapshot_.smoothed_correction_ppm +=
                alpha *
                (target -
                 snapshot_.
                     smoothed_correction_ppm);
        }

        const double maximum_step =
            config_.
                maximum_slew_ppm_per_second *
            seconds;

        const double requested_delta =
            snapshot_.smoothed_correction_ppm -
            snapshot_.requested_resampler_ppm;

        snapshot_.requested_resampler_ppm +=
            std::clamp(
                requested_delta,
                -maximum_step,
                maximum_step);

        snapshot_.requested_resampler_ppm =
            std::clamp(
                snapshot_.requested_resampler_ppm,
                -config_.
                    maximum_absolute_correction_ppm,
                config_.
                    maximum_absolute_correction_ppm);

        return snapshot_.
            requested_resampler_ppm;
    }

    [[nodiscard]] AudioDriftControllerSnapshot
    snapshot() const noexcept
    {
        return snapshot_;
    }

private:
    AudioDriftControllerConfig config_{};
    AudioDriftControllerSnapshot snapshot_{};
    bool has_smoothed_value_ = false;
};

} // namespace arssyut::core::audio
