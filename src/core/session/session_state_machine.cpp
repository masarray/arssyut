#include "core/session/session_state_machine.hpp"

namespace arssyut::core {

bool is_valid_session_transition(
    SessionState from,
    SessionState to) noexcept
{
    if (from == to)
        return false;

    switch (from) {
    case SessionState::Idle:
        return to == SessionState::Preparing;

    case SessionState::Preparing:
        return to == SessionState::Countdown ||
               to == SessionState::Idle ||
               to == SessionState::Failed;

    case SessionState::Countdown:
        return to == SessionState::Recording ||
               to == SessionState::Stopping ||
               to == SessionState::Failed;

    case SessionState::Recording:
        return to == SessionState::Paused ||
               to == SessionState::Stopping ||
               to == SessionState::RecoverableError ||
               to == SessionState::Failed;

    case SessionState::Paused:
        return to == SessionState::Recording ||
               to == SessionState::Stopping ||
               to == SessionState::RecoverableError ||
               to == SessionState::Failed;

    case SessionState::Stopping:
        return to == SessionState::Finalizing ||
               to == SessionState::RecoverableError ||
               to == SessionState::Failed;

    case SessionState::Finalizing:
        return to == SessionState::Ready ||
               to == SessionState::RecoverableError ||
               to == SessionState::Failed;

    case SessionState::Ready:
        return to == SessionState::Idle;

    case SessionState::RecoverableError:
        return to == SessionState::Finalizing ||
               to == SessionState::Idle ||
               to == SessionState::Failed;

    case SessionState::Failed:
        return to == SessionState::Idle;

    default:
        return false;
    }
}

Status SessionStateMachine::transition(SessionState target) noexcept
{
    if (!is_valid_session_transition(state_, target)) {
        const std::uint32_t detail =
            (static_cast<std::uint32_t>(state_) << 16U) |
            static_cast<std::uint32_t>(target);
        return Status::failure(
            StatusCode::InvalidStateTransition,
            detail);
    }

    state_ = target;
    return Status::success();
}

} // namespace arssyut::core
