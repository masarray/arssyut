#include "core/video/webcam_capture_policy.hpp"
#include "core/video/webcam_capture_timing.hpp"

#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>

using arssyut::core::WebcamCaptureCandidate;
using arssyut::core::WebcamPixelFormat;
using arssyut::core::select_webcam_candidate;
using arssyut::core::webcam_candidate_rank;
using arssyut::core::webcam_candidate_supported;

int main()
{
    int failed = 0;
    const auto check = [&](bool okay, const char *why) {
        if (!okay) {
            std::cerr << "FAIL: " << why << '\n';
            ++failed;
        }
    };

    using F = WebcamPixelFormat;
    const WebcamCaptureCandidate nv12_720{1280, 720, 30, 1, F::Nv12};
    const WebcamCaptureCandidate nv12_1080{1920, 1080, 30, 1, F::Nv12};
    const WebcamCaptureCandidate yuy2_720{1280, 720, 30, 1, F::Yuy2};
    const WebcamCaptureCandidate bgra_720{1280, 720, 30, 1, F::Bgra32};
    const WebcamCaptureCandidate mjpeg{1280, 720, 30, 1, F::Unsupported};
    check(webcam_candidate_supported(nv12_720), "native 720p30 accepted");
    check(!webcam_candidate_supported(mjpeg), "MJPEG rejected; no CPU decode");
    check(!webcam_candidate_supported({7680, 4320, 30, 1, F::Nv12}),
          "8K allocation not admitted");
    check(!webcam_candidate_supported({1280, 720, 30, 0, F::Nv12}),
          "zero frame-rate denominator rejected");
    check(!webcam_candidate_supported({1280, 720, 0, 1, F::Nv12}),
          "zero frame-rate numerator rejected");
    check(!webcam_candidate_supported({1280, 720, 61, 1, F::Nv12}),
          "over-60fps capture rejected");
    check(!webcam_candidate_supported({1280, 720, 14, 1, F::Nv12}),
          "under-15fps capture rejected");
    check(webcam_candidate_supported({1280, 720, 30000, 1001, F::Nv12}),
          "29.97 native rate accepted");

    check(select_webcam_candidate(std::vector<WebcamCaptureCandidate>{}) == 0,
          "empty catalog returns size");
    const std::vector<WebcamCaptureCandidate> missing{mjpeg, {0, 0, 0, 0, F::Nv12}};
    check(select_webcam_candidate(missing) == missing.size(),
          "catalog with only invalid formats returns size");
    const std::vector<WebcamCaptureCandidate> options{
        mjpeg, nv12_1080, yuy2_720, nv12_720, bgra_720
    };
    check(select_webcam_candidate(options) == 3,
          "720p NV12 selected over larger NV12, YUY2 and BGRA");
    check(webcam_candidate_rank(nv12_720) < webcam_candidate_rank(nv12_1080),
          "prefer lower GPU memory pressure at 720p");
    check(webcam_candidate_rank(nv12_720) < webcam_candidate_rank(yuy2_720),
          "prefer NV12 vs YUY2");
    check(webcam_candidate_rank(yuy2_720) < webcam_candidate_rank(bgra_720),
          "native YUY2 precedence");
    check(select_webcam_candidate(std::vector<WebcamCaptureCandidate>{
              nv12_720, nv12_720}) == 0, "ties deterministic");
    check(webcam_candidate_rank(mjpeg) == std::numeric_limits<std::uint64_t>::max(),
          "unsupported candidate rank is sentinel");
    check(webcam_candidate_supported({3840, 2160, 60, 1, F::Nv12}),
          "boundaries included");
    check(!webcam_candidate_supported({3841, 2160, 60, 1, F::Nv12}),
          "boundary overflow rejected");
    // P8W1C: measured cadence must exclude camera opening and use frame
    // *intervals*; a source tick before the first frame is not a dropped
    // streaming frame. All tests run without Windows devices.
    using arssyut::core::webcam_observed_fps_x100;
    using arssyut::core::webcam_excessive_frame_interval;
    check(webcam_observed_fps_x100(151, 10'000'000, 60'000'000) == 3000,
          "151 samples over 150 intervals in five seconds is exactly 30fps");
    check(webcam_observed_fps_x100(76, 10'000'000, 35'000'000) == 3000,
          "75 intervals over 2.5 seconds is 30fps, independent of startup");
    check(webcam_observed_fps_x100(1, 10'000'000, 10'000'000) == 0,
          "single startup frame has no steady FPS");
    check(webcam_observed_fps_x100(0, 0, 0) == 0,
          "no camera frame yields zero FPS");
    check(webcam_observed_fps_x100(5, 20'000'000, 19'000'000) == 0,
          "counter rewind is not a valid FPS");
    check(webcam_observed_fps_x100(3, 0, 20'000'000) == 0,
          "unknown first timestamp fails closed");
    check(!webcam_excessive_frame_interval(1'000'000, 1'333'333),
          "normal 30fps frame interval not counted as stall");
    check(!webcam_excessive_frame_interval(1'000'000, 2'000'000),
          "100ms threshold itself is not a stall");
    check(webcam_excessive_frame_interval(1'000'000, 2'000'001),
          "over-100ms interframe stall detected");
    check(!webcam_excessive_frame_interval(2'000'000, 1'000'000),
          "QPC rewind not misclassified as long stall");

    std::cout << "P8W1 webcam candidate policy: " <<
        (failed == 0 ? "PASS" : "FAIL") << '\n';
    return failed == 0 ? 0 : 1;
}
