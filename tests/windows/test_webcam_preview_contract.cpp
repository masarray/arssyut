#include "platform/windows/capture/webcam_preview_session.hpp"
#include <array>
#include <iostream>

int main()
{
    using arssyut::windows::WebcamPreviewSession;
    static_assert(WebcamPreviewSession::width == 192);
    static_assert(WebcamPreviewSession::height == 108);
    static_assert(WebcamPreviewSession::bytes == 192 * 108 * 4);

    WebcamPreviewSession preview;
    std::array<std::uint8_t, WebcamPreviewSession::bytes> image{};
    std::uint64_t seq = 42;
    std::uint32_t state = 42;
    if (!preview.copy_frame(image.data(),
                            static_cast<std::uint32_t>(image.size()),
                            seq, state) || seq != 0 || state != 0) {
        std::cerr << "OFF preview generated a phantom image\n";
        return 1;
    }
    if (preview.start(L"")) {
        std::cerr << "Empty device symbol link opened\n";
        return 1;
    }
    if (preview.failure_stage() != 0 || preview.failure_detail() != 0) {
        std::cerr << "OFF/invalid preview must have no phantom GPU failure\n";
        return 1;
    }
    if (!WebcamPreviewSession::validate_gpu_shader()) {
        std::cerr << "Production thumbnail GPU shader failed compile\n";
        return 1;
    }
    // Device-free OFF must never manufacture an owner, source or GPU frame.
    if (preview.begin_recording() || preview.recording_device() != nullptr ||
        preview.capture_snapshot().received != 0) {
        std::cerr << "OFF camera acquired phantom recorder GPU ownership\n";
        return 1;
    }
    preview.end_recording();
    preview.stop();
    if (preview.copy_frame(nullptr, 0, seq, state)) {
        std::cerr << "Tiny frame output accepted invalid pointer\n";
        return 1;
    }
    std::cout << "P8W2 native GPU webcam preview contract PASS\n";
    return 0;
}
