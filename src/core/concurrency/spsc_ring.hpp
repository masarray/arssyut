#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <type_traits>
#include <utility>

namespace arssyut::core {

/*
 * Fixed-capacity single-producer/single-consumer ring.
 *
 * The producer owns head_, the consumer owns tail_. One sentinel slot keeps
 * full/empty detection unambiguous. No allocation, blocking lock, or capacity
 * growth is possible after construction.
 */
template <typename T, std::size_t Capacity>
class SpscRing {
    static_assert(Capacity >= 2, "SpscRing capacity must be at least two");
    static_assert(std::is_default_constructible_v<T>,
                  "SpscRing requires a default-constructible value type");

public:
    static constexpr std::size_t capacity() noexcept
    {
        return Capacity;
    }

    SpscRing() = default;
    SpscRing(const SpscRing &) = delete;
    SpscRing &operator=(const SpscRing &) = delete;

    [[nodiscard]] bool try_push(const T &value) noexcept(
        std::is_nothrow_copy_assignable_v<T>)
    {
        return try_push_impl(value);
    }

    [[nodiscard]] bool try_push(T &&value) noexcept(
        std::is_nothrow_move_assignable_v<T>)
    {
        return try_push_impl(std::move(value));
    }

    [[nodiscard]] bool try_pop(T &out) noexcept(
        std::is_nothrow_move_assignable_v<T> &&
        std::is_nothrow_default_constructible_v<T>)
    {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        const std::size_t head = head_.load(std::memory_order_acquire);
        if (tail == head)
            return false;

        out = std::move(slots_[tail]);
        slots_[tail] = T{};
        tail_.store(increment(tail), std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return tail_.load(std::memory_order_acquire) ==
               head_.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::size_t size_approx() const noexcept
    {
        const std::size_t head = head_.load(std::memory_order_acquire);
        const std::size_t tail = tail_.load(std::memory_order_acquire);
        return head >= tail
            ? head - tail
            : storage_size - (tail - head);
    }

private:
    static constexpr std::size_t storage_size = Capacity + 1;

    [[nodiscard]] static constexpr std::size_t increment(
        std::size_t index) noexcept
    {
        ++index;
        return index == storage_size ? 0 : index;
    }

    template <typename U>
    [[nodiscard]] bool try_push_impl(U &&value) noexcept(
        std::is_nothrow_assignable_v<T &, U &&>)
    {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        const std::size_t next = increment(head);
        if (next == tail_.load(std::memory_order_acquire))
            return false;

        slots_[head] = std::forward<U>(value);
        head_.store(next, std::memory_order_release);
        return true;
    }

    std::array<T, storage_size> slots_{};
    std::atomic<std::size_t> head_{0};
    std::atomic<std::size_t> tail_{0};
};

} // namespace arssyut::core
