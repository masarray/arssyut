#pragma once

namespace arssyut::presentation {

// Deterministic Reset/full-frame interlock for momentary presenter controls.
// This object never observes keys. PresentationInputWorker remains the only
// held-key authority; the gate merely prevents a still-held chord from
// re-arming until a physical release has been observed.
class MomentaryReleaseGate final {
public:
    [[nodiscard]] bool accept(
        bool pressed) noexcept
    {
        if (!pressed) {
            blocked_until_release_ = false;
            return false;
        }

        return !blocked_until_release_;
    }

    void block_until_release() noexcept
    {
        blocked_until_release_ = true;
    }

    void reset() noexcept
    {
        blocked_until_release_ = false;
    }

    [[nodiscard]] bool blocked() const noexcept
    {
        return blocked_until_release_;
    }

private:
    bool blocked_until_release_ = false;
};

} // namespace arssyut::presentation
