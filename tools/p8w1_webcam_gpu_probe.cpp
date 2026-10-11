// P8W1A diagnostic only. This is not a recorder capture backend and does
// not advertise webcam recording. No video/audio/media payload is persisted.
#include "app/device_catalog.hpp"
#include "core/video/webcam_capture_policy.hpp"
#include "platform/windows/graphics/d3d11_device.hpp"

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

    std::uint32_t seen = 0, gpu = 0, non_gpu = 0, missed = 0;
    bool monotonic = true;
    LONGLONG last_timestamp = -1;
    if (SUCCEEDED(hr)) {
        for (unsigned long i = 0; i < requested_frames; ++i) {
            ComPtr<IMFSample> sample;
            DWORD flags = 0;
            LONGLONG timestamp = 0;
            hr = callback->next(
                reader.Get(), sample, flags, timestamp, i == 0 ? 5000 : 2500);
            if (FAILED(hr) || (flags & MF_SOURCE_READERF_ENDOFSTREAM))
                break;
            if (!sample) {
                ++missed;
                continue;
            }
            ++seen;
            if (last_timestamp > timestamp) monotonic = false;
            last_timestamp = timestamp;
            if (is_dxgi_sample_on_adapter(sample.Get(), luid))
                ++gpu;
            else
                ++non_gpu;
        }
    }

    reader.Reset();
    (void)source->Shutdown();
    const bool approved =
        SUCCEEDED(hr) && seen >= 3 && gpu == seen && missed == 0 &&
        monotonic;
    std::wcout << L"{\"status\":\""
               << (approved ? L"gpu_candidate" : L"review")
               << L"\",\"mode\":{"
               << L"\"width\":" << selected.mode.width
               << L",\"height\":" << selected.mode.height
               << L",\"fps_num\":" << selected.mode.fps_num
               << L",\"fps_den\":" << selected.mode.fps_den
               << L",\"format\":" << static_cast<int>(selected.mode.format)
               << L"},\"frames\":" << seen
               << L",\"dxgi_frames\":" << gpu
               << L",\"non_dxgi_frames\":" << non_gpu
               << L",\"empty_samples\":" << missed
               << L",\"timestamps_monotonic\":" << (monotonic ? L"true" : L"false")
               << L",\"hr\":" << static_cast<std::uint32_t>(hr)
               << L"}\n";
    return approved ? 0 : 4;
}

} // namespace

int wmain(int argc, wchar_t **argv)
{
    if (argc >= 2 && std::wstring(argv[1]) == L"--help") {
        std::wcout << L"Arssyut P8W1 GPU webcam diagnostic (not a recorder).\n"
                   << L"Usage: --list | --capture <zero-based-index> [frames: 3..60]\n"
                   << L"Exit: 0=probe GPU candidate or list, 3=no device, "
                   << L"4=review/unavailable, 2=invalid/tool failure.\n";
        return 0;
    }

    bool capture = argc >= 2 && std::wstring(argv[1]) == L"--capture";
    bool list = argc == 1 || (argc == 2 && std::wstring(argv[1]) == L"--list");
    unsigned long index = 0, frames = 12;
    if (capture) {
        if ((argc != 3 && argc != 4) ||
            !parse_uint(argv[2], index) ||
            (argc == 4 && !parse_uint(argv[3], frames)) ||
            frames < 3 || frames > 60) {
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
    return run_capture(devices[index], frames);
}
