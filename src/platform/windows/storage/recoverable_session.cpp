#include "platform/windows/storage/recoverable_session.hpp"

#ifdef _WIN32

#include "core/result/status.hpp"

#include <Windows.h>

#include <algorithm>
#include <limits>
#include <new>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>

namespace arssyut::windows {

namespace {

using arssyut::core::Result;
using arssyut::core::Status;
using arssyut::core::StatusCode;

[[nodiscard]] const char *state_name(
    RecoverySessionState state) noexcept
{
    switch (state) {
    case RecoverySessionState::Prepared:
        return "prepared";
    case RecoverySessionState::Recording:
        return "recording";
    case RecoverySessionState::Stopped:
        return "stopped";
    case RecoverySessionState::Finalizing:
        return "finalizing";
    case RecoverySessionState::Ready:
        return "ready";
    case RecoverySessionState::RecoverableError:
        return "recoverable_error";
    default:
        return "unknown";
    }
}

[[nodiscard]] std::string path_utf8(
    const std::filesystem::path &path)
{
    const std::wstring wide = path.wstring();
    if (wide.empty())
        return {};

    const int required = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        wide.data(),
        static_cast<int>(wide.size()),
        nullptr,
        0,
        nullptr,
        nullptr);

    if (required <= 0)
        return {};

    std::string output(
        static_cast<std::size_t>(required),
        '\0');

    const int written = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        wide.data(),
        static_cast<int>(wide.size()),
        output.data(),
        required,
        nullptr,
        nullptr);

    if (written != required)
        return {};

    return output;
}

[[nodiscard]] Status write_atomic(
    const std::filesystem::path &target,
    const std::string &content) noexcept
{
    const std::filesystem::path temp =
        target.wstring() + L".tmp";

    HANDLE file = CreateFileW(
        temp.c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH,
        nullptr);

    if (file == INVALID_HANDLE_VALUE) {
        return Status::failure(
            StatusCode::StorageFailure,
            GetLastError());
    }

    bool ok = true;
    std::size_t offset = 0;

    while (offset < content.size()) {
        const std::size_t remaining =
            content.size() - offset;
        const DWORD chunk = static_cast<DWORD>(
            std::min<std::size_t>(
                remaining,
                static_cast<std::size_t>(
                    std::numeric_limits<DWORD>::max())));

        DWORD written = 0;
        if (!WriteFile(
                file,
                content.data() + offset,
                chunk,
                &written,
                nullptr) ||
            written == 0) {
            ok = false;
            break;
        }
        offset += written;
    }

    if (ok && !FlushFileBuffers(file))
        ok = false;

    DWORD error = ok ? ERROR_SUCCESS : GetLastError();
    CloseHandle(file);

    if (!ok) {
        DeleteFileW(temp.c_str());
        return Status::failure(
            StatusCode::StorageFailure,
            error);
    }

    if (!MoveFileExW(
            temp.c_str(),
            target.c_str(),
            MOVEFILE_REPLACE_EXISTING |
                MOVEFILE_WRITE_THROUGH)) {
        error = GetLastError();
        DeleteFileW(temp.c_str());
        return Status::failure(
            StatusCode::StorageFailure,
            error);
    }

    return Status::success();
}

} // namespace

Result<std::unique_ptr<RecoverableSession>>
RecoverableSession::create(
    const std::filesystem::path &root,
    RecoverySessionMetadata metadata) noexcept
{
    if (metadata.session_id.empty() ||
        !metadata.output_size.valid() ||
        !metadata.frame_rate.valid() ||
        metadata.intended_output.empty()) {
        return Result<std::unique_ptr<RecoverableSession>>::failure(
            Status::failure(StatusCode::InvalidArgument));
    }

    std::error_code ec;
    std::filesystem::create_directories(root, ec);
    if (ec) {
        return Result<std::unique_ptr<RecoverableSession>>::failure(
            Status::failure(
                StatusCode::StorageFailure,
                static_cast<std::uint32_t>(ec.value())));
    }

    std::unique_ptr<RecoverableSession> session(
        new (std::nothrow) RecoverableSession{});
    if (!session) {
        return Result<std::unique_ptr<RecoverableSession>>::failure(
            Status::failure(StatusCode::InternalError));
    }

    session->metadata_ = std::move(metadata);
    session->directory_ =
        root /
        (session->metadata_.session_id +
         ".arssyut-session");

    std::filesystem::create_directories(
        session->directory_,
        ec);
    if (ec) {
        return Result<std::unique_ptr<RecoverableSession>>::failure(
            Status::failure(
                StatusCode::StorageFailure,
                static_cast<std::uint32_t>(ec.value())));
    }

    session->manifest_path_ =
        session->directory_ / "manifest.v1";

    const Status status =
        session->persist(RecoverySessionState::Prepared);
    if (!status.ok()) {
        return Result<std::unique_ptr<RecoverableSession>>::failure(
            status);
    }

    return Result<std::unique_ptr<RecoverableSession>>::success(
        std::move(session));
}

Status RecoverableSession::update_state(
    RecoverySessionState state) noexcept
{
    if (state_ == RecoverySessionState::Ready)
        return Status::failure(
            StatusCode::InvalidStateTransition);

    const Status status = persist(state);
    if (status.ok())
        state_ = state;
    return status;
}

Status RecoverableSession::persist(
    RecoverySessionState state) noexcept
{
    try {
        const std::string output =
            path_utf8(metadata_.intended_output);
        if (output.empty())
            return Status::failure(StatusCode::StorageFailure);

        std::ostringstream manifest;
        manifest
            << "format=arssyut-session-v1\n"
            << "state=" << state_name(state) << "\n"
            << "width=" << metadata_.output_size.width << "\n"
            << "height=" << metadata_.output_size.height << "\n"
            << "fps_num=" << metadata_.frame_rate.numerator << "\n"
            << "fps_den=" << metadata_.frame_rate.denominator << "\n"
            << "output=" << output << "\n";

        return write_atomic(
            manifest_path_,
            manifest.str());
    } catch (...) {
        return Status::failure(StatusCode::InternalError);
    }
}

} // namespace arssyut::windows

#endif
