#pragma once

#include "core/audio/audio_time.hpp"

#include <cstdint>
#include <optional>

namespace arssyut::core::audio {

struct DriftAnchor {
    std::uint64_t device_frame_position = 0;
    std::int64_t qpc_100ns = 0;
    bool trusted = false;
};

struct DriftEstimate {
    double rate_error_ppm = 0.0;
    std::uint64_t device_frame_delta = 0;
    std::uint64_t qpc_delta_100ns = 0;
    bool valid = false;
};

[[nodiscard]] inline DriftEstimate estimate_rate_error(
    const DriftAnchor &first,
    const DriftAnchor &second,
    std::uint32_t nominal_sample_rate) noexcept
{
    DriftEstimate result{};

    if (!first.trusted || !second.trusted ||
        nominal_sample_rate == 0 ||
        second.device_frame_position <= first.device_frame_position ||
        second.qpc_100ns <= first.qpc_100ns)
        return result;

    const std::uint64_t frame_delta =
        second.device_frame_position - first.device_frame_position;
    const std::uint64_t qpc_delta =
        static_cast<std::uint64_t>(
            second.qpc_100ns - first.qpc_100ns);

    const long double expected_frames =
        static_cast<long double>(qpc_delta) *
        static_cast<long double>(nominal_sample_rate) /
        static_cast<long double>(kMediaTicksPerSecond);

    if (expected_frames <= 0.0L)
        return result;

    const long double error =
        (static_cast<long double>(frame_delta) - expected_frames) /
        expected_frames;

    result.rate_error_ppm =
        static_cast<double>(error * 1'000'000.0L);
    result.device_frame_delta = frame_delta;
    result.qpc_delta_100ns = qpc_delta;
    result.valid = true;
    return result;
}

/*
 * Fixed-state estimator contract. It only measures trusted source clock error;
 * it does not choose smoothing windows, ppm clamps or a resampler authority.
 * Those policy decisions belong to P7A5 after the P7A1R benchmark.
 */
class DriftEstimator final {
public:
    explicit DriftEstimator(
        std::uint32_t nominal_sample_rate,
        std::uint64_t minimum_frame_span = 1) noexcept
        : nominal_sample_rate_(nominal_sample_rate),
          minimum_frame_span_(minimum_frame_span)
    {
    }

    void reset() noexcept
    {
        anchor_.reset();
        last_ = {};
        accepted_observations_ = 0;
        rejected_observations_ = 0;
    }

    [[nodiscard]] DriftEstimate observe(
        DriftAnchor observation) noexcept
    {
        if (!observation.trusted || nominal_sample_rate_ == 0) {
            ++rejected_observations_;
            return {};
        }

        if (!anchor_.has_value()) {
            anchor_ = observation;
            ++accepted_observations_;
            return {};
        }

        if (observation.device_frame_position <=
                anchor_->device_frame_position ||
            observation.qpc_100ns <= anchor_->qpc_100ns) {
            ++rejected_observations_;
            return {};
        }

        const std::uint64_t frame_span =
            observation.device_frame_position -
            anchor_->device_frame_position;
        if (frame_span < minimum_frame_span_) {
            ++accepted_observations_;
            return last_;
        }

        last_ = estimate_rate_error(
            *anchor_,
            observation,
            nominal_sample_rate_);
        anchor_ = observation;
        ++accepted_observations_;
        return last_;
    }

    [[nodiscard]] DriftEstimate last() const noexcept
    {
        return last_;
    }

    [[nodiscard]] std::uint64_t accepted_observations() const noexcept
    {
        return accepted_observations_;
    }

    [[nodiscard]] std::uint64_t rejected_observations() const noexcept
    {
        return rejected_observations_;
    }

private:
    std::uint32_t nominal_sample_rate_ = 0;
    std::uint64_t minimum_frame_span_ = 1;
    std::optional<DriftAnchor> anchor_;
    DriftEstimate last_{};
    std::uint64_t accepted_observations_ = 0;
    std::uint64_t rejected_observations_ = 0;
};

} // namespace arssyut::core::audio
