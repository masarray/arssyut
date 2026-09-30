#include "platform/windows/capture/latest_frame_slot.hpp"
#include "platform/windows/graphics/d3d11_compositor.hpp"
#include "platform/windows/graphics/d3d11_device.hpp"
#include "platform/windows/media/mf_h264_mp4_writer.hpp"
#include "platform/windows/storage/recoverable_session.hpp"
#include "platform/windows/video/native_video_pipeline.hpp"
#include "presentation/presentation_controller.hpp"

#include <d3d11.h>
#include <mfapi.h>
#include <mfobjects.h>
#include <wrl/client.h>

#include <chrono>
#include <cstdint>
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

    PresentationFrameState frame{};
    for (int i = 0; i < 90; ++i) {
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
        "Click pulse lifetime remains bounded");

    ShortcutChord chord;
    chord.key = static_cast<std::uint16_t>('C');
    chord.modifiers = ShortcutCtrl;
    controller.on_shortcut(chord, now);

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

    const std::wstring keyboard(
        frame.keyboard.text.data());
    test.expect(
        keyboard.find(L"Ctrl") != std::wstring::npos &&
            keyboard.find(L"C") != std::wstring::npos,
        "Shortcut keycap text preserves canonical modifier order");

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
