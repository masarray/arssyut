#pragma once

#ifdef _WIN32

#include "platform/windows/capture/captured_frame.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace arssyut::windows {

/*
 * Fixed three-slot latest-frame handoff.
 *
 * Producer: WGC FrameArrived internal worker.
 * Consumer: video pipeline worker.
 *
 * The producer never waits. A new frame takes a free slot, becomes the latest
 * published slot, and retires an older unread frame when safe. A consumer owns
 * a slot through FrameLease, which keeps the WGC frame alive so its pool
 * surface cannot be reused while the compositor reads it.
 */
class LatestFrameSlot {
public:
    static constexpr std::size_t slot_count = 3;

    enum class PublishResult : std::uint8_t {
        Published = 0,
        ReplacedUnread,
        DroppedBusy,
    };

private:
    enum class SlotState : std::uint8_t {
        Free = 0,
        Writing,
        Ready,
        Reading,
    };

    struct Slot {
        std::atomic<SlotState> state{SlotState::Free};
        CapturedFrame frame{};
    };

public:
    class FrameLease {
    public:
        FrameLease() = default;
        FrameLease(const FrameLease &) = delete;
        FrameLease &operator=(const FrameLease &) = delete;

        FrameLease(FrameLease &&other) noexcept
            : owner_(std::exchange(other.owner_, nullptr)),
              index_(std::exchange(other.index_, invalid_index))
        {
        }

        FrameLease &operator=(FrameLease &&other) noexcept
        {
            if (this == &other)
                return *this;
            release();
            owner_ = std::exchange(other.owner_, nullptr);
            index_ = std::exchange(other.index_, invalid_index);
            return *this;
        }

        ~FrameLease()
        {
            release();
        }

        [[nodiscard]] explicit operator bool() const noexcept
        {
            return owner_ != nullptr;
        }

        [[nodiscard]] const CapturedFrame *operator->() const noexcept
        {
            return owner_ ? &owner_->slots_[index_].frame : nullptr;
        }

        [[nodiscard]] const CapturedFrame &get() const noexcept
        {
            return owner_->slots_[index_].frame;
        }

        void release() noexcept
        {
            if (!owner_)
                return;

            auto &slot = owner_->slots_[index_];
            slot.frame = {};
            slot.state.store(SlotState::Free, std::memory_order_release);
            owner_ = nullptr;
            index_ = invalid_index;
        }

    private:
        friend class LatestFrameSlot;

        FrameLease(LatestFrameSlot *owner, std::size_t index) noexcept
            : owner_(owner), index_(index)
        {
        }

        static constexpr std::size_t invalid_index =
            static_cast<std::size_t>(-1);

        LatestFrameSlot *owner_ = nullptr;
        std::size_t index_ = invalid_index;
    };

    [[nodiscard]] PublishResult publish(CapturedFrame frame) noexcept
    {
        std::size_t target = invalid_index;

        for (std::size_t offset = 0; offset < slot_count; ++offset) {
            const std::size_t index =
                (producer_cursor_ + offset) % slot_count;
            SlotState expected = SlotState::Free;
            if (slots_[index].state.compare_exchange_strong(
                    expected,
                    SlotState::Writing,
                    std::memory_order_acq_rel,
                    std::memory_order_relaxed)) {
                target = index;
                break;
            }
        }

        if (target == invalid_index)
            return PublishResult::DroppedBusy;

        slots_[target].frame = std::move(frame);
        slots_[target].state.store(
            SlotState::Ready,
            std::memory_order_release);

        const std::size_t previous = published_.exchange(
            target,
            std::memory_order_acq_rel);

        producer_cursor_ = (target + 1) % slot_count;

        bool replaced_unread = false;
        if (previous != invalid_index && previous != target) {
            SlotState expected = SlotState::Ready;
            if (slots_[previous].state.compare_exchange_strong(
                    expected,
                    SlotState::Writing,
                    std::memory_order_acq_rel,
                    std::memory_order_relaxed)) {
                slots_[previous].frame = {};
                slots_[previous].state.store(
                    SlotState::Free,
                    std::memory_order_release);
                replaced_unread = true;
            }
        }

        return replaced_unread
            ? PublishResult::ReplacedUnread
            : PublishResult::Published;
    }

    /*
     * Producer-side resize/stop helper. Prevents a stale unread WGC frame from
     * keeping a frame-pool surface alive while the capture backend reconfigures.
     * A frame already held by the consumer is never force-released.
     */
    void producer_discard_unread() noexcept
    {
        const std::size_t index = published_.exchange(
            invalid_index,
            std::memory_order_acq_rel);

        if (index == invalid_index || index >= slot_count)
            return;

        SlotState expected = SlotState::Ready;
        if (slots_[index].state.compare_exchange_strong(
                expected,
                SlotState::Writing,
                std::memory_order_acq_rel,
                std::memory_order_relaxed)) {
            slots_[index].frame = {};
            slots_[index].state.store(
                SlotState::Free,
                std::memory_order_release);
        }
    }

    [[nodiscard]] std::size_t in_flight_count() const noexcept
    {
        std::size_t count = 0;
        for (const auto &slot : slots_) {
            if (slot.state.load(std::memory_order_acquire) !=
                SlotState::Free) {
                ++count;
            }
        }
        return count;
    }

    [[nodiscard]] bool has_in_flight() const noexcept
    {
        return in_flight_count() != 0;
    }

    [[nodiscard]] FrameLease try_acquire_latest() noexcept
    {
        const std::size_t index =
            published_.load(std::memory_order_acquire);

        if (index == invalid_index || index >= slot_count)
            return {};

        SlotState expected = SlotState::Ready;
        if (!slots_[index].state.compare_exchange_strong(
                expected,
                SlotState::Reading,
                std::memory_order_acq_rel,
                std::memory_order_relaxed)) {
            return {};
        }

        return FrameLease(this, index);
    }

private:
    static constexpr std::size_t invalid_index =
        static_cast<std::size_t>(-1);

    std::array<Slot, slot_count> slots_{};
    std::atomic<std::size_t> published_{invalid_index};

    // Single producer only.
    std::size_t producer_cursor_ = 0;
};

} // namespace arssyut::windows

#endif
