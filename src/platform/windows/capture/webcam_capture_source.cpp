#include "platform/windows/capture/webcam_capture_source.hpp"

#ifdef _WIN32

#include "core/time/monotonic_clock.hpp"
#include <Windows.h>
#include <dxgi.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfobjects.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <wrl/implements.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <mutex>
#include <new>
#include <utility>
#include <vector>

namespace arssyut::windows {
namespace {
using Microsoft::WRL::ComPtr;
using arssyut::core::WebcamCaptureCandidate;
using arssyut::core::WebcamPixelFormat;
constexpr DWORD video_stream = static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM);

class ScopedMf final {
public:
    ScopedMf() noexcept
    {
        const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        com_ = SUCCEEDED(hr);
        if (com_)
            mf_ = SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_FULL));
    }
    ~ScopedMf() noexcept
    {
        if (mf_) (void)MFShutdown();
        if (com_) CoUninitialize();
    }
    [[nodiscard]] bool ready() const noexcept { return mf_; }
private:
    bool com_ = false, mf_ = false;
};

// Callback never touches video memory/D3D, never calls ReadSample recursively,
// and never takes the producer's frame-slot lock. Exactly one outstanding
// source-reader request; no unbounded sample queue is created.
class AsyncSample final : public Microsoft::WRL::RuntimeClass<
    Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
    IMFSourceReaderCallback> {
public:
    AsyncSample() : event_(CreateEventW(nullptr, FALSE, FALSE, nullptr)) {}
    ~AsyncSample() override
    {
        if (event_) CloseHandle(event_);
    }
    [[nodiscard]] HANDLE event() const noexcept { return event_; }
    HRESULT STDMETHODCALLTYPE OnReadSample(
        HRESULT result, DWORD, DWORD flags, LONGLONG timestamp,
        IMFSample *sample) override
    {
        {
            std::lock_guard lock(mutex_);
            result_ = result;
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
    struct Completion {
        ComPtr<IMFSample> sample;
        HRESULT hr = E_PENDING;
        DWORD flags = 0;
        LONGLONG timestamp = 0;
    };
    [[nodiscard]] Completion take() noexcept
    {
        std::lock_guard lock(mutex_);
        Completion c{std::move(sample_), result_, flags_, timestamp_};
        sample_.Reset();
        result_ = E_PENDING;
        flags_ = 0;
        timestamp_ = 0;
        return c;
    }
private:
    HANDLE event_ = nullptr;
    std::mutex mutex_;
    ComPtr<IMFSample> sample_;
    HRESULT result_ = E_PENDING;
    DWORD flags_ = 0;
    LONGLONG timestamp_ = 0;
};

[[nodiscard]] bool adapter_luid(ID3D11Device *device, LUID &luid) noexcept
{
    if (!device) return false;
    ComPtr<IDXGIDevice> dxgi;
    ComPtr<IDXGIAdapter> adapter;
    DXGI_ADAPTER_DESC desc{};
    if (FAILED(device->QueryInterface(IID_PPV_ARGS(dxgi.GetAddressOf()))) ||
        FAILED(dxgi->GetAdapter(&adapter)) ||
        FAILED(adapter->GetDesc(&desc)))
        return false;
    luid = desc.AdapterLuid;
    return true;
}

[[nodiscard]] WebcamPixelFormat format_of(const GUID &format) noexcept
{
    if (format == MFVideoFormat_NV12) return WebcamPixelFormat::Nv12;
    if (format == MFVideoFormat_YUY2) return WebcamPixelFormat::Yuy2;
    if (format == MFVideoFormat_RGB32 ||
        format == MFVideoFormat_ARGB32) return WebcamPixelFormat::Bgra32;
    return WebcamPixelFormat::Unsupported;
}

struct Mode {
    WebcamCaptureCandidate format{};
    DWORD type_index = 0;
};

[[nodiscard]] HRESULT select_mode(IMFSourceReader *reader,
                                   WebcamCaptureCandidate &negotiated) noexcept
{
    if (!reader) return E_POINTER;
    Mode best{};
    auto rank = std::numeric_limits<std::uint64_t>::max();
    for (DWORD i = 0; i < 128; ++i) {
        ComPtr<IMFMediaType> type;
        const HRESULT hr = reader->GetNativeMediaType(video_stream, i, &type);
        if (hr == MF_E_NO_MORE_TYPES) break;
        if (FAILED(hr)) return hr;
        GUID subtype{};
        UINT32 w = 0, h = 0, num = 0, den = 0;
        if (FAILED(type->GetGUID(MF_MT_SUBTYPE, &subtype)) ||
            FAILED(MFGetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, &w, &h)) ||
            FAILED(MFGetAttributeRatio(type.Get(), MF_MT_FRAME_RATE, &num, &den)))
            continue;
        WebcamCaptureCandidate candidate{w, h, num, den, format_of(subtype)};
        const auto score = arssyut::core::webcam_candidate_rank(candidate);
        if (score < rank) {
            rank = score;
            best = {candidate, i};
        }
    }
    if (rank == std::numeric_limits<std::uint64_t>::max())
        return MF_E_INVALIDMEDIATYPE;

    ComPtr<IMFMediaType> type;
    HRESULT hr = reader->GetNativeMediaType(video_stream, best.type_index, &type);
    if (SUCCEEDED(hr))
        hr = reader->SetStreamSelection(
            static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE);
    if (SUCCEEDED(hr))
        hr = reader->SetStreamSelection(video_stream, TRUE);
    if (SUCCEEDED(hr))
        hr = reader->SetCurrentMediaType(video_stream, nullptr, type.Get());
    if (SUCCEEDED(hr))
        negotiated = best.format;
    return hr;
}

[[nodiscard]] HRESULT create_reader(
    const std::wstring &symbolic_link,
    ID3D11Device *device,
    IMFSourceReaderCallback *callback,
    ComPtr<IMFMediaSource> &source,
    ComPtr<IMFSourceReader> &reader,
    ComPtr<IMFDXGIDeviceManager> &manager) noexcept
{
    ComPtr<IMFAttributes> attributes;
    HRESULT hr = MFCreateAttributes(&attributes, 2);
    if (SUCCEEDED(hr))
        hr = attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                                  MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    if (SUCCEEDED(hr))
        hr = attributes->SetString(
            MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK,
            symbolic_link.c_str());
    if (SUCCEEDED(hr))
        hr = MFCreateDeviceSource(attributes.Get(), &source);
    if (FAILED(hr)) return hr;

    UINT token = 0;
    hr = MFCreateDXGIDeviceManager(&token, &manager);
    if (SUCCEEDED(hr)) hr = manager->ResetDevice(device, token);
    if (FAILED(hr)) return hr;

    ComPtr<IMFAttributes> reader_attributes;
    hr = MFCreateAttributes(&reader_attributes, 3);
    if (SUCCEEDED(hr))
        hr = reader_attributes->SetUnknown(
            MF_SOURCE_READER_D3D_MANAGER, manager.Get());
    if (SUCCEEDED(hr))
        hr = reader_attributes->SetUnknown(
            MF_SOURCE_READER_ASYNC_CALLBACK, callback);
    if (SUCCEEDED(hr))
        hr = reader_attributes->SetUINT32(
            MF_READWRITE_DISABLE_CONVERTERS, TRUE);
    if (SUCCEEDED(hr))
        hr = MFCreateSourceReaderFromMediaSource(
            source.Get(), reader_attributes.Get(), &reader);
    return hr;
}

[[nodiscard]] bool gpu_texture_from_sample(
    IMFSample *sample,
    ID3D11Device *expected_device,
    const LUID &expected_adapter,
    ComPtr<ID3D11Texture2D> &texture) noexcept
{
    ComPtr<IMFMediaBuffer> buffer;
    if (!sample || FAILED(sample->GetBufferByIndex(0, &buffer)))
        return false;
    ComPtr<IMFDXGIBuffer> dxgi;
    if (FAILED(buffer.As(&dxgi)) ||
        FAILED(dxgi->GetResource(IID_PPV_ARGS(texture.GetAddressOf()))))
        return false;
    ComPtr<ID3D11Device> texture_device;
    texture->GetDevice(&texture_device);
    // Fast path (common): the reader's surface is already on our device.
    if (texture_device.Get() == expected_device)
        return true;
    LUID actual{};
    return adapter_luid(texture_device.Get(), actual) &&
        actual.HighPart == expected_adapter.HighPart &&
        actual.LowPart == expected_adapter.LowPart;
}
} // namespace

WebcamCaptureSource::~WebcamCaptureSource() { stop(); }

void WebcamCaptureSource::reset_counters() noexcept
{
    received_.store(0); published_.store(0);
    replaced_.store(0); busy_drops_.store(0);
    stream_ticks_.store(0); invalid_samples_.store(0);
    adapter_mismatch_.store(0); source_changes_.store(0);
    callback_timeouts_.store(0); last_hresult_.store(0);
    mode_width_.store(0); mode_height_.store(0);
    mode_rate_num_.store(0); mode_rate_den_.store(0);
    mode_format_.store(static_cast<std::uint8_t>(WebcamPixelFormat::Unsupported));
}

arssyut::core::Status WebcamCaptureSource::start(
    ID3D11Device *device, std::wstring symbolic_link,
    WebcamGpuFrameSlot &gpu_frames) noexcept
{
    using arssyut::core::Status;
    using arssyut::core::StatusCode;
    if (!device || symbolic_link.empty())
        return Status::failure(StatusCode::InvalidArgument);
    if (worker_.joinable() || state_.load() != WebcamCaptureState::Off)
        return Status::failure(StatusCode::InvalidStateTransition);

    reset_counters();
    gpu_frames.discard_unread();
    slot_ = &gpu_frames;
    stop_requested_.store(false, std::memory_order_release);
    state_.store(WebcamCaptureState::Opening, std::memory_order_release);
    try {
        ComPtr<ID3D11Device> retained = device;
        worker_ = std::thread(
            &WebcamCaptureSource::worker, this,
            std::move(retained), std::move(symbolic_link));
    } catch (...) {
        slot_ = nullptr;
        state_.store(WebcamCaptureState::Off);
        return Status::failure(StatusCode::InternalError);
    }
    return Status::success();
}

void WebcamCaptureSource::fail(HRESULT hr) noexcept
{
    last_hresult_.store(static_cast<std::uint32_t>(hr));
    state_.store(WebcamCaptureState::Unavailable, std::memory_order_release);
}

void WebcamCaptureSource::stop() noexcept
{
    stop_requested_.store(true, std::memory_order_release);
    if (worker_.joinable())
        worker_.join();
    if (slot_) {
        slot_->discard_unread();
        slot_ = nullptr;
    }
    state_.store(WebcamCaptureState::Off, std::memory_order_release);
}

WebcamCaptureSnapshot WebcamCaptureSource::snapshot() const noexcept
{
    WebcamCaptureSnapshot s{};
    s.state = state_.load(std::memory_order_acquire);
    s.format = {
        mode_width_.load(), mode_height_.load(),
        mode_rate_num_.load(), mode_rate_den_.load(),
        static_cast<WebcamPixelFormat>(mode_format_.load())
    };
    s.received = received_.load();
    s.published = published_.load();
    s.replaced = replaced_.load();
    s.busy_drops = busy_drops_.load();
    s.stream_ticks = stream_ticks_.load();
    s.invalid_samples = invalid_samples_.load();
    s.adapter_mismatch = adapter_mismatch_.load();
    s.source_changes = source_changes_.load();
    s.callback_timeouts = callback_timeouts_.load();
    s.last_hresult = last_hresult_.load();
    return s;
}

void WebcamCaptureSource::worker(
    ComPtr<ID3D11Device> device, std::wstring symbolic_link) noexcept
{
    ScopedMf mf;
    if (!mf.ready()) {
        fail(E_FAIL);
        return;
    }

    auto callback = Microsoft::WRL::Make<AsyncSample>();
    if (!callback || !callback->event()) {
        fail(E_OUTOFMEMORY);
        return;
    }
    ComPtr<IMFMediaSource> source;
    ComPtr<IMFSourceReader> reader;
    ComPtr<IMFDXGIDeviceManager> manager;
    HRESULT hr = create_reader(
        symbolic_link, device.Get(), callback.Get(), source, reader, manager);

    WebcamCaptureCandidate mode{};
    if (SUCCEEDED(hr))
        hr = select_mode(reader.Get(), mode);
    LUID adapter{};
    if (SUCCEEDED(hr) && !adapter_luid(device.Get(), adapter))
        hr = E_FAIL;

    if (SUCCEEDED(hr)) {
        mode_width_.store(mode.width);
        mode_height_.store(mode.height);
        mode_rate_num_.store(mode.fps_num);
        mode_rate_den_.store(mode.fps_den);
        mode_format_.store(static_cast<std::uint8_t>(mode.format));
        if (!stop_requested_.load(std::memory_order_acquire))
            state_.store(WebcamCaptureState::Active, std::memory_order_release);
    }

    std::uint64_t sequence = 0;
    if (SUCCEEDED(hr)) {
        while (!stop_requested_.load(std::memory_order_acquire)) {
            hr = reader->ReadSample(video_stream, 0,
                                    nullptr, nullptr, nullptr, nullptr);
            if (FAILED(hr)) break;
            // Nonbusy callbacks: sample acquisition on MF threads; all COM
            // extraction, adapter checking and publication on this worker.
            DWORD wait_result = WAIT_TIMEOUT;
            for (int checks = 0; checks < 20; ++checks) {
                if (stop_requested_.load(std::memory_order_acquire))
                    break;
                wait_result = WaitForSingleObject(callback->event(), 125);
                if (wait_result != WAIT_TIMEOUT)
                    break;
            }
            if (stop_requested_.load(std::memory_order_acquire))
                break;
            if (wait_result != WAIT_OBJECT_0) {
                callback_timeouts_.fetch_add(1);
                hr = HRESULT_FROM_WIN32(
                    wait_result == WAIT_TIMEOUT ? ERROR_TIMEOUT : GetLastError());
                break;
            }
            const auto done = callback->take();
            if (FAILED(done.hr)) { hr = done.hr; break; }
            if (done.flags & (MF_SOURCE_READERF_ERROR |
                              MF_SOURCE_READERF_ENDOFSTREAM |
                              MF_SOURCE_READERF_NEWSTREAM |
                              MF_SOURCE_READERF_NATIVEMEDIATYPECHANGED |
                              MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED)) {
                source_changes_.fetch_add(1);
                hr = MF_E_INVALIDREQUEST;
                break;
            }
            if (done.flags & MF_SOURCE_READERF_STREAMTICK) {
                stream_ticks_.fetch_add(1);
                continue;
            }
            if (!done.sample) {
                invalid_samples_.fetch_add(1);
                continue;
            }
            received_.fetch_add(1);
            ComPtr<ID3D11Texture2D> texture;
            if (!gpu_texture_from_sample(
                    done.sample.Get(), device.Get(), adapter, texture)) {
                adapter_mismatch_.fetch_add(1);
                // GPU is a non-negotiable capability; no CPU fallback.
                hr = MF_E_INVALIDMEDIATYPE;
                break;
            }
            D3D11_TEXTURE2D_DESC desc{};
            texture->GetDesc(&desc);
            if (desc.Width != mode.width || desc.Height != mode.height) {
                invalid_samples_.fetch_add(1);
                hr = MF_E_INVALIDMEDIATYPE;
                break;
            }
            WebcamGpuFrame frame{};
            frame.sample = done.sample;
            frame.texture = std::move(texture);
            frame.captured_at = arssyut::core::MonotonicClock::now();
            frame.sequence = ++sequence;
            frame.width = desc.Width;
            frame.height = desc.Height;

            switch (slot_->publish(std::move(frame))) {
            case WebcamGpuFrameSlot::PublishResult::Published:
                published_.fetch_add(1);
                break;
            case WebcamGpuFrameSlot::PublishResult::ReplacedUnread:
                published_.fetch_add(1);
                replaced_.fetch_add(1);
                break;
            case WebcamGpuFrameSlot::PublishResult::DroppedBusy:
                busy_drops_.fetch_add(1);
                break;
            }
        }
    }
    if (!stop_requested_.load(std::memory_order_acquire))
        fail(hr);

    state_.store(WebcamCaptureState::Closing, std::memory_order_release);
    // A pending async request must be cancelled before we release Source
    // Reader; never destroy callback state from the driver callback itself.
    if (reader) (void)reader->Flush(video_stream);
    reader.Reset();
    if (source) (void)source->Shutdown();
    source.Reset();
    manager.Reset();
    callback.Reset();
    slot_->discard_unread();

    // A device failure stays visible to the owner after cleanup.
    state_.store(stop_requested_.load() ? WebcamCaptureState::Off
                 : WebcamCaptureState::Unavailable,
                 std::memory_order_release);
}

} // namespace arssyut::windows

#endif
