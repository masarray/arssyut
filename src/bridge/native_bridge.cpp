#include "bridge/native_bridge.h"

#ifdef _WIN32

#include "app/device_catalog.hpp"
#include "app/recorder_session.hpp"
#include "app/source_catalog.hpp"

#include <Windows.h>
#include <ShlObj.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <iterator>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <utility>
#include <vector>

namespace {

using arssyut::app::DeviceChoice;
using arssyut::app::RecorderConfig;
using arssyut::app::RecorderSession;
using arssyut::app::RecorderSnapshot;
using arssyut::app::RecorderState;
using arssyut::app::RecorderTarget;

struct NativeBridgeContext final {
    std::mutex mutex;
    std::vector<RecorderTarget> sources;
    std::vector<DeviceChoice> microphones;
    std::vector<DeviceChoice> cameras;
    std::uint32_t source_generation = 0;
    std::uint32_t device_generation = 0;

    // Exactly one native recorder authority for the Avalonia application
    // lifetime. Finished sessions are joined and replaced before a later start.
    std::unique_ptr<RecorderSession> recorder;
    std::filesystem::path last_output;
    std::filesystem::path last_diagnostics;
};

[[nodiscard]] NativeBridgeContext *
as_context(
    ArssyutBridgeHandle handle) noexcept
{
    return static_cast<NativeBridgeContext *>(
        handle);
}

[[nodiscard]] bool active_state(
    RecorderState state) noexcept
{
    return
        state == RecorderState::Preparing ||
        state == RecorderState::Recording ||
        state == RecorderState::Stopping ||
        state == RecorderState::Finalizing;
}

[[nodiscard]] std::uint64_t make_token(
    std::uint32_t generation,
    std::uint32_t index) noexcept
{
    return
        (static_cast<std::uint64_t>(
             generation)
         << 32U) |
        (static_cast<std::uint64_t>(
             index) +
         1ULL);
}

[[nodiscard]] bool decode_token(
    std::uint64_t token,
    std::uint32_t &generation,
    std::uint32_t &index) noexcept
{
    generation =
        static_cast<std::uint32_t>(
            token >> 32U);

    const auto ordinal =
        static_cast<std::uint32_t>(
            token & 0xFFFFFFFFULL);

    if (generation == 0 ||
        ordinal == 0) {
        return false;
    }

    index = ordinal - 1U;
    return true;
}

void copy_label(
    wchar_t (&destination)
        [ARSSYUT_BRIDGE_LABEL_CAPACITY],
    const std::wstring &source) noexcept
{
    std::fill(
        std::begin(destination),
        std::end(destination),
        L'\0');

    const std::size_t count =
        std::min(
            source.size(),
            ARSSYUT_BRIDGE_LABEL_CAPACITY -
                1);

    if (count > 0) {
        std::memcpy(
            destination,
            source.data(),
            count * sizeof(wchar_t));
    }
}

void copy_path(
    wchar_t (&destination)
        [ARSSYUT_BRIDGE_PATH_CAPACITY],
    const std::filesystem::path &path) noexcept
{
    std::fill(
        std::begin(destination),
        std::end(destination),
        L'\0');

    const auto source =
        path.wstring();

    const std::size_t count =
        std::min(
            source.size(),
            ARSSYUT_BRIDGE_PATH_CAPACITY -
                1);

    if (count > 0) {
        std::memcpy(
            destination,
            source.data(),
            count * sizeof(wchar_t));
    }
}

[[nodiscard]] std::wstring request_path(
    const wchar_t (&value)
        [ARSSYUT_BRIDGE_PATH_CAPACITY])
{
    const auto end =
        std::find(
            std::begin(value),
            std::end(value),
            L'\0');

    return std::wstring(
        std::begin(value),
        end);
}

[[nodiscard]] ArssyutBridgeRectV1
to_bridge_rect(
    const RECT &rect) noexcept
{
    return {
        rect.left,
        rect.top,
        rect.right,
        rect.bottom};
}

[[nodiscard]] std::uint32_t
to_bridge_state(
    RecorderState state) noexcept
{
    switch (state) {
    case RecorderState::Preparing:
        return ARSSYUT_BRIDGE_RECORDER_PREPARING;
    case RecorderState::Recording:
        return ARSSYUT_BRIDGE_RECORDER_RECORDING;
    case RecorderState::Stopping:
        return ARSSYUT_BRIDGE_RECORDER_STOPPING;
    case RecorderState::Finalizing:
        return ARSSYUT_BRIDGE_RECORDER_FINALIZING;
    case RecorderState::Ready:
        return ARSSYUT_BRIDGE_RECORDER_READY;
    case RecorderState::Failed:
        return ARSSYUT_BRIDGE_RECORDER_FAILED;
    case RecorderState::Idle:
    default:
        return ARSSYUT_BRIDGE_RECORDER_IDLE;
    }
}

[[nodiscard]] std::vector<DeviceChoice> *
device_vector(
    NativeBridgeContext &context,
    std::uint32_t kind) noexcept
{
    switch (kind) {
    case ARSSYUT_BRIDGE_DEVICE_MICROPHONE:
        return &context.microphones;
    case ARSSYUT_BRIDGE_DEVICE_CAMERA:
        return &context.cameras;
    default:
        return nullptr;
    }
}

[[nodiscard]] const RecorderTarget *
resolve_source(
    const NativeBridgeContext &context,
    std::uint64_t token) noexcept
{
    std::uint32_t generation = 0;
    std::uint32_t index = 0;

    if (!decode_token(
            token,
            generation,
            index)) {
        return nullptr;
    }

    if (generation !=
            context.source_generation ||
        index >= context.sources.size()) {
        return nullptr;
    }

    return &context.sources[index];
}

[[nodiscard]] bool source_matches_mode(
    const RecorderTarget &target,
    std::uint32_t capture_mode) noexcept
{
    const bool monitor =
        target.kind ==
            arssyut::windows::
                CaptureTargetKind::Monitor;

    switch (capture_mode) {
    case ARSSYUT_BRIDGE_CAPTURE_DISPLAY:
        return monitor;
    case ARSSYUT_BRIDGE_CAPTURE_WINDOW:
        return !monitor;
    default:
        return false;
    }
}

[[nodiscard]]
std::filesystem::path
default_output_folder()
{
    PWSTR videos = nullptr;
    std::filesystem::path folder;

    if (SUCCEEDED(
            SHGetKnownFolderPath(
                FOLDERID_Videos,
                KF_FLAG_DEFAULT,
                nullptr,
                &videos)) &&
        videos) {
        folder = videos;
        CoTaskMemFree(videos);
    } else {
        std::error_code ec;
        folder =
            std::filesystem::current_path(
                ec);
        if (ec)
            folder = L".";
    }

    folder /= L"Arssyut";
    return folder;
}

[[nodiscard]] const wchar_t *
visual_mode_file_suffix(
    std::uint32_t mode) noexcept
{
    switch (mode) {
    case ARSSYUT_BRIDGE_VISUAL_CLEAN_SCREEN:
        return L"clean-screen";
    case ARSSYUT_BRIDGE_VISUAL_VIVID_PRESENTATION:
        return L"vivid-presentation";
    case ARSSYUT_BRIDGE_VISUAL_PIXEL_ACCURATE:
    default:
        return L"pixel-accurate";
    }
}

[[nodiscard]]
std::filesystem::path
build_output_path(
    const ArssyutBridgeStartRequestV1 &request)
{
    std::filesystem::path folder =
        request_path(
            request.output_folder);

    if (folder.empty() ||
        folder.is_relative()) {
        folder =
            default_output_folder();
    }

    SYSTEMTIME time{};
    GetLocalTime(
        &time);

    wchar_t filename[192]{};
    swprintf_s(
        filename,
        L"Arssyut-%04u%02u%02u-%02u%02u%02u-%ls.mp4",
        time.wYear,
        time.wMonth,
        time.wDay,
        time.wHour,
        time.wMinute,
        time.wSecond,
        visual_mode_file_suffix(
            request.visual_mode));

    return folder / filename;
}

[[nodiscard]]
arssyut::visual::ArVisualProductMode
to_visual_mode(
    std::uint32_t mode) noexcept
{
    switch (mode) {
    case ARSSYUT_BRIDGE_VISUAL_CLEAN_SCREEN:
        return
            arssyut::visual::
                ArVisualProductMode::
                    CleanScreen;
    case ARSSYUT_BRIDGE_VISUAL_VIVID_PRESENTATION:
        return
            arssyut::visual::
                ArVisualProductMode::
                    VividPresentation;
    case ARSSYUT_BRIDGE_VISUAL_PIXEL_ACCURATE:
    default:
        return
            arssyut::visual::
                ArVisualProductMode::
                    PixelAccurate;
    }
}

void fill_snapshot(
    const RecorderSnapshot &native,
    ArssyutBridgeRecorderSnapshotV1 &snapshot) noexcept
{
    snapshot.state =
        to_bridge_state(
            native.state);
    snapshot.elapsed_ticks =
        native.elapsed_ticks;
    snapshot.worker_finished =
        native.worker_finished
            ? 1
            : 0;
    snapshot.reserved0 = 0;
    snapshot.reserved1 = 0;
    snapshot.reserved2 = 0;
    snapshot.camera_center_x =
        native.presentation_camera_center_x;
    snapshot.camera_center_y =
        native.presentation_camera_center_y;
    snapshot.camera_zoom =
        native.presentation_camera_zoom;
    snapshot.capture_received =
        native.capture_received;
    snapshot.video_rendered =
        native.video_rendered;
    snapshot.encoder_submitted =
        native.encoder_submitted;
    snapshot.encoder_backpressure =
        native.encoder_backpressure;
    snapshot.error_code =
        static_cast<std::uint32_t>(
            native.last_error.code);
    snapshot.error_detail =
        native.last_error.detail;
}

} // namespace

extern "C" {

std::uint32_t ARSSYUT_BRIDGE_CALL
arssyut_bridge_abi_version() noexcept
{
    return ARSSYUT_BRIDGE_ABI_VERSION;
}

ArssyutBridgeHandle ARSSYUT_BRIDGE_CALL
arssyut_bridge_create() noexcept
{
    try {
        return new NativeBridgeContext();
    } catch (...) {
        return nullptr;
    }
}

void ARSSYUT_BRIDGE_CALL
arssyut_bridge_destroy(
    ArssyutBridgeHandle handle) noexcept
{
    delete as_context(handle);
}

std::int32_t ARSSYUT_BRIDGE_CALL
arssyut_bridge_refresh_sources(
    ArssyutBridgeHandle handle,
    std::uintptr_t own_window) noexcept
{
    auto *context =
        as_context(handle);
    if (!context)
        return ARSSYUT_BRIDGE_INVALID_ARGUMENT;

    try {
        auto sources =
            arssyut::app::
                enumerate_recorder_targets(
                    reinterpret_cast<HWND>(
                        own_window));

        std::scoped_lock lock(
            context->mutex);

        context->sources =
            std::move(sources);

        ++context->source_generation;
        if (context->source_generation == 0)
            context->source_generation = 1;

        return ARSSYUT_BRIDGE_OK;
    } catch (...) {
        return ARSSYUT_BRIDGE_INTERNAL_ERROR;
    }
}

std::int32_t ARSSYUT_BRIDGE_CALL
arssyut_bridge_source_count(
    ArssyutBridgeHandle handle,
    std::uint32_t *count) noexcept
{
    auto *context =
        as_context(handle);
    if (!context || !count)
        return ARSSYUT_BRIDGE_INVALID_ARGUMENT;

    std::scoped_lock lock(
        context->mutex);

    *count =
        static_cast<std::uint32_t>(
            context->sources.size());

    return ARSSYUT_BRIDGE_OK;
}

std::int32_t ARSSYUT_BRIDGE_CALL
arssyut_bridge_source_at(
    ArssyutBridgeHandle handle,
    std::uint32_t index,
    ArssyutBridgeSourceV1 *source) noexcept
{
    auto *context =
        as_context(handle);
    if (!context || !source)
        return ARSSYUT_BRIDGE_INVALID_ARGUMENT;

    if (source->struct_size <
        sizeof(ArssyutBridgeSourceV1)) {
        return ARSSYUT_BRIDGE_INVALID_ARGUMENT;
    }

    std::scoped_lock lock(
        context->mutex);

    if (index >= context->sources.size())
        return ARSSYUT_BRIDGE_OUT_OF_RANGE;

    const auto &native =
        context->sources[index];

    RECT rect{};
    arssyut::app::
        recorder_target_screen_rect(
            native,
            rect);

    source->kind =
        native.kind ==
                arssyut::windows::
                    CaptureTargetKind::Monitor
            ? ARSSYUT_BRIDGE_SOURCE_MONITOR
            : ARSSYUT_BRIDGE_SOURCE_WINDOW;

    source->token =
        make_token(
            context->source_generation,
            index);

    source->screen_rect =
        to_bridge_rect(rect);

    copy_label(
        source->label,
        native.label);

    return ARSSYUT_BRIDGE_OK;
}

std::int32_t ARSSYUT_BRIDGE_CALL
arssyut_bridge_refresh_devices(
    ArssyutBridgeHandle handle) noexcept
{
    auto *context =
        as_context(handle);
    if (!context)
        return ARSSYUT_BRIDGE_INVALID_ARGUMENT;

    try {
        auto microphones =
            arssyut::app::
                enumerate_microphones();
        auto cameras =
            arssyut::app::
                enumerate_cameras();

        std::scoped_lock lock(
            context->mutex);

        context->microphones =
            std::move(microphones);
        context->cameras =
            std::move(cameras);

        ++context->device_generation;
        if (context->device_generation == 0)
            context->device_generation = 1;

        return ARSSYUT_BRIDGE_OK;
    } catch (...) {
        return ARSSYUT_BRIDGE_INTERNAL_ERROR;
    }
}

std::int32_t ARSSYUT_BRIDGE_CALL
arssyut_bridge_device_count(
    ArssyutBridgeHandle handle,
    std::uint32_t kind,
    std::uint32_t *count) noexcept
{
    auto *context =
        as_context(handle);
    if (!context || !count)
        return ARSSYUT_BRIDGE_INVALID_ARGUMENT;

    std::scoped_lock lock(
        context->mutex);

    auto *items =
        device_vector(
            *context,
            kind);

    if (!items)
        return ARSSYUT_BRIDGE_INVALID_ARGUMENT;

    *count =
        static_cast<std::uint32_t>(
            items->size());

    return ARSSYUT_BRIDGE_OK;
}

std::int32_t ARSSYUT_BRIDGE_CALL
arssyut_bridge_device_at(
    ArssyutBridgeHandle handle,
    std::uint32_t kind,
    std::uint32_t index,
    ArssyutBridgeDeviceV1 *device) noexcept
{
    auto *context =
        as_context(handle);
    if (!context || !device)
        return ARSSYUT_BRIDGE_INVALID_ARGUMENT;

    if (device->struct_size <
        sizeof(ArssyutBridgeDeviceV1)) {
        return ARSSYUT_BRIDGE_INVALID_ARGUMENT;
    }

    std::scoped_lock lock(
        context->mutex);

    auto *items =
        device_vector(
            *context,
            kind);

    if (!items)
        return ARSSYUT_BRIDGE_INVALID_ARGUMENT;

    if (index >= items->size())
        return ARSSYUT_BRIDGE_OUT_OF_RANGE;

    const auto &native =
        (*items)[index];

    device->kind =
        kind;
    device->token =
        make_token(
            context->device_generation,
            index);

    copy_label(
        device->name,
        native.name);

    return ARSSYUT_BRIDGE_OK;
}

std::int32_t ARSSYUT_BRIDGE_CALL
arssyut_bridge_recorder_start(
    ArssyutBridgeHandle handle,
    const ArssyutBridgeStartRequestV1 *request) noexcept
{
    auto *context =
        as_context(handle);
    if (!context || !request)
        return ARSSYUT_BRIDGE_INVALID_ARGUMENT;

    if (request->struct_size <
        sizeof(ArssyutBridgeStartRequestV1)) {
        return ARSSYUT_BRIDGE_INVALID_ARGUMENT;
    }

    if (request->frame_rate != 30 &&
        request->frame_rate != 60) {
        return ARSSYUT_BRIDGE_INVALID_ARGUMENT;
    }

    if (request->visual_mode >
        ARSSYUT_BRIDGE_VISUAL_VIVID_PRESENTATION) {
        return ARSSYUT_BRIDGE_INVALID_ARGUMENT;
    }

    if (request->capture_mode ==
            ARSSYUT_BRIDGE_CAPTURE_REGION ||
        request->capture_mode ==
            ARSSYUT_BRIDGE_CAPTURE_GAME) {
        // Region is deliberately bound in P6UI.4C so its existing native editor
        // remains the one rectangle/crop authority. Game has no backend yet.
        return ARSSYUT_BRIDGE_UNSUPPORTED;
    }

    constexpr std::uint32_t unsupported_input_flags =
        ARSSYUT_BRIDGE_START_SYSTEM_AUDIO |
        ARSSYUT_BRIDGE_START_MICROPHONE |
        ARSSYUT_BRIDGE_START_CAMERA;

    if ((request->flags &
         unsupported_input_flags) != 0) {
        // P6R.3/P6R.4 own these media backends. Never silently claim they were
        // recorded while only the video recorder is active.
        return ARSSYUT_BRIDGE_UNSUPPORTED;
    }

    try {
        std::unique_ptr<RecorderSession> previous;
        RecorderTarget target;

        {
            std::scoped_lock lock(
                context->mutex);

            const auto *resolved =
                resolve_source(
                    *context,
                    request->source_token);

            if (!resolved)
                return ARSSYUT_BRIDGE_STALE_TOKEN;

            if (!source_matches_mode(
                    *resolved,
                    request->capture_mode)) {
                return ARSSYUT_BRIDGE_INVALID_ARGUMENT;
            }

            target =
                *resolved;

            if (context->recorder) {
                const auto snapshot =
                    context->recorder->
                        snapshot();

                if (active_state(
                        snapshot.state) ||
                    !snapshot.worker_finished) {
                    return ARSSYUT_BRIDGE_BUSY;
                }

                previous =
                    std::move(
                        context->recorder);
            }
        }

        // Never join while holding the bridge mutex. This is only reachable
        // after worker_finished=true, so it is lifecycle cleanup rather than
        // a UI-thread wait on active recording work.
        if (previous)
            previous->wait();

        RecorderConfig config;
        config.target =
            std::move(target);
        config.output_path =
            build_output_path(
                *request);
        config.output_size =
            {1920, 1080};
        config.frame_rate =
            {request->frame_rate, 1};
        config.bitrate_bps =
            request->frame_rate == 60
                ? 18'000'000U
                : 12'000'000U;
        config.visual_mode =
            to_visual_mode(
                request->visual_mode);

        config.presentation.smart_zoom =
            (request->flags &
             ARSSYUT_BRIDGE_START_SMART_ZOOM) != 0;
        config.presentation.click_visual =
            (request->flags &
             ARSSYUT_BRIDGE_START_CLICK_VISUAL) != 0;
        config.presentation.shortcut_keys =
            (request->flags &
             ARSSYUT_BRIDGE_START_SHORTCUT_KEYS) != 0;
        config.presentation.zoom =
            2.0f;

        auto recorder =
            std::make_unique<
                RecorderSession>();

        const auto status =
            recorder->start(
                config);

        if (!status.ok())
            return ARSSYUT_BRIDGE_START_FAILED;

        const auto diagnostics =
            recorder->
                diagnostics_path();

        {
            std::scoped_lock lock(
                context->mutex);

            context->last_output =
                config.output_path;
            context->last_diagnostics =
                diagnostics;
            context->recorder =
                std::move(recorder);
        }

        return ARSSYUT_BRIDGE_OK;
    } catch (...) {
        return ARSSYUT_BRIDGE_INTERNAL_ERROR;
    }
}

std::int32_t ARSSYUT_BRIDGE_CALL
arssyut_bridge_recorder_stop(
    ArssyutBridgeHandle handle) noexcept
{
    auto *context =
        as_context(handle);
    if (!context)
        return ARSSYUT_BRIDGE_INVALID_ARGUMENT;

    std::scoped_lock lock(
        context->mutex);

    if (!context->recorder)
        return ARSSYUT_BRIDGE_INVALID_STATE;

    const auto snapshot =
        context->recorder->
            snapshot();

    if (!active_state(
            snapshot.state)) {
        return ARSSYUT_BRIDGE_INVALID_STATE;
    }

    context->recorder->
        request_stop();

    return ARSSYUT_BRIDGE_OK;
}

std::int32_t ARSSYUT_BRIDGE_CALL
arssyut_bridge_recorder_snapshot(
    ArssyutBridgeHandle handle,
    ArssyutBridgeRecorderSnapshotV1 *snapshot) noexcept
{
    auto *context =
        as_context(handle);
    if (!context || !snapshot)
        return ARSSYUT_BRIDGE_INVALID_ARGUMENT;

    if (snapshot->struct_size <
        sizeof(
            ArssyutBridgeRecorderSnapshotV1)) {
        return ARSSYUT_BRIDGE_INVALID_ARGUMENT;
    }

    std::scoped_lock lock(
        context->mutex);

    if (!context->recorder) {
        *snapshot =
            ArssyutBridgeRecorderSnapshotV1{};
        snapshot->struct_size =
            sizeof(
                ArssyutBridgeRecorderSnapshotV1);
        snapshot->state =
            ARSSYUT_BRIDGE_RECORDER_IDLE;
        snapshot->worker_finished =
            1;
        snapshot->camera_center_x =
            0.5f;
        snapshot->camera_center_y =
            0.5f;
        snapshot->camera_zoom =
            1.0f;
        return ARSSYUT_BRIDGE_OK;
    }

    const auto native =
        context->recorder->
            snapshot();

    fill_snapshot(
        native,
        *snapshot);

    return ARSSYUT_BRIDGE_OK;
}

std::int32_t ARSSYUT_BRIDGE_CALL
arssyut_bridge_recorder_result(
    ArssyutBridgeHandle handle,
    ArssyutBridgeRecorderResultV1 *result) noexcept
{
    auto *context =
        as_context(handle);
    if (!context || !result)
        return ARSSYUT_BRIDGE_INVALID_ARGUMENT;

    if (result->struct_size <
        sizeof(
            ArssyutBridgeRecorderResultV1)) {
        return ARSSYUT_BRIDGE_INVALID_ARGUMENT;
    }

    std::scoped_lock lock(
        context->mutex);

    result->reserved0 = 0;
    copy_path(
        result->output_path,
        context->last_output);
    copy_path(
        result->diagnostics_path,
        context->last_diagnostics);

    return ARSSYUT_BRIDGE_OK;
}

} // extern "C"

#endif
