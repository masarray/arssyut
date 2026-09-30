#pragma once

#include "core/result/status.hpp"

#include <cstdint>

namespace arssyut::core {

enum class SessionState : std::uint8_t {
    Idle = 0,
    Preparing,
    Countdown,
    Recording,
    Paused,
    Stopping,
    Finalizing,
    Ready,
    RecoverableError,
    Failed,
};

[[nodiscard]] constexpr bool is_active_session_state(
    SessionState state) noexcept
{
    switch (state) {
    case SessionState::Preparing:
    case SessionState::Countdown:
    case SessionState::Recording:
    case SessionState::Paused:
    case SessionState::Stopping:
    case SessionState::Finalizing:
        return true;
    case SessionState::Idle:
    case SessionState::Ready:
    case SessionState::RecoverableError:
    case SessionState::Failed:
    default:
        return false;
    }
}

[[nodiscard]] bool is_valid_session_transition(
    SessionState from,
    SessionState to) noexcept;

class SessionStateMachine {
public:
    [[nodiscard]] SessionState state() const noexcept
    {
        return state_;
    }

    [[nodiscard]] bool active() const noexcept
    {
        return is_active_session_state(state_);
    }

    [[nodiscard]] Status transition(SessionState target) noexcept;

private:
    SessionState state_ = SessionState::Idle;
};

} // namespace arssyut::core
