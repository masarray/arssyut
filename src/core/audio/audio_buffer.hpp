#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>

namespace arssyut::core::audio {

/*
 * Runtime-sized but immutable-capacity SPSC queue.
 *
 * Capacity is resolved during recorder preparation, storage is allocated once,
 * and try_push/try_pop perform no allocation or growth. Audio media is never
 * latest-wins: full means the producer must report overflow/discontinuity.
 */
template <typename T>
class BoundedSpscQueue final {
    static_assert(std::is_default_constructible_v<T>);

public:
    explicit BoundedSpscQueue(std::size_t capacity)
        : capacity_(capacity),
          storage_size_(
              capacity == std::numeric_limits<std::size_t>::max()
                  ? 0
                  : capacity + 1),
          slots_(
              storage_size_ >= 2
                  ? std::make_unique<T[]>(storage_size_)
                  : nullptr)
    {
    }

    BoundedSpscQueue(const BoundedSpscQueue &) = delete;
    BoundedSpscQueue &operator=(const BoundedSpscQueue &) = delete;

    [[nodiscard]] bool valid() const noexcept
    {
        return capacity_ != 0 && slots_ != nullptr;
    }

    [[nodiscard]] std::size_t capacity() const noexcept
    {
        return valid() ? capacity_ : 0;
    }

    [[nodiscard]] std::size_t high_water() const noexcept
    {
        return high_water_.load(std::memory_order_relaxed);
    }

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
        if (!valid())
            return false;

        const std::size_t tail =
            tail_.load(std::memory_order_relaxed);
        const std::size_t head =
            head_.load(std::memory_order_acquire);
        if (tail == head)
            return false;

        out = std::move(slots_[tail]);
        slots_[tail] = T{};
        tail_.store(increment(tail), std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool empty() const noexcept
    {
        if (!valid())
            return true;

        return tail_.load(std::memory_order_acquire) ==
               head_.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::size_t size_approx() const noexcept
    {
        if (!valid())
            return 0;

        const std::size_t head =
            head_.load(std::memory_order_acquire);
        const std::size_t tail =
            tail_.load(std::memory_order_acquire);

        return head >= tail
            ? head - tail
            : storage_size_ - (tail - head);
    }

private:
    [[nodiscard]] std::size_t increment(std::size_t index) const noexcept
    {
        ++index;
        return index == storage_size_ ? 0 : index;
    }

    void observe_high_water(std::size_t value) noexcept
    {
        auto seen = high_water_.load(std::memory_order_relaxed);
        while (seen < value &&
               !high_water_.compare_exchange_weak(
                   seen,
                   value,
                   std::memory_order_relaxed,
                   std::memory_order_relaxed)) {
        }
    }

    template <typename U>
    [[nodiscard]] bool try_push_impl(U &&value) noexcept(
        std::is_nothrow_assignable_v<T &, U &&>)
    {
        if (!valid())
            return false;

        const std::size_t head =
            head_.load(std::memory_order_relaxed);
        const std::size_t next = increment(head);
        const std::size_t tail =
            tail_.load(std::memory_order_acquire);
        if (next == tail)
            return false;

        slots_[head] = std::forward<U>(value);
        head_.store(next, std::memory_order_release);

        const std::size_t depth = next >= tail
            ? next - tail
            : storage_size_ - (tail - next);
        observe_high_water(depth);
        return true;
    }

    std::size_t capacity_ = 0;
    std::size_t storage_size_ = 0;
    std::unique_ptr<T[]> slots_;
    std::atomic<std::size_t> head_{0};
    std::atomic<std::size_t> tail_{0};
    std::atomic<std::size_t> high_water_{0};
};

class FixedAudioPacketPool final {
public:
    struct Lease {
        std::uint32_t slot = std::numeric_limits<std::uint32_t>::max();
        std::span<std::byte> bytes{};

        [[nodiscard]] bool valid() const noexcept
        {
            return slot != std::numeric_limits<std::uint32_t>::max() &&
                   !bytes.empty();
        }
    };

    FixedAudioPacketPool(
        std::size_t capacity,
        std::size_t bytes_per_slot)
        : capacity_(capacity),
          bytes_per_slot_(bytes_per_slot)
    {
        if (capacity_ == 0 || bytes_per_slot_ == 0 ||
            capacity_ >
                std::numeric_limits<std::size_t>::max() / bytes_per_slot_ ||
            capacity_ >
                static_cast<std::size_t>(
                    std::numeric_limits<std::uint32_t>::max())) {
            capacity_ = 0;
            bytes_per_slot_ = 0;
            return;
        }

        storage_ = std::make_unique<std::byte[]>(
            capacity_ * bytes_per_slot_);
        states_ = std::make_unique<std::atomic<std::uint8_t>[]>(
            capacity_);
        for (std::size_t index = 0; index < capacity_; ++index)
            states_[index].store(0, std::memory_order_relaxed);
    }

    FixedAudioPacketPool(const FixedAudioPacketPool &) = delete;
    FixedAudioPacketPool &operator=(const FixedAudioPacketPool &) = delete;

    [[nodiscard]] bool valid() const noexcept
    {
        return capacity_ != 0 &&
               bytes_per_slot_ != 0 &&
               storage_ != nullptr &&
               states_ != nullptr;
    }

    [[nodiscard]] std::size_t capacity() const noexcept
    {
        return valid() ? capacity_ : 0;
    }

    [[nodiscard]] std::size_t bytes_per_slot() const noexcept
    {
        return valid() ? bytes_per_slot_ : 0;
    }

    [[nodiscard]] std::size_t retained_bytes() const noexcept
    {
        return valid() ? capacity_ * bytes_per_slot_ : 0;
    }

    [[nodiscard]] std::size_t in_use() const noexcept
    {
        return in_use_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] std::size_t high_water() const noexcept
    {
        return high_water_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] std::optional<Lease> try_acquire(
        std::size_t required_bytes) noexcept
    {
        if (!valid() || required_bytes == 0 ||
            required_bytes > bytes_per_slot_)
            return std::nullopt;

        for (std::size_t offset = 0; offset < capacity_; ++offset) {
            const std::size_t index =
                (next_probe_ + offset) % capacity_;
            std::uint8_t expected = 0;
            if (!states_[index].compare_exchange_strong(
                    expected,
                    1,
                    std::memory_order_acquire,
                    std::memory_order_relaxed))
                continue;

            next_probe_ = (index + 1) % capacity_;
            const std::size_t current =
                in_use_.fetch_add(1, std::memory_order_relaxed) + 1;
            observe_high_water(current);

            return Lease{
                .slot = static_cast<std::uint32_t>(index),
                .bytes = {
                    storage_.get() + index * bytes_per_slot_,
                    required_bytes,
                },
            };
        }

        return std::nullopt;
    }

    [[nodiscard]] std::span<const std::byte> readable(
        std::uint32_t slot,
        std::size_t used_bytes) const noexcept
    {
        if (!valid() ||
            slot >= capacity_ ||
            used_bytes > bytes_per_slot_ ||
            states_[slot].load(std::memory_order_acquire) == 0)
            return {};

        return {
            storage_.get() +
                static_cast<std::size_t>(slot) * bytes_per_slot_,
            used_bytes,
        };
    }

    [[nodiscard]] bool release(std::uint32_t slot) noexcept
    {
        if (!valid() || slot >= capacity_)
            return false;

        const std::uint8_t previous =
            states_[slot].exchange(0, std::memory_order_release);
        if (previous == 0)
            return false;

        in_use_.fetch_sub(1, std::memory_order_relaxed);
        return true;
    }

private:
    void observe_high_water(std::size_t value) noexcept
    {
        auto seen = high_water_.load(std::memory_order_relaxed);
        while (seen < value &&
               !high_water_.compare_exchange_weak(
                   seen,
                   value,
                   std::memory_order_relaxed,
                   std::memory_order_relaxed)) {
        }
    }

    std::size_t capacity_ = 0;
    std::size_t bytes_per_slot_ = 0;
    std::unique_ptr<std::byte[]> storage_;
    std::unique_ptr<std::atomic<std::uint8_t>[]> states_;
    std::atomic<std::size_t> in_use_{0};
    std::atomic<std::size_t> high_water_{0};

    // Producer-owned probe cursor. Each source owns one capture producer.
    std::size_t next_probe_ = 0;
};

} // namespace arssyut::core::audio
