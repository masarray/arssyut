#include "platform/windows/capture/webcam_capture_source.hpp"
#include "platform/windows/graphics/d3d11_device.hpp"

#include <d3d11.h>
#include <mfapi.h>
#include <mfobjects.h>
#include <wrl/client.h>

#include <atomic>
#include <cstdint>
#include <iostream>
#include <thread>
#include <utility>

using Microsoft::WRL::ComPtr;
using arssyut::windows::WebcamCaptureSource;
using arssyut::windows::WebcamCaptureState;
using arssyut::windows::WebcamGpuFrame;
using arssyut::windows::WebcamGpuFrameSlot;

namespace {
int failures = 0;
void check(bool ok, const char *why)
{
    if (!ok) {
        ++failures;
        std::cerr << "FAIL: " << why << '\n';
    }
}

struct Fixture {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<IMFSample> sample;
    bool ok = false;
    Fixture()
    {
        if (FAILED(MFStartup(MF_VERSION))) return;
        D3D_FEATURE_LEVEL level{};
        ComPtr<ID3D11DeviceContext> context;
        if (FAILED(D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            nullptr, 0, D3D11_SDK_VERSION,
            &device, &level, &context))) return;
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = 8; desc.Height = 8;
        desc.MipLevels = desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(device->CreateTexture2D(&desc, nullptr, &texture))) return;
        ok = SUCCEEDED(MFCreateSample(&sample));
    }
    ~Fixture() { sample.Reset(); texture.Reset(); device.Reset(); MFShutdown(); }
    WebcamGpuFrame make(std::uint64_t sequence) const
    {
        WebcamGpuFrame frame{};
        frame.sample = sample;
        frame.texture = texture;
        frame.width = frame.height = 8;
        frame.sequence = sequence;
        frame.captured_at.ticks_100ns = static_cast<std::int64_t>(sequence);
        return frame;
    }
};
} // namespace

int main()
{
    Fixture f;
    check(f.ok, "WARP and synthetic MF sample available");
    if (!f.ok) return 1;

    {
        WebcamGpuFrameSlot slots;
        check(slots.in_flight_count() == 0, "empty initially");
        check(slots.publish({}) == WebcamGpuFrameSlot::PublishResult::DroppedBusy,
              "invalid frame rejected");
        check(slots.in_flight_count() == 0, "invalid does not occupy slot");
        check(slots.publish(f.make(1)) ==
              WebcamGpuFrameSlot::PublishResult::Published, "publish first");
        auto old = slots.try_acquire_latest();
        check(bool(old) && old->sequence == 1, "lease owns first");
        check(!slots.try_acquire_latest(), "one lease per frame");
        check(slots.publish(f.make(2)) ==
              WebcamGpuFrameSlot::PublishResult::Published, "publish second");
        check(slots.publish(f.make(3)) ==
              WebcamGpuFrameSlot::PublishResult::ReplacedUnread,
              "replace obsolete unread, do not queue");
        auto newest = slots.try_acquire_latest();
        check(bool(newest) && newest->sequence == 3, "newest source read");
        check(bool(old) && old->sequence == 1, "old consumer lease still valid");
        slots.discard_unread();
        check(slots.in_flight_count() == 2, "reading leases never invalidated");
        check(slots.publish(f.make(4)) ==
              WebcamGpuFrameSlot::PublishResult::Published, "last slot available");
        auto third = slots.try_acquire_latest();
        check(bool(third), "third lease acquired");
        check(slots.in_flight_count() == WebcamGpuFrameSlot::capacity,
              "strict three surface cap");
        check(slots.publish(f.make(5)) ==
              WebcamGpuFrameSlot::PublishResult::DroppedBusy,
              "busy when all three leased, producer never blocks");
        newest.release();
        check(slots.publish(f.make(6)) ==
              WebcamGpuFrameSlot::PublishResult::Published,
              "released slot reused safely");
        auto final = slots.try_acquire_latest();
        check(bool(final) && final->sequence == 6, "new generation latest");
        old.release();
        third.release();
        final.release();
        slots.discard_unread();
        check(slots.in_flight_count() == 0, "all GPU frame leases released");
    }

    {
        WebcamGpuFrameSlot slots;
        std::atomic<bool> finished{false};
        std::thread producer([&] {
            for (std::uint64_t i = 1; i <= 15000; ++i)
                (void)slots.publish(f.make(i));
            finished.store(true, std::memory_order_release);
        });
        std::uint64_t previous = 0;
        while (!finished.load(std::memory_order_acquire)) {
            auto lease = slots.try_acquire_latest();
            if (lease) {
                check(lease->sequence > previous,
                      "one consumer always observes increasing source sequence");
                previous = lease->sequence;
                check(slots.in_flight_count() <= WebcamGpuFrameSlot::capacity,
                      "contention never exceeds three samples");
            }
        }
        producer.join();
        slots.discard_unread();
        check(slots.in_flight_count() == 0, "stress drains to zero");
    }

    {
        WebcamGpuFrameSlot slots;
        WebcamCaptureSource source;
        check(source.snapshot().state == WebcamCaptureState::Off,
              "no implicit privacy access");
        check(!source.start(nullptr, L"dummy", slots).ok(),
              "null D3D device rejected");
        check(!source.start(f.device.Get(), L"", slots).ok(),
              "empty source identity rejected");
        source.stop();
        check(source.snapshot().state == WebcamCaptureState::Off,
              "stopping idle source is idempotent");
    }
    std::cout << "P8W1B native source/lease deterministic tests: "
              << (failures ? "FAIL" : "PASS") << "\n";
    return failures ? 1 : 0;
}
