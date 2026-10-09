#ifdef _WIN32

#include "app/audio_pcm16_submission_adapter.hpp"
#include "core/audio/audio_time.hpp"
#include "platform/windows/media/mf_h264_mp4_writer.hpp"
#include "platform/windows/graphics/d3d11_device.hpp"

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
#include <span>
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
    std::span<const std::int16_t> samples,
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

    // 48 kHz proves the P7A6 adapter reaches an actual MF AAC encoder.
    // The 44.1 kHz branch retains the independent P7A4 writer regression.
    app::audio::AacPcm16SubmissionAdapter converter;
    converter.reset(0);
    for (std::uint32_t block = 0; block < 32; ++block) {
        if (sample_rate == 48'000U) {
            core::audio::AudioProgramBlock canonical;
            canonical.audio.clear();
            canonical.audio.media_start_100ns =
                static_cast<std::int64_t>(
                    core::audio::frames_to_ticks_floor(
                        absolute_frame, 48'000));
            for (std::uint32_t frame = 0; frame < frames_per_block; ++frame) {
                const double phase = 2.0 * 3.14159265358979323846 *
                    1'000.0 * static_cast<double>(absolute_frame + frame) /
                    48'000.0;
                const auto index = static_cast<std::size_t>(frame) * 2U;
                const float sample = static_cast<float>(std::sin(phase) * 0.42);
                canonical.audio.samples[index] = sample;
                canonical.audio.samples[index + 1] = sample;
            }
            const auto result = converter.prepare(canonical, absolute_frame);
            if (result.status != app::audio::AacPrepareStatus::Ready ||
                result.view.frames != frames_per_block ||
                result.view.relative_pts_100ns != pts)
                return false;
            const bool accepted = write_pcm_sample(
                writer.Get(), stream_index, result.view.interleaved,
                result.view.relative_pts_100ns, result.view.duration_100ns);
            if (!converter.finish(accepted) || !accepted)
                return false;
            pts = result.view.relative_pts_100ns + result.view.duration_100ns;
        } else {
            for (std::uint32_t frame = 0; frame < frames_per_block; ++frame) {
                const double phase =
                    2.0 * 3.14159265358979323846 * 1'000.0 *
                    static_cast<double>(absolute_frame + frame) /
                    static_cast<double>(sample_rate);
                const auto value = static_cast<std::int16_t>(
                    std::sin(phase) * 12'000.0);
                const auto index = static_cast<std::size_t>(frame) * 2U;
                samples[index] = value;
                samples[index + 1] = value;
            }
            const auto duration = static_cast<std::int64_t>(
                clock.advance(frames_per_block));
            if (!write_pcm_sample(
                    writer.Get(), stream_index, samples, pts, duration))
                return false;
            pts += duration;
        }
        absolute_frame += frames_per_block;
    }
    if (sample_rate == 48'000U &&
        (converter.stats().submitted_frames != absolute_frame ||
         converter.stats().clipped_samples != 0 ||
         converter.stats().non_finite_samples != 0 ||
         converter.stats().dropped_blocks != 0))
        return false;

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
            static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM),
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
            static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),
            0,
            unexpected_video.GetAddressOf());
    test.expect(
        FAILED(video_hr) || !unexpected_video,
        "Audio-only AAC probe does not invent a second video stream");
}


void verify_decoded_canonical_aac(
    TestContext &test,
    const std::filesystem::path &path)
{
    constexpr DWORD kAudioStream =
        static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM);
    // AAC stream metadata is insufficient: actually decode and inspect
    // PCM amplitude so silent-only MP4 cannot accidentally pass.
    ComPtr<IMFSourceReader> reader;
    const auto hr = MFCreateSourceReaderFromURL(
        path.c_str(), nullptr, reader.GetAddressOf());
    test.expect(SUCCEEDED(hr) && reader,
        "P7A6 AAC MP4 reopens for audible-sample verification");
    if (!reader)
        return;

    ComPtr<IMFMediaType> pcm;
    if (FAILED(MFCreateMediaType(pcm.GetAddressOf())) || !pcm ||
        FAILED(pcm->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio)) ||
        FAILED(pcm->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM)) ||
        FAILED(pcm->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2)) ||
        FAILED(pcm->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, 48'000)) ||
        FAILED(pcm->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16)) ||
        FAILED(reader->SetCurrentMediaType(
            kAudioStream, nullptr, pcm.Get()))) {
        test.expect(false, "P7A6 decoder configures stereo PCM16");
        return;
    }

    std::int32_t peak = 0;
    std::uint64_t sample_count = 0;
    bool read_failure = false;
    for (int block = 0; block < 64; ++block) {
        DWORD actual_stream = 0;
        DWORD flags = 0;
        LONGLONG timestamp = 0;
        ComPtr<IMFSample> sample;
        if (FAILED(reader->ReadSample(
                kAudioStream, 0,
                &actual_stream, &flags, &timestamp,
                sample.GetAddressOf())) ||
            (flags & MF_SOURCE_READERF_ERROR) != 0) {
            read_failure = true;
            break;
        }
        if (sample) {
            ComPtr<IMFMediaBuffer> buffer;
            BYTE *bytes = nullptr;
            DWORD capacity = 0;
            DWORD length = 0;
            if (FAILED(sample->ConvertToContiguousBuffer(
                    buffer.GetAddressOf())) || !buffer ||
                FAILED(buffer->Lock(&bytes, &capacity, &length)) || !bytes) {
                read_failure = true;
                break;
            }
            if ((length % (sizeof(std::int16_t) * 2)) != 0)
                read_failure = true;
            else {
                for (DWORD i = 0; i < length; i += 2) {
                    std::int16_t value = 0;
                    std::memcpy(&value, bytes + i, sizeof(value));
                    const std::int32_t mag = value < 0 ?
                        -static_cast<std::int32_t>(value) : value;
                    peak = (std::max)(peak, mag);
                    ++sample_count;
                }
            }
            (void)buffer->Unlock();
            if (read_failure)
                break;
        }
        if ((flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0)
            break;
    }
    test.expect(!read_failure && sample_count > 0,
        "P7A6 AAC file contains actual decoded PCM audio");
    test.expect(peak > 1'000,
        "P7A6 AAC decoded payload is audibly non-silent");
}


void test_product_native_av_writer_with_canonical_audio(
    TestContext &test,
    const std::filesystem::path &path)
{
    using namespace arssyut;

    // WARP/media-type support varies by Windows runner. When absent,
    // explicitly SKIP rather than misreport a positive product A/V result.
    // On an acceptance machine, ARSSYUT_REQUIRE_NATIVE_AV_SMOKE=1 makes
    // every capability failure an error instead of an allowed CI skip.
    const bool required = [] {
        char flag[2]{};
        const DWORD length = GetEnvironmentVariableA(
            "ARSSYUT_REQUIRE_NATIVE_AV_SMOKE",
            flag,
            static_cast<DWORD>(sizeof(flag)));
        return length == 1 && flag[0] == '1';
    }();

    const auto skip = [&](const char *reason) {
        std::cout << "SKIP: native product A/V writer: " << reason << '\n';
        if (required)
            test.expect(false, reason);
    };

    auto device_result = windows::D3D11Device::create(
        windows::D3D11DevicePreference::WarpForTesting, false);
    if (!device_result) {
        skip("WARP D3D11 device unavailable");
        return;
    }
    auto &device = *device_result.value();

    ComPtr<ID3D11VideoDevice> video_device;
    if (FAILED(device.device()->QueryInterface(
            IID_PPV_ARGS(video_device.GetAddressOf()))) ||
        !video_device) {
        skip("D3D11 video processor unavailable");
        return;
    }

    constexpr std::uint32_t width = 320;
    constexpr std::uint32_t height = 180;
    std::vector<std::uint32_t> pixels(
        static_cast<std::size_t>(width) * height, 0xFF2266AAU);
    D3D11_TEXTURE2D_DESC texture_desc{};
    texture_desc.Width = width;
    texture_desc.Height = height;
    texture_desc.MipLevels = 1;
    texture_desc.ArraySize = 1;
    texture_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    texture_desc.SampleDesc.Count = 1;
    texture_desc.Usage = D3D11_USAGE_DEFAULT;
    texture_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA initial{};
    initial.pSysMem = pixels.data();
    initial.SysMemPitch = width * sizeof(std::uint32_t);
    ComPtr<ID3D11Texture2D> source;
    if (FAILED(device.device()->CreateTexture2D(
            &texture_desc, &initial, source.GetAddressOf())) ||
        !source) {
        skip("test BGRA surface unavailable");
        return;
    }

    windows::MfH264Mp4Writer writer;
    windows::MfVideoWriterConfig video_config;
    video_config.size = {width, height};
    video_config.frame_rate = {30, 1};
    video_config.bitrate_bps = 2'000'000;
    video_config.surface_count = 4;
    windows::MfAudioWriterConfig audio_config;
    audio_config.enabled = true;
    audio_config.sample_rate = 48'000;
    audio_config.channels = 2;
    audio_config.bitrate_bps = 192'000;

    const auto opened = writer.open(
        device.device(), path, video_config, audio_config);
    if (!opened.ok()) {
        std::cout << "Native A/V open stage: "
                  << windows::mf_writer_stage_name(writer.failure_stage())
                  << ", status code=" << static_cast<unsigned>(opened.code)
                  << ", detail=" << opened.detail << '\n';
        skip("native H264/AAC encoder unavailable on WARP runner");
        return;
    }

    test.expect(writer.audio_enabled(),
        "Real MfH264Mp4Writer enables AAC in A/V session");

    const auto frame_written = writer.write_frame(
        device.immediate_context(), source.Get(), core::TimePoint{0},
        333'333);
    test.expect(frame_written.ok(),
        "Real product writer accepts first D3D11 video frame");

    app::audio::AacPcm16SubmissionAdapter adapter;
    adapter.reset(0);
    bool samples_ok = true;
    constexpr std::uint32_t test_blocks = 6;
    for (std::uint32_t n = 0; n < test_blocks; ++n) {
        const auto first_frame = static_cast<std::uint64_t>(n) * 1'024;
        core::audio::AudioProgramBlock block;
        block.audio.clear();
        block.audio.media_start_100ns = static_cast<std::int64_t>(
            core::audio::frames_to_ticks_floor(first_frame, 48'000));
        for (std::uint32_t frame = 0; frame < 1'024; ++frame) {
            const double phase = 2.0 * 3.14159265358979323846 *
                1'000.0 * static_cast<double>(first_frame + frame) /
                48'000.0;
            const float value = static_cast<float>(
                std::sin(phase) * 0.35);
            const auto index = static_cast<std::size_t>(frame) * 2U;
            block.audio.samples[index] = value;
            block.audio.samples[index + 1] = value;
        }

        if (n == 3U)
            block.audio.discontinuity_mask = 1U;
        // Exercise the exact future RecorderSession writer-owner API.
        // Discontinuity, writer outcome and pending-buffer lifetime can no
        // longer diverge between application and test-only helper code.
        const auto submission =
            adapter.submit_to(writer, block, first_frame);
        if (submission.prepare_status !=
                app::audio::AacPrepareStatus::Ready ||
            !submission.submitted) {
            samples_ok = false;
            break;
        }
    }
    test.expect(samples_ok &&
                adapter.stats().submitted_frames ==
                    test_blocks * 1'024ULL,
        "Real product writer accepts canonical PCM16 into AAC track");

    const auto final_status = writer.finalize();
    test.expect(final_status.ok(),
        "Real H264/AAC product writer finalizes synchronized MP4");

    if (!frame_written.ok() || !samples_ok || !final_status.ok())
        return;

    ComPtr<IMFSourceReader> reader;
    const HRESULT hr = MFCreateSourceReaderFromURL(
        path.c_str(), nullptr, reader.GetAddressOf());
    test.expect(SUCCEEDED(hr) && reader,
        "Real product MP4 reopens for independent media inspection");
    if (!reader)
        return;

    ComPtr<IMFMediaType> video_type;
    const bool has_video = SUCCEEDED(reader->GetNativeMediaType(
        static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), 0,
        video_type.GetAddressOf())) && video_type;
    test.expect(has_video, "Product MP4 contains native video stream");

    ComPtr<IMFMediaType> audio_type;
    const bool has_audio = SUCCEEDED(reader->GetNativeMediaType(
        static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), 0,
        audio_type.GetAddressOf())) && audio_type;
    test.expect(has_audio, "Product MP4 contains native AAC audio stream");
    if (has_audio) {
        GUID subtype{};
        const bool is_aac = SUCCEEDED(audio_type->GetGUID(
            MF_MT_SUBTYPE, &subtype)) &&
            IsEqualGUID(subtype, MFAudioFormat_AAC);
        test.expect(is_aac,
            "Product MP4 uses actual AAC codec, not a mislabeled PCM track");
        verify_decoded_canonical_aac(test, path);
    }
}


void test_audio_discontinuity_sample_attribute(TestContext &test)
{
    using namespace arssyut;
    test.expect(
        windows::configure_mf_audio_sample_discontinuity(
            nullptr, true) == E_POINTER,
        "MF discontinuity rejects null sample");

    ComPtr<IMFSample> first;
    if (FAILED(MFCreateSample(first.GetAddressOf())) || !first) {
        test.expect(false, "MF sample exists for discontinuity contract");
        return;
    }
    test.expect(
        SUCCEEDED(windows::configure_mf_audio_sample_discontinuity(
            first.Get(), false)),
        "Continuous sample remains valid");
    UINT32 flag = 0;
    test.expect(
        FAILED(first->GetUINT32(MFSampleExtension_Discontinuity, &flag)),
        "Continuous sample has no synthetic discontinuity attribute");

    test.expect(
        SUCCEEDED(windows::configure_mf_audio_sample_discontinuity(
            first.Get(), true)),
        "Discontinuity sample accepts Media Foundation extension");
    test.expect(
        SUCCEEDED(first->GetUINT32(
            MFSampleExtension_Discontinuity, &flag)) && flag == TRUE,
        "Discontinuity marker survives exact MF sample attribute lookup");

    ComPtr<IMFSample> second;
    if (SUCCEEDED(MFCreateSample(second.GetAddressOf())) && second) {
        test.expect(
            SUCCEEDED(windows::configure_mf_audio_sample_discontinuity(
                second.Get(), false)),
            "Following fresh sample accepts non-discontinuity state");
        test.expect(
            FAILED(second->GetUINT32(
                MFSampleExtension_Discontinuity, &flag)),
            "Sample flag is not sticky across subsequent submissions");
    } else {
        test.expect(false, "Second fresh MF sample allocates");
    }
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
    };

    test.expect(
        config.valid(),
        "48 kHz stereo 192 kbps AAC config is valid");

    auto invalid = config;
    invalid.sample_rate = 96'000;
    test.expect(
        !invalid.valid(),
        "Writer rejects unsupported AAC sample rate instead of relabeling samples");

    auto invalid_pool = config;
    invalid_pool.sample_pool_count = 1;
    test.expect(
        !invalid_pool.valid(),
        "Writer rejects an unbounded/undersized audio pool contract");

    auto invalid_block = config;
    invalid_block.max_frames_per_sample = 8'192;
    test.expect(
        !invalid_block.valid(),
        "Writer rejects audio blocks larger than the retained slot contract");

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
    test_audio_discontinuity_sample_attribute(test);

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

        if (wrote) {
            validate_probe_file(test, path, rate);
            if (rate == 48'000U)
                verify_decoded_canonical_aac(test, path);
        }

        std::filesystem::remove(
            path,
            ec);
    }

    const auto native_av_path = temp /
        (L"arssyut-p7a6-real-writer-" +
         std::to_wstring(pid) + L".mp4");
    {
        std::error_code ec;
        std::filesystem::remove(native_av_path, ec);
        test_product_native_av_writer_with_canonical_audio(
            test, native_av_path);
        std::filesystem::remove(native_av_path, ec);
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
