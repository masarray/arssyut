#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>

namespace arssyut::app {

// Single owner: RecorderSession's existing A/V worker.
// Both scheduler and WASAPI source ingestion consume the SAME virtual QPC.
// No heap, OS timer, wall-clock conversion, background thread or reset of
// the Media Foundation writer is involved in pausing a recording.
class RecordingPauseClock final {
public:
    void reset(std::int64_t start_qpc) noexcept {
        start_ = start_qpc;
        pause_start_ = 0;
        completed_ = 0;
        pause_count_ = 0;
        paused_ = false;
    }
    [[nodiscard]] bool pause(std::int64_t now) noexcept {
        if (paused_ || now < start_) return false;
        paused_ = true;
        pause_start_ = now;
        return true;
    }
    [[nodiscard]] bool resume(std::int64_t now) noexcept {
        if (!paused_ || now < pause_start_) return false;
        const auto delta = now - pause_start_;
        // QPC is monotonic; reject unrepresentable durations rather than
        // publishing a wrapped timebase that can corrupt A/V PTS.
        if (delta > std::numeric_limits<std::int64_t>::max() - completed_)
            return false;
        completed_ += delta;
        ++pause_count_;
        paused_ = false;
        pause_start_ = 0;
        return true;
    }
    [[nodiscard]] std::int64_t media_now(std::int64_t now) const noexcept {
        const auto frozen = paused_ ? pause_start_ : now;
        return (std::max)(start_, frozen - completed_);
    }
    [[nodiscard]] std::int64_t elapsed(std::int64_t now) const noexcept {
        return media_now(now) - start_;
    }
    [[nodiscard]] std::int64_t excluded_ticks(std::int64_t now) const noexcept {
        return completed_ + (paused_ ? (std::max)<std::int64_t>(0,now-pause_start_) : 0);
    }
    [[nodiscard]] std::int64_t committed_excluded_ticks() const noexcept {
        return completed_;
    }
    [[nodiscard]] bool paused() const noexcept { return paused_; }
    [[nodiscard]] std::uint64_t pause_count() const noexcept { return pause_count_; }
private:
    std::int64_t start_ = 0;
    std::int64_t pause_start_ = 0;
    std::int64_t completed_ = 0;
    std::uint64_t pause_count_ = 0;
    bool paused_ = false;
};
} // namespace arssyut::app
