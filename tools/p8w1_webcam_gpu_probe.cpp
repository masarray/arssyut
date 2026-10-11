// P8W1A diagnostic only. This is not a recorder capture backend and does
// not advertise webcam recording. No video/audio/media payload is persisted.
#include "app/device_catalog.hpp"
#include "core/video/webcam_capture_policy.hpp"
#include "platform/windows/graphics/d3d11_device.hpp"
#include "platform/windows/capture/webcam_capture_source.hpp"
#include "p8w1_webcam_probe_flags.hpp"

#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfobjects.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <wrl/client.h>
#include <wrl/implements.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <chrono>
#include <thread>
#include <cwchar>
#include <iostream>
#include <mutex>
#include <string>
#include <vector>

namespace {

using Microsoft::WRL::ComPtr;
using arssyut::core::WebcamCaptureCandidate;
using arssyut::core::WebcamPixelFormat;

class ComAndMfLifetime final {
public:
    ComAndMfLifetime() noexcept
    {
        const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        com_ = SUCCEEDED(hr);
        if (com_)
            mf_ = SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_FULL));
    }
    ~ComAndMfLifetime()
    {
        if (mf_) MFShutdown();
        if (com_) CoUninitialize();
    }
    [[nodiscard]] bool ready() const noexcept { return mf_; }
private:
    bool com_ = false;
    bool mf_ = false;
};

// Media Foundation runs callbacks on its own executor. No custom thread,
// no blocking in OnReadSample and only one read outstanding at a time.
class ProbeCallback final : public Microsoft::WRL::RuntimeClass<
    Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
    IMFSourceReaderCallback> {
public:
    ProbeCallback() : event_(CreateEventW(nullptr, FALSE, FALSE, nullptr)) {}
    ~ProbeCallback() override
    {
        if (event_) CloseHandle(event_);
    }

    HRESULT STDMETHODCALLTYPE OnReadSample(
        HRESULT hr, DWORD, DWORD flags, LONGLONG timestamp,
        IMFSample *sample) override
    {
        {
            std::lock_guard lock(mutex_);
            status_ = hr;
            flags_ = flags;
            timestamp_ = timestamp;
            sample_ = sample;
        }
        if (event_) SetEvent(event_);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnEvent(DWORD, IMFMediaEvent *) override
    {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnFlush(DWORD) override
    {
        return S_OK;
    }

    HRESULT next(
        IMFSourceReader *reader,
        ComPtr<IMFSample> &sample,
        DWORD &flags,
        LONGLONG &timestamp,
        DWORD timeout_ms) noexcept
    {
        if (!event_ || !reader)
            return E_POINTER;
        {
            std::lock_guard lock(mutex_);
            sample_.Reset();
            status_ = E_PENDING;
            flags_ = 0;
            timestamp_ = 0;
        }
        ResetEvent(event_);
        HRESULT hr = reader->ReadSample(
            static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), 0,
            nullptr, nullptr, nullptr, nullptr);
        if (FAILED(hr))
            return hr;
        const DWORD wait = WaitForSingleObject(event_, timeout_ms);
        if (wait != WAIT_OBJECT_0)
            return wait == WAIT_TIMEOUT
                ? HRESULT_FROM_WIN32(ERROR_TIMEOUT)
                : HRESULT_FROM_WIN32(GetLastError());
        std::lock_guard lock(mutex_);
        sample = sample_;
        flags = flags_;
        timestamp = timestamp_;
        return status_;
    }

private:
    HANDLE event_ = nullptr;
    std::mutex mutex_;
    ComPtr<IMFSample> sample_;
    HRESULT status_ = E_PENDING;
    DWORD flags_ = 0;
    LONGLONG timestamp_ = 0;
};

struct NativeCandidate {
    WebcamCaptureCandidate mode{};
    DWORD type_index = 0;
};

[[nodiscard]] WebcamPixelFormat to_pixel_format(const GUID &subtype) noexcept
{
    if (subtype == MFVideoFormat_NV12) return WebcamPixelFormat::Nv12;
    if (subtype == MFVideoFormat_YUY2) return WebcamPixelFormat::Yuy2;
    if (subtype == MFVideoFormat_RGB32 ||
        subtype == MFVideoFormat_ARGB32)
        return WebcamPixelFormat::Bgra32;
    return WebcamPixelFormat::Unsupported;
}

[[nodiscard]] bool get_adapter_luid(
    ID3D11Device *device, LUID &luid) noexcept
{
    if (!device) return false;
    ComPtr<IDXGIDevice> dxgi;
    ComPtr<IDXGIAdapter> adapter;
    DXGI_ADAPTER_DESC desc{};
    if (FAILED(device->QueryInterface(IID_PPV_ARGS(&dxgi))) ||
        FAILED(dxgi->GetAdapter(&adapter)) ||
        FAILED(adapter->GetDesc(&desc)))
        return false;
    luid = desc.AdapterLuid;
    return true;
}

[[nodiscard]] bool is_dxgi_sample_on_adapter(
    IMFSample *sample, const LUID &expected) noexcept
{
    if (!sample) return false;
    ComPtr<IMFMediaBuffer> buffer;
    if (FAILED(sample->GetBufferByIndex(0, &buffer)))
        return false;
    ComPtr<IMFDXGIBuffer> dxgi_buffer;
    if (FAILED(buffer.As(&dxgi_buffer)))
        return false;

    // Never call IMFMediaBuffer::Lock, ConvertToContiguousBuffer, Map or CPU
    // texture readback, even in this diagnostic. Classify GPU-only surfaces.
    ComPtr<ID3D11Texture2D> texture;
    if (FAILED(dxgi_buffer->GetResource(
            IID_PPV_ARGS(texture.GetAddressOf()))))
        return false;
    ComPtr<ID3D11Device> resource_device;
    texture->GetDevice(&resource_device);
    LUID actual{};
    return get_adapter_luid(resource_device.Get(), actual) &&
        actual.HighPart == expected.HighPart &&
        actual.LowPart == expected.LowPart;
}

[[nodiscard]] bool parse_uint(
    const wchar_t *input, unsigned long &value) noexcept
{
    if (!input || !*input || *input == L'-') return false;
    wchar_t *end = nullptr;
    value = std::wcstoul(input, &end, 10);
    return end != input && *end == L'\0';
}

// Only numeric, bounded, non-sensitive telemetry is printed; no camera
// friendly name, symbolic link or camera pixels leave this process.
int run_capture(
    const arssyut::app::DeviceChoice &device,
    unsigned long requested_frames)
{
    auto graphics = arssyut::windows::D3D11Device::create(
        arssyut::windows::D3D11DevicePreference::HardwareOnly);
    if (!graphics) {
        std::wcout << L"{\"status\":\"graphics_unavailable\"}\n";
        return 4;
    }
    auto d3d = std::move(graphics).value();
    LUID luid{};
    if (!get_adapter_luid(d3d->device(), luid))
        return 4;

    ComPtr<IMFDXGIDeviceManager> manager;
    UINT reset_token = 0;
    HRESULT hr = MFCreateDXGIDeviceManager(
        &reset_token, &manager);
    if (FAILED(hr) ||
        FAILED(manager->ResetDevice(d3d->device(), reset_token))) {
        std::wcout << L"{\"status\":\"dxgi_manager_unavailable\"}\n";
        return 4;
    }

    ComPtr<IMFAttributes> source_attributes;
    hr = MFCreateAttributes(&source_attributes, 2);
    if (SUCCEEDED(hr))
        hr = source_attributes->SetGUID(
            MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
            MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    if (SUCCEEDED(hr))
        hr = source_attributes->SetString(
            MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK,
            device.id.c_str());
    ComPtr<IMFMediaSource> source;
    if (SUCCEEDED(hr))
        hr = MFCreateDeviceSource(source_attributes.Get(), &source);
    if (FAILED(hr)) {
        std::wcout << L"{\"status\":\"device_open_failed\",\"hr\":"
                   << static_cast<std::uint32_t>(hr) << L"}\n";
        return 4;
    }

    auto callback = Microsoft::WRL::Make<ProbeCallback>();
    ComPtr<IMFAttributes> reader_attributes;
    hr = MFCreateAttributes(&reader_attributes, 3);
    if (SUCCEEDED(hr))
        hr = reader_attributes->SetUnknown(
            MF_SOURCE_READER_ASYNC_CALLBACK, callback.Get());
    if (SUCCEEDED(hr))
        hr = reader_attributes->SetUnknown(
            MF_SOURCE_READER_D3D_MANAGER, manager.Get());
    if (SUCCEEDED(hr))
        hr = reader_attributes->SetUINT32(
            MF_READWRITE_DISABLE_CONVERTERS, TRUE);

    ComPtr<IMFSourceReader> reader;
    if (SUCCEEDED(hr))
        hr = MFCreateSourceReaderFromMediaSource(
            source.Get(), reader_attributes.Get(), &reader);
    if (FAILED(hr)) {
        source->Shutdown();
        std::wcout << L"{\"status\":\"source_reader_failed\",\"hr\":"
                   << static_cast<std::uint32_t>(hr) << L"}\n";
        return 4;
    }

    std::vector<NativeCandidate> candidates;
    for (DWORD index = 0; index < 128; ++index) {
        ComPtr<IMFMediaType> media_type;
        hr = reader->GetNativeMediaType(
            static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), index, &media_type);
        if (hr == MF_E_NO_MORE_TYPES) break;
        if (FAILED(hr)) break;

        GUID subtype{};
        UINT32 width = 0, height = 0, num = 0, den = 0;
        if (FAILED(media_type->GetGUID(MF_MT_SUBTYPE, &subtype)) ||
            FAILED(MFGetAttributeSize(
                media_type.Get(), MF_MT_FRAME_SIZE, &width, &height)) ||
            FAILED(MFGetAttributeRatio(
                media_type.Get(), MF_MT_FRAME_RATE, &num, &den)))
            continue;
        NativeCandidate choice{{width, height, num, den,
                               to_pixel_format(subtype)}, index};
        if (arssyut::core::webcam_candidate_supported(choice.mode))
            candidates.push_back(choice);
    }

    std::size_t best = candidates.size();
    std::uint64_t best_rank = UINT64_MAX;
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        const std::uint64_t rank =
            arssyut::core::webcam_candidate_rank(candidates[i].mode);
        if (rank < best_rank) {
            best = i;
            best_rank = rank;
        }
    }

    if (best == candidates.size()) {
        reader.Reset();
        source->Shutdown();
        std::wcout << L"{\"status\":\"no_supported_native_mode\","
                   << L"\"candidates\":" << candidates.size() << L"}\n";
        return 4;
    }

    const auto selected = candidates[best];
    ComPtr<IMFMediaType> type;
    hr = reader->GetNativeMediaType(
        static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),
        selected.type_index, &type);
    if (SUCCEEDED(hr))
        hr = reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE);
    if (SUCCEEDED(hr))
        hr = reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), TRUE);
    if (SUCCEEDED(hr))
        hr = reader->SetCurrentMediaType(
            static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), nullptr, type.Get());

    std::uint32_t callbacks = 0;
    std::uint32_t seen = 0, gpu = 0, non_gpu = 0;
    std::uint32_t empty_samples = 0, stream_ticks = 0;
    std::uint32_t unexpected_empty = 0, stream_changes = 0;
    std::uint32_t reader_errors = 0, end_of_stream = 0;
    DWORD last_flags = 0;
    bool monotonic = true;
    LONGLONG last_timestamp = -1;
    if (SUCCEEDED(hr)) {
        for (unsigned long i = 0; i < requested_frames; ++i) {
            ComPtr<IMFSample> sample;
            DWORD flags = 0;
            LONGLONG timestamp = 0;
            hr = callback->next(
                reader.Get(), sample, flags, timestamp, i == 0 ? 5000 : 2500);
            if (FAILED(hr)) {
                ++reader_errors;
                break;
            }
            ++callbacks;
            last_flags = flags;
            if (!sample)
                ++empty_samples;

            // Distinguish real Source Reader stream gaps from unexpected
            // null samples and renegotiation. Do not hide a delivery gap
            // behind GPU eligibility, and never claim a frame for a tick.
            using arssyut::webcam_probe::ReadEvent;
            switch (arssyut::webcam_probe::classify_read_event(
                sample.Get() != nullptr, flags)) {
            case ReadEvent::StreamGap:
                ++stream_ticks;
                continue;
            case ReadEvent::UnexpectedEmpty:
                ++unexpected_empty;
                continue;
            case ReadEvent::StreamConfigurationChanged:
                ++stream_changes;
                break;
            case ReadEvent::EndOfStream:
                ++end_of_stream;
                break;
            case ReadEvent::ReaderError:
                ++reader_errors;
                break;
            case ReadEvent::Frame:
                ++seen;
                if (last_timestamp > timestamp)
                    monotonic = false;
                last_timestamp = timestamp;
                if (is_dxgi_sample_on_adapter(sample.Get(), luid))
                    ++gpu;
                else
                    ++non_gpu;
                continue;
            }
            break;
        }
    }

    reader.Reset();
    (void)source->Shutdown();

    // Two separate, intentionally conservative facts:
    // 1. GPU compatibility: delivered samples use our exact D3D adapter.
    // 2. Delivery health: a stream tick denotes a real time gap and is NOT
    //    a clean timing result (even though it isn't a CPU fallback).
    const bool gpu_candidate =
        SUCCEEDED(hr) && seen >= 3 && gpu == seen &&
        non_gpu == 0 && unexpected_empty == 0 &&
        stream_changes == 0 && reader_errors == 0 &&
        end_of_stream == 0 && monotonic;
    const bool clean_delivery = gpu_candidate && stream_ticks == 0;
    const wchar_t *status =
        clean_delivery ? L"gpu_candidate" :
        gpu_candidate ? L"gpu_path_with_stream_gaps" : L"review";

    std::wcout << L"{\"status\":\"" << status
               << L"\",\"mode\":{"
               << L"\"width\":" << selected.mode.width
               << L",\"height\":" << selected.mode.height
               << L",\"fps_num\":" << selected.mode.fps_num
               << L",\"fps_den\":" << selected.mode.fps_den
               << L",\"format\":" << static_cast<int>(selected.mode.format)
               << L"},\"requested_reads\":" << requested_frames
               << L",\"completed_reads\":" << callbacks
               << L",\"frames\":" << seen
               << L",\"dxgi_frames\":" << gpu
               << L",\"non_dxgi_frames\":" << non_gpu
               << L",\"empty_samples\":" << empty_samples
               << L",\"stream_ticks\":" << stream_ticks
               << L",\"unexpected_empty\":" << unexpected_empty
               << L",\"stream_changes\":" << stream_changes
               << L",\"reader_errors\":" << reader_errors
               << L",\"end_of_stream\":" << end_of_stream
               << L",\"last_flags\":" << last_flags
               << L",\"gpu_compatible\":" << (gpu_candidate ? L"true" : L"false")
               << L",\"timestamps_monotonic\":" << (monotonic ? L"true" : L"false")
               << L",\"hr\":" << static_cast<std::uint32_t>(hr)
               << L"}\n";
    return clean_delivery ? 0 : 4;
}


int run_native_engine(const arssyut::app::DeviceChoice &camera,
                      unsigned long seconds)
{
    auto graphics = arssyut::windows::D3D11Device::create(
        arssyut::windows::D3D11DevicePreference::HardwareOnly);
    if (!graphics) {
        std::wcout << L"{\"status\":\"graphics_unavailable\"}\n";
        return 4;
    }
    auto gpu = std::move(graphics).value();
    arssyut::windows::WebcamGpuFrameSlot slot;
    arssyut::windows::WebcamCaptureSource engine;
    const auto result = engine.start(gpu->device(), camera.id, slot);
    if (!result.ok()) {
        std::wcout << L"{\"status\":\"engine_start_failed\",\"code\":"
                   << static_cast<int>(result.code) << L"}\n";
        return 4;
    }

    std::uint64_t consumed = 0, previous = 0, nonmonotonic = 0;
    std::uint64_t peak_slot_usage = 0;
    // Real Windows evidence: 95, 71 and 58 frames in "5 seconds" were
    // counted from process start. That conflates MF camera initialization
    // with capture cadence. Here 'seconds' is measured only AFTER first
    // valid GPU frame; opening is separately bounded to eight seconds.
    const auto requested_at = std::chrono::steady_clock::now();
    const auto opening_deadline = requested_at + std::chrono::seconds(8);
    auto active_at = requested_at;
    bool started = false;
    while (true) {
        const auto now = std::chrono::steady_clock::now();
        if (!started && now >= opening_deadline)
            break;
        if (started && now >= active_at + std::chrono::seconds(seconds))
            break;

        auto lease = slot.try_acquire_latest();
        if (lease) {
            ++consumed;
            if (lease->sequence <= previous)
                ++nonmonotonic;
            previous = lease->sequence;
            peak_slot_usage = std::max<std::uint64_t>(
                peak_slot_usage, slot.in_flight_count());
            // The real GPU sample and texture belong to the bounded lease.
            // No CPU readback or additional source/recorder is instantiated.
        }
        const auto snap = engine.snapshot();
        if (snap.state == arssyut::windows::WebcamCaptureState::Unavailable)
            break;
        if (!started &&
            snap.state == arssyut::windows::WebcamCaptureState::Active) {
            started = true;
            active_at = now;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    const auto finished_at = std::chrono::steady_clock::now();
    const auto snapshot = engine.snapshot();
    engine.stop();
    const auto wall_ms = std::chrono::duration_cast<
        std::chrono::milliseconds>(finished_at - requested_at).count();
    const auto steady_ms = started ?
        std::chrono::duration_cast<std::chrono::milliseconds>(
            finished_at - active_at).count() : 0;

    const bool gpu_ok = snapshot.received >= 3 &&
        snapshot.adapter_mismatch == 0 &&
        snapshot.invalid_samples == 0 &&
        snapshot.callback_timeouts == 0 &&
        snapshot.source_changes == 0 &&
        snapshot.frames_other_device_same_adapter == 0 &&
        nonmonotonic == 0;
    const bool healthy = gpu_ok && started &&
        snapshot.stream_ticks_streaming == 0 &&
        snapshot.frame_intervals_over_100ms == 0 &&
        snapshot.state == arssyut::windows::WebcamCaptureState::Active;
    const wchar_t *status = healthy ? L"engine_gpu_active" :
        gpu_ok && started ? L"engine_gpu_with_gaps" : L"review";
    std::wcout << L"{\"status\":\"" << status
               << L"\",\"seconds\":" << seconds
               << L",\"wall_ms\":" << wall_ms
               << L",\"steady_ms\":" << steady_ms
               << L",\"device_open_ms\":" << snapshot.source_open_ms
               << L",\"first_frame_ms\":" << snapshot.first_frame_ms
               << L",\"observed_fps_x100\":" << snapshot.streaming_fps_x100
               << L",\"mode\":{"
               << L"\"width\":" << snapshot.format.width
               << L",\"height\":" << snapshot.format.height
               << L",\"fps_num\":" << snapshot.format.fps_num
               << L",\"fps_den\":" << snapshot.format.fps_den
               << L",\"format\":" << static_cast<int>(snapshot.format.format)
               << L"},\"received\":" << snapshot.received
               << L",\"published\":" << snapshot.published
               << L",\"replaced\":" << snapshot.replaced
               << L",\"busy_drops\":" << snapshot.busy_drops
               << L",\"stream_ticks\":" << snapshot.stream_ticks
               << L",\"stream_ticks_startup\":" << snapshot.stream_ticks_startup
               << L",\"stream_ticks_streaming\":" << snapshot.stream_ticks_streaming
               << L",\"frame_intervals_over_100ms\":"
               << snapshot.frame_intervals_over_100ms
               << L",\"max_frame_interval_ms\":"
               << snapshot.max_frame_interval_ms
               << L",\"frames_other_device_same_adapter\":"
               << snapshot.frames_other_device_same_adapter
               << L",\"invalid_samples\":" << snapshot.invalid_samples
               << L",\"adapter_mismatch\":" << snapshot.adapter_mismatch
               << L",\"source_changes\":" << snapshot.source_changes
               << L",\"callback_timeouts\":" << snapshot.callback_timeouts
               << L",\"consumed\":" << consumed
               << L",\"consumer_nonmonotonic\":" << nonmonotonic
               << L",\"peak_occupied_slots\":" << peak_slot_usage
               << L",\"hr\":" << snapshot.last_hresult << L"}\n";
    return healthy ? 0 : 4;
}

} // namespace

int wmain(int argc, wchar_t **argv)
{
    if (argc >= 2 && std::wstring(argv[1]) == L"--help") {
        std::wcout << L"Arssyut P8W1 GPU webcam diagnostic (not a recorder).\n"
                   << L"Usage: --list | --capture <index> [frames: 3..60] | --engine <index> [seconds: 1..20]\n"
                   << L"Exit: 0=probe GPU candidate or list, 3=no device, "
                   << L"4=review/unavailable, 2=invalid/tool failure.\n";
        return 0;
    }

    bool capture = argc >= 2 && std::wstring(argv[1]) == L"--capture";
    bool engine_mode = argc >= 2 && std::wstring(argv[1]) == L"--engine";
    bool list = argc == 1 || (argc == 2 && std::wstring(argv[1]) == L"--list");
    unsigned long index = 0, frames = 12;
    if (capture || engine_mode) {
        if ((argc != 3 && argc != 4) ||
            !parse_uint(argv[2], index) ||
            (argc == 4 && !parse_uint(argv[3], frames)) ||
            (engine_mode ? (frames < 1 || frames > 20)
                         : (frames < 3 || frames > 60))) {
            return 2;
        }
    } else if (!list) {
        return 2;
    }

    ComAndMfLifetime lifetime;
    if (!lifetime.ready()) {
        std::wcout << L"{\"status\":\"media_foundation_unavailable\"}\n";
        return 4;
    }
    const auto devices = arssyut::app::enumerate_cameras();
    if (list) {
        std::wcout << L"{\"status\":\""
                   << (devices.empty() ? L"no_camera" : L"enumerated")
                   << L"\",\"camera_count\":" << devices.size() << L"}\n";
        return 0;
    }
    if (index >= devices.size()) {
        std::wcout << L"{\"status\":\"camera_not_found\"}\n";
        return 3;
    }
    return engine_mode ? run_native_engine(devices[index], argc == 4 ? frames : 5)
                       : run_capture(devices[index], frames);
}
