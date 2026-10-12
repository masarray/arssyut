"""F2B source-ownership regression: API wiring, not a fake USB webcam PASS.

Real hardware acceptance remains required. The native Windows contract checks
OFF state; this QA protects the cross-module one-source handoff and A/V path.
"""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]

def load(p):
    return (ROOT / p).read_text(encoding="utf-8")

BRIDGE = load("src/bridge/native_bridge.cpp")
RECORDER = load("src/app/recorder_session.cpp")
VIDEO = load("src/platform/windows/video/native_video_pipeline.cpp")
SOURCE = load("src/platform/windows/capture/webcam_preview_session.cpp")
UI = load("src/ui/Arssyut.UI/MainWindow.axaml.cs")

class WebcamF2BSingleSource(unittest.TestCase):
    def test_bridge_does_not_close_camera_at_record_start(self):
        start = BRIDGE.split("arssyut_bridge_recorder_start(", 1)[1]
        before_config = start.split("RecorderConfig config;", 1)[0]
        self.assertNotIn("stop_webcam_preview(*context)", before_config)
        self.assertIn("camera->begin_recording()", start)
        self.assertIn("config.shared_webcam = camera", start)
        self.assertIn("camera->end_recording()", start)

    def test_recorder_injects_exact_device_and_returns_handoff(self):
        self.assertIn("config_.shared_webcam->recording_device()", RECORDER)
        self.assertIn("config_.shared_webcam->frames()", RECORDER)
        self.assertIn("config_.shared_webcam.get()", RECORDER)
        self.assertIn("camera->end_recording()", RECORDER)
        self.assertIn("if (!config_.shared_webcam) webcam_source.stop();", RECORDER)

    def test_no_two_consumers_or_second_mp4(self):
        self.assertIn("recording_acknowledged_ = true", SOURCE)
        self.assertIn("handoff_cv_.wait(lock", SOURCE)
        self.assertIn("std::condition_variable", load(
            "src/platform/windows/capture/webcam_preview_session.hpp"))
        self.assertIn("webcam_preview_sink_->publish_from_recorder(", VIDEO)
        self.assertIn("preview_while_paused(", VIDEO)
        self.assertIn("pipeline->preview_while_paused(", RECORDER)
        self.assertIn("pipeline->process_due(", RECORDER)

    def test_post_stop_gpu_readback_submitted_and_cadence_reset(self):
        assert "ctx->Flush();" in SOURCE
        assert "D3D11_ASYNC_GETDATA_DONOTFLUSH" in SOURCE
        assert "D3D11_MAP_FLAG_DO_NOT_WAIT" in SOURCE
        resumed = SOURCE.split("recording_acknowledged_ = false;", 2)[-1]
        assert "next_preview_ticks_ = 0;" in resumed
        assert "state_.store(1, std::memory_order_release);" in resumed

    def test_camera_profile_reaches_encoded_gpu_output(self):
        settings = load("src/ui/Arssyut.UI/Preview/SettingsPreviewState.cs")
        store = load("src/ui/Arssyut.UI/Preview/ProductSettingsStore.cs")
        managed = load("src/ui/Arssyut.UI/Interop/NativeBridgeClient.cs")
        request = load("src/ui/Arssyut.UI/MainWindow.axaml.cs")
        native = load("src/bridge/native_bridge.cpp")
        compos = load("src/platform/windows/graphics/d3d11_compositor.cpp")
        self.assertIn("TrySetCameraOverlay(", settings)
        self.assertIn("CurrentSchemaVersion = 7;", store)
        self.assertIn("RecorderSetCameraOverlay(", managed)
        self.assertIn("new NativeCameraOverlay(", request)
        self.assertIn("config.camera_overlay = context->camera_overlay;", native)
        self.assertIn("set_camera_overlay_profile(config_.camera_overlay)", RECORDER)
        self.assertIn("camera_overlay_.placement", compos)
        self.assertIn("const int mask", compos)
        self.assertIn("const bool rounded", compos)
        self.assertIn("webcam_border.rgb", compos)
        self.assertIn("webcam_style.x", compos)
        self.assertIn("webcam_params.w", compos)

    def test_gpu_readback_never_waits_on_recorder(self):
        self.assertIn("D3D11_ASYNC_GETDATA_DONOTFLUSH", SOURCE)
        self.assertIn("D3D11_MAP_FLAG_DO_NOT_WAIT", SOURCE)
        self.assertNotIn("ctx->Map(staging.Get(), 0, D3D11_MAP_READ, 0", SOURCE)
        self.assertIn("1'000'000", SOURCE)
        self.assertIn("WebcamGpuFrameSlot frames_", load(
            "src/platform/windows/capture/webcam_preview_session.hpp"))

    def test_gui_has_recording_frame_state_and_repaints_bitmap(self):
        self.assertIn("(state != 2 && state != 4)", UI)
        self.assertIn("CameraPreviewImage.InvalidateVisual()", UI)
        start = UI.split("NativeBridgeStatus status;", 1)[1].split(
            "if (status !=", 1)[0]
        self.assertNotIn("bridge.StopWebcamPreview()", start)
        self.assertIn("bridge.StartRecording(", start)
        self.assertIn("TimeSpan.FromMilliseconds(1500)", UI)

    def test_audio_only_settings_untouched(self):
        self.assertIn("audio_runtime->service(media_now, writer)", RECORDER)
        self.assertIn("audio_runtime->finish(recording_stopped, writer,", RECORDER)
        self.assertIn("request_toggle_microphone_mute()", BRIDGE)
        self.assertIn("request_toggle_pause()", BRIDGE)

if __name__ == "__main__":
    unittest.main()
