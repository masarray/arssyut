#include "platform/windows/capture/latest_frame_slot.hpp"
#include "platform/windows/graphics/d3d11_compositor.hpp"
#include "platform/windows/graphics/d3d11_device.hpp"
#include "platform/windows/media/mf_h264_mp4_writer.hpp"
#include "platform/windows/storage/recoverable_session.hpp"
#include "platform/windows/video/native_video_pipeline.hpp"
#include "presentation/presentation_controller.hpp"
#include "presentation/shortcut_visualizer.hpp"
#include "visual/arvisual_modes.hpp"

#include <d3d11.h>
#include <mfapi.h>
#include <mfobjects.h>
#include <mfreadwrite.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

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

    if (!device || width == 0 || height == 0)
        return {};

    std::vector<std::uint32_t> pixels(
        static_cast<std::size_t>(width) *
        static_cast<std::size_t>(height),
        bgra);

    D3D11_SUBRESOURCE_DATA initial{};
    initial.pSysMem = pixels.data();
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

Microsoft::WRL::ComPtr<ID3D11Texture2D> create_split_texture(
    ID3D11Device *device,
    std::uint32_t width,
    std::uint32_t height,
    std::uint32_t left_bgra,
    std::uint32_t right_bgra)
{
    if (!device || width == 0 || height == 0)
        return {};

    std::vector<std::uint32_t> pixels(
        static_cast<std::size_t>(width) *
        static_cast<std::size_t>(height));

    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            pixels[
                static_cast<std::size_t>(y) * width + x] =
                x < width / 2 ? left_bgra : right_bgra;
        }
    }

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA initial{};
    initial.pSysMem = pixels.data();
    initial.SysMemPitch =
        width * sizeof(std::uint32_t);

    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    if (FAILED(device->CreateTexture2D(
            &desc,
            &initial,
            texture.GetAddressOf()))) {
        return {};
    }

    return texture;
}
Microsoft::WRL::ComPtr<ID3D11Texture2D> create_vertical_stripe_texture(
    ID3D11Device *device,
    std::uint32_t width,
    std::uint32_t height,
    std::uint32_t background_bgra,
    std::uint32_t stripe_bgra)
{
    if (!device || width < 3 || height == 0)
        return {};

    std::vector<std::uint32_t> pixels(
        static_cast<std::size_t>(width) *
            static_cast<std::size_t>(height),
        background_bgra);

    const std::uint32_t stripe_x = width / 2;
    for (std::uint32_t y = 0; y < height; ++y) {
        pixels[
            static_cast<std::size_t>(y) * width +
            stripe_x] = stripe_bgra;
    }

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA initial{};
    initial.pSysMem = pixels.data();
    initial.SysMemPitch =
        width * sizeof(std::uint32_t);

    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    if (FAILED(device->CreateTexture2D(
            &desc,
            &initial,
            texture.GetAddressOf()))) {
        return {};
    }

    return texture;
}


bool read_texture_pixel(
    ID3D11Device *device,
    ID3D11DeviceContext *context,
    ID3D11Texture2D *texture,
    std::uint32_t x,
    std::uint32_t y,
    std::uint32_t &pixel)
{
    if (!device || !context || !texture)
        return false;

    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);
    if (x >= desc.Width || y >= desc.Height)
        return false;

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

    const auto *row =
        reinterpret_cast<const std::uint32_t *>(
            static_cast<const std::uint8_t *>(mapped.pData) +
            static_cast<std::size_t>(y) * mapped.RowPitch);
    pixel = row[x];

    context->Unmap(staging.Get(), 0);
    return true;
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

bool texture_contains_non_solid_pixel(
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

    bool changed = false;
    for (std::uint32_t y = 0; y < desc.Height && !changed; ++y) {
        const auto *row =
            reinterpret_cast<const std::uint32_t *>(
                static_cast<const std::uint8_t *>(mapped.pData) +
                static_cast<std::size_t>(y) * mapped.RowPitch);

        for (std::uint32_t x = 0; x < desc.Width; ++x) {
            if (row[x] != expected) {
                changed = true;
                break;
            }
        }
    }

    context->Unmap(staging.Get(), 0);
    return changed;
}

void test_dxgi_media_buffer_length(
    TestContext &test,
    arssyut::windows::D3D11Device &owner)
{
    auto texture = create_solid_texture(
        owner.device(),
        4,
        4,
        0xFF204060u);

    test.expect(
        texture != nullptr,
        "DXGI media-buffer test texture created");
    if (!texture)
        return;

    Microsoft::WRL::ComPtr<IMFMediaBuffer> buffer;
    HRESULT hr = MFCreateDXGISurfaceBuffer(
        __uuidof(ID3D11Texture2D),
        texture.Get(),
        0,
        FALSE,
        buffer.GetAddressOf());

    test.expect(
        SUCCEEDED(hr) && buffer,
        "MF wraps D3D11 texture as DXGI media buffer");
    if (FAILED(hr) || !buffer)
        return;

    Microsoft::WRL::ComPtr<IMF2DBuffer> buffer_2d;
    hr = buffer.As(&buffer_2d);

    test.expect(
        SUCCEEDED(hr) && buffer_2d,
        "DXGI media buffer exposes IMF2DBuffer");
    if (FAILED(hr) || !buffer_2d)
        return;

    DWORD contiguous_length = 0;
    hr = buffer_2d->GetContiguousLength(
        &contiguous_length);

    test.expect(
        SUCCEEDED(hr) && contiguous_length > 0,
        "DXGI media buffer reports non-zero contiguous length");
    if (FAILED(hr) || contiguous_length == 0)
        return;

    DWORD max_length = 0;
    hr = buffer->GetMaxLength(&max_length);

    test.expect(
        SUCCEEDED(hr) &&
            max_length >= contiguous_length,
        "DXGI buffer max length covers contiguous payload");
    if (FAILED(hr) || max_length < contiguous_length)
        return;

    hr = buffer->SetCurrentLength(
        contiguous_length);

    test.expect(
        SUCCEEDED(hr),
        "DXGI media buffer accepts valid current length");
    if (FAILED(hr))
        return;

    DWORD current_length = 0;
    hr = buffer->GetCurrentLength(
        &current_length);

    test.expect(
        SUCCEEDED(hr) &&
            current_length == contiguous_length,
        "DXGI media buffer publishes non-zero valid payload length");
}

void test_presentation_controller(TestContext &test)
{
    using namespace arssyut::presentation;
    using arssyut::core::TimePoint;

    PresentationController controller;
    controller.reset();

    PresentationSettings settings;
    settings.smart_zoom = true;
    settings.click_visual = true;
    settings.shortcut_keys = true;
    settings.zoom = 2.0f;
    controller.set_settings(settings);

    TimePoint now{1'000'000};
    controller.on_click(
        ClickKind::Left,
        0.75f,
        0.50f,
        now);

    now.ticks_100ns +=
        arssyut::core::MonotonicClock::ticks_per_second * 4 / 100;

    PresentationFrameState frame =
        controller.step(
            0.040f,
            0.75f,
            0.50f,
            true,
            now,
            now);

    controller.on_click(
        ClickKind::Left,
        0.752f,
        0.501f,
        now);

    frame = controller.step(
        0.010f,
        0.752f,
        0.501f,
        true,
        now,
        now);

    std::size_t active_clicks = 0;
    for (const auto &click : frame.clicks) {
        if (click.kind != ClickKind::None)
            ++active_clicks;
    }

    test.expect(
        active_clicks == 1,
        "Rapid same-target click recharges one luminous pulse");

    for (int i = 0; i < 60; ++i) {
        now.ticks_100ns +=
            arssyut::core::MonotonicClock::ticks_per_second / 120;

        frame = controller.step(
            1.0f / 120.0f,
            0.75f,
            0.50f,
            true,
            now,
            now);
    }

    bool click_visible_midway = false;
    for (const auto &click : frame.clicks) {
        click_visible_midway =
            click_visible_midway ||
            click.kind != ClickKind::None;
    }
    test.expect(
        click_visible_midway,
        "Single-ring click remains visible around half a second");

    for (int i = 0; i < 50; ++i) {
        now.ticks_100ns +=
            arssyut::core::MonotonicClock::ticks_per_second / 120;

        frame = controller.step(
            1.0f / 120.0f,
            0.75f,
            0.50f,
            true,
            now,
            now);
    }

    test.expect(
        frame.camera_zoom > 1.50f &&
            frame.camera_zoom <= 2.01f,
        "Click-triggered Smart Zoom reaches configured magnification");

    bool click_visible = false;
    for (const auto &click : frame.clicks) {
        click_visible =
            click_visible ||
            click.kind != ClickKind::None;
    }
    test.expect(
        !click_visible,
        "Emissive click pulse remains bounded after its long fade tail");

    ShortcutChord chord;
    chord.key = ShortcutKey::C;
    chord.modifiers = ShortcutCtrl;
    const TimePoint first_shortcut_time = now;
    controller.on_shortcut(
        chord,
        first_shortcut_time);

    now.ticks_100ns +=
        arssyut::core::MonotonicClock::ticks_per_second / 20;

    frame = controller.step(
        1.0f / 120.0f,
        0.75f,
        0.50f,
        true,
        now,
        now);

    test.expect(
        frame.keyboard.generation != 0 &&
            frame.keyboard.opacity > 0.0f,
        "Shortcut produces bounded keyboard overlay state");

    test.expect(
        frame.keyboard.keycap_count == 2 &&
            std::wstring(
                frame.keyboard.keycaps[0].label.data()) == L"Ctrl" &&
            std::wstring(
                frame.keyboard.keycaps[1].label.data()) == L"C",
        "Shortcut keycaps preserve canonical modifier order");

    const std::uint32_t generation =
        frame.keyboard.generation;

    TimePoint duplicate_time{
        first_shortcut_time.ticks_100ns +
        arssyut::core::MonotonicClock::ticks_per_second * 6 / 100
    };
    controller.on_shortcut(
        chord,
        duplicate_time);

    frame = controller.step(
        1.0f / 120.0f,
        0.75f,
        0.50f,
        true,
        duplicate_time,
        duplicate_time);

    test.expect(
        frame.keyboard.generation == generation,
        "Duplicate shortcut inside coalescing window reuses overlay generation");

    now.ticks_100ns +=
        arssyut::core::MonotonicClock::ticks_per_second * 4;

    for (int i = 0; i < 120; ++i) {
        now.ticks_100ns +=
            arssyut::core::MonotonicClock::ticks_per_second / 120;

        frame = controller.step(
            1.0f / 120.0f,
            0.50f,
            0.50f,
            false,
            now,
            {});
    }

    test.expect(
        frame.camera_zoom < 1.02f,
        "Smart Zoom deterministically returns to full frame");
    test.expect(
        frame.keyboard.opacity == 0.0f,
        "Shortcut overlay expires without history growth");
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

    bool churn_ok = true;
    for (std::uint64_t sequence = 4; sequence < 2'004; ++sequence) {
        CapturedFrame frame;
        frame.sequence = sequence;
        if (slot.publish(std::move(frame)) ==
            LatestFrameSlot::PublishResult::DroppedBusy) {
            churn_ok = false;
            break;
        }

        auto current = slot.try_acquire_latest();
        if (!current || current->sequence != sequence) {
            churn_ok = false;
            break;
        }
    }

    test.expect(
        churn_ok && !slot.has_in_flight(),
        "Frame slot remains bounded across repeated publish/acquire churn");
}

void test_recoverable_session(TestContext &test)
{
    using namespace arssyut::windows;

    const auto root =
        std::filesystem::temp_directory_path() /
        ("arssyut-p1-" + std::to_string(GetCurrentProcessId()));

    std::error_code cleanup_ec;
    std::filesystem::remove_all(root, cleanup_ec);

    RecoverySessionMetadata metadata;
    metadata.session_id = "session-test";
    metadata.intended_output = root / "capture.mp4";
    metadata.output_size = {1920, 1080};
    metadata.frame_rate = {60, 1};

    auto session_result =
        RecoverableSession::create(root, metadata);

    test.expect(
        static_cast<bool>(session_result),
        "Recoverable session creates atomically");
    if (!session_result)
        return;

    auto &session = *session_result.value();

    test.expect(
        std::filesystem::exists(session.manifest_path()),
        "Recoverable manifest exists");

    test.expect(
        session.update_state(
            RecoverySessionState::Recording).ok(),
        "Recoverable state advances to recording");

    test.expect(
        session.update_state(
            RecoverySessionState::Stopped).ok(),
        "Recoverable state advances to stopped");

    std::ifstream manifest(session.manifest_path());
    const std::string content(
        (std::istreambuf_iterator<char>(manifest)),
        std::istreambuf_iterator<char>());

    test.expect(
        content.find("format=arssyut-session-v1") !=
            std::string::npos,
        "Manifest carries stable format identifier");
    test.expect(
        content.find("state=stopped") !=
            std::string::npos,
        "Manifest atomically persists latest state");
    test.expect(
        content.find("fps_num=60") !=
            std::string::npos,
        "Manifest persists canonical frame rate");

    manifest.close();

    test.expect(
        session.update_state(
            RecoverySessionState::Ready).ok(),
        "Recoverable state can be marked ready");

    test.expect(
        session.update_state(
            RecoverySessionState::Recording).code ==
            arssyut::core::StatusCode::InvalidStateTransition,
        "Ready recovery session is terminal");

    std::filesystem::remove_all(root, cleanup_ec);
}

void test_empty_video_pipeline(
    TestContext &test,
    arssyut::windows::D3D11Device &owner)
{
    arssyut::windows::LatestFrameSlot slot;
    arssyut::core::Diagnostics diagnostics;

    auto pipeline_result =
        arssyut::windows::NativeVideoPipeline::create(
            owner.device(),
            slot,
            diagnostics);

    test.expect(
        static_cast<bool>(pipeline_result),
        "Native video pipeline initializes");
    if (!pipeline_result)
        return;

    auto &pipeline = *pipeline_result.value();
    test.expect(
        pipeline.reset_timeline({1'000'000}, {60, 1}).ok(),
        "Native video timeline initializes");

    auto result = pipeline.process_due(
        owner.immediate_context(),
        {1'000'000},
        {0, 0, 1920, 1080},
        {1280, 720});

    test.expect(
        static_cast<bool>(result),
        "Empty pipeline returns a controlled decision");
    if (result) {
        test.expect(
            result.value().action ==
                arssyut::windows::VideoSlotAction::
                    NoFrameAvailable,
            "Empty pipeline reports no frame without blocking");
    }

    test.expect(
        diagnostics.load(
            arssyut::core::DiagnosticMetric::
                VideoFramesUnavailable) == 1,
        "Unavailable output slot is observable");
}

void test_media_foundation_mp4(TestContext &test)
{
    using namespace arssyut::windows;

    // D3D11 video processing is intentionally a hardware-only production
    // capability. Microsoft documents that WARP does not expose
    // ID3D11VideoDevice, so CI may legitimately lack this integration path.
    auto hardware_result =
        D3D11Device::create(
            D3D11DevicePreference::HardwareOnly,
            false);

    if (!hardware_result) {
        std::cout
            << "SKIP: hardware Media Foundation integration unavailable; "
            << "D3D11 status="
            << static_cast<unsigned>(
                   hardware_result.status().code)
            << " detail=0x" << std::hex
            << hardware_result.status().detail
            << std::dec << '\n';
        return;
    }

    auto &owner = *hardware_result.value();

    constexpr std::uint32_t width = 192;
    constexpr std::uint32_t height = 64;
    constexpr std::array<std::uint8_t, 12> gray_levels{
        0, 8, 16, 24, 32, 64,
        128, 192, 220, 232, 246, 255
    };
    constexpr std::uint32_t band_width =
        width /
        static_cast<std::uint32_t>(
            gray_levels.size());

    std::vector<std::uint32_t> pixels(
        static_cast<std::size_t>(width) *
            static_cast<std::size_t>(height));

    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            const std::size_t band =
                std::min<std::size_t>(
                    x / band_width,
                    gray_levels.size() - 1);
            const std::uint32_t g =
                gray_levels[band];
            pixels[
                static_cast<std::size_t>(y) * width + x] =
                0xFF000000u |
                (g << 16) |
                (g << 8) |
                g;
        }
    }

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags =
        D3D11_BIND_SHADER_RESOURCE |
        D3D11_BIND_RENDER_TARGET;

    D3D11_SUBRESOURCE_DATA initial{};
    initial.pSysMem = pixels.data();
    initial.SysMemPitch =
        width * sizeof(std::uint32_t);

    Microsoft::WRL::ComPtr<ID3D11Texture2D> source;
    const HRESULT texture_hr =
        owner.device()->CreateTexture2D(
            &desc,
            &initial,
            source.GetAddressOf());

    test.expect(
        SUCCEEDED(texture_hr) && source,
        "MF test source texture created");
    if (FAILED(texture_hr) || !source)
        return;

    const auto root =
        std::filesystem::temp_directory_path() /
        ("arssyut-mf-" +
         std::to_string(GetCurrentProcessId()));

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);

    const auto output =
        root / "synthetic.mp4";

    MfH264Mp4Writer writer;
    MfVideoWriterConfig config;
    config.size = {width, height};
    config.frame_rate = {30, 1};
    config.bitrate_bps = 2'000'000;
    config.surface_count = 4;

    const auto open_status =
        writer.open(
            owner.device(),
            output,
            config);

    if (open_status.code ==
        arssyut::core::StatusCode::Unsupported) {
        std::cout
            << "SKIP: hosted GPU exposes D3D11 but not required "
            << "video-processor capability; detail=0x"
            << std::hex << open_status.detail
            << std::dec << '\n';
        std::filesystem::remove_all(root, ec);
        return;
    }

    test.expect(
        open_status.ok(),
        "Media Foundation H.264 writer opens");
    if (!open_status.ok()) {
        std::cerr << "MF open status="
                  << static_cast<unsigned>(open_status.code)
                  << " detail=0x" << std::hex
                  << open_status.detail << std::dec << '\n';
        std::filesystem::remove_all(root, ec);
        return;
    }

    const std::string active_profile =
        arssyut::windows::mf_h264_profile_name(
            writer.active_profile());

    test.expect(
        active_profile == "high" ||
            active_profile == "main",
        "P5D encoder resolves a canonical H.264 profile");

    test.expect(
        writer.requested_quality_vs_speed() == 85,
        "P5D.6 encoder carries the high quality-vs-speed preference");

    test.expect(
        writer.bitrate_vbr_applied(),
        "P5D.6 encoder negotiates bitrate-controlled unconstrained VBR");

    test.expect(
        writer.color_pipeline_authoritative(),
        "P5D.7 encoder uses an explicit color-pipeline authority");

    const std::string color_pipeline =
        arssyut::windows::mf_color_pipeline_mode_name(
            writer.active_color_pipeline());

    test.expect(
        color_pipeline == "d3d11_context1_bt709" ||
            color_pipeline == "d3d11_legacy_explicit_bt709",
        "P5D.7 color pipeline resolves to explicit BT.709 conversion");

    std::cout
        << "P5D.6 encoder profile="
        << active_profile
        << " rate_control="
        << arssyut::windows::mf_rate_control_mode_name(
               writer.active_rate_control())
        << " quality_vs_speed="
        << (writer.quality_vs_speed_applied() ? "applied" : "fallback")
        << " color_pipeline="
        << color_pipeline
        << '\n';

    bool write_ok = true;
    constexpr std::int64_t duration =
        arssyut::core::MonotonicClock::
            ticks_per_second / 30;

    for (int frame = 0; frame < 12; ++frame) {
        arssyut::core::Status status;

        for (int retry = 0; retry < 200; ++retry) {
            status = writer.write_frame(
                owner.immediate_context(),
                source.Get(),
                {static_cast<std::int64_t>(
                    frame) * duration},
                duration);

            if (status.ok())
                break;

            if (status.code !=
                arssyut::core::StatusCode::
                    EncoderBackpressure) {
                break;
            }

            std::this_thread::sleep_for(
                std::chrono::milliseconds(1));
        }

        if (!status.ok()) {
            std::cerr << "MF write status="
                      << static_cast<unsigned>(status.code)
                      << " detail=0x" << std::hex
                      << status.detail << std::dec
                      << " frame=" << frame << '\n';
            write_ok = false;
            break;
        }
    }

    test.expect(
        write_ok,
        "Media Foundation accepts bounded NV12 DXGI samples");

    const auto finalize_status =
        writer.finalize();

    test.expect(
        finalize_status.ok(),
        "Media Foundation finalizes MP4");
    if (!finalize_status.ok()) {
        std::cerr << "MF finalize status="
                  << static_cast<unsigned>(finalize_status.code)
                  << " detail=0x" << std::hex
                  << finalize_status.detail << std::dec << '\n';
    }

    const auto size =
        std::filesystem::file_size(
            output,
            ec);

    test.expect(
        !ec && size > 512,
        "Finalized MP4 is non-empty");

    /*
     * P5D.7 end-to-end color-range gate.
     *
     * Re-open the actual H.264/MP4 through Media Foundation, request decoded
     * NV12, and inspect its luma plane. This catches the exact class of bug
     * found in real recordings: full-range Y values being carried in a stream
     * interpreted as studio-range, which collapses near-white gray UI and
     * near-black hierarchy.
     */
    const HRESULT decode_startup =
        MFStartup(
            MF_VERSION,
            MFSTARTUP_FULL);

    test.expect(
        SUCCEEDED(decode_startup),
        "P5D.7 Media Foundation decode session starts");

    if (SUCCEEDED(decode_startup)) {
        Microsoft::WRL::ComPtr<IMFSourceReader> reader;
        const HRESULT reader_hr =
            MFCreateSourceReaderFromURL(
                output.c_str(),
                nullptr,
                reader.GetAddressOf());

        test.expect(
            SUCCEEDED(reader_hr) && reader,
            "P5D.7 encoded MP4 opens through Source Reader");

        if (SUCCEEDED(reader_hr) && reader) {
            Microsoft::WRL::ComPtr<IMFMediaType> native_type;
            const HRESULT native_hr =
                reader->GetNativeMediaType(
                    static_cast<DWORD>(
                        MF_SOURCE_READER_FIRST_VIDEO_STREAM),
                    0,
                    native_type.GetAddressOf());

            test.expect(
                SUCCEEDED(native_hr) && native_type,
                "P5D.7 H.264 native media type is readable");

            if (SUCCEEDED(native_hr) && native_type) {
                UINT32 nominal_range = MFNominalRange_Unknown;
                UINT32 primaries = MFVideoPrimaries_Unknown;
                UINT32 transfer = MFVideoTransFunc_Unknown;
                UINT32 matrix = MFVideoTransferMatrix_Unknown;

                const HRESULT range_hr =
                    native_type->GetUINT32(
                        MF_MT_VIDEO_NOMINAL_RANGE,
                        &nominal_range);
                const HRESULT primaries_hr =
                    native_type->GetUINT32(
                        MF_MT_VIDEO_PRIMARIES,
                        &primaries);
                const HRESULT transfer_hr =
                    native_type->GetUINT32(
                        MF_MT_TRANSFER_FUNCTION,
                        &transfer);
                const HRESULT matrix_hr =
                    native_type->GetUINT32(
                        MF_MT_YUV_MATRIX,
                        &matrix);

                test.expect(
                    SUCCEEDED(range_hr) &&
                        nominal_range ==
                            MFNominalRange_16_235,
                    "P5D.7 MP4 signals studio 16-235 nominal range");
                test.expect(
                    SUCCEEDED(primaries_hr) &&
                        primaries ==
                            MFVideoPrimaries_BT709,
                    "P5D.7 MP4 signals BT.709 primaries");
                test.expect(
                    SUCCEEDED(transfer_hr) &&
                        transfer ==
                            MFVideoTransFunc_709,
                    "P5D.7 MP4 signals BT.709 transfer");
                test.expect(
                    SUCCEEDED(matrix_hr) &&
                        matrix ==
                            MFVideoTransferMatrix_BT709,
                    "P5D.7 MP4 signals BT.709 YCbCr matrix");
            }

            Microsoft::WRL::ComPtr<IMFMediaType> decode_type;
            HRESULT decode_hr =
                MFCreateMediaType(
                    decode_type.GetAddressOf());

            if (SUCCEEDED(decode_hr)) {
                decode_hr =
                    decode_type->SetGUID(
                        MF_MT_MAJOR_TYPE,
                        MFMediaType_Video);
            }
            if (SUCCEEDED(decode_hr)) {
                decode_hr =
                    decode_type->SetGUID(
                        MF_MT_SUBTYPE,
                        MFVideoFormat_NV12);
            }
            if (SUCCEEDED(decode_hr)) {
                decode_hr =
                    reader->SetCurrentMediaType(
                        static_cast<DWORD>(
                            MF_SOURCE_READER_FIRST_VIDEO_STREAM),
                        nullptr,
                        decode_type.Get());
            }

            test.expect(
                SUCCEEDED(decode_hr),
                "P5D.7 Source Reader decodes H.264 to NV12");

            Microsoft::WRL::ComPtr<IMFSample> decoded_sample;

            if (SUCCEEDED(decode_hr)) {
                for (int attempt = 0;
                     attempt < 64 &&
                     !decoded_sample;
                     ++attempt) {
                    DWORD actual_stream = 0;
                    DWORD flags = 0;
                    LONGLONG timestamp = 0;
                    Microsoft::WRL::ComPtr<IMFSample> sample;

                    const HRESULT read_hr =
                        reader->ReadSample(
                            static_cast<DWORD>(
                                MF_SOURCE_READER_FIRST_VIDEO_STREAM),
                            0,
                            &actual_stream,
                            &flags,
                            &timestamp,
                            sample.GetAddressOf());

                    if (FAILED(read_hr))
                        break;

                    if (sample)
                        decoded_sample = sample;

                    if ((flags &
                         MF_SOURCE_READERF_ENDOFSTREAM) != 0) {
                        break;
                    }
                }
            }

            test.expect(
                decoded_sample != nullptr,
                "P5D.7 decoded gray-ladder sample is available");

            if (decoded_sample) {
                Microsoft::WRL::ComPtr<IMFMediaBuffer> buffer;
                const HRESULT buffer_hr =
                    decoded_sample->
                        ConvertToContiguousBuffer(
                            buffer.GetAddressOf());

                test.expect(
                    SUCCEEDED(buffer_hr) && buffer,
                    "P5D.7 decoded NV12 sample is contiguous");

                if (SUCCEEDED(buffer_hr) && buffer) {
                    BYTE *bytes_ptr = nullptr;
                    DWORD max_length = 0;
                    DWORD current_length = 0;

                    const HRESULT lock_hr =
                        buffer->Lock(
                            &bytes_ptr,
                            &max_length,
                            &current_length);

                    test.expect(
                        SUCCEEDED(lock_hr) &&
                            bytes_ptr &&
                            current_length >=
                                width * height,
                        "P5D.7 decoded NV12 luma plane is readable");

                    if (SUCCEEDED(lock_hr) &&
                        bytes_ptr &&
                        current_length >=
                            width * height) {
                        const std::uint64_t stride_numerator =
                            static_cast<std::uint64_t>(
                                current_length) *
                            2ULL;
                        const std::uint64_t stride_denominator =
                            3ULL *
                            static_cast<std::uint64_t>(
                                height);

                        const std::uint32_t stride =
                            stride_denominator != 0 &&
                                    stride_numerator %
                                        stride_denominator ==
                                        0
                                ? static_cast<std::uint32_t>(
                                      stride_numerator /
                                      stride_denominator)
                                : width;

                        test.expect(
                            stride >= width,
                            "P5D.7 decoded NV12 stride covers the luma width");

                        std::array<int, gray_levels.size()>
                            decoded_y{};

                        const std::uint32_t sample_y =
                            height / 2;

                        for (std::size_t band = 0;
                             band < gray_levels.size();
                             ++band) {
                            const std::uint32_t sample_x =
                                static_cast<std::uint32_t>(
                                    band) *
                                    band_width +
                                band_width / 2;

                            decoded_y[band] =
                                bytes_ptr[
                                    static_cast<std::size_t>(
                                        sample_y) *
                                        stride +
                                    sample_x];
                        }

                        bool monotonic = true;
                        bool within_studio_mapping = true;

                        for (std::size_t band = 0;
                             band < gray_levels.size();
                             ++band) {
                            const double normalized =
                                static_cast<double>(
                                    gray_levels[band]) /
                                255.0;
                            const int expected =
                                static_cast<int>(
                                    16.0 +
                                    219.0 * normalized +
                                    0.5);

                            if (std::abs(
                                    decoded_y[band] -
                                    expected) > 10) {
                                within_studio_mapping = false;
                            }

                            if (band > 0 &&
                                decoded_y[band] + 2 <
                                    decoded_y[band - 1]) {
                                monotonic = false;
                            }
                        }

                        test.expect(
                            within_studio_mapping,
                            "P5D.7 gray ladder follows studio-range BT.709 luma mapping");
                        test.expect(
                            monotonic,
                            "P5D.7 gray ladder remains monotonic after H.264 round-trip");
                        test.expect(
                            decoded_y.front() >= 10 &&
                                decoded_y.back() <= 241,
                            "P5D.7 black/white endpoints remain in studio-range neighborhood");
                        test.expect(
                            decoded_y.back() -
                                    decoded_y[
                                        decoded_y.size() -
                                        2] >=
                                3,
                            "P5D.7 near-white gray remains distinct from white");
                        test.expect(
                            decoded_y[2] -
                                    decoded_y.front() >=
                                5,
                            "P5D.7 near-black gray remains distinct from black");

                        std::cout
                            << "P5D.7 decoded Y:"
                            << " black="
                            << decoded_y.front()
                            << " gray246="
                            << decoded_y[
                                   decoded_y.size() - 2]
                            << " white="
                            << decoded_y.back()
                            << '\n';
                    }

                    if (SUCCEEDED(lock_hr))
                        (void)buffer->Unlock();
                }
            }
        }

        MFShutdown();
    }

    std::filesystem::remove_all(root, ec);
}

void test_retained_source_camera_cadence(
    TestContext &test,
    arssyut::windows::D3D11Device &owner)
{
    constexpr std::uint32_t left_bgra = 0xFF2040E0u;
    constexpr std::uint32_t right_bgra = 0xFFE06020u;

    auto source = create_split_texture(
        owner.device(),
        8,
        4,
        left_bgra,
        right_bgra);

    test.expect(
        source != nullptr,
        "Retained-source test texture created");
    if (!source)
        return;

    auto compositor_result =
        arssyut::windows::D3D11Compositor::create(
            owner.device());
    test.expect(
        static_cast<bool>(compositor_result),
        "Retained-source compositor initializes");
    if (!compositor_result)
        return;

    auto &compositor = *compositor_result.value();

    test.expect(
        compositor.update_source(
            owner.immediate_context(),
            source.Get()).ok(),
        "Compositor retains latest source texture");

    arssyut::presentation::PresentationFrameState left_camera{};
    left_camera.camera_center_x = 0.25f;
    left_camera.camera_center_y = 0.50f;
    left_camera.camera_zoom = 2.0f;

    test.expect(
        compositor.render_retained(
            owner.immediate_context(),
            {0, 0, 8, 4},
            {8, 4},
            &left_camera).ok(),
        "First camera frame renders from retained source");

    std::uint32_t left_pixel = 0;
    test.expect(
        read_texture_pixel(
            owner.device(),
            owner.immediate_context(),
            compositor.output_texture(),
            4,
            2,
            left_pixel),
        "First camera frame can be inspected");

    arssyut::presentation::PresentationFrameState right_camera{};
    right_camera.camera_center_x = 0.75f;
    right_camera.camera_center_y = 0.50f;
    right_camera.camera_zoom = 2.0f;

    const auto generation =
        compositor.resource_generation();

    test.expect(
        compositor.render_retained(
            owner.immediate_context(),
            {0, 0, 8, 4},
            {8, 4},
            &right_camera).ok(),
        "Second camera frame re-renders without a new source frame");

    std::uint32_t right_pixel = 0;
    test.expect(
        read_texture_pixel(
            owner.device(),
            owner.immediate_context(),
            compositor.output_texture(),
            4,
            2,
            right_pixel),
        "Second camera frame can be inspected");

    test.expect(
        left_pixel != right_pixel,
        "Camera transform advances visually while source texture is reused");

    test.expect(
        compositor.resource_generation() == generation,
        "Retained-source camera animation allocates no new frame resources");
}

void test_single_ring_click_compositor(
    TestContext &test,
    arssyut::windows::D3D11Device &owner)
{
    constexpr std::uint32_t dark_bgra = 0xFF20242Au;
    constexpr std::uint32_t bright_bgra = 0xFFF8F8F8u;

    auto dark_source = create_solid_texture(
        owner.device(),
        4,
        4,
        dark_bgra);
    test.expect(
        dark_source != nullptr,
        "Emissive click dark source texture created");
    if (!dark_source)
        return;

    auto compositor_result =
        arssyut::windows::D3D11Compositor::create(
            owner.device());
    test.expect(
        static_cast<bool>(compositor_result),
        "Emissive single-ring compositor initializes");
    if (!compositor_result)
        return;

    auto &compositor = *compositor_result.value();

    arssyut::presentation::PresentationFrameState state{};
    state.clicks[0].content_x = 0.5f;
    state.clicks[0].content_y = 0.5f;
    state.clicks[0].age_seconds = 0.22f;
    state.clicks[0].lifetime_seconds = 0.88f;
    state.clicks[0].kind =
        arssyut::presentation::ClickKind::Left;

    test.expect(
        compositor.render(
            owner.immediate_context(),
            dark_source.Get(),
            {0, 0, 4, 4},
            {640, 360},
            &state).ok(),
        "Emissive single-ring renders on dark content");

    std::uint32_t dark_center = 0;
    std::uint32_t dark_ring = 0;
    std::uint32_t dark_halo = 0;

    test.expect(
        read_texture_pixel(
            owner.device(),
            owner.immediate_context(),
            compositor.output_texture(),
            320,
            180,
            dark_center),
        "Dark click center pixel can be inspected");

    test.expect(
        read_texture_pixel(
            owner.device(),
            owner.immediate_context(),
            compositor.output_texture(),
            355,
            180,
            dark_ring),
        "Dark click core pixel can be inspected");

    test.expect(
        read_texture_pixel(
            owner.device(),
            owner.immediate_context(),
            compositor.output_texture(),
            370,
            180,
            dark_halo),
        "Dark click halo pixel can be inspected");

    test.expect(
        dark_center == dark_bgra,
        "Emissive click keeps the center unfilled with no center dot");
    test.expect(
        dark_ring != dark_bgra,
        "Emissive click produces a strong visible core");
    test.expect(
        dark_halo != dark_bgra,
        "Emissive click produces a diffuse halo from the same ring");

    auto bright_source = create_solid_texture(
        owner.device(),
        4,
        4,
        bright_bgra);
    test.expect(
        bright_source != nullptr,
        "Emissive click bright source texture created");
    if (!bright_source)
        return;

    test.expect(
        compositor.render(
            owner.immediate_context(),
            bright_source.Get(),
            {0, 0, 4, 4},
            {640, 360},
            &state).ok(),
        "Emissive single-ring renders on bright content");

    std::uint32_t bright_center = 0;
    std::uint32_t bright_ring = 0;
    std::uint32_t bright_halo = 0;

    test.expect(
        read_texture_pixel(
            owner.device(),
            owner.immediate_context(),
            compositor.output_texture(),
            320,
            180,
            bright_center),
        "Bright click center pixel can be inspected");
    test.expect(
        read_texture_pixel(
            owner.device(),
            owner.immediate_context(),
            compositor.output_texture(),
            355,
            180,
            bright_ring),
        "Bright click core pixel can be inspected");
    test.expect(
        read_texture_pixel(
            owner.device(),
            owner.immediate_context(),
            compositor.output_texture(),
            370,
            180,
            bright_halo),
        "Bright click halo pixel can be inspected");

    test.expect(
        bright_center == bright_bgra,
        "Bright-background click still keeps center transparent");
    test.expect(
        bright_ring != bright_bgra,
        "Chromatic core remains visible on near-white content");
    test.expect(
        bright_halo != bright_bgra,
        "Chromatic support keeps the glow visible on near-white content");

    state.clicks[0].age_seconds = 0.030f;
    test.expect(
        compositor.render(
            owner.immediate_context(),
            dark_source.Get(),
            {0, 0, 4, 4},
            {640, 360},
            &state).ok(),
        "Ignition-phase click renders");

    std::uint32_t ignition_center = 0;
    test.expect(
        read_texture_pixel(
            owner.device(),
            owner.immediate_context(),
            compositor.output_texture(),
            320,
            180,
            ignition_center),
        "Ignition center pixel can be inspected");
    test.expect(
        ignition_center == dark_bgra,
        "Wide ignition glow never turns into a filled center");
}

void test_arvisual_grade_compositor(
    TestContext &test,
    arssyut::windows::D3D11Device &owner)
{
    auto compositor_result =
        arssyut::windows::D3D11Compositor::create(
            owner.device());
    test.expect(
        static_cast<bool>(compositor_result),
        "P5A ArVisual compositor initializes");
    if (!compositor_result)
        return;

    auto &compositor =
        *compositor_result.value();

    arssyut::visual::ArVisualGradeSettings grade;
    grade.enabled = true;

    constexpr std::uint32_t warm_bgra = 0xFFB06030u;
    auto warm = create_solid_texture(
        owner.device(),
        4,
        4,
        warm_bgra);
    test.expect(
        warm != nullptr,
        "P5A colorful source texture created");
    if (!warm)
        return;

    test.expect(
        compositor.render(
            owner.immediate_context(),
            warm.Get(),
            {0, 0, 4, 4},
            {64, 64},
            nullptr,
            &grade).ok(),
        "P5A colorful source renders through standalone grade");

    std::uint32_t warm_out = 0;
    test.expect(
        read_texture_pixel(
            owner.device(),
            owner.immediate_context(),
            compositor.output_texture(),
            32,
            32,
            warm_out),
        "P5A colorful output can be inspected");
    test.expect(
        warm_out != warm_bgra,
        "P5A portable behavior produces a real bounded grade");

    const auto generation =
        compositor.resource_generation();

    constexpr std::uint32_t neutral_bgra = 0xFFC0C0C0u;
    auto neutral = create_solid_texture(
        owner.device(),
        4,
        4,
        neutral_bgra);
    test.expect(
        neutral != nullptr,
        "P5A neutral source texture created");
    if (!neutral)
        return;

    test.expect(
        compositor.render(
            owner.immediate_context(),
            neutral.Get(),
            {0, 0, 4, 4},
            {64, 64},
            nullptr,
            &grade).ok(),
        "P5A neutral source renders");

    std::uint32_t neutral_out = 0;
    test.expect(
        read_texture_pixel(
            owner.device(),
            owner.immediate_context(),
            compositor.output_texture(),
            32,
            32,
            neutral_out),
        "P5A neutral output can be inspected");

    const int nb =
        static_cast<int>(neutral_out & 0xFFu);
    const int ng =
        static_cast<int>((neutral_out >> 8) & 0xFFu);
    const int nr =
        static_cast<int>((neutral_out >> 16) & 0xFFu);

    test.expect(
        std::abs(nr - ng) <= 1 &&
            std::abs(ng - nb) <= 1,
        "P5A preserves neutral white/gray balance without color cast");

    constexpr std::uint32_t bright_bgra = 0xFFF0D060u;
    auto bright = create_solid_texture(
        owner.device(),
        4,
        4,
        bright_bgra);
    test.expect(
        bright != nullptr,
        "P5A highlight source texture created");
    if (!bright)
        return;

    test.expect(
        compositor.render(
            owner.immediate_context(),
            bright.Get(),
            {0, 0, 4, 4},
            {64, 64},
            nullptr,
            &grade).ok(),
        "P5A highlight source renders");

    std::uint32_t bright_out = 0;
    test.expect(
        read_texture_pixel(
            owner.device(),
            owner.immediate_context(),
            compositor.output_texture(),
            32,
            32,
            bright_out),
        "P5A highlight output can be inspected");

    const unsigned bb =
        bright_out & 0xFFu;
    const unsigned bg =
        (bright_out >> 8) & 0xFFu;
    const unsigned br =
        (bright_out >> 16) & 0xFFu;

    test.expect(
        std::max({br, bg, bb}) < 255u,
        "P5A highlight/gamut guard avoids manufactured digital clipping");

    grade.enabled = false;
    test.expect(
        compositor.render(
            owner.immediate_context(),
            warm.Get(),
            {0, 0, 4, 4},
            {64, 64},
            nullptr,
            &grade).ok(),
        "P5A can return to Pixel Accurate without rebuilding pipeline");

    std::uint32_t bypass_out = 0;
    test.expect(
        read_texture_pixel(
            owner.device(),
            owner.immediate_context(),
            compositor.output_texture(),
            32,
            32,
            bypass_out),
        "P5A bypass output can be inspected");

    test.expect(
        bypass_out == warm_bgra,
        "P5A disabled path is pixel-accurate");

    test.expect(
        compositor.resource_generation() == generation,
        "P5A grade/bypass changes create no compositor resources");
}

void test_arvisual_product_modes(
    TestContext &test,
    arssyut::windows::D3D11Device &owner)
{
    constexpr std::uint32_t warm_bgra = 0xFFB06030u;

    auto source = create_solid_texture(
        owner.device(),
        4,
        4,
        warm_bgra);
    test.expect(
        source != nullptr,
        "P5C product-mode source texture created");
    if (!source)
        return;

    auto compositor_result =
        arssyut::windows::D3D11Compositor::create(
            owner.device());
    test.expect(
        static_cast<bool>(compositor_result),
        "P5C product-mode compositor initializes");
    if (!compositor_result)
        return;

    auto &compositor =
        *compositor_result.value();

    const auto pixel_grade =
        arssyut::visual::grade_for_mode(
            arssyut::visual::ArVisualProductMode::
                PixelAccurate);
    const auto clean_grade =
        arssyut::visual::grade_for_mode(
            arssyut::visual::ArVisualProductMode::
                CleanScreen);
    const auto vivid_grade =
        arssyut::visual::grade_for_mode(
            arssyut::visual::ArVisualProductMode::
                VividPresentation);

    auto render_pixel =
        [&](const arssyut::visual::ArVisualGradeSettings &grade,
            std::uint32_t &pixel) {
            if (!compositor.render(
                    owner.immediate_context(),
                    source.Get(),
                    {0, 0, 4, 4},
                    {64, 64},
                    nullptr,
                    &grade).ok()) {
                return false;
            }

            return read_texture_pixel(
                owner.device(),
                owner.immediate_context(),
                compositor.output_texture(),
                32,
                32,
                pixel);
        };

    std::uint32_t pixel_out = 0;
    std::uint32_t clean_out = 0;
    std::uint32_t vivid_out = 0;

    test.expect(
        render_pixel(
            pixel_grade,
            pixel_out),
        "P5C Pixel Accurate output can be inspected");

    const auto generation =
        compositor.resource_generation();
    test.expect(
        render_pixel(
            clean_grade,
            clean_out),
        "P5C Clean Screen output can be inspected");
    test.expect(
        render_pixel(
            vivid_grade,
            vivid_out),
        "P5C Vivid Presentation output can be inspected");

    test.expect(
        pixel_out == warm_bgra,
        "P5C Pixel Accurate remains pixel-identical");
    test.expect(
        clean_out != warm_bgra,
        "P5C Clean Screen applies a real bounded grade");
    test.expect(
        vivid_out != warm_bgra,
        "P5C Vivid Presentation applies a real bounded grade");
    test.expect(
        clean_out != vivid_out,
        "P5C Clean and Vivid modes are visually distinct");

    test.expect(
        compositor.resource_generation() == generation,
        "P5C mode changes allocate no compositor resources");
}

void test_screen_native_surface_anchor(
    TestContext &test,
    arssyut::windows::D3D11Device &owner)
{
    auto compositor_result =
        arssyut::windows::D3D11Compositor::create(
            owner.device());
    test.expect(
        static_cast<bool>(compositor_result),
        "P5E screen-native compositor initializes");
    if (!compositor_result)
        return;

    auto &compositor =
        *compositor_result.value();

    auto clean =
        arssyut::visual::grade_for_mode(
            arssyut::visual::ArVisualProductMode::
                CleanScreen);
    clean.smart_auto = false;
    clean.smart_screen_ui = 1.0f;

    auto unanchored = clean;
    unanchored.neutral_surface_anchor = 0.0f;

    const auto channel =
        [](std::uint32_t bgra) {
            return static_cast<int>(
                bgra & 0xFFu);
        };

    constexpr std::array<std::uint8_t, 6>
        gray_levels{
            32, 64, 128, 192, 232, 246
        };

    std::uint64_t stable_generation = 0;
    bool generation_initialized = false;

    for (const auto gray : gray_levels) {
        const std::uint32_t bgra =
            0xFF000000u |
            (static_cast<std::uint32_t>(gray) << 16) |
            (static_cast<std::uint32_t>(gray) << 8) |
            static_cast<std::uint32_t>(gray);

        auto source =
            create_solid_texture(
                owner.device(),
                16,
                16,
                bgra);
        test.expect(
            source != nullptr,
            "P5E gray-ladder source created");
        if (!source)
            return;

        std::uint32_t base_out = 0;
        std::uint32_t anchored_out = 0;

        test.expect(
            compositor.render(
                owner.immediate_context(),
                source.Get(),
                {0, 0, 16, 16},
                {64, 64},
                nullptr,
                &unanchored).ok() &&
            read_texture_pixel(
                owner.device(),
                owner.immediate_context(),
                compositor.output_texture(),
                32,
                32,
                base_out),
            "P5E unanchored neutral surface renders");

        if (!generation_initialized) {
            stable_generation =
                compositor.resource_generation();
            generation_initialized = true;
        }

        test.expect(
            compositor.render(
                owner.immediate_context(),
                source.Get(),
                {0, 0, 16, 16},
                {64, 64},
                nullptr,
                &clean).ok() &&
            read_texture_pixel(
                owner.device(),
                owner.immediate_context(),
                compositor.output_texture(),
                32,
                32,
                anchored_out),
            "P5E anchored neutral surface renders");

        const int source_value =
            static_cast<int>(gray);
        const int anchored_error =
            std::abs(
                channel(anchored_out) -
                source_value);
        const int base_error =
            std::abs(
                channel(base_out) -
                source_value);

        test.expect(
            anchored_error <= 4,
            "P5E Clean Screen keeps flat neutral luma within a tight authored-value budget");

        test.expect(
            anchored_error <=
                base_error,
            "P5E neutral anchor never moves a flat gray farther from its source luma");

        test.expect(
            compositor.resource_generation() ==
                stable_generation,
            "P5E gray-ladder anchoring allocates no compositor resources");
    }

    constexpr std::uint32_t saturated_blue =
        0xFFFF4010u;
    auto colored_source =
        create_solid_texture(
            owner.device(),
            16,
            16,
            saturated_blue);
    test.expect(
        colored_source != nullptr,
        "P5E saturated non-neutral fixture created");
    if (!colored_source)
        return;

    std::uint32_t colored_base = 0;
    std::uint32_t colored_anchor = 0;

    test.expect(
        compositor.render(
            owner.immediate_context(),
            colored_source.Get(),
            {0, 0, 16, 16},
            {64, 64},
            nullptr,
            &unanchored).ok() &&
        read_texture_pixel(
            owner.device(),
            owner.immediate_context(),
            compositor.output_texture(),
            32,
            32,
            colored_base) &&
        compositor.render(
            owner.immediate_context(),
            colored_source.Get(),
            {0, 0, 16, 16},
            {64, 64},
            nullptr,
            &clean).ok() &&
        read_texture_pixel(
            owner.device(),
            owner.immediate_context(),
            compositor.output_texture(),
            32,
            32,
            colored_anchor),
        "P5E colored anchor-exclusion outputs can be inspected");

    test.expect(
        colored_anchor == colored_base,
        "P5E neutral-surface anchor does not alter saturated media/color regions");
}

void test_screen_text_legibility(
    TestContext &test,
    arssyut::windows::D3D11Device &owner)
{
    constexpr std::uint32_t dark_gray = 0xFF505050u;
    constexpr std::uint32_t light_gray = 0xFFD0D0D0u;

    auto source =
        create_split_texture(
            owner.device(),
            64,
            64,
            dark_gray,
            light_gray);
    test.expect(
        source != nullptr,
        "P5D neutral text-edge fixture created");
    if (!source)
        return;

    auto compositor_result =
        arssyut::windows::D3D11Compositor::create(
            owner.device());
    test.expect(
        static_cast<bool>(compositor_result),
        "P5D text-fidelity compositor initializes");
    if (!compositor_result)
        return;

    auto &compositor =
        *compositor_result.value();

    const auto pixel_grade =
        arssyut::visual::grade_for_mode(
            arssyut::visual::ArVisualProductMode::
                PixelAccurate);

    test.expect(
        compositor.render(
            owner.immediate_context(),
            source.Get(),
            {0, 0, 64, 64},
            {64, 64},
            nullptr,
            &pixel_grade).ok(),
        "P5D Pixel Accurate edge fixture renders");

    std::uint32_t pixel_dark = 0;
    std::uint32_t pixel_light = 0;
    test.expect(
        read_texture_pixel(
            owner.device(),
            owner.immediate_context(),
            compositor.output_texture(),
            8,
            32,
            pixel_dark) &&
        read_texture_pixel(
            owner.device(),
            owner.immediate_context(),
            compositor.output_texture(),
            55,
            32,
            pixel_light),
        "P5D Pixel Accurate fixture pixels can be inspected");

    test.expect(
        pixel_dark == dark_gray &&
            pixel_light == light_gray,
        "P5D Pixel Accurate preserves neutral screen pixels exactly at 1:1");

    arssyut::visual::ArVisualGradeSettings baseline;
    baseline.enabled = true;
    baseline.smart_auto = false;
    baseline.master = 0.0f;
    baseline.enhance = 0.0f;
    baseline.color_pop = 0.0f;
    baseline.clean_white = 0.0f;
    baseline.clarity = 0.0f;
    baseline.skin_protect = 0.0f;
    baseline.skin_beauty = 0.0f;
    baseline.healthy_tone = 0.0f;
    baseline.toy_gloss = 0.0f;
    baseline.depth_pop = 0.0f;
    baseline.highlight_guard = 0.0f;
    baseline.performance = 1.0f;
    baseline.text_legibility = 0.0f;

    test.expect(
        compositor.render(
            owner.immediate_context(),
            source.Get(),
            {0, 0, 64, 64},
            {64, 64},
            nullptr,
            &baseline).ok(),
        "P5D neutral baseline edge renders");

    std::uint32_t base_left = 0;
    std::uint32_t base_right = 0;
    std::uint32_t base_flat = 0;
    test.expect(
        read_texture_pixel(
            owner.device(),
            owner.immediate_context(),
            compositor.output_texture(),
            31,
            32,
            base_left) &&
        read_texture_pixel(
            owner.device(),
            owner.immediate_context(),
            compositor.output_texture(),
            32,
            32,
            base_right) &&
        read_texture_pixel(
            owner.device(),
            owner.immediate_context(),
            compositor.output_texture(),
            8,
            32,
            base_flat),
        "P5D baseline edge samples can be inspected");

    const auto generation =
        compositor.resource_generation();

    auto enhanced = baseline;
    enhanced.text_legibility = 0.80f;

    test.expect(
        compositor.render(
            owner.immediate_context(),
            source.Get(),
            {0, 0, 64, 64},
            {64, 64},
            nullptr,
            &enhanced).ok(),
        "P5D enhanced neutral edge renders");

    std::uint32_t enhanced_left = 0;
    std::uint32_t enhanced_right = 0;
    std::uint32_t enhanced_flat = 0;
    test.expect(
        read_texture_pixel(
            owner.device(),
            owner.immediate_context(),
            compositor.output_texture(),
            31,
            32,
            enhanced_left) &&
        read_texture_pixel(
            owner.device(),
            owner.immediate_context(),
            compositor.output_texture(),
            32,
            32,
            enhanced_right) &&
        read_texture_pixel(
            owner.device(),
            owner.immediate_context(),
            compositor.output_texture(),
            8,
            32,
            enhanced_flat),
        "P5D enhanced edge samples can be inspected");

    const auto channel =
        [](std::uint32_t bgra) {
            return static_cast<int>(bgra & 0xFFu);
        };

    const int base_contrast =
        channel(base_right) -
        channel(base_left);
    const int enhanced_contrast =
        channel(enhanced_right) -
        channel(enhanced_left);

    test.expect(
        enhanced_contrast > base_contrast,
        "P5D reinforces neutral micro-edge luma contrast");

    test.expect(
        std::abs(
            channel(enhanced_flat) -
            channel(base_flat)) <= 1,
        "P5D leaves flat neutral regions unchanged");

    test.expect(
        compositor.resource_generation() == generation,
        "P5D text-legibility changes allocate no compositor resources");

    auto downscaled =
        create_split_texture(
            owner.device(),
            128,
            128,
            dark_gray,
            light_gray);
    test.expect(
        downscaled != nullptr,
        "P5D minified edge fixture created");
    if (!downscaled)
        return;

    baseline.text_legibility = 0.0f;
    test.expect(
        compositor.render(
            owner.immediate_context(),
            downscaled.Get(),
            {0, 0, 128, 128},
            {64, 64},
            nullptr,
            &baseline).ok(),
        "P5D minified baseline renders");

    std::uint32_t min_base_left = 0;
    std::uint32_t min_base_right = 0;
    (void)read_texture_pixel(
        owner.device(),
        owner.immediate_context(),
        compositor.output_texture(),
        31,
        32,
        min_base_left);
    (void)read_texture_pixel(
        owner.device(),
        owner.immediate_context(),
        compositor.output_texture(),
        32,
        32,
        min_base_right);

    enhanced.text_legibility = 0.80f;
    test.expect(
        compositor.render(
            owner.immediate_context(),
            downscaled.Get(),
            {0, 0, 128, 128},
            {64, 64},
            nullptr,
            &enhanced).ok(),
        "P5D minified enhanced edge renders");

    std::uint32_t min_enhanced_left = 0;
    std::uint32_t min_enhanced_right = 0;
    (void)read_texture_pixel(
        owner.device(),
        owner.immediate_context(),
        compositor.output_texture(),
        31,
        32,
        min_enhanced_left);
    (void)read_texture_pixel(
        owner.device(),
        owner.immediate_context(),
        compositor.output_texture(),
        32,
        32,
        min_enhanced_right);

    test.expect(
        channel(min_enhanced_right) -
            channel(min_enhanced_left) >
        channel(min_base_right) -
            channel(min_base_left),
        "P5D scale-aware legibility survives 2x source minification");

    /*
     * P5D.5 real-recording calibration: black/gray text on a bright neutral
     * browser background needs more stroke survival than the already-good
     * white-on-dark IDE path. Use one-pixel neutral stripe fixtures to prove
     * that the additional gain is directional rather than global.
     */
    // Model the anti-aliased edge pixel of a glyph rather than its opaque
    // black/white core. P5D intentionally rejects very large edges through
    // the anti-halo gate; the perceptual-thinning problem lives in these
    // intermediate stroke pixels after rasterization/downscale.
    constexpr std::uint32_t bright_ui = 0xFFE8E8E8u;
    constexpr std::uint32_t dark_text_edge = 0xFFB8B8B8u;
    constexpr std::uint32_t dark_ui = 0xFF303030u;
    constexpr std::uint32_t light_text_edge = 0xFF606060u;

    auto dark_on_bright =
        create_vertical_stripe_texture(
            owner.device(),
            64,
            64,
            bright_ui,
            dark_text_edge);
    auto light_on_dark =
        create_vertical_stripe_texture(
            owner.device(),
            64,
            64,
            dark_ui,
            light_text_edge);

    test.expect(
        dark_on_bright != nullptr &&
            light_on_dark != nullptr,
        "P5D.5 directional text fixtures created");
    if (!dark_on_bright || !light_on_dark)
        return;

    baseline.text_legibility = 0.0f;

    std::uint32_t dark_base = 0;
    std::uint32_t light_base = 0;

    test.expect(
        compositor.render(
            owner.immediate_context(),
            dark_on_bright.Get(),
            {0, 0, 64, 64},
            {64, 64},
            nullptr,
            &baseline).ok() &&
        read_texture_pixel(
            owner.device(),
            owner.immediate_context(),
            compositor.output_texture(),
            32,
            32,
            dark_base),
        "P5D.5 dark-on-bright baseline can be inspected");

    // The first 64x64 stripe render legitimately rebuilds the retained input
    // after the preceding 128x128 minification fixture. From this point on all
    // P5D.5 comparisons use identical geometry, so generation must stay fixed.
    const auto directional_generation =
        compositor.resource_generation();

    test.expect(
        compositor.render(
            owner.immediate_context(),
            light_on_dark.Get(),
            {0, 0, 64, 64},
            {64, 64},
            nullptr,
            &baseline).ok() &&
        read_texture_pixel(
            owner.device(),
            owner.immediate_context(),
            compositor.output_texture(),
            32,
            32,
            light_base),
        "P5D.5 light-on-dark baseline can be inspected");

    enhanced.text_legibility = 0.80f;

    std::uint32_t dark_enhanced = 0;
    std::uint32_t light_enhanced = 0;

    test.expect(
        compositor.render(
            owner.immediate_context(),
            dark_on_bright.Get(),
            {0, 0, 64, 64},
            {64, 64},
            nullptr,
            &enhanced).ok() &&
        read_texture_pixel(
            owner.device(),
            owner.immediate_context(),
            compositor.output_texture(),
            32,
            32,
            dark_enhanced),
        "P5D.5 dark-on-bright enhanced stroke can be inspected");

    test.expect(
        compositor.render(
            owner.immediate_context(),
            light_on_dark.Get(),
            {0, 0, 64, 64},
            {64, 64},
            nullptr,
            &enhanced).ok() &&
        read_texture_pixel(
            owner.device(),
            owner.immediate_context(),
            compositor.output_texture(),
            32,
            32,
            light_enhanced),
        "P5D.5 light-on-dark enhanced stroke can be inspected");

    const int dark_reinforcement =
        channel(dark_base) -
        channel(dark_enhanced);
    const int light_reinforcement =
        channel(light_enhanced) -
        channel(light_base);

    std::cout
        << "P5D.5 reinforcement dark-on-bright="
        << dark_reinforcement
        << " light-on-dark="
        << light_reinforcement
        << '\n';

    test.expect(
        dark_reinforcement > 0,
        "P5D.5 makes a dark neutral stroke more solid on bright UI");
    test.expect(
        light_reinforcement > 0,
        "P5D retains the validated light-on-dark text reinforcement");
    test.expect(
        dark_reinforcement >
            light_reinforcement,
        "P5D.5 adds directional strength only to dark-on-bright text");

    test.expect(
        compositor.resource_generation() == directional_generation,
        "P5D.5 directional calibration allocates no compositor resources");

    /*
     * P5D.6 real-recording calibration: shallow 1px neutral card borders and
     * separators must survive the grade/encoder/player path without turning
     * into dark outlines. Isolate UI-structure preservation from text
     * legibility and use realistic low-contrast neutral stripe fixtures.
     */
    constexpr std::uint32_t bright_panel = 0xFFF4F4F4u;
    constexpr std::uint32_t bright_border = 0xFFE4E4E4u;
    constexpr std::uint32_t dark_panel = 0xFF242424u;
    constexpr std::uint32_t dark_separator = 0xFF343434u;
    constexpr std::uint32_t strong_dark_edge = 0xFF909090u;

    auto bright_structure =
        create_vertical_stripe_texture(
            owner.device(),
            64,
            64,
            bright_panel,
            bright_border);
    auto dark_structure =
        create_vertical_stripe_texture(
            owner.device(),
            64,
            64,
            dark_panel,
            dark_separator);
    auto strong_structure =
        create_vertical_stripe_texture(
            owner.device(),
            64,
            64,
            bright_panel,
            strong_dark_edge);

    test.expect(
        bright_structure != nullptr &&
            dark_structure != nullptr &&
            strong_structure != nullptr,
        "P5D.6 low-contrast UI structure fixtures created");
    if (!bright_structure ||
        !dark_structure ||
        !strong_structure) {
        return;
    }

    auto structure_baseline = baseline;
    structure_baseline.text_legibility = 0.0f;
    structure_baseline.ui_structure = 0.0f;

    auto structure_grade = structure_baseline;
    structure_grade.ui_structure = 0.80f;

    std::uint32_t bright_base_center = 0;
    std::uint32_t bright_base_flat = 0;
    test.expect(
        compositor.render(
            owner.immediate_context(),
            bright_structure.Get(),
            {0, 0, 64, 64},
            {64, 64},
            nullptr,
            &structure_baseline).ok() &&
        read_texture_pixel(
            owner.device(),
            owner.immediate_context(),
            compositor.output_texture(),
            32,
            32,
            bright_base_center) &&
        read_texture_pixel(
            owner.device(),
            owner.immediate_context(),
            compositor.output_texture(),
            8,
            32,
            bright_base_flat),
        "P5D.6 bright-border baseline can be inspected");

    const auto structure_generation =
        compositor.resource_generation();

    std::uint32_t bright_preserved_center = 0;
    std::uint32_t bright_preserved_flat = 0;
    test.expect(
        compositor.render(
            owner.immediate_context(),
            bright_structure.Get(),
            {0, 0, 64, 64},
            {64, 64},
            nullptr,
            &structure_grade).ok() &&
        read_texture_pixel(
            owner.device(),
            owner.immediate_context(),
            compositor.output_texture(),
            32,
            32,
            bright_preserved_center) &&
        read_texture_pixel(
            owner.device(),
            owner.immediate_context(),
            compositor.output_texture(),
            8,
            32,
            bright_preserved_flat),
        "P5D.6 bright-border preserved output can be inspected");

    test.expect(
        channel(bright_preserved_center) <
            channel(bright_base_center),
        "P5D.6 keeps a shallow gray border darker than bright UI");
    test.expect(
        std::abs(
            channel(bright_preserved_flat) -
            channel(bright_base_flat)) <= 1,
        "P5D.6 leaves flat bright UI materially unchanged");

    std::uint32_t dark_base_center = 0;
    std::uint32_t dark_preserved_center = 0;
    test.expect(
        compositor.render(
            owner.immediate_context(),
            dark_structure.Get(),
            {0, 0, 64, 64},
            {64, 64},
            nullptr,
            &structure_baseline).ok() &&
        read_texture_pixel(
            owner.device(),
            owner.immediate_context(),
            compositor.output_texture(),
            32,
            32,
            dark_base_center) &&
        compositor.render(
            owner.immediate_context(),
            dark_structure.Get(),
            {0, 0, 64, 64},
            {64, 64},
            nullptr,
            &structure_grade).ok() &&
        read_texture_pixel(
            owner.device(),
            owner.immediate_context(),
            compositor.output_texture(),
            32,
            32,
            dark_preserved_center),
        "P5D.6 dark-separator outputs can be inspected");

    test.expect(
        channel(dark_preserved_center) >
            channel(dark_base_center),
        "P5D.6 keeps a shallow gray separator visible on dark UI");

    std::uint32_t strong_base_center = 0;
    std::uint32_t strong_preserved_center = 0;
    test.expect(
        compositor.render(
            owner.immediate_context(),
            strong_structure.Get(),
            {0, 0, 64, 64},
            {64, 64},
            nullptr,
            &structure_baseline).ok() &&
        read_texture_pixel(
            owner.device(),
            owner.immediate_context(),
            compositor.output_texture(),
            32,
            32,
            strong_base_center) &&
        compositor.render(
            owner.immediate_context(),
            strong_structure.Get(),
            {0, 0, 64, 64},
            {64, 64},
            nullptr,
            &structure_grade).ok() &&
        read_texture_pixel(
            owner.device(),
            owner.immediate_context(),
            compositor.output_texture(),
            32,
            32,
            strong_preserved_center),
        "P5D.6 strong-edge exclusion can be inspected");

    test.expect(
        std::abs(
            channel(strong_preserved_center) -
            channel(strong_base_center)) <= 1,
        "P5D.6 does not duplicate text/strong-edge sharpening");

    test.expect(
        compositor.resource_generation() == structure_generation,
        "P5D.6 UI-structure preservation allocates no compositor resources");
}

void test_arvisual_async_scene_analyzer(
    TestContext &test,
    arssyut::windows::D3D11Device &owner)
{
    constexpr std::uint32_t hot_vivid_bgra =
        0xFFFF1408u;

    auto source =
        create_solid_texture(
            owner.device(),
            64,
            36,
            hot_vivid_bgra);
    test.expect(
        source != nullptr,
        "P5B hot-vivid analysis source created");
    if (!source)
        return;

    auto compositor_result =
        arssyut::windows::D3D11Compositor::create(
            owner.device());
    test.expect(
        static_cast<bool>(compositor_result),
        "P5B compositor initializes with optional analyzer");
    if (!compositor_result)
        return;

    auto &compositor =
        *compositor_result.value();

    test.expect(
        compositor.scene_analysis_available(),
        "P5B retained scene-analysis resources are available");

    arssyut::visual::ArVisualGradeSettings static_grade;
    static_grade.enabled = true;
    static_grade.smart_auto = false;

    test.expect(
        compositor.render(
            owner.immediate_context(),
            source.Get(),
            {0, 0, 64, 36},
            {64, 36},
            nullptr,
            &static_grade).ok(),
        "P5B baseline static grade renders");

    std::uint32_t static_pixel = 0;
    test.expect(
        read_texture_pixel(
            owner.device(),
            owner.immediate_context(),
            compositor.output_texture(),
            32,
            18,
            static_pixel),
        "P5B baseline static pixel can be inspected");

    test.expect(
        compositor.update_source(
            owner.immediate_context(),
            source.Get()).ok(),
        "P5B retained source is ready for analysis");

    arssyut::visual::ArVisualGradeSettings smart_grade;
    smart_grade.enabled = true;
    smart_grade.smart_auto = true;

    const auto generation =
        compositor.resource_generation();

    auto now =
        arssyut::core::MonotonicClock::now();

    test.expect(
        compositor.submit_scene_analysis(
            owner.immediate_context(),
            now,
            &smart_grade).ok(),
        "P5B first asynchronous analysis submission succeeds");

    now.ticks_100ns +=
        arssyut::core::MonotonicClock::
            ticks_per_second / 5;

    test.expect(
        compositor.submit_scene_analysis(
            owner.immediate_context(),
            now,
            &smart_grade).ok(),
        "P5B second staging slot can be queued");

    now.ticks_100ns +=
        arssyut::core::MonotonicClock::
            ticks_per_second / 5;

    test.expect(
        compositor.submit_scene_analysis(
            owner.immediate_context(),
            now,
            &smart_grade).ok(),
        "P5B saturated staging queue returns without failure");

    test.expect(
        compositor.scene_analysis_submitted() == 2,
        "P5B analysis queue remains fixed at two pending slots");
    test.expect(
        compositor.scene_analysis_busy_skips() == 1,
        "P5B busy analysis queue skips instead of waiting or flushing");

    bool completed = false;
    for (int i = 0; i < 250; ++i) {
        if (!compositor.render_retained(
                owner.immediate_context(),
                {0, 0, 64, 36},
                {64, 36},
                nullptr,
                &smart_grade).ok()) {
            break;
        }

        if (compositor.scene_analysis_completed() > 0) {
            completed = true;
            break;
        }

        std::this_thread::sleep_for(
            std::chrono::milliseconds(1));
    }

    test.expect(
        completed,
        "P5B read-later staging completes after nonblocking GPU submission");
    test.expect(
        compositor.scene_analysis_map_failures() == 0,
        "P5B ready staging surfaces map without failure");

    test.expect(
        compositor.scene_analysis_primed(),
        "P5C calibration evidence reports a primed scene model");

    const auto calibration_stats =
        compositor.scene_analysis_stats();
    const auto calibration_adaptive =
        compositor.scene_analysis_adaptive();

    test.expect(
        calibration_stats.mean_saturation > 0.90f &&
            calibration_stats.hot_vivid_frac > 0.90f,
        "P5C calibration exports real hot-vivid scene statistics");
    test.expect(
        calibration_adaptive.highlight > 0.95f &&
            calibration_adaptive.pop <= 0.53f &&
            calibration_adaptive.chroma_limit <= 0.905f,
        "P5C calibration exports the applied adaptive safety state");

    test.expect(
        compositor.render_retained(
            owner.immediate_context(),
            {0, 0, 64, 36},
            {64, 36},
            nullptr,
            &smart_grade).ok(),
        "P5B latest adaptive state renders after read-later completion");

    std::uint32_t smart_pixel = 0;
    test.expect(
        read_texture_pixel(
            owner.device(),
            owner.immediate_context(),
            compositor.output_texture(),
            32,
            18,
            smart_pixel),
        "P5B adaptive pixel can be inspected");

    test.expect(
        smart_pixel != static_pixel,
        "P5B hot-vivid scene materially changes the P5A adaptive grade");

    test.expect(
        compositor.resource_generation() == generation,
        "P5B analysis cadence creates no compositor frame resources");
}



struct SpotlightPerfSample {
    std::uint32_t cpu_p95_us = 0;
    std::uint32_t gpu_p95_us = 0;
    std::uint64_t cpu_samples = 0;
    std::uint64_t gpu_samples = 0;
    std::uint64_t stable_generation = 0;
    bool renders_ok = false;
};

SpotlightPerfSample run_spotlight_perf_sample(
    arssyut::windows::D3D11Device &owner,
    arssyut::core::FrameSize output_size,
    bool enabled)
{
    SpotlightPerfSample sample;

    auto source =
        create_solid_texture(
            owner.device(),
            8,
            8,
            0xFF707070u);
    if (!source)
        return sample;

    auto compositor_result =
        arssyut::windows::D3D11Compositor::create(
            owner.device());
    if (!compositor_result)
        return sample;

    auto &compositor =
        *compositor_result.value();

    arssyut::presentation::PresentationFrameState state{};
    state.camera_center_x = 0.58f;
    state.camera_center_y = 0.46f;
    state.camera_zoom = 2.0f;
    state.spotlight.enabled = true;
    state.spotlight.runtime_requested = enabled;
    state.spotlight.focus_valid = true;
    state.spotlight.content_x = 0.62f;
    state.spotlight.content_y = 0.48f;
    state.spotlight.focus_mix = 1.0f;
    state.spotlight.dim_mix = enabled ? 1.0f : 0.0f;
    state.spotlight.area_scale_percent = 100.0f;
    state.spotlight.feather_short_edge_fraction = 0.12f;
    state.spotlight.dim_strength = 0.38f;

    if (!compositor.render(
            owner.immediate_context(),
            source.Get(),
            {0, 0, 8, 8},
            output_size,
            &state).ok()) {
        return sample;
    }

    sample.stable_generation =
        compositor.resource_generation();

    const int frames =
        output_size.width >= 3840
            ? 6
            : 12;

    sample.renders_ok = true;
    for (int frame = 0; frame < frames; ++frame) {
        if (!compositor.render(
                owner.immediate_context(),
                source.Get(),
                {0, 0, 8, 8},
                output_size,
                &state).ok() ||
            compositor.resource_generation() !=
                sample.stable_generation) {
            sample.renders_ok = false;
            break;
        }
    }

    owner.immediate_context()->Flush();

    // One more retained render gives pending timestamp queries an opportunity
    // to resolve without adding a synchronization/readback path to production.
    if (sample.renders_ok) {
        sample.renders_ok =
            compositor.render(
                owner.immediate_context(),
                source.Get(),
                {0, 0, 8, 8},
                output_size,
                &state).ok() &&
            compositor.resource_generation() ==
                sample.stable_generation;
    }

    const auto cpu =
        compositor.cpu_submit_latency();
    const auto gpu =
        compositor.gpu_execution_latency();

    sample.cpu_samples =
        cpu.total;
    sample.gpu_samples =
        gpu.total;
    sample.cpu_p95_us =
        cpu.quantile_upper_bound(
            95,
            100);
    sample.gpu_p95_us =
        gpu.quantile_upper_bound(
            95,
            100);

    return sample;
}

void test_spotlight_resolution_performance_contract(
    TestContext &test,
    arssyut::windows::D3D11Device &owner)
{
    const std::array<
        arssyut::core::FrameSize,
        2> outputs{
            arssyut::core::FrameSize{
                1920,
                1080},
            arssyut::core::FrameSize{
                3840,
                2160}
        };

    for (const auto output : outputs) {
        const auto off =
            run_spotlight_perf_sample(
                owner,
                output,
                false);
        const auto on =
            run_spotlight_perf_sample(
                owner,
                output,
                true);

        test.expect(
            off.renders_ok &&
                on.renders_ok,
            output.width >= 3840
                ? "P6UI.6D-I 4K Spotlight OFF/ON retained renders succeed"
                : "P6UI.6D-I 1080p Spotlight OFF/ON retained renders succeed");

        test.expect(
            off.cpu_samples > 0 &&
                on.cpu_samples > 0,
            output.width >= 3840
                ? "P6UI.6D-I 4K OFF/ON CPU latency diagnostics are populated"
                : "P6UI.6D-I 1080p OFF/ON CPU latency diagnostics are populated");

        test.expect(
            off.stable_generation ==
                on.stable_generation,
            output.width >= 3840
                ? "P6UI.6D-I 4K Spotlight toggle requires no extra retained GPU resource generation"
                : "P6UI.6D-I 1080p Spotlight toggle requires no extra retained GPU resource generation");

        std::cout
            << "P6UI.6D-I Spotlight WARP diagnostic "
            << output.width << "x" << output.height
            << " OFF cpu_p95_us=" << off.cpu_p95_us
            << " gpu_p95_us=" << off.gpu_p95_us
            << " gpu_samples=" << off.gpu_samples
            << " | ON cpu_p95_us=" << on.cpu_p95_us
            << " gpu_p95_us=" << on.gpu_p95_us
            << " gpu_samples=" << on.gpu_samples
            << '\n';
    }
}

void test_spotlight_compositor(
    TestContext &test,
    arssyut::windows::D3D11Device &owner)
{
    constexpr std::uint32_t solid_bgra =
        0xFF808080u;
    constexpr arssyut::core::FrameSize output_size{
        640,
        360
    };

    const auto channel =
        [](std::uint32_t bgra) {
            return static_cast<int>(
                bgra & 0xFFu);
        };

    auto source =
        create_solid_texture(
            owner.device(),
            4,
            4,
            solid_bgra);
    test.expect(
        source != nullptr,
        "P6UI.6D-D Spotlight source texture created");
    if (!source)
        return;

    auto compositor_result =
        arssyut::windows::D3D11Compositor::create(
            owner.device());
    test.expect(
        static_cast<bool>(compositor_result),
        "P6UI.6D-D Spotlight reuses the retained compositor");
    if (!compositor_result)
        return;

    auto &compositor =
        *compositor_result.value();

    arssyut::presentation::PresentationFrameState state{};
    state.spotlight.enabled = true;
    state.spotlight.focus_valid = true;
    state.spotlight.content_x = 0.5f;
    state.spotlight.content_y = 0.5f;
    state.spotlight.focus_mix = 1.0f;
    state.spotlight.dim_mix = 1.0f;
    state.spotlight.area_scale_percent = 50.0f;
    state.spotlight.feather_short_edge_fraction = 0.04f;
    state.spotlight.dim_strength = 0.50f;

    // Master/style state without runtime intent must remain exact pass-through.
    state.spotlight.runtime_requested = false;
    test.expect(
        compositor.render(
            owner.immediate_context(),
            source.Get(),
            {0, 0, 4, 4},
            output_size,
            &state).ok() &&
        verify_solid_texture(
            owner.device(),
            owner.immediate_context(),
            compositor.output_texture(),
            solid_bgra),
        "P6UI.6D-D disabled runtime is exact pixel pass-through");

    const auto stable_generation =
        compositor.resource_generation();

    // Runtime focus: source stays bright inside; outside dims analytically.
    state.spotlight.runtime_requested = true;

    std::uint32_t center_pixel = 0;
    std::uint32_t corner_pixel = 0;
    test.expect(
        compositor.render(
            owner.immediate_context(),
            source.Get(),
            {0, 0, 4, 4},
            output_size,
            &state).ok() &&
        read_texture_pixel(
            owner.device(),
            owner.immediate_context(),
            compositor.output_texture(),
            320,
            180,
            center_pixel) &&
        read_texture_pixel(
            owner.device(),
            owner.immediate_context(),
            compositor.output_texture(),
            8,
            8,
            corner_pixel),
        "P6UI.6D-D focused Spotlight output is inspectable");

    test.expect(
        std::abs(channel(center_pixel) - 128) <= 1,
        "P6UI.6D-D focus aperture preserves source brightness");
    test.expect(
        channel(corner_pixel) <= 72,
        "P6UI.6D-D outside scene is dimmed without blur/readback");

    // focus_mix=0 is the full-aperture endpoint. Even for an edge focus and
    // dim_mix=1 it must cover the complete output exactly.
    state.spotlight.content_x = 0.02f;
    state.spotlight.content_y = 0.02f;
    state.spotlight.focus_mix = 0.0f;

    test.expect(
        compositor.render(
            owner.immediate_context(),
            source.Get(),
            {0, 0, 4, 4},
            output_size,
            &state).ok() &&
        verify_solid_texture(
            owner.device(),
            owner.immediate_context(),
            compositor.output_texture(),
            solid_bgra),
        "P6UI.6D-D full aperture covers edge-focus frame exactly");

    // Every supported shape stays bounded in the same single shader path.
    state.spotlight.content_x = 0.5f;
    state.spotlight.content_y = 0.5f;
    state.spotlight.focus_mix = 1.0f;

    const std::array<
        arssyut::presentation::SpotlightShape,
        3> shapes{
            arssyut::presentation::SpotlightShape::Circle,
            arssyut::presentation::SpotlightShape::Ellipse,
            arssyut::presentation::SpotlightShape::RoundedRectangle
        };

    bool shapes_ok = true;
    for (const auto shape : shapes) {
        state.spotlight.shape = shape;

        std::uint32_t focus = 0;
        std::uint32_t outside = 0;
        if (!compositor.render(
                owner.immediate_context(),
                source.Get(),
                {0, 0, 4, 4},
                output_size,
                &state).ok() ||
            !read_texture_pixel(
                owner.device(),
                owner.immediate_context(),
                compositor.output_texture(),
                320,
                180,
                focus) ||
            !read_texture_pixel(
                owner.device(),
                owner.immediate_context(),
                compositor.output_texture(),
                8,
                8,
                outside) ||
            std::abs(channel(focus) - 128) > 1 ||
            channel(outside) > 72) {
            shapes_ok = false;
            break;
        }
    }
    test.expect(
        shapes_ok,
        "P6UI.6D-D Circle/Ellipse/RoundedRectangle share one analytic pass");

    // Reuse the exact project_content camera transform: content focus 0.80,
    // 0.45 under center 0.75,0.40 at 2x maps to output 0.60,0.60.
    state.spotlight.shape =
        arssyut::presentation::SpotlightShape::Circle;
    state.camera_center_x = 0.75f;
    state.camera_center_y = 0.40f;
    state.camera_zoom = 2.0f;
    state.spotlight.content_x = 0.80f;
    state.spotlight.content_y = 0.45f;

    std::uint32_t projected_focus = 0;
    std::uint32_t unprojected_location = 0;
    test.expect(
        compositor.render(
            owner.immediate_context(),
            source.Get(),
            {0, 0, 4, 4},
            output_size,
            &state).ok() &&
        read_texture_pixel(
            owner.device(),
            owner.immediate_context(),
            compositor.output_texture(),
            384,
            216,
            projected_focus) &&
        read_texture_pixel(
            owner.device(),
            owner.immediate_context(),
            compositor.output_texture(),
            512,
            162,
            unprojected_location),
        "P6UI.6D-D camera-projected Spotlight coordinates are inspectable");

    test.expect(
        std::abs(channel(projected_focus) - 128) <= 1 &&
        channel(unprojected_location) <= 72,
        "P6UI.6D-D Spotlight reuses camera content-to-output projection exactly once");

    // Presentation state changes are constant-buffer updates only.
    bool steady_ok = true;
    for (int i = 0; i < 256; ++i) {
        state.spotlight.content_x =
            0.20f +
            static_cast<float>(i % 41) / 100.0f;
        state.spotlight.content_y =
            0.25f +
            static_cast<float>(i % 31) / 100.0f;
        state.spotlight.focus_mix =
            static_cast<float>(i % 101) / 100.0f;
        state.spotlight.dim_mix =
            static_cast<float>((i * 7) % 101) / 100.0f;

        if (!compositor.render(
                owner.immediate_context(),
                source.Get(),
                {0, 0, 4, 4},
                output_size,
                &state).ok()) {
            steady_ok = false;
            break;
        }
    }

    test.expect(
        steady_ok,
        "P6UI.6D-D Spotlight survives high-churn state updates");
    test.expect(
        compositor.resource_generation() ==
            stable_generation,
        "P6UI.6D-D Spotlight allocates no steady-state GPU resources");
}

void test_keyboard_overlay_compositor(
    TestContext &test,
    arssyut::windows::D3D11Device &owner)
{
    constexpr std::uint32_t solid_bgra = 0xFF20242Au;

    auto source = create_solid_texture(
        owner.device(),
        4,
        4,
        solid_bgra);
    test.expect(
        source != nullptr,
        "Keyboard overlay source texture created");
    if (!source)
        return;

    auto compositor_result =
        arssyut::windows::D3D11Compositor::create(
            owner.device());
    test.expect(
        static_cast<bool>(compositor_result),
        "Keyboard overlay compositor initializes");
    if (!compositor_result)
        return;

    auto &compositor = *compositor_result.value();

    arssyut::presentation::PresentationFrameState state{};
    state.keyboard =
        arssyut::presentation::build_keyboard_overlay(
            {
                arssyut::presentation::ShortcutKey::C,
                arssyut::presentation::ShortcutCtrl
            },
            1);
    state.keyboard.opacity = 1.0f;

    test.expect(
        compositor.render(
            owner.immediate_context(),
            source.Get(),
            {0, 0, 4, 4},
            {640, 360},
            &state).ok(),
        "Physical keycap overlay renders at representative output size");

    test.expect(
        texture_contains_non_solid_pixel(
            owner.device(),
            owner.immediate_context(),
            compositor.output_texture(),
            solid_bgra),
        "Structured keycap overlay changes visible output pixels");

    const auto generation =
        compositor.resource_generation();

    state.keyboard =
        arssyut::presentation::build_keyboard_overlay(
            {
                arssyut::presentation::ShortcutKey::S,
                static_cast<std::uint8_t>(
                    arssyut::presentation::ShortcutCtrl |
                    arssyut::presentation::ShortcutShift |
                    arssyut::presentation::ShortcutWin)
            },
            2);
    state.keyboard.opacity = 1.0f;

    test.expect(
        compositor.render(
            owner.immediate_context(),
            source.Get(),
            {0, 0, 4, 4},
            {640, 360},
            &state).ok(),
        "New keycap generation updates retained overlay texture");

    test.expect(
        compositor.resource_generation() == generation,
        "Keycap generation change creates no new compositor resources");
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

    bool steady_ok = true;
    for (int i = 0; i < 256; ++i) {
        if (!compositor.render(
                owner.immediate_context(),
                source.Get(),
                {0, 0, 4, 4},
                {8, 6}).ok()) {
            steady_ok = false;
            break;
        }
    }

    test.expect(
        steady_ok,
        "Compositor survives repeated steady-state renders");
    test.expect(
        compositor.resource_generation() == generation,
        "Steady-state render loop creates no new frame resources");

    arssyut::presentation::PresentationFrameState keyboard_state{};
    keyboard_state.keyboard =
        arssyut::presentation::build_keyboard_overlay(
            {
                arssyut::presentation::ShortcutKey::C,
                arssyut::presentation::ShortcutCtrl
            },
            1);
    keyboard_state.keyboard.opacity = 1.0f;

    const auto keyboard_generation =
        compositor.resource_generation();

    test.expect(
        compositor.render(
            owner.immediate_context(),
            source.Get(),
            {0, 0, 4, 4},
            {8, 6},
            &keyboard_state).ok(),
        "Structured keycap overlay render succeeds");

    keyboard_state.keyboard =
        arssyut::presentation::build_keyboard_overlay(
            {
                arssyut::presentation::ShortcutKey::V,
                static_cast<std::uint8_t>(
                    arssyut::presentation::ShortcutCtrl |
                    arssyut::presentation::ShortcutShift)
            },
            2);
    keyboard_state.keyboard.opacity = 1.0f;

    test.expect(
        compositor.render(
            owner.immediate_context(),
            source.Get(),
            {0, 0, 4, 4},
            {8, 6},
            &keyboard_state).ok(),
        "Keycap generation update reuses retained GPU resources");

    test.expect(
        compositor.resource_generation() == keyboard_generation,
        "Shortcut changes allocate no new compositor frame resources");

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

    test_dxgi_media_buffer_length(test, device);
    test_presentation_controller(test);
    test_latest_frame_slot(test);
    test_recoverable_session(test);
    test_empty_video_pipeline(test, device);
    test_media_foundation_mp4(test);
    test_retained_source_camera_cadence(test, device);
    test_single_ring_click_compositor(test, device);
    test_arvisual_grade_compositor(test, device);
    test_arvisual_product_modes(test, device);
    test_screen_native_surface_anchor(test, device);
    test_screen_text_legibility(test, device);
    test_arvisual_async_scene_analyzer(test, device);
    test_spotlight_compositor(test, device);
    test_spotlight_resolution_performance_contract(
        test,
        device);
    test_keyboard_overlay_compositor(test, device);
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
