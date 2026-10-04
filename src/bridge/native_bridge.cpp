#include "bridge/native_bridge.h"

#ifdef _WIN32

#include "app/device_catalog.hpp"
#include "app/recorder_session.hpp"
#include "app/source_catalog.hpp"

#include <Windows.h>

#include <algorithm>
#include <cstring>
#include <iterator>
#include <mutex>
#include <new>
#include <string>
#include <utility>
#include <vector>

namespace {

using arssyut::app::DeviceChoice;
using arssyut::app::RecorderSession;
using arssyut::app::RecorderState;
using arssyut::app::RecorderTarget;

struct NativeBridgeContext final {
    std::mutex mutex;
    std::vector<RecorderTarget> sources;
    std::vector<DeviceChoice> microphones;
    std::vector<DeviceChoice> cameras;
    std::uint32_t source_generation = 0;
    std::uint32_t device_generation = 0;

    // P6UI.4A exposes this read-only. P6UI.4B will route Start/Stop through the
    // same instance instead of creating a second recorder/session authority.
    RecorderSession recorder;
};

[[nodiscard]] NativeBridgeContext *
as_context(
    ArssyutBridgeHandle handle) noexcept
{
    return static_cast<NativeBridgeContext *>(
        handle);
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

    const auto native =
        context->recorder.snapshot();

    snapshot->state =
        to_bridge_state(
            native.state);
    snapshot->elapsed_ticks =
        native.elapsed_ticks;
    snapshot->worker_finished =
        native.worker_finished
            ? 1
            : 0;
    snapshot->reserved0 = 0;
    snapshot->reserved1 = 0;
    snapshot->reserved2 = 0;
    snapshot->camera_center_x =
        native.presentation_camera_center_x;
    snapshot->camera_center_y =
        native.presentation_camera_center_y;
    snapshot->camera_zoom =
        native.presentation_camera_zoom;
    snapshot->capture_received =
        native.capture_received;
    snapshot->video_rendered =
        native.video_rendered;
    snapshot->encoder_submitted =
        native.encoder_submitted;
    snapshot->encoder_backpressure =
        native.encoder_backpressure;

    return ARSSYUT_BRIDGE_OK;
}

} // extern "C"

#endif
