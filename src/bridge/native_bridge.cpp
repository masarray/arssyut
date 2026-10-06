#include "bridge/native_bridge.h"

#ifdef _WIN32

#include "app/device_catalog.hpp"
#include "app/recorder_overlay.hpp"
#include "app/recorder_session.hpp"
#include "app/recorder_ui_model.hpp"
#include "app/region_geometry.hpp"
#include "app/source_catalog.hpp"

#include <Windows.h>
#include <ShlObj.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
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
using arssyut::app::RecorderOverlay;
using arssyut::app::RecorderOverlayCommands;
using arssyut::app::PresenterCommand;
using arssyut::app::RecorderSession;
using arssyut::app::RecorderSnapshot;
using arssyut::app::RecorderState;
using arssyut::app::RecorderTarget;
using arssyut::app::RegionCropMapping;

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

    // P6UI.4C reuses the accepted P6R overlay and Region geometry. The hidden
    // owner exists only for native Region edit/timer messages; Avalonia never
    // becomes a capture-geometry authority.
    HINSTANCE overlay_instance = nullptr;
    HWND overlay_owner = nullptr;
    DWORD overlay_thread_id = 0;
    std::unique_ptr<RecorderOverlay> overlay;
    std::uint32_t overlay_capture_mode =
        ARSSYUT_BRIDGE_CAPTURE_DISPLAY;
    std::uint64_t overlay_source_token = 0;
    bool overlay_visible = false;

    RECT region_screen_rect{};
    bool region_screen_rect_valid = false;
    HMONITOR region_monitor = nullptr;

    struct HotkeyBinding {
        std::uint32_t modifiers = 0;
        std::uint32_t virtual_key = 0;
        bool registered = false;
    };

    std::array<HotkeyBinding, 9> hotkeys{};
    std::atomic<std::uint32_t> pending_hotkey_events{0};
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
    case ARSSYUT_BRIDGE_CAPTURE_REGION:
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

constexpr wchar_t kBridgeOverlayOwnerClass[] =
    L"ArssyutBridgeOverlayOwner";
constexpr UINT_PTR kBridgeOverlayTimer = 1;
constexpr int kBridgeHotkeyIdBase = 0x5A40;
constexpr int kBridgeHotkeyProbeId = 0x5AF0;
constexpr std::uint32_t kBridgeHotkeyActionCount = 9;

[[nodiscard]] bool valid_hotkey_action(std::uint32_t action) noexcept
{
    return action < kBridgeHotkeyActionCount;
}

[[nodiscard]] bool valid_momentary_binding(
    std::uint32_t modifiers,
    std::uint32_t virtual_key) noexcept
{
    constexpr std::uint32_t valid_modifiers =
        ARSSYUT_BRIDGE_HOTKEY_CTRL |
        ARSSYUT_BRIDGE_HOTKEY_SHIFT |
        ARSSYUT_BRIDGE_HOTKEY_ALT |
        ARSSYUT_BRIDGE_HOTKEY_WIN;

    if ((modifiers & ~valid_modifiers) != 0)
        return false;

    if (virtual_key == 0)
        return modifiers == 0;

    return virtual_key <= 0xFFU;
}

[[nodiscard]] UINT windows_hotkey_modifiers(
    std::uint32_t modifiers) noexcept
{
    UINT native = MOD_NOREPEAT;
    if ((modifiers & ARSSYUT_BRIDGE_HOTKEY_CTRL) != 0) native |= MOD_CONTROL;
    if ((modifiers & ARSSYUT_BRIDGE_HOTKEY_SHIFT) != 0) native |= MOD_SHIFT;
    if ((modifiers & ARSSYUT_BRIDGE_HOTKEY_ALT) != 0) native |= MOD_ALT;
    if ((modifiers & ARSSYUT_BRIDGE_HOTKEY_WIN) != 0) native |= MOD_WIN;
    return native;
}

void dispatch_presenter_hotkey(
    NativeBridgeContext &context,
    std::uint32_t action) noexcept
{
    PresenterCommand command{};

    switch (action) {
    case ARSSYUT_BRIDGE_HOTKEY_TOGGLE_ZOOM:
        command = PresenterCommand::ToggleZoom;
        break;
    case ARSSYUT_BRIDGE_HOTKEY_ZOOM_IN:
        command = PresenterCommand::ZoomIn;
        break;
    case ARSSYUT_BRIDGE_HOTKEY_ZOOM_OUT:
        command = PresenterCommand::ZoomOut;
        break;
    case ARSSYUT_BRIDGE_HOTKEY_RESET_FULL_FRAME:
        command = PresenterCommand::ResetFullFrame;
        break;
    case ARSSYUT_BRIDGE_HOTKEY_FREEZE_CAMERA:
        command = PresenterCommand::ToggleFreezeCamera;
        break;
    default:
        return;
    }

    std::scoped_lock lock(
        context.mutex);

    if (context.recorder) {
        (void)context.recorder->
            request_presenter_command(
                command);
    }
}

void unregister_all_hotkeys(NativeBridgeContext &context) noexcept
{
    if (!context.overlay_owner)
        return;

    for (std::uint32_t action = 0;
         action < kBridgeHotkeyActionCount;
         ++action) {
        auto &binding = context.hotkeys[action];
        if (!binding.registered)
            continue;

        UnregisterHotKey(
            context.overlay_owner,
            kBridgeHotkeyIdBase + static_cast<int>(action));
        binding = NativeBridgeContext::HotkeyBinding{};
    }

    context.pending_hotkey_events.store(
        0,
        std::memory_order_relaxed);
}

void sync_overlay_locked(
    NativeBridgeContext &context) noexcept;
void commit_region_edit_locked(
    NativeBridgeContext &context) noexcept;

LRESULT CALLBACK bridge_overlay_owner_proc(
    HWND window,
    UINT message,
    WPARAM wparam,
    LPARAM lparam)
{
    auto *context =
        reinterpret_cast<NativeBridgeContext *>(
            GetWindowLongPtrW(
                window,
                GWLP_USERDATA));

    if (message == WM_NCCREATE) {
        const auto *create =
            reinterpret_cast<
                const CREATESTRUCTW *>(lparam);
        context =
            static_cast<NativeBridgeContext *>(
                create->lpCreateParams);
        SetWindowLongPtrW(
            window,
            GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(
                context));
    }

    if (!context) {
        return DefWindowProcW(
            window,
            message,
            wparam,
            lparam);
    }

    if (message == WM_HOTKEY) {
        const int action =
            static_cast<int>(wparam) -
            kBridgeHotkeyIdBase;

        if (action >= 0 &&
            action < static_cast<int>(
                kBridgeHotkeyActionCount)) {
            const auto native_action =
                static_cast<std::uint32_t>(
                    action);

            if (native_action >=
                ARSSYUT_BRIDGE_HOTKEY_TOGGLE_ZOOM) {
                // Presenter zoom is a repeat-sensitive semantic command:
                // dispatch every WM_HOTKEY directly into the recorder's
                // bounded atomic mailbox. Do not coalesce Zoom In/Out through
                // the UI event bitmask.
                dispatch_presenter_hotkey(
                    *context,
                    native_action);
            } else {
                context->pending_hotkey_events.fetch_or(
                    1U << native_action,
                    std::memory_order_relaxed);
            }
            return 0;
        }
    }

    if (message ==
        arssyut::app::kUiRegionChanged) {
        std::scoped_lock lock(
            context->mutex);
        commit_region_edit_locked(
            *context);
        return 0;
    }

    if (message == WM_TIMER &&
        wparam == kBridgeOverlayTimer) {
        std::scoped_lock lock(
            context->mutex);
        sync_overlay_locked(
            *context);
        return 0;
    }

    return DefWindowProcW(
        window,
        message,
        wparam,
        lparam);
}

[[nodiscard]] bool initialize_overlay_context(
    NativeBridgeContext &context)
{
    context.overlay_instance =
        GetModuleHandleW(
            nullptr);

    if (!context.overlay_instance)
        return false;

    WNDCLASSEXW window_class{};
    window_class.cbSize =
        sizeof(window_class);
    window_class.lpfnWndProc =
        bridge_overlay_owner_proc;
    window_class.hInstance =
        context.overlay_instance;
    window_class.lpszClassName =
        kBridgeOverlayOwnerClass;

    if (!RegisterClassExW(
            &window_class) &&
        GetLastError() !=
            ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    context.overlay_owner =
        CreateWindowExW(
            0,
            kBridgeOverlayOwnerClass,
            L"",
            0,
            0,
            0,
            0,
            0,
            HWND_MESSAGE,
            nullptr,
            context.overlay_instance,
            &context);

    if (!context.overlay_owner)
        return false;

    context.overlay_thread_id =
        GetCurrentThreadId();

    auto overlay =
        std::make_unique<
            RecorderOverlay>();

    if (!overlay->create(
            context.overlay_instance,
            context.overlay_owner,
            RecorderOverlayCommands{})) {
        DestroyWindow(
            context.overlay_owner);
        context.overlay_owner =
            nullptr;
        context.overlay_thread_id =
            0;
        return false;
    }

    context.overlay =
        std::move(overlay);
    return true;
}

void shutdown_overlay_context(
    NativeBridgeContext &context) noexcept
{
    if (!context.overlay_owner)
        return;

    KillTimer(
        context.overlay_owner,
        kBridgeOverlayTimer);

    unregister_all_hotkeys(context);

    if (context.overlay) {
        context.overlay->
            hide_toolbar();
        context.overlay->
            hide_boundary();
        context.overlay.reset();
    }

    SetWindowLongPtrW(
        context.overlay_owner,
        GWLP_USERDATA,
        0);

    DestroyWindow(
        context.overlay_owner);

    context.overlay_owner =
        nullptr;
    context.overlay_thread_id =
        0;
}

[[nodiscard]] bool ensure_region_mapping_locked(
    NativeBridgeContext &context,
    const RecorderTarget &target,
    RegionCropMapping &mapping) noexcept
{
    if (target.kind !=
        arssyut::windows::
            CaptureTargetKind::Monitor) {
        return false;
    }

    RECT bounds{};
    if (!arssyut::app::
            recorder_target_screen_rect(
                target,
                bounds)) {
        return false;
    }

    RECT candidate{};

    if (context.
            region_screen_rect_valid &&
        context.region_monitor ==
            target.monitor) {
        candidate =
            context.region_screen_rect;
    } else {
        candidate =
            arssyut::app::
                default_region_rect(
                    bounds);
    }

    candidate =
        arssyut::app::
            clamp_region_rect(
                candidate,
                bounds);

    if (!arssyut::app::
            map_region_to_crop(
                bounds,
                candidate,
                mapping)) {
        return false;
    }

    context.region_screen_rect =
        mapping.screen_rect;
    context.region_screen_rect_valid =
        true;
    context.region_monitor =
        target.monitor;

    return true;
}

void sync_overlay_locked(
    NativeBridgeContext &context) noexcept
{
    if (!context.overlay ||
        !context.overlay_owner ||
        !context.overlay_visible ||
        context.overlay_source_token == 0) {
        if (context.overlay)
            context.overlay->
                hide_boundary();
        if (context.overlay_owner) {
            KillTimer(
                context.overlay_owner,
                kBridgeOverlayTimer);
        }
        return;
    }

    const auto *target =
        resolve_source(
            context,
            context.overlay_source_token);

    if (!target ||
        !source_matches_mode(
            *target,
            context.overlay_capture_mode)) {
        context.overlay->
            hide_boundary();
        context.overlay_visible =
            false;
        context.overlay_source_token =
            0;
        KillTimer(
            context.overlay_owner,
            kBridgeOverlayTimer);
        return;
    }

    RECT base_rect{};

    if (context.overlay_capture_mode ==
        ARSSYUT_BRIDGE_CAPTURE_REGION) {
        RegionCropMapping mapping;
        if (!ensure_region_mapping_locked(
                context,
                *target,
                mapping)) {
            context.overlay->
                hide_boundary();
            return;
        }
        base_rect =
            mapping.screen_rect;
    } else if (!arssyut::app::
                   recorder_target_screen_rect(
                       *target,
                       base_rect)) {
        context.overlay->
            hide_boundary();
        return;
    }

    RecorderSnapshot recorder_snapshot{};
    bool recording = false;

    if (context.recorder) {
        recorder_snapshot =
            context.recorder->
                snapshot();
        recording =
            active_state(
                recorder_snapshot.state);
    }

    RECT visible_rect =
        base_rect;

    if (recording) {
        visible_rect =
            arssyut::app::
                camera_viewport_rect(
                    base_rect,
                    recorder_snapshot.
                        presentation_camera_center_x,
                    recorder_snapshot.
                        presentation_camera_center_y,
                    recorder_snapshot.
                        presentation_camera_zoom);
    }

    const bool editable =
        context.overlay_capture_mode ==
            ARSSYUT_BRIDGE_CAPTURE_REGION &&
        !recording;

    context.overlay->
        show_boundary(
            visible_rect,
            recording,
            editable);

    if (recording) {
        SetTimer(
            context.overlay_owner,
            kBridgeOverlayTimer,
            33,
            nullptr);
    } else {
        KillTimer(
            context.overlay_owner,
            kBridgeOverlayTimer);
    }
}

void commit_region_edit_locked(
    NativeBridgeContext &context) noexcept
{
    if (!context.overlay ||
        !context.overlay_visible ||
        context.overlay_capture_mode !=
            ARSSYUT_BRIDGE_CAPTURE_REGION ||
        !context.overlay->
            boundary_editable()) {
        return;
    }

    const auto *target =
        resolve_source(
            context,
            context.overlay_source_token);

    if (!target ||
        target->kind !=
            arssyut::windows::
                CaptureTargetKind::Monitor) {
        return;
    }

    RECT bounds{};
    if (!arssyut::app::
            recorder_target_screen_rect(
                *target,
                bounds)) {
        return;
    }

    const RECT candidate =
        arssyut::app::
            clamp_region_rect(
                context.overlay->
                    boundary_rect(),
                bounds);

    RegionCropMapping mapping;
    if (!arssyut::app::
            map_region_to_crop(
                bounds,
                candidate,
                mapping)) {
        return;
    }

    context.region_screen_rect =
        mapping.screen_rect;
    context.region_screen_rect_valid =
        true;
    context.region_monitor =
        target->monitor;

    context.overlay->
        show_boundary(
            mapping.screen_rect,
            false,
            true);
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
        auto context =
            std::make_unique<
                NativeBridgeContext>();

        if (!initialize_overlay_context(
                *context)) {
            return nullptr;
        }

        return context.release();
    } catch (...) {
        return nullptr;
    }
}

void ARSSYUT_BRIDGE_CALL
arssyut_bridge_destroy(
    ArssyutBridgeHandle handle) noexcept
{
    auto *context =
        as_context(handle);

    if (!context)
        return;

    if (context->overlay_thread_id != 0 &&
        context->overlay_thread_id !=
            GetCurrentThreadId()) {
        // HWND ownership is UI-thread-affine. Normal Avalonia shutdown disposes
        // on the owner thread; an off-thread GC fallback intentionally leaks
        // until process teardown instead of risking a dangling native WndProc.
        return;
    }

    shutdown_overlay_context(
        *context);
    delete context;
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

        if (context->overlay) {
            context->overlay->
                hide_boundary();
        }
        if (context->overlay_owner) {
            KillTimer(
                context->overlay_owner,
                kBridgeOverlayTimer);
        }
        context->overlay_visible =
            false;
        context->overlay_source_token =
            0;

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
    if (!arssyut::app::
            recorder_target_screen_rect(
                native,
                rect)) {
        return ARSSYUT_BRIDGE_INTERNAL_ERROR;
    }

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
arssyut_bridge_overlay_set_target(
    ArssyutBridgeHandle handle,
    std::uint32_t capture_mode,
    std::uint64_t source_token) noexcept
{
    auto *context =
        as_context(handle);
    if (!context || source_token == 0)
        return ARSSYUT_BRIDGE_INVALID_ARGUMENT;

    if (capture_mode ==
        ARSSYUT_BRIDGE_CAPTURE_GAME) {
        return ARSSYUT_BRIDGE_UNSUPPORTED;
    }

    std::scoped_lock lock(
        context->mutex);

    const auto *target =
        resolve_source(
            *context,
            source_token);

    if (!target)
        return ARSSYUT_BRIDGE_STALE_TOKEN;

    if (!source_matches_mode(
            *target,
            capture_mode)) {
        return ARSSYUT_BRIDGE_INVALID_ARGUMENT;
    }

    context->overlay_capture_mode =
        capture_mode;
    context->overlay_source_token =
        source_token;
    context->overlay_visible =
        true;

    sync_overlay_locked(
        *context);

    return ARSSYUT_BRIDGE_OK;
}

std::int32_t ARSSYUT_BRIDGE_CALL
arssyut_bridge_overlay_hide(
    ArssyutBridgeHandle handle) noexcept
{
    auto *context =
        as_context(handle);
    if (!context)
        return ARSSYUT_BRIDGE_INVALID_ARGUMENT;

    std::scoped_lock lock(
        context->mutex);

    context->overlay_visible =
        false;
    context->overlay_source_token =
        0;

    if (context->overlay) {
        context->overlay->
            hide_boundary();
    }

    if (context->overlay_owner) {
        KillTimer(
            context->overlay_owner,
            kBridgeOverlayTimer);
    }

    return ARSSYUT_BRIDGE_OK;
}

std::int32_t ARSSYUT_BRIDGE_CALL
arssyut_bridge_overlay_snapshot(
    ArssyutBridgeHandle handle,
    ArssyutBridgeOverlaySnapshotV1 *snapshot) noexcept
{
    auto *context =
        as_context(handle);

    if (!context || !snapshot)
        return ARSSYUT_BRIDGE_INVALID_ARGUMENT;

    if (snapshot->struct_size <
        sizeof(
            ArssyutBridgeOverlaySnapshotV1)) {
        return ARSSYUT_BRIDGE_INVALID_ARGUMENT;
    }

    std::scoped_lock lock(
        context->mutex);

    *snapshot =
        ArssyutBridgeOverlaySnapshotV1{};
    snapshot->struct_size =
        sizeof(
            ArssyutBridgeOverlaySnapshotV1);
    snapshot->capture_mode =
        context->overlay_capture_mode;
    snapshot->source_token =
        context->overlay_source_token;
    snapshot->visible =
        context->overlay_visible
            ? 1
            : 0;
    snapshot->region_valid =
        context->
            region_screen_rect_valid
            ? 1
            : 0;

    if (context->
            region_screen_rect_valid) {
        snapshot->region_rect =
            to_bridge_rect(
                context->
                    region_screen_rect);
    }

    if (context->overlay &&
        context->overlay_visible) {
        snapshot->boundary_rect =
            to_bridge_rect(
                context->overlay->
                    boundary_rect());
        snapshot->editable =
            context->overlay->
                boundary_editable()
                ? 1
                : 0;
    }

    return ARSSYUT_BRIDGE_OK;
}


std::int32_t ARSSYUT_BRIDGE_CALL
arssyut_bridge_hotkey_register(
    ArssyutBridgeHandle handle,
    std::uint32_t action,
    std::uint32_t modifiers,
    std::uint32_t virtual_key) noexcept
{
    auto *context = as_context(handle);
    if (!context ||
        !valid_hotkey_action(action) ||
        virtual_key == 0 ||
        virtual_key > 0xFFU)
        return ARSSYUT_BRIDGE_INVALID_ARGUMENT;

    constexpr std::uint32_t valid_modifiers =
        ARSSYUT_BRIDGE_HOTKEY_CTRL |
        ARSSYUT_BRIDGE_HOTKEY_SHIFT |
        ARSSYUT_BRIDGE_HOTKEY_ALT |
        ARSSYUT_BRIDGE_HOTKEY_WIN;

    if ((modifiers & ~valid_modifiers) != 0)
        return ARSSYUT_BRIDGE_INVALID_ARGUMENT;

    if (!context->overlay_owner ||
        context->overlay_thread_id != GetCurrentThreadId())
        return ARSSYUT_BRIDGE_INVALID_STATE;

    auto &binding = context->hotkeys[action];
    if (binding.registered &&
        binding.modifiers == modifiers &&
        binding.virtual_key == virtual_key)
        return ARSSYUT_BRIDGE_OK;

    const auto previous = binding;
    if (binding.registered) {
        UnregisterHotKey(
            context->overlay_owner,
            kBridgeHotkeyIdBase + static_cast<int>(action));
        binding = NativeBridgeContext::HotkeyBinding{};
    }

    if (!RegisterHotKey(
            context->overlay_owner,
            kBridgeHotkeyIdBase + static_cast<int>(action),
            windows_hotkey_modifiers(modifiers),
            virtual_key)) {
        const DWORD error = GetLastError();

        if (previous.registered &&
            RegisterHotKey(
                context->overlay_owner,
                kBridgeHotkeyIdBase + static_cast<int>(action),
                windows_hotkey_modifiers(previous.modifiers),
                previous.virtual_key)) {
            binding = previous;
        }

        return error == ERROR_HOTKEY_ALREADY_REGISTERED
            ? ARSSYUT_BRIDGE_BUSY
            : ARSSYUT_BRIDGE_INTERNAL_ERROR;
    }

    binding.modifiers = modifiers;
    binding.virtual_key = virtual_key;
    binding.registered = true;
    return ARSSYUT_BRIDGE_OK;
}

std::int32_t ARSSYUT_BRIDGE_CALL
arssyut_bridge_hotkey_probe(
    ArssyutBridgeHandle handle,
    std::uint32_t modifiers,
    std::uint32_t virtual_key) noexcept
{
    auto *context = as_context(handle);
    if (!context ||
        virtual_key == 0 ||
        virtual_key > 0xFFU ||
        !valid_momentary_binding(
            modifiers,
            virtual_key)) {
        return ARSSYUT_BRIDGE_INVALID_ARGUMENT;
    }

    if (!context->overlay_owner ||
        context->overlay_thread_id != GetCurrentThreadId()) {
        return ARSSYUT_BRIDGE_INVALID_STATE;
    }

    if (!RegisterHotKey(
            context->overlay_owner,
            kBridgeHotkeyProbeId,
            windows_hotkey_modifiers(modifiers),
            virtual_key)) {
        const DWORD error = GetLastError();
        return error == ERROR_HOTKEY_ALREADY_REGISTERED
            ? ARSSYUT_BRIDGE_BUSY
            : ARSSYUT_BRIDGE_INTERNAL_ERROR;
    }

    UnregisterHotKey(
        context->overlay_owner,
        kBridgeHotkeyProbeId);
    return ARSSYUT_BRIDGE_OK;
}

std::int32_t ARSSYUT_BRIDGE_CALL
arssyut_bridge_hotkey_unregister(
    ArssyutBridgeHandle handle,
    std::uint32_t action) noexcept
{
    auto *context = as_context(handle);
    if (!context || !valid_hotkey_action(action))
        return ARSSYUT_BRIDGE_INVALID_ARGUMENT;

    if (!context->overlay_owner ||
        context->overlay_thread_id != GetCurrentThreadId())
        return ARSSYUT_BRIDGE_INVALID_STATE;

    auto &binding = context->hotkeys[action];
    if (binding.registered) {
        UnregisterHotKey(
            context->overlay_owner,
            kBridgeHotkeyIdBase + static_cast<int>(action));
        binding = NativeBridgeContext::HotkeyBinding{};
    }

    context->pending_hotkey_events.fetch_and(
        ~(1U << action),
        std::memory_order_relaxed);
    return ARSSYUT_BRIDGE_OK;
}

std::int32_t ARSSYUT_BRIDGE_CALL
arssyut_bridge_hotkey_take_events(
    ArssyutBridgeHandle handle,
    std::uint32_t *events) noexcept
{
    auto *context = as_context(handle);
    if (!context || !events)
        return ARSSYUT_BRIDGE_INVALID_ARGUMENT;

    *events = context->pending_hotkey_events.exchange(
        0,
        std::memory_order_acq_rel);
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

    if (!valid_momentary_binding(
            request->hold_zoom_modifiers,
            request->hold_zoom_virtual_key) ||
        !valid_momentary_binding(
            request->overview_peek_modifiers,
            request->overview_peek_virtual_key)) {
        return ARSSYUT_BRIDGE_INVALID_ARGUMENT;
    }

    if (!std::isfinite(
            request->presenter_zoom) ||
        request->presenter_zoom < 1.10f ||
        request->presenter_zoom > 4.00f) {
        return ARSSYUT_BRIDGE_INVALID_ARGUMENT;
    }

    if (request->spotlight_size >
        ARSSYUT_BRIDGE_SPOTLIGHT_WIDE) {
        return ARSSYUT_BRIDGE_INVALID_ARGUMENT;
    }

    if (request->spotlight_motion >
        ARSSYUT_BRIDGE_SPOTLIGHT_SNAPPY) {
        return ARSSYUT_BRIDGE_INVALID_ARGUMENT;
    }

    if (!std::isfinite(
            request->spotlight_dim_strength) ||
        request->spotlight_dim_strength < 0.0f ||
        request->spotlight_dim_strength > 0.75f) {
        return ARSSYUT_BRIDGE_INVALID_ARGUMENT;
    }

    if (request->capture_mode ==
        ARSSYUT_BRIDGE_CAPTURE_GAME) {
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
        RegionCropMapping region_mapping{};
        bool have_region_mapping = false;

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

            if (request->capture_mode ==
                ARSSYUT_BRIDGE_CAPTURE_REGION) {
                if (!ensure_region_mapping_locked(
                        *context,
                        *resolved,
                        region_mapping)) {
                    return
                        ARSSYUT_BRIDGE_INVALID_ARGUMENT;
                }
                have_region_mapping =
                    true;
            }

            context->overlay_capture_mode =
                request->capture_mode;
            context->overlay_source_token =
                request->source_token;
            context->overlay_visible =
                true;

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

        if (have_region_mapping) {
            config.crop =
                region_mapping.crop;
            config.output_size =
                region_mapping.output_size;
            config.presentation_screen_rect =
                region_mapping.screen_rect;
            config.
                presentation_screen_rect_valid =
                    true;
        }

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
        config.presentation.presenter_controls =
            (request->flags &
             ARSSYUT_BRIDGE_START_PRESENTER_CONTROLS) != 0 ||
            request->hold_zoom_virtual_key != 0 ||
            request->overview_peek_virtual_key != 0;
        config.presentation.zoom =
            request->presenter_zoom;

        config.presentation.spotlight.enabled =
            (request->flags &
             ARSSYUT_BRIDGE_START_SPOTLIGHT) != 0;
        config.presentation.spotlight.link_to_zoom =
            (request->flags &
             ARSSYUT_BRIDGE_START_SPOTLIGHT_LINK_TO_ZOOM) != 0;
        config.presentation.spotlight.size =
            static_cast<
                arssyut::presentation::SpotlightSize>(
                    request->spotlight_size);
        config.presentation.spotlight.cinematic_speed =
            static_cast<
                arssyut::presentation::SpotlightCinematicSpeed>(
                    request->spotlight_motion);
        config.presentation.spotlight.dim_strength =
            request->spotlight_dim_strength;
        config.hold_zoom_hotkey.virtual_key =
            static_cast<std::uint16_t>(
                request->hold_zoom_virtual_key);
        config.hold_zoom_hotkey.modifiers =
            static_cast<std::uint8_t>(
                request->hold_zoom_modifiers);
        config.overview_peek_hotkey.virtual_key =
            static_cast<std::uint16_t>(
                request->overview_peek_virtual_key);
        config.overview_peek_hotkey.modifiers =
            static_cast<std::uint8_t>(
                request->overview_peek_modifiers);

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

            sync_overlay_locked(
                *context);
        }

        return ARSSYUT_BRIDGE_OK;
    } catch (...) {
        return ARSSYUT_BRIDGE_INTERNAL_ERROR;
    }
}

std::int32_t ARSSYUT_BRIDGE_CALL
arssyut_bridge_recorder_presenter_command(
    ArssyutBridgeHandle handle,
    std::uint32_t command) noexcept
{
    auto *context =
        as_context(handle);
    if (!context)
        return ARSSYUT_BRIDGE_INVALID_ARGUMENT;

    PresenterCommand native_command{};
    switch (command) {
    case ARSSYUT_BRIDGE_PRESENTER_TOGGLE_ZOOM:
        native_command = PresenterCommand::ToggleZoom;
        break;
    case ARSSYUT_BRIDGE_PRESENTER_ZOOM_IN:
        native_command = PresenterCommand::ZoomIn;
        break;
    case ARSSYUT_BRIDGE_PRESENTER_ZOOM_OUT:
        native_command = PresenterCommand::ZoomOut;
        break;
    case ARSSYUT_BRIDGE_PRESENTER_RESET_FULL_FRAME:
        native_command = PresenterCommand::ResetFullFrame;
        break;
    case ARSSYUT_BRIDGE_PRESENTER_TOGGLE_FREEZE_CAMERA:
        native_command = PresenterCommand::ToggleFreezeCamera;
        break;
    default:
        return ARSSYUT_BRIDGE_INVALID_ARGUMENT;
    }

    std::scoped_lock lock(
        context->mutex);

    if (!context->recorder)
        return ARSSYUT_BRIDGE_INVALID_STATE;

    return context->recorder->
               request_presenter_command(
                   native_command)
        ? ARSSYUT_BRIDGE_OK
        : ARSSYUT_BRIDGE_INVALID_STATE;
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

    sync_overlay_locked(
        *context);

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
