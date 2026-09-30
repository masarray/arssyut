#include "platform/windows/media/mf_h264_mp4_writer.hpp"

#ifdef _WIN32

#include "core/result/status.hpp"

#include <mfapi.h>
#include <mferror.h>
#include <mfreadwrite.h>

#include <algorithm>
#include <chrono>
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

[[nodiscard]] Status mf_failure(HRESULT hr) noexcept
{
    return Status::failure(
        StatusCode::MediaFoundationFailure,
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

} // namespace

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

        UINT32 slot = 0;
        if (FAILED(sample->GetUINT32(
                kArssyutSurfaceSlot,
                &slot))) {
            return S_OK;
        }

        auto *owner =
            owner_.load(std::memory_order_acquire);
        if (owner)
            owner->on_sample_released(slot);

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
    MfVideoWriterConfig config) noexcept
{
    if (open_ ||
        !device ||
        path.empty() ||
        !config.size.valid() ||
        !config.frame_rate.valid() ||
        config.bitrate_bps == 0) {
        return Status::failure(StatusCode::InvalidArgument);
    }

    config.surface_count =
        std::clamp<std::uint32_t>(
            config.surface_count,
            3U,
            static_cast<std::uint32_t>(
                max_surface_count));

    config_ = config;
    path_ = path;

    HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_FULL);
    if (FAILED(hr))
        return mf_failure(hr);
    mf_started_ = true;

    hr = MFCreateDXGIDeviceManager(
        &dxgi_reset_token_,
        dxgi_manager_.GetAddressOf());
    if (FAILED(hr)) {
        teardown();
        return mf_failure(hr);
    }

    hr = dxgi_manager_->ResetDevice(
        device,
        dxgi_reset_token_);
    if (FAILED(hr)) {
        teardown();
        return mf_failure(hr);
    }

    Microsoft::WRL::ComPtr<IMFAttributes> attributes;
    hr = MFCreateAttributes(
        attributes.GetAddressOf(),
        6);
    if (FAILED(hr)) {
        teardown();
        return mf_failure(hr);
    }

    hr = attributes->SetUINT32(
        MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS,
        TRUE);
    if (FAILED(hr)) {
        teardown();
        return mf_failure(hr);
    }

    hr = attributes->SetUINT32(
        MF_SINK_WRITER_DISABLE_THROTTLING,
        TRUE);
    if (FAILED(hr)) {
        teardown();
        return mf_failure(hr);
    }

    hr = attributes->SetUINT32(
        MF_LOW_LATENCY,
        TRUE);
    if (FAILED(hr)) {
        teardown();
        return mf_failure(hr);
    }

    hr = attributes->SetUnknown(
        MF_SINK_WRITER_D3D_MANAGER,
        dxgi_manager_.Get());
    if (FAILED(hr)) {
        teardown();
        return mf_failure(hr);
    }

    hr = MFCreateSinkWriterFromURL(
        path_.c_str(),
        nullptr,
        attributes.Get(),
        writer_.GetAddressOf());
    if (FAILED(hr)) {
        teardown();
        return mf_failure(hr);
    }

    Microsoft::WRL::ComPtr<IMFMediaType> output_type;
    hr = MFCreateMediaType(
        output_type.GetAddressOf());
    if (FAILED(hr)) {
        teardown();
        return mf_failure(hr);
    }

    Status status =
        set_common_video_attributes(
            output_type.Get(),
            config_);
    if (!status.ok()) {
        teardown();
        return status;
    }

    hr = output_type->SetGUID(
        MF_MT_SUBTYPE,
        MFVideoFormat_H264);
    if (FAILED(hr)) {
        teardown();
        return mf_failure(hr);
    }

    hr = output_type->SetUINT32(
        MF_MT_AVG_BITRATE,
        config_.bitrate_bps);
    if (FAILED(hr)) {
        teardown();
        return mf_failure(hr);
    }

    hr = writer_->AddStream(
        output_type.Get(),
        &stream_index_);
    if (FAILED(hr)) {
        teardown();
        return mf_failure(hr);
    }

    Microsoft::WRL::ComPtr<IMFMediaType> input_type;
    hr = MFCreateMediaType(
        input_type.GetAddressOf());
    if (FAILED(hr)) {
        teardown();
        return mf_failure(hr);
    }

    status =
        set_common_video_attributes(
            input_type.Get(),
            config_);
    if (!status.ok()) {
        teardown();
        return status;
    }

    hr = input_type->SetGUID(
        MF_MT_SUBTYPE,
        MFVideoFormat_ARGB32);
    if (FAILED(hr)) {
        teardown();
        return mf_failure(hr);
    }

    hr = input_type->SetUINT32(
        MF_MT_DEFAULT_STRIDE,
        config_.size.width * 4U);
    if (FAILED(hr)) {
        teardown();
        return mf_failure(hr);
    }

    hr = writer_->SetInputMediaType(
        stream_index_,
        input_type.Get(),
        nullptr);
    if (FAILED(hr)) {
        teardown();
        return mf_failure(hr);
    }

    status = create_surface_pool(device);
    if (!status.ok()) {
        teardown();
        return status;
    }

    auto *callback =
        new (std::nothrow) ReleaseCallback(this);
    if (!callback) {
        teardown();
        return Status::failure(StatusCode::InternalError);
    }
    release_callback_.Attach(callback);

    hr = writer_->BeginWriting();
    if (FAILED(hr)) {
        teardown();
        return mf_failure(hr);
    }

    submitted_frames_.store(0, std::memory_order_release);
    backpressure_events_.store(0, std::memory_order_release);
    open_ = true;
    return Status::success();
}

Status MfH264Mp4Writer::create_surface_pool(
    ID3D11Device *device) noexcept
{
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = config_.size.width;
    desc.Height = config_.size.height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags =
        D3D11_BIND_SHADER_RESOURCE |
        D3D11_BIND_RENDER_TARGET;

    for (std::size_t i = 0;
         i < config_.surface_count;
         ++i) {
        const HRESULT hr = device->CreateTexture2D(
            &desc,
            nullptr,
            surfaces_[i].texture.GetAddressOf());

        if (FAILED(hr))
            return mf_failure(hr);

        surfaces_[i].in_use.store(
            false,
            std::memory_order_release);
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

    context->CopyResource(
        surfaces_[slot].texture.Get(),
        source);

    Microsoft::WRL::ComPtr<IMFTrackedSample> tracked;
    HRESULT hr = MFCreateTrackedSample(
        tracked.GetAddressOf());
    if (FAILED(hr)) {
        release_surface(slot);
        return mf_failure(hr);
    }

    Microsoft::WRL::ComPtr<IMFSample> sample;
    hr = tracked.As(&sample);
    if (FAILED(hr) || !sample) {
        release_surface(slot);
        return mf_failure(hr);
    }

    Microsoft::WRL::ComPtr<IMFMediaBuffer> buffer;
    hr = MFCreateDXGISurfaceBuffer(
        __uuidof(ID3D11Texture2D),
        surfaces_[slot].texture.Get(),
        0,
        FALSE,
        buffer.GetAddressOf());
    if (FAILED(hr)) {
        release_surface(slot);
        return mf_failure(hr);
    }

    hr = sample->AddBuffer(buffer.Get());
    if (FAILED(hr)) {
        release_surface(slot);
        return mf_failure(hr);
    }

    hr = sample->SetUINT32(
        kArssyutSurfaceSlot,
        static_cast<UINT32>(slot));
    if (FAILED(hr)) {
        release_surface(slot);
        return mf_failure(hr);
    }

    hr = sample->SetSampleTime(
        relative_pts.ticks_100ns);
    if (FAILED(hr)) {
        release_surface(slot);
        return mf_failure(hr);
    }

    hr = sample->SetSampleDuration(
        duration_ticks);
    if (FAILED(hr)) {
        release_surface(slot);
        return mf_failure(hr);
    }

    hr = tracked->SetAllocator(
        release_callback_.Get(),
        nullptr);
    if (FAILED(hr)) {
        release_surface(slot);
        return mf_failure(hr);
    }

    hr = writer_->WriteSample(
        stream_index_,
        sample.Get());
    if (FAILED(hr))
        return mf_failure(hr);

    submitted_frames_.fetch_add(
        1,
        std::memory_order_relaxed);

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

    if (FAILED(hr))
        return mf_failure(hr);

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

    for (auto &slot : surfaces_) {
        slot.texture.Reset();
        slot.in_use.store(
            false,
            std::memory_order_release);
    }

    dxgi_manager_.Reset();

    if (mf_started_) {
        MFShutdown();
        mf_started_ = false;
    }

    open_ = false;
}

} // namespace arssyut::windows

#endif
