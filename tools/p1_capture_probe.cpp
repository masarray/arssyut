#include "core/diagnostics/diagnostics.hpp"
#include "core/time/monotonic_clock.hpp"
#include "platform/windows/capture/latest_frame_slot.hpp"
#include "platform/windows/capture/wgc_capture_source.hpp"
#include "platform/windows/graphics/d3d11_device.hpp"
#include "platform/windows/video/native_video_pipeline.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cwchar>
#include <iostream>
#include <thread>

namespace {

int parse_int(const wchar_t *value, int fallback)
{
    if (!value)
        return fallback;

    wchar_t *end = nullptr;
    const long parsed = std::wcstol(value, &end, 10);
    if (end == value || *end != L'\0')
        return fallback;

    return static_cast<int>(parsed);
}

void print_histogram(
    const char *name,
    const arssyut::core::LatencyHistogram::Snapshot &snapshot)
{
    std::cout
        << name
        << " samples=" << snapshot.total
        << " p50<=" << snapshot.quantile_upper_bound(50, 100) << "us"
        << " p95<=" << snapshot.quantile_upper_bound(95, 100) << "us"
        << " p99<=" << snapshot.quantile_upper_bound(99, 100) << "us"
        << '\n';
}

} // namespace

int wmain(int argc, wchar_t **argv)
{
    using namespace arssyut::core;
    using namespace arssyut::windows;

    const int seconds = std::clamp(
        argc > 1 ? parse_int(argv[1], 10) : 10,
        1,
        3600);

    int fps = argc > 2 ? parse_int(argv[2], 60) : 60;
    if (fps != 30 && fps != 60)
        fps = 60;

    auto device_result =
        D3D11Device::create(D3D11DevicePreference::HardwareOnly, false);
    if (!device_result) {
        std::cerr
            << "P1 probe: hardware D3D11 device failed, detail=0x"
            << std::hex << device_result.status().detail << '\n';
        return 2;
    }

    auto &device = *device_result.value();

    LatestFrameSlot frame_slot;
    Diagnostics diagnostics;

    auto pipeline_result =
        NativeVideoPipeline::create(
            device.device(),
            frame_slot,
            diagnostics);
    if (!pipeline_result) {
        std::cerr
            << "P1 probe: video pipeline init failed, status="
            << static_cast<unsigned>(pipeline_result.status().code)
            << " detail=0x" << std::hex
            << pipeline_result.status().detail << '\n';
        return 3;
    }

    auto &pipeline = *pipeline_result.value();

    POINT origin{0, 0};
    HMONITOR monitor = MonitorFromPoint(
        origin,
        MONITOR_DEFAULTTOPRIMARY);
    if (!monitor) {
        std::cerr << "P1 probe: primary monitor not found\n";
        return 4;
    }

    WgcCaptureSource capture;
    const Status start_status = capture.start_monitor(
        device.device(),
        monitor,
        frame_slot,
        diagnostics);
    if (!start_status.ok()) {
        std::cerr
            << "P1 probe: WGC start failed, status="
            << static_cast<unsigned>(start_status.code)
            << " detail=0x" << std::hex
            << start_status.detail << '\n';
        return 5;
    }

    const TimePoint start = MonotonicClock::now();
    const Status timeline_status =
        pipeline.reset_timeline(
            start,
            {static_cast<std::uint32_t>(fps), 1});
    if (!timeline_status.ok()) {
        capture.stop();
        std::cerr << "P1 probe: timeline initialization failed\n";
        return 6;
    }

    const std::int64_t run_ticks =
        static_cast<std::int64_t>(seconds) *
        MonotonicClock::ticks_per_second;
    const TimePoint finish{
        start.ticks_100ns + run_ticks
    };

    bool pipeline_failed = false;

    while (MonotonicClock::now() < finish) {
        const auto now = MonotonicClock::now();

        auto result = pipeline.process_due(
            device.immediate_context(),
            now,
            {},
            {1280, 720});

        if (!result) {
            std::cerr
                << "P1 probe: pipeline failure, status="
                << static_cast<unsigned>(result.status().code)
                << " detail=0x" << std::hex
                << result.status().detail << '\n';
            pipeline_failed = true;
            break;
        }

        if (capture.source_closed())
            break;

        // Validation-tool pacing only. Production output pacing remains owned
        // by FrameScheduler; this sleep does not participate in engine timing.
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    capture.stop();

    const auto received =
        diagnostics.load(DiagnosticMetric::CaptureFramesReceived);
    const auto replaced =
        diagnostics.load(DiagnosticMetric::CaptureFramesReplaced);
    const auto dropped =
        diagnostics.load(DiagnosticMetric::CaptureFramesDroppedBusy);
    const auto rendered =
        diagnostics.load(DiagnosticMetric::VideoFramesRendered);
    const auto reused =
        diagnostics.load(DiagnosticMetric::VideoFramesReused);
    const auto unavailable =
        diagnostics.load(DiagnosticMetric::VideoFramesUnavailable);
    const auto skipped =
        diagnostics.load(DiagnosticMetric::VideoFramesSkipped);

    std::cout
        << "Arssyut P1 capture probe\n"
        << "duration=" << seconds << "s fps=" << fps << "\n"
        << "capture.received=" << received << "\n"
        << "capture.replaced_unread=" << replaced << "\n"
        << "capture.dropped_busy=" << dropped << "\n"
        << "video.rendered_new=" << rendered << "\n"
        << "video.reused_output=" << reused << "\n"
        << "video.unavailable=" << unavailable << "\n"
        << "video.skipped_intervals=" << skipped << "\n"
        << "compositor.resource_generation="
        << pipeline.compositor().resource_generation() << "\n";

    print_histogram(
        "capture.callback",
        capture.capture_callback_latency());
    print_histogram(
        "compositor.cpu_submit",
        pipeline.compositor().cpu_submit_latency());

    if (pipeline_failed)
        return 7;

    if (received == 0 || rendered == 0) {
        std::cerr
            << "P1 probe: no usable WGC frames were processed\n";
        return 8;
    }

    if (dropped > received / 20 + 1) {
        std::cerr
            << "P1 probe: excessive busy-slot drops detected\n";
        return 9;
    }

    std::cout << "P1 probe: PASS\n";
    return 0;
}
