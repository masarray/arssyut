#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace arssyut::core {

enum class DiagnosticMetric : std::uint8_t {
    CaptureFramesReceived = 0,
    CaptureFramesReplaced,
    CaptureFramesDroppedBusy,
    CaptureSourceResizes,
    CaptureSourceClosed,
    CaptureCallbackFailures,
    VideoFramesRendered,
    VideoFramesReused,
    VideoFramesUnavailable,
    VideoFramesSkipped,
    EncoderFramesSubmitted,
    EncoderFramesBackpressured,
    EncoderWriteFailures,
    InputEventsDropped,
    AudioUnderflows,
    AudioOverflows,
    GraphicsDeviceResets,
    MaxVideoQueueDepth,
    MaxAudioQueueDepth,
    Count,
};

class Diagnostics {
public:
    static constexpr std::size_t metric_count =
        static_cast<std::size_t>(DiagnosticMetric::Count);

    void increment(
        DiagnosticMetric metric,
        std::uint64_t amount = 1) noexcept
    {
        counters_[index(metric)].fetch_add(amount, std::memory_order_relaxed);
    }

    [[nodiscard]] std::uint64_t load(
        DiagnosticMetric metric) const noexcept
    {
        return counters_[index(metric)].load(std::memory_order_relaxed);
    }

    void observe_max(
        DiagnosticMetric metric,
        std::uint64_t value) noexcept
    {
        auto &counter = counters_[index(metric)];
        std::uint64_t current = counter.load(std::memory_order_relaxed);

        for (int attempt = 0;
             attempt < 8 && current < value;
             ++attempt) {
            if (counter.compare_exchange_weak(
                    current,
                    value,
                    std::memory_order_relaxed,
                    std::memory_order_relaxed)) {
                break;
            }
        }
    }

    [[nodiscard]] std::array<std::uint64_t, metric_count>
    snapshot() const noexcept
    {
        std::array<std::uint64_t, metric_count> values{};
        for (std::size_t i = 0; i < metric_count; ++i)
            values[i] = counters_[i].load(std::memory_order_relaxed);
        return values;
    }

private:
    [[nodiscard]] static constexpr std::size_t index(
        DiagnosticMetric metric) noexcept
    {
        return static_cast<std::size_t>(metric);
    }

    std::array<std::atomic<std::uint64_t>, metric_count> counters_{};
};

} // namespace arssyut::core
