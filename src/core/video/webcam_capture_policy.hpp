#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

namespace arssyut::core {

// P8W1A: selection is a *probe candidate*, not a promise that the camera
// can deliver a DXGI-backed surface. Real samples must pass the GPU gate.
enum class WebcamPixelFormat : std::uint8_t {
    Nv12 = 0,
    Yuy2,
    Bgra32,
    Unsupported,
};

struct WebcamCaptureCandidate {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t fps_num = 0;
    std::uint32_t fps_den = 0;
    WebcamPixelFormat format = WebcamPixelFormat::Unsupported;
};

[[nodiscard]] constexpr bool webcam_candidate_supported(
    const WebcamCaptureCandidate &c) noexcept
{
    if (c.width < 320 || c.height < 240 ||
        c.width > 3840 || c.height > 2160 ||
        c.fps_den == 0 || c.fps_num == 0 ||
        c.format == WebcamPixelFormat::Unsupported)
        return false;

    const std::uint64_t num = c.fps_num;
    const std::uint64_t den = c.fps_den;
    return num >= 15ULL * den && num <= 60ULL * den;
}

// Prefer native NV12/720p30 for bounded GPU input. No MJPEG/CPU conversion
// or silently oversized buffers. Stable tie-breaking uses source order.
[[nodiscard]] constexpr std::uint64_t webcam_candidate_rank(
    const WebcamCaptureCandidate &c) noexcept
{
    if (!webcam_candidate_supported(c))
        return std::numeric_limits<std::uint64_t>::max();

    const auto abs_diff = [](std::uint64_t a, std::uint64_t b) {
        return a > b ? a - b : b - a;
    };
    const std::uint64_t shape_delta =
        abs_diff(c.width, 1280) + abs_diff(c.height, 720);
    const std::uint64_t frame_rate_delta =
        abs_diff(c.fps_num, 30ULL * c.fps_den) * 100ULL / c.fps_den;

    return static_cast<std::uint64_t>(c.format) * 10'000'000ULL +
        shape_delta * 1'000ULL + frame_rate_delta;
}

template <typename CandidateContainer>
[[nodiscard]] std::size_t select_webcam_candidate(
    const CandidateContainer &candidates) noexcept
{
    std::size_t selected = candidates.size();
    std::uint64_t best = std::numeric_limits<std::uint64_t>::max();
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        const auto rank = webcam_candidate_rank(candidates[i]);
        if (rank < best) {
            best = rank;
            selected = i;
        }
    }
    return selected;
}

} // namespace arssyut::core
