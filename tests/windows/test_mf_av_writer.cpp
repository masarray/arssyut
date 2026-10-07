#ifdef _WIN32

#include "core/audio/audio_time.hpp"
#include "platform/windows/media/mf_h264_mp4_writer.hpp"

#include <Windows.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfreadwrite.h>
#include <wrl/client.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

using Microsoft::WRL::ComPtr;

struct TestContext {
    int checks = 0;
    int failures = 0;

    void expect(bool condition, const char *message)
    {
        ++checks;
        if (!condition) {
            ++failures;
            std::cerr << "FAIL: " << message << '\n';
        }
    }
};

class ScopedMediaFoundation final {
public:
    ScopedMediaFoundation() noexcept
    {
        const HRESULT com_hr =
            CoInitializeEx(
                nullptr,
                COINIT_MULTITHREADED);
        owns_com_ =
            com_hr == S_OK ||
            com_hr == S_FALSE;

        mf_hr_ =
            MFStartup(
                MF_VERSION,
                MFSTARTUP_FULL);
    }

    ~ScopedMediaFoundation()
    {
        if (SUCCEEDED(mf_hr_))
            MFShutdown();
        if (owns_com_)
            CoUninitialize();
    }

    [[nodiscard]] bool valid() const noexcept
    {
        return SUCCEEDED(mf_hr_);
    }

private:
    bool owns_com_ = false;
    HRESULT mf_hr_ = E_FAIL;
};

[[nodiscard]] bool write_pcm_sample(
    IMFSinkWriter *writer,
    DWORD stream_index,
    const std::vector<std::int16_t> &samples,
    std::int64_t pts_100ns,
    std::int64_t duration_100ns)
{
    if (!writer || samples.empty() || duration_100ns <= 0)
        return false;

    const auto byte_count =
        samples.size() * sizeof(std::int16_t);
    if (byte_count > MAXDWORD)
        return false;

    ComPtr<IMFSample> sample;
    if (FAILED(MFCreateSample(
            sample.GetAddressOf())))
        return false;

    ComPtr<IMFMediaBuffer> buffer;
    if (FAILED(MFCreateMemoryBuffer(
            static_cast<DWORD>(byte_count),
            buffer.GetAddressOf())))
        return false;

    BYTE *destination = nullptr;
    DWORD max_length = 0;
    DWORD current_length = 0;
    if (FAILED(buffer->Lock(
            &destination,
            &max_length,
            &current_length)) ||
        !destination ||
        max_length < byte_count) {
        if (destination)
            buffer->Unlock();
        return false;
    }

    std::memcpy(
        destination,
        samples.data(),
        byte_count);

    if (FAILED(buffer->Unlock()))
        return false;

    if (FAILED(buffer->SetCurrentLength(
            static_cast<DWORD>(byte_count))))
        return false;

    if (FAILED(sample->AddBuffer(buffer.Get())))
        return false;
    if (FAILED(sample->SetSampleTime(pts_100ns)))
        return false;
    if (FAILED(sample->SetSampleDuration(duration_100ns)))
        return false;

    return SUCCEEDED(
        writer->WriteSample(
            stream_index,
            sample.Get()));
}

[[nodiscard]] bool write_aac_probe(
    std::uint32_t sample_rate,
    const std::filesystem::path &path)
{
    using namespace arssyut;

    ComPtr<IMFSinkWriter> writer;
    HRESULT hr =
        MFCreateSinkWriterFromURL(
            path.c_str(),
            nullptr,
            nullptr,
            writer.GetAddressOf());
    if (FAILED(hr) || !writer)
        return false;

    windows::MfAudioWriterConfig config{
        .enabled = true,
        .sample_rate = sample_rate,
        .channels = 2,
        .bitrate_bps = 192'000,
        .allow_bitrate_fallback = false,
    };

    ComPtr<IMFMediaType> output_type;
    hr = MFCreateMediaType(
        output_type.GetAddressOf());
    if (FAILED(hr))
        return false;

    if (!windows::configure_mf_aac_output_type(
             output_type.Get(),
             config).ok())
        return false;

    DWORD stream_index = 0;
    hr = writer->AddStream(
        output_type.Get(),
        &stream_index);
    if (FAILED(hr))
        return false;

    ComPtr<IMFMediaType> input_type;
    hr = MFCreateMediaType(
        input_type.GetAddressOf());
    if (FAILED(hr))
        return false;

    if (!windows::configure_mf_pcm16_input_type(
             input_type.Get(),
             config).ok())
        return false;

    hr = writer->SetInputMediaType(
        stream_index,
        input_type.Get(),
        nullptr);
    if (FAILED(hr))
        return false;

    hr = writer->BeginWriting();
    if (FAILED(hr))
        return false;

    constexpr std::uint32_t frames_per_block = 1'024;
    std::vector<std::int16_t> samples(
        static_cast<std::size_t>(frames_per_block) * 2U);

    core::audio::FrameTimeAccumulator clock{
        sample_rate};
    std::uint64_t absolute_frame = 0;
    std::int64_t pts = 0;

    for (std::uint32_t block = 0;
         block < 32;
         ++block) {
        for (std::uint32_t frame = 0;
             frame < frames_per_block;
             ++frame) {
            const double phase =
                2.0 * 3.14159265358979323846 *
                1'000.0 *
                static_cast<double>(
                    absolute_frame + frame) /
                static_cast<double>(sample_rate);
            const auto value =
                static_cast<std::int16_t>(
                    std::sin(phase) * 12'000.0);
            const std::size_t index =
                static_cast<std::size_t>(frame) * 2U;
            samples[index] = value;
            samples[index + 1] = value;
        }

        const auto duration_u64 =
            clock.advance(frames_per_block);
        const auto duration =
            static_cast<std::int64_t>(
                duration_u64);

        if (!write_pcm_sample(
                writer.Get(),
                stream_index,
                samples,
                pts,
                duration)) {
            return false;
        }

        pts += duration;
        absolute_frame += frames_per_block;
    }

    return SUCCEEDED(writer->Finalize());
}

void validate_probe_file(
    TestContext &test,
    const std::filesystem::path &path,
    std::uint32_t expected_rate)
{
    ComPtr<IMFSourceReader> reader;
    const HRESULT reader_hr =
        MFCreateSourceReaderFromURL(
            path.c_str(),
            nullptr,
            reader.GetAddressOf());
    test.expect(
        SUCCEEDED(reader_hr) && reader,
        "Finalized AAC MP4 reopens through Media Foundation");
    if (FAILED(reader_hr) || !reader)
        return;

    ComPtr<IMFMediaType> audio_type;
    const HRESULT audio_hr =
        reader->GetNativeMediaType(
            MF_SOURCE_READER_FIRST_AUDIO_STREAM,
            0,
            audio_type.GetAddressOf());
    test.expect(
        SUCCEEDED(audio_hr) && audio_type,
        "Finalized probe contains one native audio stream");
    if (FAILED(audio_hr) || !audio_type)
        return;

    GUID subtype{};
    UINT32 rate = 0;
    UINT32 channels = 0;

    test.expect(
        SUCCEEDED(audio_type->GetGUID(
            MF_MT_SUBTYPE,
            &subtype)) &&
        IsEqualGUID(
            subtype,
            MFAudioFormat_AAC),
        "Finalized MP4 audio stream is AAC");

    test.expect(
        SUCCEEDED(audio_type->GetUINT32(
            MF_MT_AUDIO_SAMPLES_PER_SECOND,
            &rate)) &&
        rate == expected_rate,
        "Finalized AAC stream preserves configured sample rate");

    test.expect(
        SUCCEEDED(audio_type->GetUINT32(
            MF_MT_AUDIO_NUM_CHANNELS,
            &channels)) &&
        channels == 2,
        "Finalized AAC stream is stereo");

    ComPtr<IMFMediaType> unexpected_video;
    const HRESULT video_hr =
        reader->GetNativeMediaType(
            MF_SOURCE_READER_FIRST_VIDEO_STREAM,
            0,
            unexpected_video.GetAddressOf());
    test.expect(
        FAILED(video_hr) || !unexpected_video,
        "Audio-only AAC probe does not invent a second video stream");
}

void test_audio_media_contract(
    TestContext &test)
{
    using namespace arssyut;

    windows::MfAudioWriterConfig config{
        .enabled = true,
        .sample_rate = 48'000,
        .channels = 2,
        .bitrate_bps = 192'000,
        .allow_bitrate_fallback = true,
    };

    test.expect(
        config.valid(),
        "48 kHz stereo 192 kbps AAC config is valid");

    auto invalid = config;
    invalid.sample_rate = 96'000;
    test.expect(
        !invalid.valid(),
        "Writer rejects unsupported AAC sample rate instead of relabeling samples");

    ComPtr<IMFMediaType> output_type;
    ComPtr<IMFMediaType> input_type;
    test.expect(
        SUCCEEDED(MFCreateMediaType(
            output_type.GetAddressOf())) &&
        SUCCEEDED(MFCreateMediaType(
            input_type.GetAddressOf())),
        "Media Foundation audio media types allocate");

    if (!output_type || !input_type)
        return;

    test.expect(
        windows::configure_mf_aac_output_type(
            output_type.Get(),
            config).ok(),
        "AAC output type config succeeds");

    test.expect(
        windows::configure_mf_pcm16_input_type(
            input_type.Get(),
            config).ok(),
        "PCM16 input type config succeeds");

    UINT32 block_align = 0;
    UINT32 avg_bytes = 0;
    test.expect(
        SUCCEEDED(input_type->GetUINT32(
            MF_MT_AUDIO_BLOCK_ALIGNMENT,
            &block_align)) &&
        block_align == 4,
        "Stereo PCM16 block alignment is exact");

    test.expect(
        SUCCEEDED(input_type->GetUINT32(
            MF_MT_AUDIO_AVG_BYTES_PER_SECOND,
            &avg_bytes)) &&
        avg_bytes == 192'000,
        "48 kHz stereo PCM16 byte rate is explicit and correct");
}

} // namespace

int main()
{
    TestContext test;
    ScopedMediaFoundation runtime;

    test.expect(
        runtime.valid(),
        "Media Foundation runtime starts for AAC probe");
    if (!runtime.valid())
        return 1;

    test_audio_media_contract(test);

    const auto temp =
        std::filesystem::temp_directory_path();
    const auto pid =
        static_cast<unsigned long>(
            GetCurrentProcessId());

    for (const auto rate : {44'100U, 48'000U}) {
        const auto path =
            temp /
            (L"arssyut-p7a4-aac-" +
             std::to_wstring(pid) +
             L"-" +
             std::to_wstring(rate) +
             L".mp4");

        std::error_code ec;
        std::filesystem::remove(path, ec);

        const bool wrote =
            write_aac_probe(
                rate,
                path);
        test.expect(
            wrote,
            rate == 44'100U
                ? "44.1 kHz PCM16 encodes to finalized AAC MP4"
                : "48 kHz PCM16 encodes to finalized AAC MP4");

        if (wrote)
            validate_probe_file(
                test,
                path,
                rate);

        std::filesystem::remove(
            path,
            ec);
    }

    if (test.failures != 0) {
        std::cerr
            << test.failures
            << " of "
            << test.checks
            << " checks failed\n";
        return 1;
    }

    std::cout
        << "PASS: "
        << test.checks
        << " P7A4 MF AV-writer checks\n";
    return 0;
}

#endif
