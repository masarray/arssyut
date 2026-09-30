#pragma once

#ifdef _WIN32

#include "core/result/status.hpp"
#include "core/time/monotonic_clock.hpp"
#include "core/video/frame_geometry.hpp"
#include "core/video/frame_scheduler.hpp"

#include <d3d11.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>

namespace arssyut::windows {

struct MfVideoWriterConfig {
    arssyut::core::FrameSize size{1920, 1080};
    arssyut::core::FrameRate frame_rate{60, 1};
    std::uint32_t bitrate_bps = 12'000'000;
    std::uint32_t surface_count = 6;
};

class MfH264Mp4Writer final {
public:
    static constexpr std::size_t max_surface_count = 8;

    MfH264Mp4Writer() = default;
    ~MfH264Mp4Writer();

    MfH264Mp4Writer(const MfH264Mp4Writer &) = delete;
    MfH264Mp4Writer &operator=(const MfH264Mp4Writer &) = delete;

    [[nodiscard]] arssyut::core::Status open(
        ID3D11Device *device,
        const std::filesystem::path &path,
        MfVideoWriterConfig config) noexcept;

    [[nodiscard]] arssyut::core::Status write_frame(
        ID3D11DeviceContext *context,
        ID3D11Texture2D *source,
        arssyut::core::TimePoint relative_pts,
        std::int64_t duration_ticks) noexcept;

    [[nodiscard]] arssyut::core::Status finalize() noexcept;

    [[nodiscard]] bool open() const noexcept
    {
        return open_;
    }

    [[nodiscard]] std::uint64_t submitted_frames() const noexcept
    {
        return submitted_frames_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] std::uint64_t backpressure_events() const noexcept
    {
        return backpressure_events_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] std::uint32_t in_flight_surfaces() const noexcept;

    [[nodiscard]] const std::filesystem::path &path() const noexcept
    {
        return path_;
    }

    void on_sample_released(std::uint32_t slot) noexcept;

private:
    class ReleaseCallback;

    struct SurfaceSlot {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        std::atomic<bool> in_use{false};
    };

    [[nodiscard]] arssyut::core::Status create_surface_pool(
        ID3D11Device *device) noexcept;

    [[nodiscard]] std::size_t acquire_surface() noexcept;

    void release_surface(std::size_t index) noexcept;

    void teardown() noexcept;

    MfVideoWriterConfig config_{};
    std::filesystem::path path_;

    Microsoft::WRL::ComPtr<IMFDXGIDeviceManager> dxgi_manager_;
    Microsoft::WRL::ComPtr<IMFSinkWriter> writer_;
    Microsoft::WRL::ComPtr<IMFAsyncCallback> release_callback_;

    std::array<SurfaceSlot, max_surface_count> surfaces_{};

    DWORD stream_index_ = 0;
    UINT dxgi_reset_token_ = 0;
    bool mf_started_ = false;
    bool open_ = false;

    std::atomic<std::uint64_t> submitted_frames_{0};
    std::atomic<std::uint64_t> backpressure_events_{0};
};

} // namespace arssyut::windows

#endif
