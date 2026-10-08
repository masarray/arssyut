#ifdef _WIN32

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <ks.h>
#include <ksmedia.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mftransform.h>
#include <wmcodecdsp.h>
#include <wrl/client.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <span>
#include <vector>

namespace {

using Microsoft::WRL::ComPtr;

constexpr std::uint32_t kOutputRate = 48'000;
constexpr std::uint32_t kChannels = 2;
constexpr std::uint32_t kBytesPerFloatFrame =
    kChannels * static_cast<std::uint32_t>(sizeof(float));
constexpr double kPi = 3.14159265358979323846;

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

    constexpr std::uint32_t bits = 32;
    constexpr std::uint32_t block_align =
        kChannels * (bits / 8U);

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
            kChannels);
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

[[nodiscard]] bool create_transform(
    std::uint32_t input_rate,
    std::uint32_t output_rate,
    ComPtr<IMFTransform> &transform,
    MFT_OUTPUT_STREAM_INFO &output_info)
{
    HRESULT hr =
        CoCreateInstance(
            CLSID_CResamplerMediaObject,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(
                transform.ReleaseAndGetAddressOf()));
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

    hr = transform->GetOutputStreamInfo(
        0,
        &output_info);
    if (FAILED(hr))
        return false;

    return true;
}

[[nodiscard]] bool create_input_sample(
    std::span<const float> samples,
    std::uint32_t frame_count,
    std::uint32_t sample_rate,
    ComPtr<IMFSample> &sample)
{
    if (frame_count == 0 ||
        samples.size() !=
            static_cast<std::size_t>(
                frame_count) *
                kChannels)
        return false;

    const std::size_t byte_count =
        samples.size() *
        sizeof(float);
    if (byte_count >
        static_cast<std::size_t>(
            std::numeric_limits<DWORD>::max()))
        return false;

    HRESULT hr =
        MFCreateSample(
            sample.ReleaseAndGetAddressOf());
    if (FAILED(hr))
        return false;

    ComPtr<IMFMediaBuffer> buffer;
    hr = MFCreateMemoryBuffer(
        static_cast<DWORD>(byte_count),
        buffer.GetAddressOf());
    if (FAILED(hr))
        return false;

    BYTE *destination = nullptr;
    DWORD max_length = 0;
    DWORD current_length = 0;
    hr = buffer->Lock(
        &destination,
        &max_length,
        &current_length);
    if (FAILED(hr) ||
        destination == nullptr ||
        max_length < byte_count) {
        if (SUCCEEDED(hr))
            buffer->Unlock();
        return false;
    }

    std::memcpy(
        destination,
        samples.data(),
        byte_count);

    hr = buffer->Unlock();
    if (FAILED(hr))
        return false;

    hr = buffer->SetCurrentLength(
        static_cast<DWORD>(byte_count));
    if (FAILED(hr))
        return false;

    hr = sample->AddBuffer(
        buffer.Get());
    if (FAILED(hr))
        return false;

    const LONGLONG duration =
        static_cast<LONGLONG>(
            (10'000'000ULL *
             static_cast<std::uint64_t>(
                 frame_count)) /
            sample_rate);

    hr = sample->SetSampleTime(0);
    if (FAILED(hr))
        return false;

    return SUCCEEDED(
        sample->SetSampleDuration(
            duration));
}

[[nodiscard]] bool append_output_sample(
    IMFSample *sample,
    std::vector<float> &output)
{
    if (!sample)
        return false;

    ComPtr<IMFMediaBuffer> contiguous;
    HRESULT hr =
        sample->ConvertToContiguousBuffer(
            contiguous.GetAddressOf());
    if (FAILED(hr))
        return false;

    BYTE *data = nullptr;
    DWORD max_length = 0;
    DWORD current_length = 0;
    hr = contiguous->Lock(
        &data,
        &max_length,
        &current_length);
    if (FAILED(hr))
        return false;

    const bool valid =
        current_length % sizeof(float) == 0;

    if (valid && current_length != 0) {
        const auto *begin =
            reinterpret_cast<const float *>(
                data);
        const std::size_t count =
            current_length /
            sizeof(float);
        output.insert(
            output.end(),
            begin,
            begin + count);
    }

    const HRESULT unlock_hr =
        contiguous->Unlock();
    return valid &&
           SUCCEEDED(unlock_hr);
}

[[nodiscard]] bool collect_output(
    IMFTransform *transform,
    const MFT_OUTPUT_STREAM_INFO &stream_info,
    std::vector<float> &output)
{
    if (!transform)
        return false;

    constexpr std::uint32_t kChunkFrames = 4'096;
    constexpr DWORD kFallbackBytes =
        kChunkFrames *
        kBytesPerFloatFrame;

    const DWORD buffer_bytes =
        std::max<DWORD>(
            kFallbackBytes,
            stream_info.cbSize);

    for (std::uint32_t iteration = 0;
         iteration < 256;
         ++iteration) {
        ComPtr<IMFSample> supplied_sample;
        ComPtr<IMFMediaBuffer> supplied_buffer;

        const bool transform_provides_samples =
            (stream_info.dwFlags &
             MFT_OUTPUT_STREAM_PROVIDES_SAMPLES) != 0;

        if (!transform_provides_samples) {
            HRESULT hr =
                MFCreateSample(
                    supplied_sample.GetAddressOf());
            if (FAILED(hr))
                return false;

            hr = MFCreateMemoryBuffer(
                buffer_bytes,
                supplied_buffer.GetAddressOf());
            if (FAILED(hr))
                return false;

            hr = supplied_sample->AddBuffer(
                supplied_buffer.Get());
            if (FAILED(hr))
                return false;
        }

        MFT_OUTPUT_DATA_BUFFER data{};
        data.dwStreamID = 0;
        data.pSample =
            supplied_sample.Get();

        DWORD status = 0;
        const HRESULT hr =
            transform->ProcessOutput(
                0,
                1,
                &data,
                &status);

        if (data.pEvents != nullptr) {
            data.pEvents->Release();
            data.pEvents = nullptr;
        }

        if (hr ==
            MF_E_TRANSFORM_NEED_MORE_INPUT) {
            return true;
        }

        if (FAILED(hr)) {
            std::cerr
                << "ProcessOutput failed: 0x"
                << std::hex
                << static_cast<std::uint32_t>(hr)
                << std::dec << '\n';
            return false;
        }

        IMFSample *produced =
            data.pSample != nullptr
                ? data.pSample
                : supplied_sample.Get();

        if (!append_output_sample(
                produced,
                output))
            return false;

        if (data.pSample != nullptr &&
            data.pSample != supplied_sample.Get()) {
            data.pSample->Release();
            data.pSample = nullptr;
        }
    }

    std::cerr
        << "ProcessOutput exceeded bounded drain iteration limit\n";
    return false;
}

[[nodiscard]] double estimate_frequency_hz(
    std::span<const float> interleaved,
    std::uint32_t sample_rate)
{
    const std::size_t frames =
        interleaved.size() /
        kChannels;

    if (frames < 4'096)
        return 0.0;

    const std::size_t margin =
        std::min<std::size_t>(
            2'048,
            frames / 8);
    const std::size_t begin =
        margin;
    const std::size_t end =
        frames - margin;
    if (end <= begin + 1)
        return 0.0;

    std::uint64_t rising_crossings = 0;
    float previous =
        interleaved[
            (begin - 1) *
            kChannels];

    for (std::size_t frame = begin;
         frame < end;
         ++frame) {
        const float current =
            interleaved[
                frame *
                kChannels];
        if (previous <= 0.0F &&
            current > 0.0F) {
            ++rising_crossings;
        }
        previous = current;
    }

    const double seconds =
        static_cast<double>(
            end - begin) /
        static_cast<double>(
            sample_rate);
    return seconds > 0.0
        ? static_cast<double>(
              rising_crossings) /
              seconds
        : 0.0;
}

[[nodiscard]] bool probe_tone_rate(
    std::uint32_t input_rate,
    std::uint32_t output_rate,
    double tone_hz)
{
    ComPtr<IMFTransform> transform;
    MFT_OUTPUT_STREAM_INFO output_info{};

    if (!create_transform(
            input_rate,
            output_rate,
            transform,
            output_info))
        return false;

    const std::uint32_t input_frames =
        input_rate;
    std::vector<float> input(
        static_cast<std::size_t>(
            input_frames) *
        kChannels);

    for (std::uint32_t frame = 0;
         frame < input_frames;
         ++frame) {
        const double phase =
            2.0 *
            kPi *
            tone_hz *
            static_cast<double>(frame) /
            static_cast<double>(input_rate);
        const float sample =
            static_cast<float>(
                0.25 *
                std::sin(phase));
        input[
            static_cast<std::size_t>(
                frame) *
                kChannels] =
            sample;
        input[
            static_cast<std::size_t>(
                frame) *
                kChannels +
            1] =
            sample;
    }

    ComPtr<IMFSample> input_sample;
    if (!create_input_sample(
            input,
            input_frames,
            input_rate,
            input_sample)) {
        std::cerr
            << "Failed to create input sample for "
            << input_rate
            << " Hz\n";
        return false;
    }

    HRESULT hr =
        transform->ProcessMessage(
            MFT_MESSAGE_NOTIFY_BEGIN_STREAMING,
            0);
    if (FAILED(hr))
        return false;

    hr =
        transform->ProcessMessage(
            MFT_MESSAGE_NOTIFY_START_OF_STREAM,
            0);
    if (FAILED(hr))
        return false;

    const auto start =
        std::chrono::steady_clock::now();

    hr = transform->ProcessInput(
        0,
        input_sample.Get(),
        0);
    if (FAILED(hr)) {
        std::cerr
            << "ProcessInput failed: 0x"
            << std::hex
            << static_cast<std::uint32_t>(hr)
            << std::dec << '\n';
        return false;
    }

    std::vector<float> output;
    output.reserve(
        static_cast<std::size_t>(
            output_rate + 4'096) *
        kChannels);

    if (!collect_output(
            transform.Get(),
            output_info,
            output))
        return false;

    hr =
        transform->ProcessMessage(
            MFT_MESSAGE_NOTIFY_END_OF_STREAM,
            0);
    if (FAILED(hr))
        return false;

    hr =
        transform->ProcessMessage(
            MFT_MESSAGE_COMMAND_DRAIN,
            0);
    if (FAILED(hr))
        return false;

    if (!collect_output(
            transform.Get(),
            output_info,
            output))
        return false;

    (void)transform->ProcessMessage(
        MFT_MESSAGE_NOTIFY_END_STREAMING,
        0);

    const auto end =
        std::chrono::steady_clock::now();

    if (output.size() %
        kChannels != 0)
        return false;

    const std::uint64_t output_frames =
        output.size() /
        kChannels;
    const std::uint64_t nominal_frames =
        (static_cast<std::uint64_t>(
             input_frames) *
         output_rate +
         input_rate / 2U) /
        input_rate;

    const double frequency_hz =
        estimate_frequency_hz(
            output,
            output_rate);
    const double frequency_error_hz =
        std::abs(
            frequency_hz -
            tone_hz);

    const auto processing_us =
        std::chrono::duration_cast<
            std::chrono::microseconds>(
            end - start)
            .count();

    const std::uint64_t frame_delta =
        output_frames > nominal_frames
            ? output_frames - nominal_frames
            : nominal_frames - output_frames;

    // One second of steady input must not hide large accounting error behind a
    // percentage gate. Permit only a small, bounded filter tail/startup delay.
    constexpr std::uint64_t kMaxStaticFrameDelta = 256;
    const bool frame_count_plausible =
        frame_delta <= kMaxStaticFrameDelta;
    const bool frequency_plausible =
        frequency_hz > 0.0 &&
        frequency_error_hz <=
            std::max(
                0.25,
                tone_hz * 0.001);

    std::cout
        << "MF_RESAMPLER_MEASURE"
        << " input_rate=" << input_rate
        << " output_rate=" << output_rate
        << " input_frames=" << input_frames
        << " output_frames=" << output_frames
        << " nominal_frames=" << nominal_frames
        << " frame_delta="
        << static_cast<std::int64_t>(
               output_frames) -
               static_cast<std::int64_t>(
                   nominal_frames)
        << " expected_tone_hz=" << tone_hz
        << " measured_tone_hz=" << frequency_hz
        << " tone_error_hz="
        << frequency_error_hz
        << " process_us="
        << processing_us
        << " output_cbSize="
        << output_info.cbSize
        << " output_alignment="
        << output_info.cbAlignment
        << '\n';

    if (!frame_count_plausible ||
        !frequency_plausible) {
        std::cerr
            << "Static SRC quality sanity gate failed for "
            << input_rate
            << " -> "
            << output_rate
            << '\n';
        return false;
    }

    return true;
}

[[nodiscard]] bool probe_impulse_delay(
    std::uint32_t input_rate,
    std::uint32_t output_rate)
{
    ComPtr<IMFTransform> transform;
    MFT_OUTPUT_STREAM_INFO output_info{};
    if (!create_transform(
            input_rate,
            output_rate,
            transform,
            output_info))
        return false;

    constexpr std::uint32_t input_frames = 8'192;
    std::vector<float> input(
        static_cast<std::size_t>(input_frames) *
        kChannels,
        0.0F);
    input[0] = 1.0F;
    input[1] = 1.0F;

    ComPtr<IMFSample> sample;
    if (!create_input_sample(
            input,
            input_frames,
            input_rate,
            sample))
        return false;

    HRESULT hr = transform->ProcessMessage(
        MFT_MESSAGE_NOTIFY_BEGIN_STREAMING,
        0);
    if (FAILED(hr))
        return false;
    hr = transform->ProcessMessage(
        MFT_MESSAGE_NOTIFY_START_OF_STREAM,
        0);
    if (FAILED(hr))
        return false;
    hr = transform->ProcessInput(
        0,
        sample.Get(),
        0);
    if (FAILED(hr))
        return false;

    std::vector<float> output;
    output.reserve(
        static_cast<std::size_t>(12'288) *
        kChannels);
    if (!collect_output(
            transform.Get(),
            output_info,
            output))
        return false;

    hr = transform->ProcessMessage(
        MFT_MESSAGE_NOTIFY_END_OF_STREAM,
        0);
    if (FAILED(hr))
        return false;
    hr = transform->ProcessMessage(
        MFT_MESSAGE_COMMAND_DRAIN,
        0);
    if (FAILED(hr))
        return false;
    if (!collect_output(
            transform.Get(),
            output_info,
            output))
        return false;

    if (output.size() % kChannels != 0)
        return false;

    const std::size_t frames =
        output.size() / kChannels;
    if (frames == 0)
        return false;

    std::size_t peak_frame = 0;
    float peak = 0.0F;
    for (std::size_t frame = 0;
         frame < frames;
         ++frame) {
        const float magnitude =
            std::abs(
                output[frame * kChannels]);
        if (magnitude > peak) {
            peak = magnitude;
            peak_frame = frame;
        }
    }

    const std::uint64_t delay_100ns =
        (10'000'000ULL *
         static_cast<std::uint64_t>(peak_frame)) /
        output_rate;

    // The filter may have a short startup/group delay, but a recorder cannot
    // accept an unbounded or seconds-scale hidden latency.
    constexpr std::uint64_t kMaxDelay100ns =
        1'000'000ULL; // 100 ms

    std::cout
        << "MF_RESAMPLER_IMPULSE"
        << " input_rate=" << input_rate
        << " output_rate=" << output_rate
        << " peak_frame=" << peak_frame
        << " delay_100ns=" << delay_100ns
        << " peak=" << peak
        << " output_frames=" << frames
        << '\n';

    return peak > 0.01F &&
           delay_100ns <= kMaxDelay100ns;
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

    const bool tone_44k1_440 =
        probe_tone_rate(
            44'100,
            kOutputRate,
            440.0);
    const bool tone_44k1_1k =
        probe_tone_rate(
            44'100,
            kOutputRate,
            1'000.0);
    const bool tone_96k_440 =
        probe_tone_rate(
            96'000,
            kOutputRate,
            440.0);
    const bool tone_96k_1k =
        probe_tone_rate(
            96'000,
            kOutputRate,
            1'000.0);
    const bool impulse_44k1 =
        probe_impulse_delay(
            44'100,
            kOutputRate);
    const bool impulse_96k =
        probe_impulse_delay(
            96'000,
            kOutputRate);

    if (!tone_44k1_440 ||
        !tone_44k1_1k ||
        !tone_96k_440 ||
        !tone_96k_1k ||
        !impulse_44k1 ||
        !impulse_96k)
        return 2;

    std::cout
        << "MF_RESAMPLER_BASELINE PASS"
        << " static_src=44.1k_to_48k,96k_to_48k"
        << " tones=440Hz,1kHz"
        << " impulse_delay=bounded"
        << " drift_ppm_control=not_exposed_by_IWMResamplerProps"
        << " sole_p7a5_authority=no"
        << '\n';

    return 0;
}

#endif
