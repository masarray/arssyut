#ifdef _WIN32

#include <Windows.h>
#include <ks.h>
#include <ksmedia.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mftransform.h>
#include <wmcodecdsp.h>
#include <wrl/client.h>

#include <cstdint>
#include <iostream>

namespace {

using Microsoft::WRL::ComPtr;

class ScopedCom final {
public:
    ScopedCom() noexcept
        : hr_(CoInitializeEx(
              nullptr,
              COINIT_MULTITHREADED))
    {
        owns_ = hr_ == S_OK || hr_ == S_FALSE;
    }

    ~ScopedCom()
    {
        if (owns_)
            CoUninitialize();
    }

    [[nodiscard]] HRESULT status() const noexcept
    {
        return hr_;
    }

private:
    HRESULT hr_ = E_FAIL;
    bool owns_ = false;
};

class ScopedMf final {
public:
    ScopedMf() noexcept
        : hr_(MFStartup(
              MF_VERSION,
              MFSTARTUP_FULL))
    {
        started_ = SUCCEEDED(hr_);
    }

    ~ScopedMf()
    {
        if (started_)
            MFShutdown();
    }

    [[nodiscard]] HRESULT status() const noexcept
    {
        return hr_;
    }

private:
    HRESULT hr_ = E_FAIL;
    bool started_ = false;
};

[[nodiscard]] HRESULT configure_float_stereo_type(
    IMFMediaType *type,
    std::uint32_t sample_rate) noexcept
{
    if (!type || sample_rate == 0)
        return E_INVALIDARG;

    constexpr std::uint32_t channels = 2;
    constexpr std::uint32_t bits = 32;
    constexpr std::uint32_t block_align =
        channels * (bits / 8U);

    const std::uint64_t avg_bytes =
        static_cast<std::uint64_t>(sample_rate) *
        block_align;
    if (avg_bytes >
        static_cast<std::uint64_t>(UINT32_MAX))
        return E_INVALIDARG;

    HRESULT hr =
        type->SetGUID(
            MF_MT_MAJOR_TYPE,
            MFMediaType_Audio);
    if (FAILED(hr))
        return hr;

    hr =
        type->SetGUID(
            MF_MT_SUBTYPE,
            MFAudioFormat_Float);
    if (FAILED(hr))
        return hr;

    hr =
        type->SetUINT32(
            MF_MT_AUDIO_NUM_CHANNELS,
            channels);
    if (FAILED(hr))
        return hr;

    hr =
        type->SetUINT32(
            MF_MT_AUDIO_SAMPLES_PER_SECOND,
            sample_rate);
    if (FAILED(hr))
        return hr;

    hr =
        type->SetUINT32(
            MF_MT_AUDIO_BITS_PER_SAMPLE,
            bits);
    if (FAILED(hr))
        return hr;

    hr =
        type->SetUINT32(
            MF_MT_AUDIO_BLOCK_ALIGNMENT,
            block_align);
    if (FAILED(hr))
        return hr;

    hr =
        type->SetUINT32(
            MF_MT_AUDIO_AVG_BYTES_PER_SECOND,
            static_cast<UINT32>(avg_bytes));
    if (FAILED(hr))
        return hr;

    hr =
        type->SetUINT32(
            MF_MT_AUDIO_CHANNEL_MASK,
            KSAUDIO_SPEAKER_STEREO);
    if (FAILED(hr))
        return hr;

    return type->SetUINT32(
        MF_MT_ALL_SAMPLES_INDEPENDENT,
        TRUE);
}

[[nodiscard]] bool probe_rate(
    std::uint32_t input_rate,
    std::uint32_t output_rate)
{
    ComPtr<IMFTransform> transform;
    HRESULT hr =
        CoCreateInstance(
            CLSID_CResamplerMediaObject,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(
                transform.GetAddressOf()));
    if (FAILED(hr)) {
        std::cerr
            << "CoCreateInstance(CLSID_CResamplerMediaObject) failed: 0x"
            << std::hex
            << static_cast<std::uint32_t>(hr)
            << std::dec << '\n';
        return false;
    }

    ComPtr<IWMResamplerProps> props;
    hr = transform.As(&props);
    if (FAILED(hr) || !props) {
        std::cerr
            << "IWMResamplerProps is unavailable: 0x"
            << std::hex
            << static_cast<std::uint32_t>(hr)
            << std::dec << '\n';
        return false;
    }

    ComPtr<IMFMediaType> input_type;
    ComPtr<IMFMediaType> output_type;

    hr = MFCreateMediaType(
        input_type.GetAddressOf());
    if (FAILED(hr))
        return false;

    hr = MFCreateMediaType(
        output_type.GetAddressOf());
    if (FAILED(hr))
        return false;

    hr = configure_float_stereo_type(
        input_type.Get(),
        input_rate);
    if (FAILED(hr)) {
        std::cerr
            << "Input media type configuration failed for "
            << input_rate << " Hz: 0x"
            << std::hex
            << static_cast<std::uint32_t>(hr)
            << std::dec << '\n';
        return false;
    }

    hr = configure_float_stereo_type(
        output_type.Get(),
        output_rate);
    if (FAILED(hr)) {
        std::cerr
            << "Output media type configuration failed for "
            << output_rate << " Hz: 0x"
            << std::hex
            << static_cast<std::uint32_t>(hr)
            << std::dec << '\n';
        return false;
    }

    hr = transform->SetInputType(
        0,
        input_type.Get(),
        0);
    if (FAILED(hr)) {
        std::cerr
            << "SetInputType failed for "
            << input_rate << " Hz: 0x"
            << std::hex
            << static_cast<std::uint32_t>(hr)
            << std::dec << '\n';
        return false;
    }

    hr = transform->SetOutputType(
        0,
        output_type.Get(),
        0);
    if (FAILED(hr)) {
        std::cerr
            << "SetOutputType failed for "
            << output_rate << " Hz: 0x"
            << std::hex
            << static_cast<std::uint32_t>(hr)
            << std::dec << '\n';
        return false;
    }

    MFT_INPUT_STREAM_INFO input_info{};
    MFT_OUTPUT_STREAM_INFO output_info{};

    hr = transform->GetInputStreamInfo(
        0,
        &input_info);
    if (FAILED(hr))
        return false;

    hr = transform->GetOutputStreamInfo(
        0,
        &output_info);
    if (FAILED(hr))
        return false;

    std::cout
        << "MF_RESAMPLER_CONFIG"
        << " input_rate=" << input_rate
        << " output_rate=" << output_rate
        << " input_cbSize=" << input_info.cbSize
        << " output_cbSize=" << output_info.cbSize
        << " output_alignment=" << output_info.cbAlignment
        << " props=IWMResamplerProps"
        << '\n';

    return true;
}

} // namespace

int main()
{
    ScopedCom com;
    if (FAILED(com.status())) {
        std::cerr
            << "COM MTA init failed: 0x"
            << std::hex
            << static_cast<std::uint32_t>(
                   com.status())
            << std::dec << '\n';
        return 1;
    }

    ScopedMf mf;
    if (FAILED(mf.status())) {
        std::cerr
            << "MFStartup failed: 0x"
            << std::hex
            << static_cast<std::uint32_t>(
                   mf.status())
            << std::dec << '\n';
        return 1;
    }

    const bool rate_44k1 =
        probe_rate(
            44'100,
            48'000);
    const bool rate_96k =
        probe_rate(
            96'000,
            48'000);

    if (!rate_44k1 || !rate_96k)
        return 2;

    std::cout
        << "MF_RESAMPLER_BASELINE PASS"
        << " static_src=44.1k_to_48k,96k_to_48k"
        << " drift_ppm_control=not_exposed_by_IWMResamplerProps"
        << '\n';

    return 0;
}

#endif
