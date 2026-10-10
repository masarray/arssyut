#pragma once

#ifdef _WIN32

#include "core/result/status.hpp"
#include "core/time/monotonic_clock.hpp"
#include "core/video/frame_geometry.hpp"
#include "core/video/frame_scheduler.hpp"

#include <d3d11.h>
#include <d3d11_1.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>

namespace arssyut::windows {

enum class MfWriterStage : std::uint16_t {
    None = 0,
    CreateVideoProcessor,
    MediaFoundationStartup,
    CreateDxgiManager,
    ResetDxgiDevice,
    CreateSinkWriter,
    ConfigureOutputType,
    AddOutputStream,
    ConfigureInputType,
    SetInputMediaType,
    ConfigureAudioOutputType,
    AddAudioOutputStream,
    ConfigureAudioInputType,
    SetAudioInputMediaType,
    CreateAudioSample,
    WriteAudioSample,
    CreateSurfacePool,
    BeginWriting,
    ConvertToNv12,
    CreateTrackedSample,
    CreateDxgiBuffer,
    SetBufferLength,
    ConfigureSample,
    WriteSample,
    Finalize,
};

[[nodiscard]] const char *mf_writer_stage_name(
    MfWriterStage stage) noexcept;

enum class MfH264Profile : std::uint8_t {
    Main = 0,
    High,
};

[[nodiscard]] const char *mf_h264_profile_name(
    MfH264Profile profile) noexcept;

enum class MfRateControlMode : std::uint8_t {
    Default = 0,
    UnconstrainedVbr,
};

[[nodiscard]] const char *mf_rate_control_mode_name(
    MfRateControlMode mode) noexcept;

enum class MfColorPipelineMode : std::uint8_t {
    LegacyExplicit = 0,
    Context1Explicit,
};

[[nodiscard]] const char *mf_color_pipeline_mode_name(
    MfColorPipelineMode mode) noexcept;

struct MfVideoWriterConfig {
    arssyut::core::FrameSize size{1920, 1080};
    arssyut::core::FrameRate frame_rate{60, 1};
    std::uint32_t bitrate_bps = 18'000'000;
    std::uint32_t surface_count = 6;

    // P5D/P5D.6 screen-content quality policy.
    //
    // High Profile and bitrate-controlled unconstrained VBR are preferences.
    // If a hardware/software MFT rejects explicit rate-control attributes,
    // the writer falls back to normal Sink Writer negotiation instead of
    // failing recorder startup.
    bool prefer_high_profile = true;
    bool prefer_bitrate_vbr = true;
    std::uint32_t quality_vs_speed = 85;
};

struct MfAudioWriterConfig {
    bool enabled = false;
    std::uint32_t sample_rate = 48'000;
    std::uint16_t channels = 2;
    std::uint32_t bitrate_bps = 192'000;
    std::uint32_t sample_pool_count = 8;
    std::uint32_t max_frames_per_sample = 1'024;

    [[nodiscard]] constexpr bool valid() const noexcept
    {
        if (!enabled)
            return true;

        return
            (sample_rate == 44'100 || sample_rate == 48'000) &&
            (channels == 1 || channels == 2) &&
            bitrate_bps >= 96'000 &&
            bitrate_bps <= 320'000 &&
            sample_pool_count >= 2 &&
            sample_pool_count <= 16 &&
            max_frames_per_sample >= 1 &&
            max_frames_per_sample <= 4'096;
    }
};

[[nodiscard]] arssyut::core::Status
configure_mf_aac_output_type(
    IMFMediaType *type,
    MfAudioWriterConfig config) noexcept;

[[nodiscard]] arssyut::core::Status
configure_mf_pcm16_input_type(
    IMFMediaType *type,
    MfAudioWriterConfig config) noexcept;

// Explicit one-sample boundary signal used by the existing MF writer; a
// non-discontinuous sample leaves the extension absent. Testable without
// Windows capture devices or an encoder.
[[nodiscard]] HRESULT configure_mf_audio_sample_discontinuity(
    IMFSample *sample,
    bool discontinuity) noexcept;

class MfH264Mp4Writer final {
public:
    static constexpr std::size_t max_surface_count = 8;
    static constexpr std::size_t max_audio_slot_count = 16;

    MfH264Mp4Writer() = default;
    ~MfH264Mp4Writer();

    MfH264Mp4Writer(const MfH264Mp4Writer &) = delete;
    MfH264Mp4Writer &operator=(const MfH264Mp4Writer &) = delete;

    [[nodiscard]] arssyut::core::Status open(
        ID3D11Device *device,
        const std::filesystem::path &path,
        MfVideoWriterConfig config,
        MfAudioWriterConfig audio_config = {}) noexcept;

    [[nodiscard]] arssyut::core::Status write_frame(
        ID3D11DeviceContext *context,
        ID3D11Texture2D *source,
        arssyut::core::TimePoint relative_pts,
        std::int64_t duration_ticks) noexcept;

    // Backpressure/drop gaps keep their original media PTS. The caller
    // forwards canonical program discontinuity to the existing MF sample,
    // rather than silently resetting the writer's own timing authority.
    [[nodiscard]] arssyut::core::Status write_audio_pcm16(
        std::span<const std::int16_t> interleaved,
        arssyut::core::TimePoint relative_pts,
        std::int64_t duration_ticks,
        bool discontinuity = false) noexcept;

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

    [[nodiscard]] bool audio_enabled() const noexcept
    {
        return audio_enabled_;
    }

    [[nodiscard]] std::uint32_t active_audio_bitrate_bps() const noexcept
    {
        return active_audio_bitrate_bps_;
    }

    [[nodiscard]] std::uint64_t submitted_audio_samples() const noexcept
    {
        return submitted_audio_samples_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] std::uint64_t submitted_audio_frames() const noexcept
    {
        return submitted_audio_frames_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] std::uint64_t audio_backpressure_events() const noexcept
    {
        return audio_backpressure_events_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] std::uint32_t in_flight_audio_samples() const noexcept;

    [[nodiscard]] std::uint32_t in_flight_surfaces() const noexcept;

    [[nodiscard]] std::uint32_t last_sample_buffer_length() const noexcept
    {
        return sample_buffer_length_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] std::uint32_t last_sample_buffer_max_length() const noexcept
    {
        return sample_buffer_max_length_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] MfWriterStage failure_stage() const noexcept
    {
        return failure_stage_.load(std::memory_order_acquire);
    }

    [[nodiscard]] MfH264Profile active_profile() const noexcept
    {
        return active_profile_;
    }

    [[nodiscard]] MfRateControlMode active_rate_control() const noexcept
    {
        return active_rate_control_;
    }

    [[nodiscard]] bool bitrate_vbr_applied() const noexcept
    {
        return active_rate_control_ ==
            MfRateControlMode::UnconstrainedVbr;
    }

    [[nodiscard]] bool quality_vs_speed_applied() const noexcept
    {
        return quality_vs_speed_applied_;
    }

    [[nodiscard]] std::uint32_t requested_quality_vs_speed() const noexcept
    {
        return config_.quality_vs_speed;
    }

    [[nodiscard]] MfColorPipelineMode active_color_pipeline() const noexcept
    {
        return active_color_pipeline_;
    }

    [[nodiscard]] bool color_pipeline_authoritative() const noexcept
    {
        return color_pipeline_authoritative_;
    }

    [[nodiscard]] const std::filesystem::path &path() const noexcept
    {
        return path_;
    }

    void on_sample_released(std::uint32_t slot) noexcept;

    void on_audio_sample_released(std::uint32_t slot) noexcept;

private:
    class ReleaseCallback;

    struct SurfaceSlot {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        Microsoft::WRL::ComPtr<ID3D11VideoProcessorOutputView> output_view;
        std::atomic<bool> in_use{false};
    };

    struct AudioSlot {
        Microsoft::WRL::ComPtr<IMFMediaBuffer> buffer;
        std::atomic<bool> in_use{false};
    };

    [[nodiscard]] arssyut::core::Status create_video_processor(
        ID3D11Device *device) noexcept;

    [[nodiscard]] arssyut::core::Status configure_audio_stream() noexcept;

    [[nodiscard]] arssyut::core::Status create_audio_pool() noexcept;

    [[nodiscard]] arssyut::core::Status create_surface_pool(
        ID3D11Device *device) noexcept;

    [[nodiscard]] arssyut::core::Status convert_to_nv12(
        ID3D11DeviceContext *context,
        ID3D11Texture2D *source,
        std::size_t output_slot) noexcept;

    [[nodiscard]] std::size_t acquire_surface() noexcept;

    [[nodiscard]] std::size_t acquire_audio_slot() noexcept;

    void release_surface(std::size_t index) noexcept;

    void release_audio_slot(std::size_t index) noexcept;

    void teardown() noexcept;

    MfVideoWriterConfig config_{};
    MfAudioWriterConfig audio_config_{};
    std::filesystem::path path_;

    Microsoft::WRL::ComPtr<IMFDXGIDeviceManager> dxgi_manager_;
    Microsoft::WRL::ComPtr<IMFSinkWriter> writer_;
    Microsoft::WRL::ComPtr<IMFAsyncCallback> release_callback_;

    Microsoft::WRL::ComPtr<ID3D11VideoDevice> video_device_;
    Microsoft::WRL::ComPtr<ID3D11VideoContext> video_context_;
    Microsoft::WRL::ComPtr<ID3D11VideoContext1> video_context1_;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessorEnumerator> video_enumerator_;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessor> video_processor_;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> input_copy_;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessorInputView> input_view_;

    std::array<SurfaceSlot, max_surface_count> surfaces_{};
    std::array<AudioSlot, max_audio_slot_count> audio_slots_{};

    static constexpr DWORD invalid_stream_index = 0xFFFFFFFFu;

    DWORD video_stream_index_ = 0;
    DWORD audio_stream_index_ = invalid_stream_index;
    UINT dxgi_reset_token_ = 0;
    bool mf_started_ = false;
    bool open_ = false;
    MfH264Profile active_profile_ = MfH264Profile::Main;
    MfRateControlMode active_rate_control_ =
        MfRateControlMode::Default;
    bool quality_vs_speed_applied_ = false;
    MfColorPipelineMode active_color_pipeline_ =
        MfColorPipelineMode::LegacyExplicit;
    bool color_pipeline_authoritative_ = false;
    bool audio_enabled_ = false;
    std::uint32_t active_audio_bitrate_bps_ = 0;
    bool have_audio_timeline_ = false;
    std::int64_t last_audio_end_100ns_ = 0;

    std::atomic<std::uint64_t> submitted_frames_{0};
    std::atomic<std::uint32_t> dynamic_audio_in_flight_{0};
    std::atomic<std::uint64_t> submitted_audio_samples_{0};
    std::atomic<std::uint64_t> submitted_audio_frames_{0};
    std::atomic<std::uint64_t> audio_backpressure_events_{0};
    std::atomic<std::uint64_t> backpressure_events_{0};
    std::atomic<std::uint32_t> sample_buffer_length_{0};
    std::atomic<std::uint32_t> sample_buffer_max_length_{0};
    std::atomic<MfWriterStage> failure_stage_{MfWriterStage::None};
};

} // namespace arssyut::windows

#endif
