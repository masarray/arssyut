#pragma once

#include "core/result/status.hpp"
#include "core/time/monotonic_clock.hpp"

#include <cstdint>
#include <limits>

namespace arssyut::core {

struct FrameRate {
    std::uint32_t numerator = 60;
    std::uint32_t denominator = 1;

    [[nodiscard]] constexpr bool valid() const noexcept
    {
        return numerator > 0 && denominator > 0;
    }
};

struct ScheduledFrame {
    bool emit = false;
    std::uint64_t frame_index = 0;
    TimePoint pts{};
    std::uint64_t skipped_intervals = 0;
};

/*
 * Canonical CFR scheduler.
 *
 * It never loops to "catch up" missed output intervals. If the consumer wakes
 * late, the scheduler coalesces obsolete deadlines into one latest due slot and
 * reports how many intervals were skipped. This preserves bounded work and
 * prevents latency growth.
 */
class FrameScheduler {
public:
    [[nodiscard]] Status reset(
        TimePoint start,
        FrameRate rate) noexcept
    {
        if (!rate.valid())
            return Status::failure(StatusCode::InvalidArgument);

        const std::uint64_t ticks_numerator =
            static_cast<std::uint64_t>(MonotonicClock::ticks_per_second) *
            static_cast<std::uint64_t>(rate.denominator);

        if (ticks_numerator <
            static_cast<std::uint64_t>(rate.denominator)) {
            return Status::failure(StatusCode::InvalidArgument);
        }

        start_ = start;
        rate_ = rate;
        next_index_ = 0;
        initialized_ = true;
        return Status::success();
    }

    [[nodiscard]] ScheduledFrame poll(TimePoint now) noexcept
    {
        if (!initialized_ || now < start_)
            return {};

        const std::uint64_t elapsed =
            static_cast<std::uint64_t>(
                MonotonicClock::duration_ticks(start_, now));

        const std::uint64_t period_numerator =
            static_cast<std::uint64_t>(MonotonicClock::ticks_per_second) *
            static_cast<std::uint64_t>(rate_.denominator);

        if (elapsed >
            std::numeric_limits<std::uint64_t>::max() /
                static_cast<std::uint64_t>(rate_.numerator)) {
            return {};
        }

        const std::uint64_t latest_due =
            (elapsed * static_cast<std::uint64_t>(rate_.numerator)) /
            period_numerator;

        if (latest_due < next_index_)
            return {};

        const std::uint64_t skipped = latest_due - next_index_;
        next_index_ = latest_due + 1;

        return {
            true,
            latest_due,
            pts_for_index(latest_due),
            skipped,
        };
    }

    [[nodiscard]] TimePoint pts_for_index(
        std::uint64_t frame_index) const noexcept
    {
        if (!initialized_)
            return {};

        const std::uint64_t period_numerator =
            static_cast<std::uint64_t>(MonotonicClock::ticks_per_second) *
            static_cast<std::uint64_t>(rate_.denominator);

        const std::uint64_t whole =
            frame_index / static_cast<std::uint64_t>(rate_.numerator);
        const std::uint64_t remainder =
            frame_index % static_cast<std::uint64_t>(rate_.numerator);

        const std::uint64_t ticks =
            whole * period_numerator +
            (remainder * period_numerator) /
                static_cast<std::uint64_t>(rate_.numerator);

        return {
            start_.ticks_100ns +
            static_cast<std::int64_t>(ticks)
        };
    }

    [[nodiscard]] std::uint64_t next_index() const noexcept
    {
        return next_index_;
    }

private:
    TimePoint start_{};
    FrameRate rate_{};
    std::uint64_t next_index_ = 0;
    bool initialized_ = false;
};

} // namespace arssyut::core
