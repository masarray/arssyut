#include "platform/windows/capture/latest_frame_slot.hpp"
#include "platform/windows/graphics/d3d11_compositor.hpp"
#include "platform/windows/graphics/d3d11_device.hpp"

#include <d3d11.h>
#include <wrl/client.h>

#include <cstdint>
#include <iostream>

namespace {

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

Microsoft::WRL::ComPtr<ID3D11Texture2D> create_solid_texture(
    ID3D11Device *device,
    std::uint32_t width,
    std::uint32_t height,
    std::uint32_t bgra)
{
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    std::uint32_t pixels[16]{};
    for (auto &pixel : pixels)
        pixel = bgra;

    D3D11_SUBRESOURCE_DATA initial{};
    initial.pSysMem = pixels;
    initial.SysMemPitch = width * sizeof(std::uint32_t);

    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    if (FAILED(device->CreateTexture2D(
            &desc,
            &initial,
            texture.GetAddressOf()))) {
        return {};
    }
    return texture;
}

bool verify_solid_texture(
    ID3D11Device *device,
    ID3D11DeviceContext *context,
    ID3D11Texture2D *texture,
    std::uint32_t expected)
{
    if (!device || !context || !texture)
        return false;

    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);

    D3D11_TEXTURE2D_DESC staging_desc = desc;
    staging_desc.BindFlags = 0;
    staging_desc.MiscFlags = 0;
    staging_desc.Usage = D3D11_USAGE_STAGING;
    staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
    if (FAILED(device->CreateTexture2D(
            &staging_desc,
            nullptr,
            staging.GetAddressOf()))) {
        return false;
    }

    context->CopyResource(staging.Get(), texture);

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context->Map(
            staging.Get(),
            0,
            D3D11_MAP_READ,
            0,
            &mapped))) {
        return false;
    }

    bool valid = true;
    for (std::uint32_t y = 0; y < desc.Height && valid; ++y) {
        const auto *row = reinterpret_cast<const std::uint32_t *>(
            static_cast<const std::uint8_t *>(mapped.pData) +
            static_cast<std::size_t>(y) * mapped.RowPitch);

        for (std::uint32_t x = 0; x < desc.Width; ++x) {
            if (row[x] != expected) {
                valid = false;
                break;
            }
        }
    }

    context->Unmap(staging.Get(), 0);
    return valid;
}

void test_latest_frame_slot(TestContext &test)
{
    using arssyut::windows::CapturedFrame;
    using arssyut::windows::LatestFrameSlot;

    LatestFrameSlot slot;

    CapturedFrame first;
    first.sequence = 1;
    test.expect(
        slot.publish(std::move(first)) ==
            LatestFrameSlot::PublishResult::Published,
        "First frame publishes");

    CapturedFrame second;
    second.sequence = 2;
    test.expect(
        slot.publish(std::move(second)) ==
            LatestFrameSlot::PublishResult::ReplacedUnread,
        "New frame coalesces unread predecessor");

    auto lease = slot.try_acquire_latest();
    test.expect(static_cast<bool>(lease), "Latest frame can be acquired");
    test.expect(lease->sequence == 2, "Latest frame wins");

    CapturedFrame third;
    third.sequence = 3;
    test.expect(
        slot.publish(std::move(third)) ==
            LatestFrameSlot::PublishResult::Published,
        "Producer publishes while previous frame is leased");

    auto latest = slot.try_acquire_latest();
    test.expect(static_cast<bool>(latest), "New latest frame is independent");
    test.expect(latest->sequence == 3, "Lease observes newest sequence");

    latest.release();
    lease.release();
    test.expect(!slot.has_in_flight(), "All frame slots drain deterministically");
}

void test_compositor(
    TestContext &test,
    arssyut::windows::D3D11Device &owner)
{
    constexpr std::uint32_t solid_bgra = 0xFF884422u;

    auto source = create_solid_texture(
        owner.device(),
        4,
        4,
        solid_bgra);
    test.expect(source != nullptr, "Synthetic source texture created");
    if (!source)
        return;

    auto compositor_result =
        arssyut::windows::D3D11Compositor::create(owner.device());
    test.expect(
        static_cast<bool>(compositor_result),
        "D3D11 compositor initializes");
    if (!compositor_result)
        return;

    auto &compositor = *compositor_result.value();

    const auto status = compositor.render(
        owner.immediate_context(),
        source.Get(),
        {1, 1, 3, 3},
        {8, 6});

    test.expect(status.ok(), "Compositor crop/scale render succeeds");
    test.expect(
        compositor.output_size() == arssyut::core::FrameSize{8, 6},
        "Compositor owns requested output size");
    test.expect(
        verify_solid_texture(
            owner.device(),
            owner.immediate_context(),
            compositor.output_texture(),
            solid_bgra),
        "GPU crop/scale preserves solid BGRA content");

    const auto generation = compositor.resource_generation();

    test.expect(
        compositor.render(
            owner.immediate_context(),
            source.Get(),
            {0, 0, 4, 4},
            {8, 6}).ok(),
        "Second compositor render succeeds");

    test.expect(
        compositor.resource_generation() == generation,
        "Steady-state render performs no resource rebuild");

    test.expect(
        compositor.render(
            owner.immediate_context(),
            source.Get(),
            {0, 0, 4, 4},
            {6, 4}).ok(),
        "Output resize render succeeds");

    test.expect(
        compositor.resource_generation() == generation + 1,
        "Output resources rebuild only when geometry changes");
}

} // namespace

int main()
{
    using arssyut::windows::D3D11Device;
    using arssyut::windows::D3D11DevicePreference;

    TestContext test;

    auto result = D3D11Device::create(
        D3D11DevicePreference::WarpForTesting,
        false);

    test.expect(static_cast<bool>(result), "D3D11 WARP device creation");
    if (!result) {
        std::cerr << "HRESULT detail=0x"
                  << std::hex << result.status().detail << '\n';
        return 1;
    }

    auto &device = *result.value();
    test.expect(device.valid(), "D3D11 owner returns valid resources");
    test.expect(
        device.feature_level() >= D3D_FEATURE_LEVEL_10_0,
        "D3D11 feature level meets baseline");

    test_latest_frame_slot(test);
    test_compositor(test, device);

    if (test.failures != 0) {
        std::cerr << test.failures << " of " << test.checks
                  << " checks failed\n";
        return 1;
    }

    std::cout << "PASS: " << test.checks
              << " Windows/D3D11 checks\n";
    return 0;
}
