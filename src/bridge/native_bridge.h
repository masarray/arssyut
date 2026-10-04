#pragma once

#ifdef _WIN32

#include <cstddef>
#include <cstdint>

#if defined(ARSSYUT_BRIDGE_BUILD)
#define ARSSYUT_BRIDGE_API __declspec(dllexport)
#else
#define ARSSYUT_BRIDGE_API __declspec(dllimport)
#endif

#define ARSSYUT_BRIDGE_CALL __cdecl

extern "C" {

constexpr std::uint32_t ARSSYUT_BRIDGE_ABI_VERSION = 1;
constexpr std::size_t ARSSYUT_BRIDGE_LABEL_CAPACITY = 256;

enum ArssyutBridgeStatus : std::int32_t {
    ARSSYUT_BRIDGE_OK = 0,
    ARSSYUT_BRIDGE_INVALID_ARGUMENT = 1,
    ARSSYUT_BRIDGE_OUT_OF_RANGE = 2,
    ARSSYUT_BRIDGE_INTERNAL_ERROR = 3,
};

enum ArssyutBridgeSourceKind : std::uint32_t {
    ARSSYUT_BRIDGE_SOURCE_MONITOR = 0,
    ARSSYUT_BRIDGE_SOURCE_WINDOW = 1,
};

enum ArssyutBridgeDeviceKind : std::uint32_t {
    ARSSYUT_BRIDGE_DEVICE_MICROPHONE = 0,
    ARSSYUT_BRIDGE_DEVICE_CAMERA = 1,
};

enum ArssyutBridgeRecorderState : std::uint32_t {
    ARSSYUT_BRIDGE_RECORDER_IDLE = 0,
    ARSSYUT_BRIDGE_RECORDER_PREPARING = 1,
    ARSSYUT_BRIDGE_RECORDER_RECORDING = 2,
    ARSSYUT_BRIDGE_RECORDER_STOPPING = 3,
    ARSSYUT_BRIDGE_RECORDER_FINALIZING = 4,
    ARSSYUT_BRIDGE_RECORDER_READY = 5,
    ARSSYUT_BRIDGE_RECORDER_FAILED = 6,
};

struct ArssyutBridgeRectV1 {
    std::int32_t left;
    std::int32_t top;
    std::int32_t right;
    std::int32_t bottom;
};

struct ArssyutBridgeSourceV1 {
    std::uint32_t struct_size;
    std::uint32_t kind;
    std::uint64_t token;
    ArssyutBridgeRectV1 screen_rect;
    wchar_t label[ARSSYUT_BRIDGE_LABEL_CAPACITY];
};

struct ArssyutBridgeDeviceV1 {
    std::uint32_t struct_size;
    std::uint32_t kind;
    std::uint64_t token;
    wchar_t name[ARSSYUT_BRIDGE_LABEL_CAPACITY];
};

struct ArssyutBridgeRecorderSnapshotV1 {
    std::uint32_t struct_size;
    std::uint32_t state;
    std::int64_t elapsed_ticks;
    std::uint8_t worker_finished;
    std::uint8_t reserved0;
    std::uint8_t reserved1;
    std::uint8_t reserved2;
    float camera_center_x;
    float camera_center_y;
    float camera_zoom;
    std::uint64_t capture_received;
    std::uint64_t video_rendered;
    std::uint64_t encoder_submitted;
    std::uint64_t encoder_backpressure;
};

using ArssyutBridgeHandle = void *;

ARSSYUT_BRIDGE_API
std::uint32_t ARSSYUT_BRIDGE_CALL
arssyut_bridge_abi_version() noexcept;

ARSSYUT_BRIDGE_API
ArssyutBridgeHandle ARSSYUT_BRIDGE_CALL
arssyut_bridge_create() noexcept;

ARSSYUT_BRIDGE_API
void ARSSYUT_BRIDGE_CALL
arssyut_bridge_destroy(
    ArssyutBridgeHandle handle) noexcept;

ARSSYUT_BRIDGE_API
std::int32_t ARSSYUT_BRIDGE_CALL
arssyut_bridge_refresh_sources(
    ArssyutBridgeHandle handle,
    std::uintptr_t own_window) noexcept;

ARSSYUT_BRIDGE_API
std::int32_t ARSSYUT_BRIDGE_CALL
arssyut_bridge_source_count(
    ArssyutBridgeHandle handle,
    std::uint32_t *count) noexcept;

ARSSYUT_BRIDGE_API
std::int32_t ARSSYUT_BRIDGE_CALL
arssyut_bridge_source_at(
    ArssyutBridgeHandle handle,
    std::uint32_t index,
    ArssyutBridgeSourceV1 *source) noexcept;

ARSSYUT_BRIDGE_API
std::int32_t ARSSYUT_BRIDGE_CALL
arssyut_bridge_refresh_devices(
    ArssyutBridgeHandle handle) noexcept;

ARSSYUT_BRIDGE_API
std::int32_t ARSSYUT_BRIDGE_CALL
arssyut_bridge_device_count(
    ArssyutBridgeHandle handle,
    std::uint32_t kind,
    std::uint32_t *count) noexcept;

ARSSYUT_BRIDGE_API
std::int32_t ARSSYUT_BRIDGE_CALL
arssyut_bridge_device_at(
    ArssyutBridgeHandle handle,
    std::uint32_t kind,
    std::uint32_t index,
    ArssyutBridgeDeviceV1 *device) noexcept;

ARSSYUT_BRIDGE_API
std::int32_t ARSSYUT_BRIDGE_CALL
arssyut_bridge_recorder_snapshot(
    ArssyutBridgeHandle handle,
    ArssyutBridgeRecorderSnapshotV1 *snapshot) noexcept;

} // extern "C"

#endif
