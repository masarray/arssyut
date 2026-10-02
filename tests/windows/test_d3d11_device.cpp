#include "platform/windows/capture/latest_frame_slot.hpp"
#include "platform/windows/graphics/d3d11_compositor.hpp"
#include "platform/windows/graphics/d3d11_device.hpp"
#include "platform/windows/media/mf_h264_mp4_writer.hpp"
#include "platform/windows/storage/recoverable_session.hpp"
#include "platform/windows/video/native_video_pipeline.hpp"
#include "presentation/presentation_controller.hpp"
#include "presentation/shortcut_visualizer.hpp"

#include <d3d11.h>
#include <mfapi.h>
#include <mfobjects.h>
#include <wrl/client.h>

#include <algorithm>
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

    constexpr std::uint32_t width = 64;
    constexpr std::uint32_t height = 64;
    constexpr std::uint32_t bgra = 0xFF3050A0u;

    std::vector<std::uint32_t> pixels(
        static_cast<std::size_t>(width) *
            static_cast<std::size_t>(height),
        bgra);

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
    config.bitrate_bps = 500'000;
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
    constexpr std::uint32_t solid_bgra = 0xFF20242Au;

    auto source = create_solid_texture(
        owner.device(),
        4,
        4,
        solid_bgra);
    test.expect(
        source != nullptr,
        "Single-ring click source texture created");
    if (!source)
        return;

    auto compositor_result =
        arssyut::windows::D3D11Compositor::create(
            owner.device());
    test.expect(
        static_cast<bool>(compositor_result),
        "Single-ring click compositor initializes");
    if (!compositor_result)
        return;

    auto &compositor = *compositor_result.value();

    arssyut::presentation::PresentationFrameState state{};
    state.clicks[0].content_x = 0.5f;
    state.clicks[0].content_y = 0.5f;
    state.clicks[0].age_seconds = 0.32f;
    state.clicks[0].lifetime_seconds = 0.64f;
    state.clicks[0].kind =
        arssyut::presentation::ClickKind::Left;

    test.expect(
        compositor.render(
            owner.immediate_context(),
            source.Get(),
            {0, 0, 4, 4},
            {640, 360},
            &state).ok(),
        "Single-ring click renders at representative output size");

    std::uint32_t center_pixel = 0;
    std::uint32_t ring_pixel = 0;

    test.expect(
        read_texture_pixel(
            owner.device(),
            owner.immediate_context(),
            compositor.output_texture(),
            320,
            180,
            center_pixel),
        "Click center pixel can be inspected");

    test.expect(
        read_texture_pixel(
            owner.device(),
            owner.immediate_context(),
            compositor.output_texture(),
            361,
            180,
            ring_pixel),
        "Click ring pixel can be inspected");

    test.expect(
        center_pixel == solid_bgra,
        "Single-ring click keeps center unfilled with no center dot");

    test.expect(
        ring_pixel != solid_bgra,
        "Single-ring click produces a visible grown ring");
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
    test_arvisual_async_scene_analyzer(test, device);
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
