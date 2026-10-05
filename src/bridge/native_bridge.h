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

constexpr std::uint32_t ARSSYUT_BRIDGE_ABI_VERSION = 6;
constexpr std::size_t ARSSYUT_BRIDGE_LABEL_CAPACITY = 256;
constexpr std::size_t ARSSYUT_BRIDGE_PATH_CAPACITY = 512;

enum ArssyutBridgeStatus : std::int32_t {
    ARSSYUT_BRIDGE_OK = 0,
    ARSSYUT_BRIDGE_INVALID_ARGUMENT = 1,
    ARSSYUT_BRIDGE_OUT_OF_RANGE = 2,
    ARSSYUT_BRIDGE_INTERNAL_ERROR = 3,
    ARSSYUT_BRIDGE_BUSY = 4,
    ARSSYUT_BRIDGE_UNSUPPORTED = 5,
    ARSSYUT_BRIDGE_STALE_TOKEN = 6,
    ARSSYUT_BRIDGE_START_FAILED = 7,
    ARSSYUT_BRIDGE_INVALID_STATE = 8,
};

enum ArssyutBridgeSourceKind : std::uint32_t {
    ARSSYUT_BRIDGE_SOURCE_MONITOR = 0,
    ARSSYUT_BRIDGE_SOURCE_WINDOW = 1,
};

enum ArssyutBridgeDeviceKind : std::uint32_t {
    ARSSYUT_BRIDGE_DEVICE_MICROPHONE = 0,
    ARSSYUT_BRIDGE_DEVICE_CAMERA = 1,
};

enum ArssyutBridgeCaptureMode : std::uint32_t {
    ARSSYUT_BRIDGE_CAPTURE_DISPLAY = 0,
    ARSSYUT_BRIDGE_CAPTURE_WINDOW = 1,
    ARSSYUT_BRIDGE_CAPTURE_REGION = 2,
    ARSSYUT_BRIDGE_CAPTURE_GAME = 3,
};

enum ArssyutBridgeHotkeyModifiers : std::uint32_t {
    ARSSYUT_BRIDGE_HOTKEY_CTRL = 1U << 0U,
    ARSSYUT_BRIDGE_HOTKEY_SHIFT = 1U << 1U,
    ARSSYUT_BRIDGE_HOTKEY_ALT = 1U << 2U,
    ARSSYUT_BRIDGE_HOTKEY_WIN = 1U << 3U,
};

enum ArssyutBridgeHotkeyAction : std::uint32_t {
    ARSSYUT_BRIDGE_HOTKEY_TOGGLE_RECORD = 0,
    ARSSYUT_BRIDGE_HOTKEY_TOGGLE_PAUSE = 1,
    ARSSYUT_BRIDGE_HOTKEY_TOGGLE_MICROPHONE = 2,
    ARSSYUT_BRIDGE_HOTKEY_TOGGLE_CAMERA = 3,
    ARSSYUT_BRIDGE_HOTKEY_TOGGLE_ZOOM = 4,
    ARSSYUT_BRIDGE_HOTKEY_ZOOM_IN = 5,
    ARSSYUT_BRIDGE_HOTKEY_ZOOM_OUT = 6,
    ARSSYUT_BRIDGE_HOTKEY_RESET_FULL_FRAME = 7,
};

enum ArssyutBridgeHotkeyEvents : std::uint32_t {
    ARSSYUT_BRIDGE_HOTKEY_EVENT_RECORD = 1U << 0U,
    ARSSYUT_BRIDGE_HOTKEY_EVENT_PAUSE = 1U << 1U,
    ARSSYUT_BRIDGE_HOTKEY_EVENT_MICROPHONE = 1U << 2U,
    ARSSYUT_BRIDGE_HOTKEY_EVENT_CAMERA = 1U << 3U,
};

enum ArssyutBridgePresenterCommand : std::uint32_t {
    ARSSYUT_BRIDGE_PRESENTER_TOGGLE_ZOOM = 0,
    ARSSYUT_BRIDGE_PRESENTER_ZOOM_IN = 1,
    ARSSYUT_BRIDGE_PRESENTER_ZOOM_OUT = 2,
    ARSSYUT_BRIDGE_PRESENTER_RESET_FULL_FRAME = 3,
};

enum ArssyutBridgeVisualMode : std::uint32_t {
    ARSSYUT_BRIDGE_VISUAL_PIXEL_ACCURATE = 0,
    ARSSYUT_BRIDGE_VISUAL_CLEAN_SCREEN = 1,
    ARSSYUT_BRIDGE_VISUAL_VIVID_PRESENTATION = 2,
};

enum ArssyutBridgeStartFlags : std::uint32_t {
    ARSSYUT_BRIDGE_START_SYSTEM_AUDIO = 1U << 0U,
    ARSSYUT_BRIDGE_START_MICROPHONE = 1U << 1U,
    ARSSYUT_BRIDGE_START_CAMERA = 1U << 2U,
    ARSSYUT_BRIDGE_START_SMART_ZOOM = 1U << 3U,
    ARSSYUT_BRIDGE_START_CLICK_VISUAL = 1U << 4U,
    ARSSYUT_BRIDGE_START_SHORTCUT_KEYS = 1U << 5U,
    ARSSYUT_BRIDGE_START_PRESENTER_CONTROLS = 1U << 6U,
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
    std::uint32_t error_code;
    std::uint32_t error_detail;
};

struct ArssyutBridgeStartRequestV1 {
    std::uint32_t struct_size;
    std::uint32_t capture_mode;
    std::uint64_t source_token;
    std::uint32_t frame_rate;
    std::uint32_t visual_mode;
    std::uint32_t flags;
    std::uint32_t reserved0;
    std::uint64_t microphone_device_token;
    std::uint64_t camera_device_token;
    wchar_t output_folder[ARSSYUT_BRIDGE_PATH_CAPACITY];
    std::uint32_t hold_zoom_modifiers;
    std::uint32_t hold_zoom_virtual_key;
    std::uint32_t overview_peek_modifiers;
    std::uint32_t overview_peek_virtual_key;
};

struct ArssyutBridgeRecorderResultV1 {
    std::uint32_t struct_size;
    std::uint32_t reserved0;
    wchar_t output_path[ARSSYUT_BRIDGE_PATH_CAPACITY];
    wchar_t diagnostics_path[ARSSYUT_BRIDGE_PATH_CAPACITY];
};

struct ArssyutBridgeOverlaySnapshotV1 {
    std::uint32_t struct_size;
    std::uint32_t capture_mode;
    std::uint64_t source_token;
    ArssyutBridgeRectV1 boundary_rect;
    ArssyutBridgeRectV1 region_rect;
    std::uint8_t visible;
    std::uint8_t editable;
    std::uint8_t region_valid;
    std::uint8_t reserved0;
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
arssyut_bridge_overlay_set_target(
    ArssyutBridgeHandle handle,
    std::uint32_t capture_mode,
    std::uint64_t source_token) noexcept;

ARSSYUT_BRIDGE_API
std::int32_t ARSSYUT_BRIDGE_CALL
arssyut_bridge_overlay_hide(
    ArssyutBridgeHandle handle) noexcept;

ARSSYUT_BRIDGE_API
std::int32_t ARSSYUT_BRIDGE_CALL
arssyut_bridge_overlay_snapshot(
    ArssyutBridgeHandle handle,
    ArssyutBridgeOverlaySnapshotV1 *snapshot) noexcept;

ARSSYUT_BRIDGE_API
std::int32_t ARSSYUT_BRIDGE_CALL
arssyut_bridge_hotkey_register(
    ArssyutBridgeHandle handle,
    std::uint32_t action,
    std::uint32_t modifiers,
    std::uint32_t virtual_key) noexcept;

ARSSYUT_BRIDGE_API
std::int32_t ARSSYUT_BRIDGE_CALL
arssyut_bridge_hotkey_unregister(
    ArssyutBridgeHandle handle,
    std::uint32_t action) noexcept;

ARSSYUT_BRIDGE_API
std::int32_t ARSSYUT_BRIDGE_CALL
arssyut_bridge_hotkey_take_events(
    ArssyutBridgeHandle handle,
    std::uint32_t *events) noexcept;

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
arssyut_bridge_recorder_start(
    ArssyutBridgeHandle handle,
    const ArssyutBridgeStartRequestV1 *request) noexcept;

ARSSYUT_BRIDGE_API
std::int32_t ARSSYUT_BRIDGE_CALL
arssyut_bridge_recorder_presenter_command(
    ArssyutBridgeHandle handle,
    std::uint32_t command) noexcept;

ARSSYUT_BRIDGE_API
std::int32_t ARSSYUT_BRIDGE_CALL
arssyut_bridge_recorder_stop(
    ArssyutBridgeHandle handle) noexcept;

ARSSYUT_BRIDGE_API
std::int32_t ARSSYUT_BRIDGE_CALL
arssyut_bridge_recorder_snapshot(
    ArssyutBridgeHandle handle,
    ArssyutBridgeRecorderSnapshotV1 *snapshot) noexcept;

ARSSYUT_BRIDGE_API
std::int32_t ARSSYUT_BRIDGE_CALL
arssyut_bridge_recorder_result(
    ArssyutBridgeHandle handle,
    ArssyutBridgeRecorderResultV1 *result) noexcept;

} // extern "C"

#endif
