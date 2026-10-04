#include "bridge/native_bridge.h"

#ifdef _WIN32

#include <Windows.h>

#include <cstdint>
#include <cstdlib>
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

} // namespace

int main()
{
    require(
        arssyut_bridge_abi_version() ==
            ARSSYUT_BRIDGE_ABI_VERSION,
        "bridge ABI version mismatch");

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
        << "P6UI.4A native bridge ABI checks passed.\n";

    return 0;
}

#else

int main()
{
    return 0;
}

#endif
