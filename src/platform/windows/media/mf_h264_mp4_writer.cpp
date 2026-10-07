#include "platform/windows/media/mf_h264_mp4_writer.hpp"

#ifdef _WIN32

#include "core/result/status.hpp"
#include "core/audio/audio_time.hpp"

#include <codecapi.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfobjects.h>
#include <mfreadwrite.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <limits>
#include <new>
#include <thread>

namespace arssyut::windows {

namespace {

using arssyut::core::Status;
using arssyut::core::StatusCode;

inline constexpr GUID kArssyutSurfaceSlot = {
    0x3b3c1fc0, 0x48b4, 0x4a7b,
    {0x8f, 0x56, 0x74, 0x52, 0x1f, 0x77, 0x2c, 0xa1}
};

inline constexpr GUID kArssyutAudioSlot = {
    0x5264ba8a, 0x69f7, 0x4a76,
    {0x95, 0x4a, 0xc1, 0x38, 0x8e, 0x11, 0x2e, 0x2f}
};

[[nodiscard]] Status mf_failure(HRESULT hr) noexcept
{
    return Status::failure(
        StatusCode::MediaFoundationFailure,
        static_cast<std::uint32_t>(hr));
}

[[nodiscard]] Status unsupported(HRESULT hr = E_NOINTERFACE) noexcept
{
    return Status::failure(
        StatusCode::Unsupported,
        static_cast<std::uint32_t>(hr));
}

[[nodiscard]] Status set_common_video_attributes(
    IMFMediaType *type,
    const MfVideoWriterConfig &config) noexcept
{
    if (!type)
        return Status::failure(StatusCode::InvalidArgument);

    HRESULT hr = type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    if (FAILED(hr))
        return mf_failure(hr);

    hr = MFSetAttributeSize(
        type,
        MF_MT_FRAME_SIZE,
        config.size.width,
        config.size.height);
    if (FAILED(hr))
        return mf_failure(hr);

    hr = MFSetAttributeRatio(
        type,
        MF_MT_FRAME_RATE,
        config.frame_rate.numerator,
        config.frame_rate.denominator);
    if (FAILED(hr))
        return mf_failure(hr);

    hr = MFSetAttributeRatio(
        type,
        MF_MT_PIXEL_ASPECT_RATIO,
        1,
        1);
    if (FAILED(hr))
        return mf_failure(hr);

    hr = type->SetUINT32(
        MF_MT_INTERLACE_MODE,
        MFVideoInterlace_Progressive);
    if (FAILED(hr))
        return mf_failure(hr);

    return Status::success();
}

[[nodiscard]] Status set_bt709_studio_attributes(
    IMFMediaType *type) noexcept
{
    if (!type)
        return Status::failure(StatusCode::InvalidArgument);

    /*
     * P5D.7 color authority:
     * - compositor/video-processor input is full-range SDR RGB;
     * - NV12 and H.264 are studio-range BT.709.
     *
     * These Media Foundation attributes must agree with the D3D11 Video
     * Processor color spaces. Leaving them unknown lets downstream encoders,
     * muxers or players guess and can collapse near-white/near-black UI
     * levels.
     */
    HRESULT hr = type->SetUINT32(
        MF_MT_VIDEO_PRIMARIES,
        MFVideoPrimaries_BT709);
    if (FAILED(hr))
        return mf_failure(hr);

    hr = type->SetUINT32(
        MF_MT_TRANSFER_FUNCTION,
        MFVideoTransFunc_709);
    if (FAILED(hr))
        return mf_failure(hr);

    hr = type->SetUINT32(
        MF_MT_YUV_MATRIX,
        MFVideoTransferMatrix_BT709);
    if (FAILED(hr))
        return mf_failure(hr);

    hr = type->SetUINT32(
        MF_MT_VIDEO_NOMINAL_RANGE,
        MFNominalRange_16_235);
    if (FAILED(hr))
        return mf_failure(hr);

    return Status::success();
}

} // namespace

Status configure_mf_aac_output_type(
    IMFMediaType *type,
    MfAudioWriterConfig config) noexcept
{
    if (!type || !config.enabled || !config.valid())
        return Status::failure(StatusCode::InvalidArgument);

    HRESULT hr =
        type->SetGUID(
            MF_MT_MAJOR_TYPE,
            MFMediaType_Audio);
    if (FAILED(hr))
        return mf_failure(hr);

    hr = type->SetGUID(
        MF_MT_SUBTYPE,
        MFAudioFormat_AAC);
    if (FAILED(hr))
        return mf_failure(hr);

    hr = type->SetUINT32(
        MF_MT_AUDIO_NUM_CHANNELS,
        config.channels);
    if (FAILED(hr))
        return mf_failure(hr);

    hr = type->SetUINT32(
        MF_MT_AUDIO_SAMPLES_PER_SECOND,
        config.sample_rate);
    if (FAILED(hr))
        return mf_failure(hr);

    hr = type->SetUINT32(
        MF_MT_AUDIO_BITS_PER_SAMPLE,
        16);
    if (FAILED(hr))
        return mf_failure(hr);

    hr = type->SetUINT32(
        MF_MT_AUDIO_AVG_BYTES_PER_SECOND,
        config.bitrate_bps / 8U);
    if (FAILED(hr))
        return mf_failure(hr);

    hr = type->SetUINT32(
        MF_MT_AAC_PAYLOAD_TYPE,
        0);
    if (FAILED(hr))
        return mf_failure(hr);

    return Status::success();
}

Status configure_mf_pcm16_input_type(
    IMFMediaType *type,
    MfAudioWriterConfig config) noexcept
{
    if (!type || !config.enabled || !config.valid())
        return Status::failure(StatusCode::InvalidArgument);

    const std::uint32_t block_align =
        static_cast<std::uint32_t>(config.channels) *
        static_cast<std::uint32_t>(sizeof(std::int16_t));
    const std::uint64_t avg_bytes =
        static_cast<std::uint64_t>(config.sample_rate) *
        block_align;
    if (avg_bytes >
        static_cast<std::uint64_t>(
            std::numeric_limits<UINT32>::max())) {
        return Status::failure(StatusCode::InvalidArgument);
    }

    HRESULT hr =
        type->SetGUID(
            MF_MT_MAJOR_TYPE,
            MFMediaType_Audio);
    if (FAILED(hr))
        return mf_failure(hr);

    hr = type->SetGUID(
        MF_MT_SUBTYPE,
        MFAudioFormat_PCM);
    if (FAILED(hr))
        return mf_failure(hr);

    hr = type->SetUINT32(
        MF_MT_AUDIO_NUM_CHANNELS,
        config.channels);
    if (FAILED(hr))
        return mf_failure(hr);

    hr = type->SetUINT32(
        MF_MT_AUDIO_SAMPLES_PER_SECOND,
        config.sample_rate);
    if (FAILED(hr))
        return mf_failure(hr);

    hr = type->SetUINT32(
        MF_MT_AUDIO_BITS_PER_SAMPLE,
        16);
    if (FAILED(hr))
        return mf_failure(hr);

    hr = type->SetUINT32(
        MF_MT_AUDIO_BLOCK_ALIGNMENT,
        block_align);
    if (FAILED(hr))
        return mf_failure(hr);

    hr = type->SetUINT32(
        MF_MT_AUDIO_AVG_BYTES_PER_SECOND,
        static_cast<UINT32>(avg_bytes));
    if (FAILED(hr))
        return mf_failure(hr);

    hr = type->SetUINT32(
        MF_MT_ALL_SAMPLES_INDEPENDENT,
        TRUE);
    if (FAILED(hr))
        return mf_failure(hr);

    return Status::success();
}

const char *mf_h264_profile_name(
    MfH264Profile profile) noexcept
{
    switch (profile) {
    case MfH264Profile::High:
        return "high";
    case MfH264Profile::Main:
    default:
        return "main";
    }
}

const char *mf_rate_control_mode_name(
    MfRateControlMode mode) noexcept
{
    switch (mode) {
    case MfRateControlMode::UnconstrainedVbr:
        return "unconstrained_vbr";
    case MfRateControlMode::Default:
    default:
        return "default";
    }
}

const char *mf_color_pipeline_mode_name(
    MfColorPipelineMode mode) noexcept
{
    switch (mode) {
    case MfColorPipelineMode::Context1Explicit:
        return "d3d11_context1_bt709";
    case MfColorPipelineMode::LegacyExplicit:
    default:
        return "d3d11_legacy_explicit_bt709";
    }
}

const char *mf_writer_stage_name(
    MfWriterStage stage) noexcept
{
    switch (stage) {
    case MfWriterStage::None:
        return "none";
    case MfWriterStage::CreateVideoProcessor:
        return "create_video_processor";
    case MfWriterStage::MediaFoundationStartup:
        return "mf_startup";
    case MfWriterStage::CreateDxgiManager:
        return "create_dxgi_manager";
    case MfWriterStage::ResetDxgiDevice:
        return "reset_dxgi_device";
    case MfWriterStage::CreateSinkWriter:
        return "create_sink_writer";
    case MfWriterStage::ConfigureOutputType:
        return "configure_h264_output";
    case MfWriterStage::AddOutputStream:
        return "add_output_stream";
    case MfWriterStage::ConfigureInputType:
        return "configure_nv12_input";
    case MfWriterStage::SetInputMediaType:
        return "set_input_media_type";
    case MfWriterStage::ConfigureAudioOutputType:
        return "configure_aac_output";
    case MfWriterStage::AddAudioOutputStream:
        return "add_aac_output_stream";
    case MfWriterStage::ConfigureAudioInputType:
        return "configure_pcm16_audio_input";
    case MfWriterStage::SetAudioInputMediaType:
        return "set_pcm16_audio_input";
    case MfWriterStage::CreateAudioSample:
        return "create_audio_sample";
    case MfWriterStage::WriteAudioSample:
        return "write_audio_sample";
    case MfWriterStage::CreateSurfacePool:
        return "create_surface_pool";
    case MfWriterStage::BeginWriting:
        return "begin_writing";
    case MfWriterStage::ConvertToNv12:
        return "video_processor_blt";
    case MfWriterStage::CreateTrackedSample:
        return "create_tracked_sample";
    case MfWriterStage::CreateDxgiBuffer:
        return "create_dxgi_buffer";
    case MfWriterStage::SetBufferLength:
        return "set_dxgi_buffer_length";
    case MfWriterStage::ConfigureSample:
        return "configure_sample";
    case MfWriterStage::WriteSample:
        return "write_sample";
    case MfWriterStage::Finalize:
        return "finalize";
    default:
        return "unknown";
    }
}

class MfH264Mp4Writer::ReleaseCallback final
    : public IMFAsyncCallback {
public:
    explicit ReleaseCallback(MfH264Mp4Writer *owner) noexcept
        : owner_(owner)
    {
    }

    void detach() noexcept
    {
        owner_.store(nullptr, std::memory_order_release);
    }

    STDMETHODIMP QueryInterface(
        REFIID riid,
        void **object) noexcept override
    {
        if (!object)
            return E_POINTER;

        *object = nullptr;

        if (riid == __uuidof(IUnknown) ||
            riid == __uuidof(IMFAsyncCallback)) {
            *object = static_cast<IMFAsyncCallback *>(this);
            AddRef();
            return S_OK;
        }

        return E_NOINTERFACE;
    }

    STDMETHODIMP_(ULONG) AddRef() noexcept override
    {
        return ref_count_.fetch_add(
                   1,
                   std::memory_order_relaxed) +
               1;
    }

    STDMETHODIMP_(ULONG) Release() noexcept override
    {
        const ULONG remaining =
            ref_count_.fetch_sub(
                1,
                std::memory_order_acq_rel) -
            1;

        if (remaining == 0)
            delete this;

        return remaining;
    }

    STDMETHODIMP GetParameters(
        DWORD *,
        DWORD *) noexcept override
    {
        return E_NOTIMPL;
    }

    STDMETHODIMP Invoke(
        IMFAsyncResult *result) noexcept override
    {
        if (!result)
            return E_POINTER;

        Microsoft::WRL::ComPtr<IUnknown> object;
        const HRESULT object_hr =
            result->GetObject(object.GetAddressOf());
        if (FAILED(object_hr) || !object)
            return S_OK;

        Microsoft::WRL::ComPtr<IMFSample> sample;
        if (FAILED(object.As(&sample)) || !sample)
            return S_OK;

        auto *owner =
            owner_.load(std::memory_order_acquire);
        if (!owner)
            return S_OK;

        UINT32 slot = 0;
        if (SUCCEEDED(sample->GetUINT32(
                kArssyutAudioSlot,
                &slot))) {
            owner->on_audio_sample_released(slot);
            return S_OK;
        }

        if (SUCCEEDED(sample->GetUINT32(
                kArssyutSurfaceSlot,
                &slot))) {
            owner->on_sample_released(slot);
        }

        return S_OK;
    }

private:
    std::atomic<ULONG> ref_count_{1};
    std::atomic<MfH264Mp4Writer *> owner_;
};

MfH264Mp4Writer::~MfH264Mp4Writer()
{
    if (open_)
        (void)finalize();
    else
        teardown();
}

Status MfH264Mp4Writer::open(
    ID3D11Device *device,
    const std::filesystem::path &path,
    MfVideoWriterConfig config,
    MfAudioWriterConfig audio_config) noexcept
{
    if (open_ ||
        !device ||
        path.empty() ||
        !config.size.valid() ||
        (config.size.width & 1U) != 0 ||
        (config.size.height & 1U) != 0 ||
        !config.frame_rate.valid() ||
        config.bitrate_bps == 0 ||
        !audio_config.valid()) {
        return Status::failure(StatusCode::InvalidArgument);
    }

    failure_stage_.store(
        MfWriterStage::None,
        std::memory_order_release);
    active_profile_ = MfH264Profile::Main;
    active_rate_control_ =
        MfRateControlMode::Default;
    quality_vs_speed_applied_ = false;
    active_color_pipeline_ =
        MfColorPipelineMode::LegacyExplicit;
    color_pipeline_authoritative_ = false;
    audio_config_ = {};
    audio_enabled_ = false;
    active_audio_bitrate_bps_ = 0;
    audio_stream_index_ = invalid_stream_index;
    have_audio_timeline_ = false;
    last_audio_end_100ns_ = 0;

    auto fail_hr =
        [this](MfWriterStage stage, HRESULT hr) noexcept -> Status {
            failure_stage_.store(stage, std::memory_order_release);
            teardown();
            return mf_failure(hr);
        };

    auto fail_status =
        [this](MfWriterStage stage, Status status) noexcept -> Status {
            failure_stage_.store(stage, std::memory_order_release);
            teardown();
            return status;
        };

    config.surface_count =
        std::clamp<std::uint32_t>(
            config.surface_count,
            3U,
            static_cast<std::uint32_t>(
                max_surface_count));
    config.quality_vs_speed =
        std::clamp<std::uint32_t>(
            config.quality_vs_speed,
            0U,
            100U);

    config_ = config;
    audio_config_ = audio_config;
    path_ = path;

    Status status = create_video_processor(device);
    if (!status.ok())
        return fail_status(
            MfWriterStage::CreateVideoProcessor,
            status);

    HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_FULL);
    if (FAILED(hr))
        return fail_hr(
            MfWriterStage::MediaFoundationStartup,
            hr);
    mf_started_ = true;

    hr = MFCreateDXGIDeviceManager(
        &dxgi_reset_token_,
        dxgi_manager_.GetAddressOf());
    if (FAILED(hr))
        return fail_hr(
            MfWriterStage::CreateDxgiManager,
            hr);

    hr = dxgi_manager_->ResetDevice(
        device,
        dxgi_reset_token_);
    if (FAILED(hr))
        return fail_hr(
            MfWriterStage::ResetDxgiDevice,
            hr);

    Microsoft::WRL::ComPtr<IMFAttributes> attributes;
    hr = MFCreateAttributes(
        attributes.GetAddressOf(),
        6);
    if (FAILED(hr))
        return fail_hr(
            MfWriterStage::CreateSinkWriter,
            hr);

    hr = attributes->SetUINT32(
        MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS,
        TRUE);
    if (FAILED(hr))
        return fail_hr(
            MfWriterStage::CreateSinkWriter,
            hr);

    hr = attributes->SetUINT32(
        MF_SINK_WRITER_DISABLE_THROTTLING,
        TRUE);
    if (FAILED(hr))
        return fail_hr(
            MfWriterStage::CreateSinkWriter,
            hr);

    hr = attributes->SetUINT32(
        MF_LOW_LATENCY,
        TRUE);
    if (FAILED(hr))
        return fail_hr(
            MfWriterStage::CreateSinkWriter,
            hr);

    hr = attributes->SetUnknown(
        MF_SINK_WRITER_D3D_MANAGER,
        dxgi_manager_.Get());
    if (FAILED(hr))
        return fail_hr(
            MfWriterStage::CreateSinkWriter,
            hr);

    hr = MFCreateSinkWriterFromURL(
        path_.c_str(),
        nullptr,
        attributes.Get(),
        writer_.GetAddressOf());
    if (FAILED(hr))
        return fail_hr(
            MfWriterStage::CreateSinkWriter,
            hr);

    Microsoft::WRL::ComPtr<IMFMediaType> output_type;
    hr = MFCreateMediaType(
        output_type.GetAddressOf());
    if (FAILED(hr))
        return fail_hr(
            MfWriterStage::ConfigureOutputType,
            hr);

    status = set_common_video_attributes(
        output_type.Get(),
        config_);
    if (!status.ok())
        return fail_status(
            MfWriterStage::ConfigureOutputType,
            status);

    hr = output_type->SetGUID(
        MF_MT_SUBTYPE,
        MFVideoFormat_H264);
    if (FAILED(hr))
        return fail_hr(
            MfWriterStage::ConfigureOutputType,
            hr);

    status = set_bt709_studio_attributes(
        output_type.Get());
    if (!status.ok())
        return fail_status(
            MfWriterStage::ConfigureOutputType,
            status);

    hr = output_type->SetUINT32(
        MF_MT_AVG_BITRATE,
        config_.bitrate_bps);
    if (FAILED(hr))
        return fail_hr(
            MfWriterStage::ConfigureOutputType,
            hr);

    // P5D prefers High Profile for better compression efficiency on detailed
    // screen content. If AddStream rejects it, fall back to Main rather than
    // turning a quality preference into a recorder startup failure.
    active_profile_ =
        config_.prefer_high_profile
            ? MfH264Profile::High
            : MfH264Profile::Main;

    hr = output_type->SetUINT32(
        MF_MT_MPEG2_PROFILE,
        active_profile_ == MfH264Profile::High
            ? eAVEncH264VProfile_High
            : eAVEncH264VProfile_Main);
    if (FAILED(hr))
        return fail_hr(
            MfWriterStage::ConfigureOutputType,
            hr);

    hr = writer_->AddStream(
        output_type.Get(),
        &video_stream_index_);

    if (FAILED(hr) &&
        active_profile_ == MfH264Profile::High) {
        active_profile_ =
            MfH264Profile::Main;

        const HRESULT profile_hr =
            output_type->SetUINT32(
                MF_MT_MPEG2_PROFILE,
                eAVEncH264VProfile_Main);
        if (FAILED(profile_hr))
            return fail_hr(
                MfWriterStage::ConfigureOutputType,
                profile_hr);

        hr = writer_->AddStream(
            output_type.Get(),
            &video_stream_index_);
    }

    if (FAILED(hr))
        return fail_hr(
            MfWriterStage::AddOutputStream,
            hr);

    Microsoft::WRL::ComPtr<IMFMediaType> input_type;
    hr = MFCreateMediaType(
        input_type.GetAddressOf());
    if (FAILED(hr))
        return fail_hr(
            MfWriterStage::ConfigureInputType,
            hr);

    status = set_common_video_attributes(
        input_type.Get(),
        config_);
    if (!status.ok())
        return fail_status(
            MfWriterStage::ConfigureInputType,
            status);

    hr = input_type->SetGUID(
        MF_MT_SUBTYPE,
        MFVideoFormat_NV12);
    if (FAILED(hr))
        return fail_hr(
            MfWriterStage::ConfigureInputType,
            hr);

    status = set_bt709_studio_attributes(
        input_type.Get());
    if (!status.ok())
        return fail_status(
            MfWriterStage::ConfigureInputType,
            status);

    // MF_MT_DEFAULT_STRIDE is optional when the contiguous stride equals the
    // width in bytes (NV12 luma plane here). Omitting it avoids over-
    // constraining hardware encoder negotiation on some drivers.
    //
    // P5D.6 uses bitrate-controlled *unconstrained* VBR. Microsoft documents
    // this mode as targeting MF_MT_AVG_BITRATE / AVEncCommonMeanBitRate,
    // unlike quality-VBR where the mean bitrate is ignored. This matters for
    // desktop capture because almost-static frames can otherwise collapse to
    // a very low bitrate and quantize away 1px neutral borders/separators.
    //
    // Negotiate in tiers:
    //   1) VBR + mean bitrate + high quality-vs-speed preference,
    //   2) VBR + mean bitrate only,
    //   3) normal Sink Writer negotiation.
    // Rate-control preferences must never make recorder startup fail.
    Microsoft::WRL::ComPtr<IMFAttributes> encoding_parameters;

    auto make_bitrate_vbr_parameters =
        [&](bool include_quality_vs_speed)
            -> Microsoft::WRL::ComPtr<IMFAttributes> {
            Microsoft::WRL::ComPtr<IMFAttributes> parameters;
            HRESULT params_hr =
                MFCreateAttributes(
                    parameters.GetAddressOf(),
                    include_quality_vs_speed ? 3U : 2U);

            if (SUCCEEDED(params_hr)) {
                params_hr =
                    parameters->SetUINT32(
                        CODECAPI_AVEncCommonRateControlMode,
                        static_cast<UINT32>(
                            eAVEncCommonRateControlMode_UnconstrainedVBR));
            }

            if (SUCCEEDED(params_hr)) {
                params_hr =
                    parameters->SetUINT32(
                        CODECAPI_AVEncCommonMeanBitRate,
                        config_.bitrate_bps);
            }

            if (SUCCEEDED(params_hr) &&
                include_quality_vs_speed) {
                params_hr =
                    parameters->SetUINT32(
                        CODECAPI_AVEncCommonQualityVsSpeed,
                        config_.quality_vs_speed);
            }

            if (FAILED(params_hr))
                parameters.Reset();

            return parameters;
        };

    hr = E_FAIL;

    if (config_.prefer_bitrate_vbr) {
        encoding_parameters =
            make_bitrate_vbr_parameters(true);

        if (encoding_parameters) {
            hr = writer_->SetInputMediaType(
                video_stream_index_,
                input_type.Get(),
                encoding_parameters.Get());

            if (SUCCEEDED(hr)) {
                active_rate_control_ =
                    MfRateControlMode::UnconstrainedVbr;
                quality_vs_speed_applied_ = true;
            }
        }

        if (FAILED(hr)) {
            encoding_parameters =
                make_bitrate_vbr_parameters(false);

            if (encoding_parameters) {
                hr = writer_->SetInputMediaType(
                    video_stream_index_,
                    input_type.Get(),
                    encoding_parameters.Get());

                if (SUCCEEDED(hr)) {
                    active_rate_control_ =
                        MfRateControlMode::UnconstrainedVbr;
                    quality_vs_speed_applied_ = false;
                }
            }
        }
    }

    if (FAILED(hr)) {
        active_rate_control_ =
            MfRateControlMode::Default;
        quality_vs_speed_applied_ = false;

        hr = writer_->SetInputMediaType(
            video_stream_index_,
            input_type.Get(),
            nullptr);
    }

    if (FAILED(hr))
        return fail_hr(
            MfWriterStage::SetInputMediaType,
            hr);

    status = configure_audio_stream();
    if (!status.ok()) {
        teardown();
        return status;
    }

    status = create_surface_pool(device);
    if (!status.ok())
        return fail_status(
            MfWriterStage::CreateSurfacePool,
            status);

    status = create_audio_pool();
    if (!status.ok())
        return fail_status(
            MfWriterStage::CreateAudioSample,
            status);

    auto *callback =
        new (std::nothrow) ReleaseCallback(this);
    if (!callback)
        return fail_status(
            MfWriterStage::CreateSurfacePool,
            Status::failure(StatusCode::InternalError));
    release_callback_.Attach(callback);

    hr = writer_->BeginWriting();
    if (FAILED(hr))
        return fail_hr(
            MfWriterStage::BeginWriting,
            hr);

    submitted_frames_.store(0, std::memory_order_release);
    submitted_audio_samples_.store(0, std::memory_order_release);
    submitted_audio_frames_.store(0, std::memory_order_release);
    audio_backpressure_events_.store(0, std::memory_order_release);
    backpressure_events_.store(0, std::memory_order_release);
    sample_buffer_length_.store(0, std::memory_order_release);
    sample_buffer_max_length_.store(0, std::memory_order_release);
    failure_stage_.store(
        MfWriterStage::None,
        std::memory_order_release);
    audio_enabled_ = audio_config_.enabled;
    open_ = true;
    return Status::success();
}

Status MfH264Mp4Writer::configure_audio_stream() noexcept
{
    if (!audio_config_.enabled)
        return Status::success();

    if (!writer_ || !audio_config_.valid()) {
        failure_stage_.store(
            MfWriterStage::ConfigureAudioOutputType,
            std::memory_order_release);
        return Status::failure(StatusCode::InvalidArgument);
    }

    Microsoft::WRL::ComPtr<IMFMediaType> output_type;
    HRESULT hr =
        MFCreateMediaType(
            output_type.GetAddressOf());
    if (FAILED(hr)) {
        failure_stage_.store(
            MfWriterStage::ConfigureAudioOutputType,
            std::memory_order_release);
        return mf_failure(hr);
    }

    auto status =
        configure_mf_aac_output_type(
            output_type.Get(),
            audio_config_);
    if (!status.ok()) {
        failure_stage_.store(
            MfWriterStage::ConfigureAudioOutputType,
            std::memory_order_release);
        return status;
    }

    hr = writer_->AddStream(
        output_type.Get(),
        &audio_stream_index_);
    if (FAILED(hr)) {
        failure_stage_.store(
            MfWriterStage::AddAudioOutputStream,
            std::memory_order_release);
        return mf_failure(hr);
    }

    Microsoft::WRL::ComPtr<IMFMediaType> input_type;
    hr = MFCreateMediaType(
        input_type.GetAddressOf());
    if (FAILED(hr)) {
        failure_stage_.store(
            MfWriterStage::ConfigureAudioInputType,
            std::memory_order_release);
        return mf_failure(hr);
    }

    status =
        configure_mf_pcm16_input_type(
            input_type.Get(),
            audio_config_);
    if (!status.ok()) {
        failure_stage_.store(
            MfWriterStage::ConfigureAudioInputType,
            std::memory_order_release);
        return status;
    }

    hr = writer_->SetInputMediaType(
        audio_stream_index_,
        input_type.Get(),
        nullptr);
    if (FAILED(hr)) {
        // No partial in-place bitrate fallback is attempted after AddStream.
        // A caller that chooses another supported AAC bitrate must retry the
        // complete open() on a fresh writer instance, preserving one coherent
        // video+audio negotiation transaction.
        audio_stream_index_ = invalid_stream_index;
        failure_stage_.store(
            MfWriterStage::SetAudioInputMediaType,
            std::memory_order_release);
        return mf_failure(hr);
    }

    active_audio_bitrate_bps_ =
        audio_config_.bitrate_bps;
    return Status::success();
}

Status MfH264Mp4Writer::create_video_processor(
    ID3D11Device *device) noexcept
{
    if (!device)
        return Status::failure(StatusCode::InvalidArgument);

    HRESULT hr = device->QueryInterface(
        IID_PPV_ARGS(video_device_.GetAddressOf()));
    if (FAILED(hr) || !video_device_)
        return unsupported(hr);

    Microsoft::WRL::ComPtr<ID3D11DeviceContext> immediate;
    device->GetImmediateContext(immediate.GetAddressOf());
    if (!immediate)
        return Status::failure(StatusCode::GraphicsDeviceUnavailable);

    hr = immediate->QueryInterface(
        IID_PPV_ARGS(video_context_.GetAddressOf()));
    if (FAILED(hr) || !video_context_)
        return unsupported(hr);

    /*
     * ID3D11VideoContext1 gives an unambiguous DXGI color-space contract.
     * Windows 10 should expose it; retain an explicit legacy D3D11 color-space
     * fallback so older/quirky drivers do not turn color authority into a
     * startup failure.
     */
    (void)video_context_.As(
        &video_context1_);

    D3D11_VIDEO_PROCESSOR_CONTENT_DESC content{};
    content.InputFrameFormat =
        D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
    content.InputFrameRate.Numerator =
        config_.frame_rate.numerator;
    content.InputFrameRate.Denominator =
        config_.frame_rate.denominator;
    content.InputWidth = config_.size.width;
    content.InputHeight = config_.size.height;
    content.OutputFrameRate.Numerator =
        config_.frame_rate.numerator;
    content.OutputFrameRate.Denominator =
        config_.frame_rate.denominator;
    content.OutputWidth = config_.size.width;
    content.OutputHeight = config_.size.height;
    content.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;

    hr = video_device_->CreateVideoProcessorEnumerator(
        &content,
        video_enumerator_.GetAddressOf());
    if (FAILED(hr) || !video_enumerator_)
        return unsupported(hr);

    UINT input_support = 0;
    hr = video_enumerator_->CheckVideoProcessorFormat(
        DXGI_FORMAT_B8G8R8A8_UNORM,
        &input_support);
    if (FAILED(hr) ||
        (input_support &
         D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_INPUT) == 0) {
        return unsupported(FAILED(hr) ? hr : E_NOTIMPL);
    }

    UINT output_support = 0;
    hr = video_enumerator_->CheckVideoProcessorFormat(
        DXGI_FORMAT_NV12,
        &output_support);
    if (FAILED(hr) ||
        (output_support &
         D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_OUTPUT) == 0) {
        return unsupported(FAILED(hr) ? hr : E_NOTIMPL);
    }

    D3D11_VIDEO_PROCESSOR_CAPS caps{};
    hr = video_enumerator_->GetVideoProcessorCaps(&caps);
    if (FAILED(hr) || caps.RateConversionCapsCount == 0)
        return unsupported(FAILED(hr) ? hr : E_NOTIMPL);

    hr = video_device_->CreateVideoProcessor(
        video_enumerator_.Get(),
        0,
        video_processor_.GetAddressOf());
    if (FAILED(hr) || !video_processor_)
        return unsupported(hr);

    RECT rect{
        0,
        0,
        static_cast<LONG>(config_.size.width),
        static_cast<LONG>(config_.size.height)
    };

    video_context_->VideoProcessorSetStreamFrameFormat(
        video_processor_.Get(),
        0,
        D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE);
    video_context_->VideoProcessorSetStreamSourceRect(
        video_processor_.Get(),
        0,
        TRUE,
        &rect);
    video_context_->VideoProcessorSetStreamDestRect(
        video_processor_.Get(),
        0,
        TRUE,
        &rect);
    video_context_->VideoProcessorSetOutputTargetRect(
        video_processor_.Get(),
        TRUE,
        &rect);

    /*
     * P5D.7: explicit desktop RGB -> video YCbCr contract.
     *
     * Source compositor texture:
     *   full-range SDR RGB, BT.709/sRGB primaries.
     *
     * Encoder surface:
     *   studio-range NV12, BT.709 matrix/transfer.
     *
     * The prior implicit/default state could let the processor emit full-range
     * Y while the H.264 path was interpreted as studio-range, collapsing
     * near-white gray UI into white and near-black hierarchy into black.
     */
    if (video_context1_) {
        video_context1_->VideoProcessorSetStreamColorSpace1(
            video_processor_.Get(),
            0,
            DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
        video_context1_->VideoProcessorSetOutputColorSpace1(
            video_processor_.Get(),
            DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709);

        active_color_pipeline_ =
            MfColorPipelineMode::Context1Explicit;
    } else {
        D3D11_VIDEO_PROCESSOR_COLOR_SPACE input_color{};
        input_color.Usage = 1;
        input_color.RGB_Range = 0; // full-range RGB
        input_color.YCbCr_Matrix = 1; // BT.709 when conversion is required
        input_color.YCbCr_xvYCC = 0;
        input_color.Nominal_Range =
            D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_0_255;

        D3D11_VIDEO_PROCESSOR_COLOR_SPACE output_color{};
        output_color.Usage = 1;
        output_color.RGB_Range = 0;
        output_color.YCbCr_Matrix = 1; // BT.709
        output_color.YCbCr_xvYCC = 0;
        output_color.Nominal_Range =
            D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_16_235;

        video_context_->VideoProcessorSetStreamColorSpace(
            video_processor_.Get(),
            0,
            &input_color);
        video_context_->VideoProcessorSetOutputColorSpace(
            video_processor_.Get(),
            &output_color);

        active_color_pipeline_ =
            MfColorPipelineMode::LegacyExplicit;
    }

    color_pipeline_authoritative_ = true;

    return Status::success();
}

Status MfH264Mp4Writer::create_audio_pool() noexcept
{
    if (!audio_config_.enabled)
        return Status::success();

    const std::uint64_t bytes_per_slot =
        static_cast<std::uint64_t>(
            audio_config_.max_frames_per_sample) *
        static_cast<std::uint64_t>(
            audio_config_.channels) *
        sizeof(std::int16_t);
    if (bytes_per_slot == 0 ||
        bytes_per_slot >
            static_cast<std::uint64_t>(
                std::numeric_limits<DWORD>::max())) {
        return Status::failure(StatusCode::InvalidArgument);
    }

    for (std::size_t i = 0;
         i < audio_config_.sample_pool_count;
         ++i) {
        HRESULT hr =
            MFCreateMemoryBuffer(
                static_cast<DWORD>(bytes_per_slot),
                audio_slots_[i].buffer.GetAddressOf());
        if (FAILED(hr) ||
            !audio_slots_[i].buffer) {
            return mf_failure(
                FAILED(hr) ? hr : E_OUTOFMEMORY);
        }

        audio_slots_[i].in_use.store(
            false,
            std::memory_order_release);
    }

    return Status::success();
}

Status MfH264Mp4Writer::create_surface_pool(
    ID3D11Device *device) noexcept
{
    D3D11_TEXTURE2D_DESC input_desc{};
    input_desc.Width = config_.size.width;
    input_desc.Height = config_.size.height;
    input_desc.MipLevels = 1;
    input_desc.ArraySize = 1;
    input_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    input_desc.SampleDesc.Count = 1;
    input_desc.Usage = D3D11_USAGE_DEFAULT;
    input_desc.BindFlags = 0;

    HRESULT hr = device->CreateTexture2D(
        &input_desc,
        nullptr,
        input_copy_.GetAddressOf());
    if (FAILED(hr))
        return mf_failure(hr);

    D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC input_view_desc{};
    input_view_desc.FourCC = 0;
    input_view_desc.ViewDimension =
        D3D11_VPIV_DIMENSION_TEXTURE2D;
    input_view_desc.Texture2D.MipSlice = 0;
    input_view_desc.Texture2D.ArraySlice = 0;

    hr = video_device_->CreateVideoProcessorInputView(
        input_copy_.Get(),
        video_enumerator_.Get(),
        &input_view_desc,
        input_view_.GetAddressOf());
    if (FAILED(hr) || !input_view_)
        return mf_failure(hr);

    D3D11_TEXTURE2D_DESC output_desc{};
    output_desc.Width = config_.size.width;
    output_desc.Height = config_.size.height;
    output_desc.MipLevels = 1;
    output_desc.ArraySize = 1;
    output_desc.Format = DXGI_FORMAT_NV12;
    output_desc.SampleDesc.Count = 1;
    output_desc.Usage = D3D11_USAGE_DEFAULT;
    output_desc.BindFlags = D3D11_BIND_RENDER_TARGET;

    D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC output_view_desc{};
    output_view_desc.ViewDimension =
        D3D11_VPOV_DIMENSION_TEXTURE2D;
    output_view_desc.Texture2D.MipSlice = 0;

    for (std::size_t i = 0;
         i < config_.surface_count;
         ++i) {
        hr = device->CreateTexture2D(
            &output_desc,
            nullptr,
            surfaces_[i].texture.GetAddressOf());
        if (FAILED(hr))
            return mf_failure(hr);

        hr = video_device_->CreateVideoProcessorOutputView(
            surfaces_[i].texture.Get(),
            video_enumerator_.Get(),
            &output_view_desc,
            surfaces_[i].output_view.GetAddressOf());
        if (FAILED(hr) || !surfaces_[i].output_view)
            return mf_failure(hr);

        surfaces_[i].in_use.store(
            false,
            std::memory_order_release);
    }

    return Status::success();
}

Status MfH264Mp4Writer::convert_to_nv12(
    ID3D11DeviceContext *context,
    ID3D11Texture2D *source,
    std::size_t output_slot) noexcept
{
    if (!context ||
        !source ||
        output_slot >= config_.surface_count ||
        !input_copy_ ||
        !input_view_ ||
        !surfaces_[output_slot].output_view ||
        !video_context_ ||
        !video_processor_) {
        return Status::failure(StatusCode::InvalidArgument);
    }

    D3D11_TEXTURE2D_DESC source_desc{};
    source->GetDesc(&source_desc);
    if (source_desc.Width != config_.size.width ||
        source_desc.Height != config_.size.height ||
        source_desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM ||
        source_desc.SampleDesc.Count != 1) {
        return Status::failure(StatusCode::InvalidArgument);
    }

    context->CopyResource(
        input_copy_.Get(),
        source);

    D3D11_VIDEO_PROCESSOR_STREAM stream{};
    stream.Enable = TRUE;
    stream.OutputIndex = 0;
    stream.InputFrameOrField = 0;
    stream.PastFrames = 0;
    stream.FutureFrames = 0;
    stream.pInputSurface = input_view_.Get();

    const HRESULT hr =
        video_context_->VideoProcessorBlt(
            video_processor_.Get(),
            surfaces_[output_slot].output_view.Get(),
            0,
            1,
            &stream);

    if (FAILED(hr)) {
        failure_stage_.store(
            MfWriterStage::ConvertToNv12,
            std::memory_order_release);
        return mf_failure(hr);
    }

    return Status::success();
}

std::size_t MfH264Mp4Writer::acquire_surface() noexcept
{
    for (std::size_t i = 0;
         i < config_.surface_count;
         ++i) {
        bool expected = false;
        if (surfaces_[i].in_use.compare_exchange_strong(
                expected,
                true,
                std::memory_order_acq_rel,
                std::memory_order_relaxed)) {
            return i;
        }
    }

    return max_surface_count;
}

void MfH264Mp4Writer::release_surface(
    std::size_t index) noexcept
{
    if (index >= max_surface_count)
        return;

    surfaces_[index].in_use.store(
        false,
        std::memory_order_release);
}

void MfH264Mp4Writer::on_sample_released(
    std::uint32_t slot) noexcept
{
    release_surface(
        static_cast<std::size_t>(slot));
}

std::size_t MfH264Mp4Writer::acquire_audio_slot() noexcept
{
    for (std::size_t i = 0;
         i < audio_config_.sample_pool_count;
         ++i) {
        bool expected = false;
        if (audio_slots_[i].in_use.compare_exchange_strong(
                expected,
                true,
                std::memory_order_acq_rel,
                std::memory_order_relaxed)) {
            return i;
        }
    }

    return max_audio_slot_count;
}

void MfH264Mp4Writer::release_audio_slot(
    std::size_t index) noexcept
{
    if (index >= max_audio_slot_count)
        return;

    audio_slots_[index].in_use.store(
        false,
        std::memory_order_release);
}

void MfH264Mp4Writer::on_audio_sample_released(
    std::uint32_t slot) noexcept
{
    release_audio_slot(
        static_cast<std::size_t>(slot));
}

std::uint32_t
MfH264Mp4Writer::in_flight_audio_samples() const noexcept
{
    std::uint32_t count = 0;
    for (std::size_t i = 0;
         i < audio_config_.sample_pool_count;
         ++i) {
        if (audio_slots_[i].in_use.load(
                std::memory_order_acquire)) {
            ++count;
        }
    }
    return count;
}

std::uint32_t
MfH264Mp4Writer::in_flight_surfaces() const noexcept
{
    std::uint32_t count = 0;
    for (std::size_t i = 0;
         i < config_.surface_count;
         ++i) {
        if (surfaces_[i].in_use.load(
                std::memory_order_acquire)) {
            ++count;
        }
    }
    return count;
}

Status MfH264Mp4Writer::write_frame(
    ID3D11DeviceContext *context,
    ID3D11Texture2D *source,
    arssyut::core::TimePoint relative_pts,
    std::int64_t duration_ticks) noexcept
{
    if (!open_ ||
        !context ||
        !source ||
        duration_ticks <= 0) {
        return Status::failure(StatusCode::InvalidArgument);
    }

    const std::size_t slot = acquire_surface();
    if (slot >= max_surface_count) {
        backpressure_events_.fetch_add(
            1,
            std::memory_order_relaxed);
        return Status::failure(
            StatusCode::EncoderBackpressure);
    }

    const Status convert_status =
        convert_to_nv12(
            context,
            source,
            slot);
    if (!convert_status.ok()) {
        release_surface(slot);
        return convert_status;
    }

    Microsoft::WRL::ComPtr<IMFTrackedSample> tracked;
    HRESULT hr = MFCreateTrackedSample(
        tracked.GetAddressOf());
    if (FAILED(hr)) {
        failure_stage_.store(
            MfWriterStage::CreateTrackedSample,
            std::memory_order_release);
        release_surface(slot);
        return mf_failure(hr);
    }

    Microsoft::WRL::ComPtr<IMFSample> sample;
    hr = tracked.As(&sample);
    if (FAILED(hr) || !sample) {
        failure_stage_.store(
            MfWriterStage::CreateTrackedSample,
            std::memory_order_release);
        release_surface(slot);
        return mf_failure(FAILED(hr) ? hr : E_NOINTERFACE);
    }

    Microsoft::WRL::ComPtr<IMFMediaBuffer> buffer;
    hr = MFCreateDXGISurfaceBuffer(
        __uuidof(ID3D11Texture2D),
        surfaces_[slot].texture.Get(),
        0,
        FALSE,
        buffer.GetAddressOf());
    if (FAILED(hr)) {
        failure_stage_.store(
            MfWriterStage::CreateDxgiBuffer,
            std::memory_order_release);
        release_surface(slot);
        return mf_failure(hr);
    }

    // MFCreateDXGISurfaceBuffer wraps the surface, but the media buffer's
    // current length starts at zero. Sink Writer treats a zero-length sample
    // as invalid even though the D3D11 surface contains a complete NV12 frame.
    // Use IMF2DBuffer to obtain the actual contiguous byte count (including any
    // driver-defined pitch) and explicitly mark that payload as valid.
    Microsoft::WRL::ComPtr<IMF2DBuffer> buffer_2d;
    hr = buffer.As(&buffer_2d);
    if (FAILED(hr) || !buffer_2d) {
        failure_stage_.store(
            MfWriterStage::SetBufferLength,
            std::memory_order_release);
        release_surface(slot);
        return mf_failure(FAILED(hr) ? hr : E_NOINTERFACE);
    }

    DWORD contiguous_length = 0;
    hr = buffer_2d->GetContiguousLength(
        &contiguous_length);
    if (FAILED(hr) || contiguous_length == 0) {
        failure_stage_.store(
            MfWriterStage::SetBufferLength,
            std::memory_order_release);
        release_surface(slot);
        return mf_failure(FAILED(hr) ? hr : E_INVALIDARG);
    }

    DWORD max_length = 0;
    hr = buffer->GetMaxLength(&max_length);
    if (FAILED(hr) ||
        max_length == 0 ||
        contiguous_length > max_length) {
        failure_stage_.store(
            MfWriterStage::SetBufferLength,
            std::memory_order_release);
        release_surface(slot);
        return mf_failure(FAILED(hr) ? hr : E_INVALIDARG);
    }

    hr = buffer->SetCurrentLength(
        contiguous_length);
    if (FAILED(hr)) {
        failure_stage_.store(
            MfWriterStage::SetBufferLength,
            std::memory_order_release);
        release_surface(slot);
        return mf_failure(hr);
    }

    sample_buffer_length_.store(
        contiguous_length,
        std::memory_order_relaxed);
    sample_buffer_max_length_.store(
        max_length,
        std::memory_order_relaxed);

    hr = sample->AddBuffer(buffer.Get());
    if (FAILED(hr)) {
        failure_stage_.store(
            MfWriterStage::ConfigureSample,
            std::memory_order_release);
        release_surface(slot);
        return mf_failure(hr);
    }

    hr = sample->SetUINT32(
        kArssyutSurfaceSlot,
        static_cast<UINT32>(slot));
    if (FAILED(hr)) {
        failure_stage_.store(
            MfWriterStage::ConfigureSample,
            std::memory_order_release);
        release_surface(slot);
        return mf_failure(hr);
    }

    hr = sample->SetSampleTime(
        relative_pts.ticks_100ns);
    if (FAILED(hr)) {
        failure_stage_.store(
            MfWriterStage::ConfigureSample,
            std::memory_order_release);
        release_surface(slot);
        return mf_failure(hr);
    }

    hr = sample->SetSampleDuration(
        duration_ticks);
    if (FAILED(hr)) {
        failure_stage_.store(
            MfWriterStage::ConfigureSample,
            std::memory_order_release);
        release_surface(slot);
        return mf_failure(hr);
    }

    hr = tracked->SetAllocator(
        release_callback_.Get(),
        nullptr);
    if (FAILED(hr)) {
        failure_stage_.store(
            MfWriterStage::ConfigureSample,
            std::memory_order_release);
        release_surface(slot);
        return mf_failure(hr);
    }

    hr = writer_->WriteSample(
        video_stream_index_,
        sample.Get());
    if (FAILED(hr)) {
        failure_stage_.store(
            MfWriterStage::WriteSample,
            std::memory_order_release);
        release_surface(slot);
        return mf_failure(hr);
    }

    submitted_frames_.fetch_add(
        1,
        std::memory_order_relaxed);

    return Status::success();
}

Status MfH264Mp4Writer::write_audio_pcm16(
    std::span<const std::int16_t> interleaved,
    arssyut::core::TimePoint relative_pts,
    std::int64_t duration_ticks) noexcept
{
    if (!open_ ||
        !audio_enabled_ ||
        !writer_ ||
        audio_stream_index_ == invalid_stream_index ||
        relative_pts.ticks_100ns < 0 ||
        duration_ticks <= 0 ||
        interleaved.empty() ||
        audio_config_.channels == 0 ||
        (interleaved.size() % audio_config_.channels) != 0) {
        return Status::failure(StatusCode::InvalidArgument);
    }

    const std::uint64_t frame_count =
        interleaved.size() /
        static_cast<std::size_t>(audio_config_.channels);
    if (frame_count == 0 ||
        frame_count >
            static_cast<std::uint64_t>(
                std::numeric_limits<std::uint32_t>::max())) {
        return Status::failure(StatusCode::InvalidArgument);
    }

    const auto floor_duration =
        core::audio::frames_to_ticks_floor(
            frame_count,
            audio_config_.sample_rate);
    if (floor_duration >
        static_cast<std::uint64_t>(
            std::numeric_limits<std::int64_t>::max())) {
        return Status::failure(StatusCode::InvalidArgument);
    }

    const auto floor_i64 =
        static_cast<std::int64_t>(floor_duration);
    if (duration_ticks < floor_i64 ||
        duration_ticks > floor_i64 + 1) {
        return Status::failure(StatusCode::InvalidArgument);
    }

    if (have_audio_timeline_ &&
        relative_pts.ticks_100ns <
            last_audio_end_100ns_) {
        return Status::failure(
            StatusCode::InvalidStateTransition);
    }

    if (relative_pts.ticks_100ns >
        std::numeric_limits<std::int64_t>::max() -
            duration_ticks) {
        return Status::failure(StatusCode::InvalidArgument);
    }

    const std::size_t byte_count =
        interleaved.size_bytes();
    if (byte_count == 0 ||
        byte_count >
            static_cast<std::size_t>(
                std::numeric_limits<DWORD>::max())) {
        return Status::failure(StatusCode::InvalidArgument);
    }

    Microsoft::WRL::ComPtr<IMFSample> sample;
    HRESULT hr =
        MFCreateSample(
            sample.GetAddressOf());
    if (FAILED(hr)) {
        failure_stage_.store(
            MfWriterStage::CreateAudioSample,
            std::memory_order_release);
        return mf_failure(hr);
    }

    Microsoft::WRL::ComPtr<IMFMediaBuffer> buffer;
    hr = MFCreateMemoryBuffer(
        static_cast<DWORD>(byte_count),
        buffer.GetAddressOf());
    if (FAILED(hr)) {
        failure_stage_.store(
            MfWriterStage::CreateAudioSample,
            std::memory_order_release);
        return mf_failure(hr);
    }

    BYTE *destination = nullptr;
    DWORD max_length = 0;
    DWORD current_length = 0;
    hr = buffer->Lock(
        &destination,
        &max_length,
        &current_length);
    if (FAILED(hr) ||
        !destination ||
        max_length < byte_count) {
        if (SUCCEEDED(hr))
            buffer->Unlock();
        failure_stage_.store(
            MfWriterStage::CreateAudioSample,
            std::memory_order_release);
        return mf_failure(
            FAILED(hr) ? hr : E_UNEXPECTED);
    }

    std::memcpy(
        destination,
        interleaved.data(),
        byte_count);
    const HRESULT unlock_hr =
        buffer->Unlock();
    if (FAILED(unlock_hr)) {
        failure_stage_.store(
            MfWriterStage::CreateAudioSample,
            std::memory_order_release);
        return mf_failure(unlock_hr);
    }

    hr = buffer->SetCurrentLength(
        static_cast<DWORD>(byte_count));
    if (FAILED(hr)) {
        failure_stage_.store(
            MfWriterStage::CreateAudioSample,
            std::memory_order_release);
        return mf_failure(hr);
    }

    hr = sample->AddBuffer(buffer.Get());
    if (FAILED(hr)) {
        failure_stage_.store(
            MfWriterStage::CreateAudioSample,
            std::memory_order_release);
        return mf_failure(hr);
    }

    hr = sample->SetSampleTime(
        relative_pts.ticks_100ns);
    if (FAILED(hr)) {
        failure_stage_.store(
            MfWriterStage::CreateAudioSample,
            std::memory_order_release);
        return mf_failure(hr);
    }

    hr = sample->SetSampleDuration(
        duration_ticks);
    if (FAILED(hr)) {
        failure_stage_.store(
            MfWriterStage::CreateAudioSample,
            std::memory_order_release);
        return mf_failure(hr);
    }

    hr = writer_->WriteSample(
        audio_stream_index_,
        sample.Get());
    if (FAILED(hr)) {
        failure_stage_.store(
            MfWriterStage::WriteAudioSample,
            std::memory_order_release);
        return mf_failure(hr);
    }

    submitted_audio_samples_.fetch_add(
        1,
        std::memory_order_relaxed);
    submitted_audio_frames_.fetch_add(
        frame_count,
        std::memory_order_relaxed);

    last_audio_end_100ns_ =
        relative_pts.ticks_100ns +
        duration_ticks;
    have_audio_timeline_ = true;

    return Status::success();
}

Status MfH264Mp4Writer::finalize() noexcept
{
    if (!open_) {
        teardown();
        return Status::success();
    }

    open_ = false;

    HRESULT hr = S_OK;
    if (writer_)
        hr = writer_->Finalize();

    writer_.Reset();

    const auto deadline =
        std::chrono::steady_clock::now() +
        std::chrono::seconds(3);

    while (in_flight_surfaces() != 0 &&
           std::chrono::steady_clock::now() <
               deadline) {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(1));
    }

    if (release_callback_) {
        auto *callback =
            static_cast<ReleaseCallback *>(
                release_callback_.Get());
        callback->detach();
    }

    teardown();

    if (FAILED(hr)) {
        failure_stage_.store(
            MfWriterStage::Finalize,
            std::memory_order_release);
        return mf_failure(hr);
    }

    return Status::success();
}

void MfH264Mp4Writer::teardown() noexcept
{
    writer_.Reset();

    if (release_callback_) {
        auto *callback =
            static_cast<ReleaseCallback *>(
                release_callback_.Get());
        callback->detach();
    }
    release_callback_.Reset();

    input_view_.Reset();
    input_copy_.Reset();

    for (auto &slot : surfaces_) {
        slot.output_view.Reset();
        slot.texture.Reset();
        slot.in_use.store(
            false,
            std::memory_order_release);
    }

    video_processor_.Reset();
    video_enumerator_.Reset();
    video_context1_.Reset();
    video_context_.Reset();
    video_device_.Reset();

    dxgi_manager_.Reset();

    video_stream_index_ = 0;
    audio_stream_index_ = invalid_stream_index;
    have_audio_timeline_ = false;
    last_audio_end_100ns_ = 0;

    if (mf_started_) {
        MFShutdown();
        mf_started_ = false;
    }

    open_ = false;
}

} // namespace arssyut::windows

#endif
