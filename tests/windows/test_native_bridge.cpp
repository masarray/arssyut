#include "bridge/native_bridge.h"

#ifdef _WIN32

#include <Windows.h>

#include <cstdint>
#include <cstdlib>
#include <cwchar>
#include <iostream>

namespace {

[[noreturn]] void fail(
    const char *message)
{
    std::cerr << message << '\n';
    std::exit(1);
}

void require(
    bool condition,
    const char *message)
{
    if (!condition)
        fail(message);
}


struct WindowSearch {
    DWORD process_id = 0;
    const wchar_t *class_name = nullptr;
    HWND window = nullptr;
};

BOOL CALLBACK find_window_proc(
    HWND window,
    LPARAM parameter)
{
    auto *search =
        reinterpret_cast<
            WindowSearch *>(parameter);

    DWORD process_id = 0;
    GetWindowThreadProcessId(
        window,
        &process_id);

    if (process_id !=
        search->process_id) {
        return TRUE;
    }

    wchar_t class_name[128]{};
    GetClassNameW(
        window,
        class_name,
        128);

    if (std::wcscmp(
            class_name,
            search->class_name) == 0) {
        search->window =
            window;
        return FALSE;
    }

    return TRUE;
}

[[nodiscard]] HWND find_process_window(
    const wchar_t *class_name)
{
    WindowSearch search;
    search.process_id =
        GetCurrentProcessId();
    search.class_name =
        class_name;

    EnumWindows(
        find_window_proc,
        reinterpret_cast<LPARAM>(
            &search));

    return search.window;
}

[[nodiscard]] LPARAM screen_point(
    int x,
    int y) noexcept
{
    return MAKELPARAM(
        static_cast<WORD>(
            static_cast<SHORT>(x)),
        static_cast<WORD>(
            static_cast<SHORT>(y)));
}

void drain_messages()
{
    MSG message{};
    while (PeekMessageW(
               &message,
               nullptr,
               0,
               0,
               PM_REMOVE)) {
        TranslateMessage(
            &message);
        DispatchMessageW(
            &message);
    }
}

} // namespace

int main()
{
    require(
        arssyut_bridge_abi_version() ==
            ARSSYUT_BRIDGE_ABI_VERSION &&
            ARSSYUT_BRIDGE_ABI_VERSION == 10,
        "bridge ABI version mismatch");

    require(
        ARSSYUT_BRIDGE_RECORDER_ARMED == 2 &&
            ARSSYUT_BRIDGE_RECORDER_RECORDING == 3,
        "Armed Start state ordering must stay explicit across ABI 10");

    ArssyutBridgeHandle bridge =
        arssyut_bridge_create();
    require(
        bridge != nullptr,
        "bridge context creation failed");

    const auto destroy =
        [&]() noexcept {
            arssyut_bridge_destroy(
                bridge);
        };

    require(
        arssyut_bridge_refresh_sources(
            bridge,
            0) ==
            ARSSYUT_BRIDGE_OK,
        "source refresh failed");

    std::uint32_t source_count = 0;
    require(
        arssyut_bridge_source_count(
            bridge,
            &source_count) ==
            ARSSYUT_BRIDGE_OK,
        "source count failed");

    if (source_count > 0) {
        ArssyutBridgeSourceV1 source{};
        source.struct_size =
            sizeof(source);

        require(
            arssyut_bridge_source_at(
                bridge,
                0,
                &source) ==
                ARSSYUT_BRIDGE_OK,
            "source snapshot read failed");

        require(
            source.token != 0,
            "source token must be non-zero");
        require(
            source.kind ==
                    ARSSYUT_BRIDGE_SOURCE_MONITOR ||
                source.kind ==
                    ARSSYUT_BRIDGE_SOURCE_WINDOW,
            "source kind is invalid");
        require(
            source.label[0] != L'\0',
            "source label must not be empty");
    }


    ArssyutBridgeSourceV1 monitor_source{};
    bool have_monitor_source = false;

    for (std::uint32_t index = 0;
         index < source_count;
         ++index) {
        ArssyutBridgeSourceV1 candidate{};
        candidate.struct_size =
            sizeof(candidate);

        require(
            arssyut_bridge_source_at(
                bridge,
                index,
                &candidate) ==
                ARSSYUT_BRIDGE_OK,
            "source read for overlay test failed");

        if (candidate.kind ==
            ARSSYUT_BRIDGE_SOURCE_MONITOR) {
            monitor_source =
                candidate;
            have_monitor_source =
                true;
            break;
        }
    }

    require(
        have_monitor_source,
        "native bridge must expose a monitor for overlay regression");

    require(
        arssyut_bridge_overlay_set_target(
            bridge,
            ARSSYUT_BRIDGE_CAPTURE_DISPLAY,
            monitor_source.token) ==
            ARSSYUT_BRIDGE_OK,
        "Display overlay target failed");

    HWND boundary =
        find_process_window(
            L"ArssyutCaptureBoundary");

    require(
        boundary != nullptr,
        "native capture boundary window not found");
    require(
        IsWindowVisible(
            boundary) != FALSE,
        "Display capture boundary must be visible");

    const auto boundary_class_style =
        GetClassLongPtrW(
            boundary,
            GCL_STYLE);

    require(
        (boundary_class_style & CS_HREDRAW) != 0 &&
        (boundary_class_style & CS_VREDRAW) != 0,
        "Region boundary class must fully redraw on interactive resize");

    const LONG_PTR display_style =
        GetWindowLongPtrW(
            boundary,
            GWL_EXSTYLE);

    require(
        (display_style &
         WS_EX_TRANSPARENT) != 0,
        "Display boundary must carry WS_EX_TRANSPARENT");

    RECT display_rect{};
    require(
        GetWindowRect(
            boundary,
            &display_rect) != FALSE,
        "Display boundary rect unavailable");

    const auto display_hit =
        SendMessageW(
            boundary,
            WM_NCHITTEST,
            0,
            screen_point(
                display_rect.left +
                    (display_rect.right -
                     display_rect.left) /
                        2,
                display_rect.top +
                    (display_rect.bottom -
                     display_rect.top) /
                        2));

    require(
        display_hit ==
            HTTRANSPARENT,
        "Display viewport border must be click-through");

    require(
        arssyut_bridge_overlay_set_target(
            bridge,
            ARSSYUT_BRIDGE_CAPTURE_REGION,
            monitor_source.token) ==
            ARSSYUT_BRIDGE_OK,
        "Region overlay target failed");

    ArssyutBridgeOverlaySnapshotV1 region_before{};
    region_before.struct_size =
        sizeof(region_before);

    require(
        arssyut_bridge_overlay_snapshot(
            bridge,
            &region_before) ==
            ARSSYUT_BRIDGE_OK,
        "Region overlay snapshot failed");
    require(
        region_before.visible != 0 &&
            region_before.editable != 0 &&
            region_before.region_valid != 0,
        "Region overlay must be visible, editable and valid while idle");

    const LONG_PTR region_style =
        GetWindowLongPtrW(
            boundary,
            GWL_EXSTYLE);

    require(
        (region_style &
         WS_EX_TRANSPARENT) == 0,
        "editable Region must enable native edge/pill input");

    RECT region_rect{};
    require(
        GetWindowRect(
            boundary,
            &region_rect) != FALSE,
        "Region boundary rect unavailable");

    const int region_mid_y =
        region_rect.top +
        (region_rect.bottom -
         region_rect.top) /
            2;

    const auto region_center_hit =
        SendMessageW(
            boundary,
            WM_NCHITTEST,
            0,
            screen_point(
                region_rect.left +
                    (region_rect.right -
                     region_rect.left) /
                        2,
                region_mid_y));

    require(
        region_center_hit ==
            HTTRANSPARENT,
        "Region interior must remain click-through");

    const auto region_edge_hit =
        SendMessageW(
            boundary,
            WM_NCHITTEST,
            0,
            screen_point(
                region_rect.left + 2,
                region_mid_y));

    require(
        region_edge_hit ==
            HTLEFT,
        "Region left edge must remain a resize handle");

    const int region_width =
        region_rect.right -
        region_rect.left;
    const int region_height =
        region_rect.bottom -
        region_rect.top;

    require(
        SetWindowPos(
            boundary,
            nullptr,
            region_rect.left + 20,
            region_rect.top + 20,
            region_width,
            region_height,
            SWP_NOZORDER |
                SWP_NOACTIVATE) != FALSE,
        "Region synthetic move failed");

    SendMessageW(
        boundary,
        WM_EXITSIZEMOVE,
        0,
        0);
    drain_messages();

    ArssyutBridgeOverlaySnapshotV1 region_after{};
    region_after.struct_size =
        sizeof(region_after);

    require(
        arssyut_bridge_overlay_snapshot(
            bridge,
            &region_after) ==
            ARSSYUT_BRIDGE_OK,
        "Region post-edit snapshot failed");

    require(
        region_after.region_rect.left !=
                region_before.region_rect.left ||
            region_after.region_rect.top !=
                region_before.region_rect.top,
        "Region move must publish the canonical native rectangle");

    require(
        arssyut_bridge_overlay_set_target(
            bridge,
            ARSSYUT_BRIDGE_CAPTURE_DISPLAY,
            monitor_source.token) ==
            ARSSYUT_BRIDGE_OK,
        "Region to Display switch failed");

    require(
        arssyut_bridge_overlay_set_target(
            bridge,
            ARSSYUT_BRIDGE_CAPTURE_REGION,
            monitor_source.token) ==
            ARSSYUT_BRIDGE_OK,
        "Display to Region switch failed");

    ArssyutBridgeOverlaySnapshotV1 region_restored{};
    region_restored.struct_size =
        sizeof(region_restored);

    require(
        arssyut_bridge_overlay_snapshot(
            bridge,
            &region_restored) ==
            ARSSYUT_BRIDGE_OK,
        "restored Region snapshot failed");

    require(
        region_restored.region_rect.left ==
                region_after.region_rect.left &&
            region_restored.region_rect.top ==
                region_after.region_rect.top &&
            region_restored.region_rect.right ==
                region_after.region_rect.right &&
            region_restored.region_rect.bottom ==
                region_after.region_rect.bottom,
        "Region -> Display -> Region must preserve canonical geometry");

    require(
        arssyut_bridge_overlay_hide(
            bridge) ==
            ARSSYUT_BRIDGE_OK,
        "overlay hide failed");

    require(
        arssyut_bridge_recorder_presenter_command(
            bridge,
            ARSSYUT_BRIDGE_PRESENTER_TOGGLE_ZOOM) ==
            ARSSYUT_BRIDGE_INVALID_STATE,
        "presenter command must reject idle recorder state");

    require(
        arssyut_bridge_recorder_presenter_command(
            bridge,
            ARSSYUT_BRIDGE_PRESENTER_TOGGLE_FREEZE_CAMERA) ==
            ARSSYUT_BRIDGE_INVALID_STATE,
        "Freeze Camera command must use the same recorder-state gate");

    constexpr std::uint32_t hotkey_modifiers =
        ARSSYUT_BRIDGE_HOTKEY_CTRL |
        ARSSYUT_BRIDGE_HOTKEY_SHIFT;

    require(
        arssyut_bridge_hotkey_register(
            bridge,
            ARSSYUT_BRIDGE_HOTKEY_TOGGLE_RECORD,
            hotkey_modifiers,
            VK_F24) == ARSSYUT_BRIDGE_OK,
        "global Record hotkey registration failed");

    auto second_bridge = arssyut_bridge_create();
    require(second_bridge != nullptr,
        "second bridge for hotkey conflict test failed");

    require(
        arssyut_bridge_hotkey_register(
            second_bridge,
            ARSSYUT_BRIDGE_HOTKEY_TOGGLE_RECORD,
            hotkey_modifiers,
            VK_F24) == ARSSYUT_BRIDGE_BUSY,
        "duplicate Windows global hotkey must report Busy");

    require(
        arssyut_bridge_hotkey_probe(
            second_bridge,
            hotkey_modifiers,
            VK_F24) == ARSSYUT_BRIDGE_BUSY,
        "hotkey probe must report a Windows registration conflict");

    require(
        arssyut_bridge_hotkey_register(
            bridge,
            ARSSYUT_BRIDGE_HOTKEY_TOGGLE_ZOOM,
            ARSSYUT_BRIDGE_HOTKEY_CTRL |
                ARSSYUT_BRIDGE_HOTKEY_ALT,
            VK_F23) == ARSSYUT_BRIDGE_OK,
        "global Toggle Zoom hotkey registration failed");

    require(
        arssyut_bridge_hotkey_unregister(
            bridge,
            ARSSYUT_BRIDGE_HOTKEY_TOGGLE_ZOOM) == ARSSYUT_BRIDGE_OK,
        "global Toggle Zoom hotkey unregister failed");

    require(
        arssyut_bridge_hotkey_register(
            bridge,
            ARSSYUT_BRIDGE_HOTKEY_FREEZE_CAMERA,
            ARSSYUT_BRIDGE_HOTKEY_CTRL |
                ARSSYUT_BRIDGE_HOTKEY_SHIFT,
            VK_F22) == ARSSYUT_BRIDGE_OK,
        "Freeze Camera must register through the canonical global hotkey bridge");

    require(
        arssyut_bridge_hotkey_unregister(
            bridge,
            ARSSYUT_BRIDGE_HOTKEY_FREEZE_CAMERA) == ARSSYUT_BRIDGE_OK,
        "Freeze Camera hotkey unregister failed");

    require(
        arssyut_bridge_hotkey_unregister(
            bridge,
            ARSSYUT_BRIDGE_HOTKEY_TOGGLE_RECORD) == ARSSYUT_BRIDGE_OK,
        "global Record hotkey unregister failed");

    require(
        arssyut_bridge_hotkey_register(
            second_bridge,
            ARSSYUT_BRIDGE_HOTKEY_TOGGLE_RECORD,
            hotkey_modifiers,
            VK_F24) == ARSSYUT_BRIDGE_OK,
        "released global hotkey must be reusable");

    require(
        arssyut_bridge_hotkey_unregister(
            second_bridge,
            ARSSYUT_BRIDGE_HOTKEY_TOGGLE_RECORD) == ARSSYUT_BRIDGE_OK,
        "second global Record hotkey unregister failed");

    require(
        arssyut_bridge_hotkey_probe(
            second_bridge,
            hotkey_modifiers,
            VK_F24) == ARSSYUT_BRIDGE_OK,
        "hotkey probe must succeed after the conflicting binding is released");

    std::uint32_t pending_hotkeys = 0;
    require(
        arssyut_bridge_hotkey_take_events(
            second_bridge,
            &pending_hotkeys) == ARSSYUT_BRIDGE_OK &&
        pending_hotkeys == 0,
        "fresh hotkey event queue must be empty");

    arssyut_bridge_destroy(second_bridge);

    require(
        arssyut_bridge_refresh_devices(
            bridge) ==
            ARSSYUT_BRIDGE_OK,
        "device refresh failed");

    for (const std::uint32_t kind : {
             static_cast<std::uint32_t>(
                 ARSSYUT_BRIDGE_DEVICE_MICROPHONE),
             static_cast<std::uint32_t>(
                 ARSSYUT_BRIDGE_DEVICE_CAMERA)}) {
        std::uint32_t count = 0;
        require(
            arssyut_bridge_device_count(
                bridge,
                kind,
                &count) ==
                ARSSYUT_BRIDGE_OK,
            "device count failed");

        if (count > 0) {
            ArssyutBridgeDeviceV1 device{};
            device.struct_size =
                sizeof(device);

            require(
                arssyut_bridge_device_at(
                    bridge,
                    kind,
                    0,
                    &device) ==
                    ARSSYUT_BRIDGE_OK,
                "device snapshot read failed");

            require(
                device.token != 0,
                "device token must be non-zero");
            require(
                device.name[0] != L'\0',
                "device name must not be empty");
        }
    }

    ArssyutBridgeRecorderSnapshotV1 snapshot{};
    snapshot.struct_size =
        sizeof(snapshot);

    require(
        arssyut_bridge_recorder_snapshot(
            bridge,
            &snapshot) ==
            ARSSYUT_BRIDGE_OK,
        "recorder snapshot failed");

    require(
        snapshot.state ==
            ARSSYUT_BRIDGE_RECORDER_IDLE,
        "new bridge recorder must start Idle");
    require(
        snapshot.worker_finished != 0,
        "new bridge recorder worker must be finished");
    require(
        snapshot.camera_zoom >= 1.0f,
        "default camera zoom must be valid");

    // P6UI.4B command semantics: stale tokens and unsupported inputs must
    // fail before the recorder starts, rather than silently selecting another
    // source or pretending an unavailable media stream was recorded.
    ArssyutBridgeStartRequestV1 invalid_start{};
    invalid_start.struct_size =
        sizeof(invalid_start);
    invalid_start.capture_mode =
        ARSSYUT_BRIDGE_CAPTURE_DISPLAY;
    invalid_start.source_token =
        0;
    invalid_start.frame_rate =
        60;
    invalid_start.visual_mode =
        ARSSYUT_BRIDGE_VISUAL_PIXEL_ACCURATE;
    invalid_start.flags =
        ARSSYUT_BRIDGE_START_SMART_ZOOM |
        ARSSYUT_BRIDGE_START_CLICK_VISUAL |
        ARSSYUT_BRIDGE_START_SHORTCUT_KEYS;
    invalid_start.presenter_zoom =
        2.0f;

    invalid_start.hold_zoom_modifiers =
        0x80000000U;
    invalid_start.hold_zoom_virtual_key =
        VK_F12;

    require(
        arssyut_bridge_recorder_start(
            bridge,
            &invalid_start) ==
            ARSSYUT_BRIDGE_INVALID_ARGUMENT,
        "invalid momentary presenter modifier mask must be rejected");

    invalid_start.hold_zoom_modifiers = 0;
    invalid_start.hold_zoom_virtual_key = 0;
    invalid_start.presenter_zoom = 0.0f;

    require(
        arssyut_bridge_recorder_start(
            bridge,
            &invalid_start) ==
            ARSSYUT_BRIDGE_INVALID_ARGUMENT,
        "presenter zoom below 1.10x must be rejected");

    invalid_start.presenter_zoom = 4.01f;

    require(
        arssyut_bridge_recorder_start(
            bridge,
            &invalid_start) ==
            ARSSYUT_BRIDGE_INVALID_ARGUMENT,
        "presenter zoom above 4.00x must be rejected");

    invalid_start.presenter_zoom = 2.50f;

    invalid_start.spotlight_size = 99;
    require(
        arssyut_bridge_recorder_start(
            bridge,
            &invalid_start) ==
            ARSSYUT_BRIDGE_INVALID_ARGUMENT,
        "invalid Spotlight size must be rejected");

    invalid_start.spotlight_size =
        ARSSYUT_BRIDGE_SPOTLIGHT_BALANCED;
    invalid_start.spotlight_motion = 99;
    require(
        arssyut_bridge_recorder_start(
            bridge,
            &invalid_start) ==
            ARSSYUT_BRIDGE_INVALID_ARGUMENT,
        "invalid Spotlight motion must be rejected");

    invalid_start.spotlight_motion =
        ARSSYUT_BRIDGE_SPOTLIGHT_MOTION_BALANCED;
    invalid_start.spotlight_dim_strength = -0.01f;
    require(
        arssyut_bridge_recorder_start(
            bridge,
            &invalid_start) ==
            ARSSYUT_BRIDGE_INVALID_ARGUMENT,
        "negative Spotlight dim strength must be rejected");

    invalid_start.spotlight_dim_strength = 0.76f;
    require(
        arssyut_bridge_recorder_start(
            bridge,
            &invalid_start) ==
            ARSSYUT_BRIDGE_INVALID_ARGUMENT,
        "Spotlight dim strength above the bounded product range must be rejected");

    invalid_start.spotlight_dim_strength = 0.38f;
    require(
        arssyut_bridge_recorder_start(
            bridge,
            &invalid_start) ==
            ARSSYUT_BRIDGE_STALE_TOKEN,
        "valid Spotlight settings must continue to normal source-token validation");

    if (source_count > 0) {
        ArssyutBridgeSourceV1 source{};
        source.struct_size =
            sizeof(source);

        require(
            arssyut_bridge_source_at(
                bridge,
                0,
                &source) ==
                ARSSYUT_BRIDGE_OK,
            "source read for command test failed");

        ArssyutBridgeStartRequestV1 unsupported{};
        unsupported.struct_size =
            sizeof(unsupported);
        unsupported.capture_mode =
            source.kind ==
                    ARSSYUT_BRIDGE_SOURCE_MONITOR
                ? ARSSYUT_BRIDGE_CAPTURE_DISPLAY
                : ARSSYUT_BRIDGE_CAPTURE_WINDOW;
        unsupported.source_token =
            source.token;
        unsupported.frame_rate =
            60;
        unsupported.visual_mode =
            ARSSYUT_BRIDGE_VISUAL_PIXEL_ACCURATE;
        unsupported.flags =
            ARSSYUT_BRIDGE_START_MICROPHONE |
            ARSSYUT_BRIDGE_START_SMART_ZOOM;
        unsupported.presenter_zoom =
            2.0f;

        require(
            arssyut_bridge_recorder_start(
                bridge,
                &unsupported) ==
                ARSSYUT_BRIDGE_UNSUPPORTED,
            "unbound microphone backend must fail explicitly");
    }

    require(
        arssyut_bridge_recorder_commit_start(
            bridge) ==
            ARSSYUT_BRIDGE_INVALID_STATE,
        "Armed commit while idle must report invalid state");

    require(
        arssyut_bridge_recorder_stop(
            bridge) ==
            ARSSYUT_BRIDGE_INVALID_STATE,
        "Stop while idle must report invalid state");

    ArssyutBridgeRecorderResultV1 result{};
    result.struct_size =
        sizeof(result);

    require(
        arssyut_bridge_recorder_result(
            bridge,
            &result) ==
            ARSSYUT_BRIDGE_OK,
        "recorder result snapshot failed");

    ArssyutBridgeSourceV1 invalid_source{};
    invalid_source.struct_size =
        sizeof(invalid_source);

    require(
        arssyut_bridge_source_at(
            bridge,
            source_count,
            &invalid_source) ==
            ARSSYUT_BRIDGE_OUT_OF_RANGE,
        "out-of-range source access must fail deterministically");

    destroy();

    std::cout
        << "P6UI.4A/4B/4C native bridge, overlay and Region checks passed.\n";

    return 0;
}

#else

int main()
{
    return 0;
}

#endif
