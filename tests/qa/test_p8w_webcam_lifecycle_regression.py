"""Field regression guard: initial webcam toggle and frame freshness.

The contract asserts *ownership/state* wiring without faking a camera on
GitHub's device-free Windows runners. Actual live-preview motion still needs
a physical GPU webcam and one-source F2B compositor handoff.
"""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
UI = (ROOT / "src/ui/Arssyut.UI/MainWindow.axaml.cs").read_text(
    encoding="utf-8")
BRIDGE = (ROOT / "src/bridge/native_bridge.cpp").read_text(encoding="utf-8")
NATIVE = (ROOT / "src/platform/windows/capture/webcam_preview_session.cpp").read_text(
    encoding="utf-8")

class WebcamLifecycleRegression(unittest.TestCase):
    def test_source_intent_survives_worker_failure(self):
        assert "private void ReportWebcamPreviewProblem" in UI
        reporter = UI.split("private void ReportWebcamPreviewProblem", 1)[1].split(
            "private void DisableWebcamPreview", 1)[0]
        assert "_session.CameraEnabled = false" not in reporter
        assert "CameraToggle.IsChecked = false" not in reporter
        assert "ReadWebcamPreviewFailure(" in UI
        assert "arssyut_bridge_webcam_preview_failure(" in BRIDGE

    def test_initial_retry_is_one_bounded_serialized_recovery(self):
        assert "if (!_webcamStartupRecoveryUsed &&" in UI
        assert "_webcamStartupRecoveryUsed = true;" in UI
        assert "await ApplyWebcamPreviewIntentAsync(version, token, true)" in UI
        assert "_webcamLifecycle.WaitAsync()" in UI
        assert "_webcamIntentVersion" in UI
        assert "StopWebcamPreview()" in UI

    def test_stale_image_hidden_without_resetting_sequence(self):
        assert "TimeSpan.FromMilliseconds(1500)" in UI
        assert "CameraPreviewImage.IsVisible = false;" in UI
        assert "_webcamPreviewSequence = sequence;" in UI
        assert "Stopwatch.GetTimestamp()" in UI

    def test_camera_owner_not_opened_during_active_recording(self):
        assert "RestoreWebcamAfterRecordingAsync()" in UI
        assert "NativeRecorderState.Ready or" in UI
        assert "NativeRecorderState.Failed or NativeRecorderState.Idle" in UI
        assert "NativeBridgeStatus.Busy" in UI
        assert "webcam_preview_mutex" in BRIDGE

    def test_native_worker_reports_failure_origin(self):
        for stage in range(1, 6):
            assert f"failure_stage_.store({stage}," in NATIVE
        assert "camera_status.last_hresult" in NATIVE

if __name__ == "__main__":
    unittest.main()
