#pragma once

#ifdef _WIN32

#include "core/result/result.hpp"
#include "core/video/frame_geometry.hpp"
#include "core/video/frame_scheduler.hpp"

#include <filesystem>
#include <memory>
#include <string>

namespace arssyut::windows {

enum class RecoverySessionState : std::uint8_t {
    Prepared = 0,
    Recording,
    Stopped,
    Finalizing,
    Ready,
    RecoverableError,
};

struct RecoverySessionMetadata {
    std::string session_id;
    std::filesystem::path intended_output;
    arssyut::core::FrameSize output_size{};
    arssyut::core::FrameRate frame_rate{};
};

class RecoverableSession final {
public:
    RecoverableSession(const RecoverableSession &) = delete;
    RecoverableSession &operator=(const RecoverableSession &) = delete;

    [[nodiscard]]
    static arssyut::core::Result<std::unique_ptr<RecoverableSession>>
    create(
        const std::filesystem::path &root,
        RecoverySessionMetadata metadata) noexcept;

    [[nodiscard]] arssyut::core::Status update_state(
        RecoverySessionState state) noexcept;

    [[nodiscard]] const std::filesystem::path &
    directory() const noexcept
    {
        return directory_;
    }

    [[nodiscard]] const std::filesystem::path &
    manifest_path() const noexcept
    {
        return manifest_path_;
    }

    [[nodiscard]] RecoverySessionState state() const noexcept
    {
        return state_;
    }

private:
    RecoverableSession() = default;

    [[nodiscard]] arssyut::core::Status persist(
        RecoverySessionState state) noexcept;

    RecoverySessionMetadata metadata_;
    std::filesystem::path directory_;
    std::filesystem::path manifest_path_;
    RecoverySessionState state_ = RecoverySessionState::Prepared;
};

} // namespace arssyut::windows

#endif
