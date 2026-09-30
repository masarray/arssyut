#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace arssyut::core {

/*
 * Allocation-free latency histogram for realtime instrumentation.
 *
 * Values are microseconds. Buckets intentionally use practical capture/render
 * latency boundaries instead of fine-grained samples that would need a growing
 * history. Quantiles are conservative upper bounds.
 */
class LatencyHistogram {
public:
    static constexpr std::array<std::uint32_t, 12> upper_bounds_us = {
        25, 50, 100, 150, 250, 500,
        1'000, 2'000, 4'000, 8'000, 16'000,
        std::numeric_limits<std::uint32_t>::max()
    };

    struct Snapshot {
        std::array<std::uint64_t, upper_bounds_us.size()> buckets{};
        std::uint64_t total = 0;

        [[nodiscard]] std::uint32_t quantile_upper_bound(
            std::uint32_t numerator,
            std::uint32_t denominator) const noexcept
        {
            if (total == 0 || denominator == 0 || numerator == 0)
                return 0;

            const std::uint64_t wanted =
                (total * numerator + denominator - 1) / denominator;

            std::uint64_t accumulated = 0;
            for (std::size_t i = 0; i < buckets.size(); ++i) {
                accumulated += buckets[i];
                if (accumulated >= wanted)
                    return upper_bounds_us[i];
            }
            return upper_bounds_us.back();
        }
    };

    void observe(std::uint32_t microseconds) noexcept
    {
        for (std::size_t i = 0; i < upper_bounds_us.size(); ++i) {
            if (microseconds <= upper_bounds_us[i]) {
                buckets_[i].fetch_add(1, std::memory_order_relaxed);
                total_.fetch_add(1, std::memory_order_relaxed);
                return;
            }
        }
    }

    [[nodiscard]] Snapshot snapshot() const noexcept
    {
        Snapshot result;
        result.total = total_.load(std::memory_order_relaxed);
        for (std::size_t i = 0; i < upper_bounds_us.size(); ++i)
            result.buckets[i] =
                buckets_[i].load(std::memory_order_relaxed);
        return result;
    }

private:
    std::array<
        std::atomic<std::uint64_t>,
        upper_bounds_us.size()> buckets_{};
    std::atomic<std::uint64_t> total_{0};
};

} // namespace arssyut::core
