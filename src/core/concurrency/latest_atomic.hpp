#pragma once

#include <atomic>
#include <cstdint>
#include <type_traits>

namespace arssyut::core {

/*
 * Bounded latest-wins mailbox for small lock-free atomic values.
 *
 * It is intentionally not a generic object mailbox. Larger/non-trivial
 * realtime resources (such as D3D frame handles) get a dedicated bounded slot
 * with explicit lifetime ownership in the subsystem that understands them.
 */
template <typename T>
class LatestAtomic {
    static_assert(std::is_trivially_copyable_v<T>,
                  "LatestAtomic requires trivially-copyable values");
    static_assert(std::atomic<T>::is_always_lock_free,
                  "LatestAtomic requires a lock-free atomic value type");

public:
    explicit constexpr LatestAtomic(T initial = {}) noexcept
        : value_(initial)
    {
    }

    void publish(T value) noexcept
    {
        value_.store(value, std::memory_order_relaxed);
        sequence_.fetch_add(1, std::memory_order_release);
    }

    /*
     * Returns true only when a stable value newer than seen_sequence was read.
     * The retry budget is bounded so a pathological producer cannot spin-lock
     * the consumer.
     */
    [[nodiscard]] bool read_if_new(
        std::uint64_t &seen_sequence,
        T &out) const noexcept
    {
        constexpr int retry_budget = 4;

        for (int attempt = 0; attempt < retry_budget; ++attempt) {
            const std::uint64_t before =
                sequence_.load(std::memory_order_acquire);
            if (before == seen_sequence)
                return false;

            const T candidate = value_.load(std::memory_order_acquire);
            const std::uint64_t after =
                sequence_.load(std::memory_order_acquire);

            if (before == after) {
                out = candidate;
                seen_sequence = after;
                return true;
            }
        }

        return false;
    }

    [[nodiscard]] std::uint64_t sequence() const noexcept
    {
        return sequence_.load(std::memory_order_acquire);
    }

private:
    std::atomic<T> value_{};
    std::atomic<std::uint64_t> sequence_{0};
};

} // namespace arssyut::core
