#pragma once

#ifdef _WIN32

#include "core/result/result.hpp"
#include "core/time/monotonic_clock.hpp"
#include "core/video/frame_geometry.hpp"

#include <d3d11.h>
#include <wrl/client.h>

#include <winrt/Windows.Graphics.Capture.h>

#include <cstdint>

namespace arssyut::windows {

struct CapturedFrame {
    winrt::Windows::Graphics::Capture::Direct3D11CaptureFrame frame{nullptr};
    arssyut::core::TimePoint captured_at{};
    arssyut::core::FrameSize content_size{};
    std::uint64_t sequence = 0;

    [[nodiscard]] bool valid() const noexcept
    {
        return frame != nullptr && content_size.valid();
    }
};

[[nodiscard]]
arssyut::core::Result<Microsoft::WRL::ComPtr<ID3D11Texture2D>>
capture_texture(const CapturedFrame &frame) noexcept;

} // namespace arssyut::windows

#endif
