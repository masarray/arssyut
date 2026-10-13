#pragma once

#ifdef _WIN32

#include "core/time/monotonic_clock.hpp"
#include <d3d11.h>
#include <mfobjects.h>
#include <wrl/client.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace arssyut::windows {

// The MF IMFSample lease owns the DXGI-backed texture's lifetime. No webcam
// CPU copies, no queue growth, no D3D immediate-context access on callbacks.
struct WebcamGpuFrame {
    Microsoft::WRL::ComPtr<IMFSample> sample;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    arssyut::core::TimePoint captured_at{}; // canonical QPC domain
    std::uint64_t sequence = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t subresource = 0;

    [[nodiscard]] bool valid() const noexcept
    {
        return sample && texture && width != 0 && height != 0;
    }
};

// One publisher (camera owner worker), one consumer (video worker).
// The consumer holds a lease while using the GPU surface; the producer never
// reclaims that slot and retires *only* obsolete unread frames. 3-slot bound
// includes the frame in flight at the compositor. Slots must outlive leases.
class WebcamGpuFrameSlot final {
    enum class State : std::uint8_t { Free, Writing, Ready, Reading };
    static constexpr std::size_t none = 3;
    struct Slot {
        std::atomic<State> state{State::Free};
        WebcamGpuFrame frame{};
    };

public:
    static constexpr std::size_t capacity = 3;
    enum class PublishResult : std::uint8_t {
        Published, ReplacedUnread, DroppedBusy
    };

    class Lease final {
    public:
        Lease() noexcept = default;
        Lease(const Lease &) = delete;
        Lease &operator=(const Lease &) = delete;
        Lease(Lease &&rhs) noexcept
            : owner_(std::exchange(rhs.owner_, nullptr)),
              index_(std::exchange(rhs.index_, none)) {}
        Lease &operator=(Lease &&rhs) noexcept
        {
            if (this != &rhs) {
                release();
                owner_ = std::exchange(rhs.owner_, nullptr);
                index_ = std::exchange(rhs.index_, none);
            }
            return *this;
        }
        ~Lease() { release(); }
        [[nodiscard]] explicit operator bool() const noexcept
        {
            return owner_ != nullptr;
        }
        [[nodiscard]] const WebcamGpuFrame &get() const noexcept
        {
            return owner_->slots_[index_].frame;
        }
        [[nodiscard]] const WebcamGpuFrame *operator->() const noexcept
        {
            return owner_ ? &owner_->slots_[index_].frame : nullptr;
        }
        void release() noexcept
        {
            if (!owner_) return;
            auto &s = owner_->slots_[index_];
            s.frame = {};
            s.state.store(State::Free, std::memory_order_release);
            owner_ = nullptr;
            index_ = none;
        }
    private:
        friend class WebcamGpuFrameSlot;
        Lease(WebcamGpuFrameSlot *owner, std::size_t index) noexcept
            : owner_(owner), index_(index) {}
        WebcamGpuFrameSlot *owner_ = nullptr;
        std::size_t index_ = none;
    };

    WebcamGpuFrameSlot() = default;
    WebcamGpuFrameSlot(const WebcamGpuFrameSlot &) = delete;
    WebcamGpuFrameSlot &operator=(const WebcamGpuFrameSlot &) = delete;

    [[nodiscard]] PublishResult publish(WebcamGpuFrame frame) noexcept
    {
        if (!frame.valid())
            return PublishResult::DroppedBusy;
        std::size_t index = none;
        for (std::size_t n = 0; n < capacity; ++n) {
            const std::size_t candidate = (cursor_ + n) % capacity;
            State expected = State::Free;
            if (slots_[candidate].state.compare_exchange_strong(
                    expected, State::Writing, std::memory_order_acq_rel)) {
                index = candidate;
                break;
            }
        }
        if (index == none)
            return PublishResult::DroppedBusy;

        slots_[index].frame = std::move(frame);
        slots_[index].state.store(State::Ready, std::memory_order_release);
        const std::size_t previous =
            published_.exchange(index, std::memory_order_acq_rel);
        cursor_ = (index + 1) % capacity;

        bool replaced = false;
        if (previous != none && previous != index) {
            State expected = State::Ready;
            if (slots_[previous].state.compare_exchange_strong(
                    expected, State::Writing, std::memory_order_acq_rel)) {
                slots_[previous].frame = {};
                slots_[previous].state.store(State::Free, std::memory_order_release);
                replaced = true;
            }
        }
        return replaced ? PublishResult::ReplacedUnread
                        : PublishResult::Published;
    }

    [[nodiscard]] Lease try_acquire_latest() noexcept
    {
        const std::size_t index = published_.load(std::memory_order_acquire);
        if (index == none) return {};
        State expected = State::Ready;
        if (!slots_[index].state.compare_exchange_strong(
                expected, State::Reading, std::memory_order_acq_rel))
            return {};
        return Lease(this, index);
    }

    // Producer-side invalidation; never frees a consumer-owned texture.
    void discard_unread() noexcept
    {
        const std::size_t index =
            published_.exchange(none, std::memory_order_acq_rel);
        if (index == none) return;
        State expected = State::Ready;
        if (slots_[index].state.compare_exchange_strong(
                expected, State::Writing, std::memory_order_acq_rel)) {
            slots_[index].frame = {};
            slots_[index].state.store(State::Free, std::memory_order_release);
        }
    }

    [[nodiscard]] std::size_t in_flight_count() const noexcept
    {
        std::size_t n = 0;
        for (const auto &s : slots_)
            if (s.state.load(std::memory_order_acquire) != State::Free) ++n;
        return n;
    }

private:
    std::array<Slot, capacity> slots_{};
    std::atomic<std::size_t> published_{none};
    std::size_t cursor_ = 0; // only the camera worker writes cursor
};

} // namespace arssyut::windows

#endif
