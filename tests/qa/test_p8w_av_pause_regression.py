"""P8W A/V product regression wiring, not a replacement for real-media QA.

Bridges the user-reported regression: F1 green but native Pause button disabled
and Mic Mute source state unobservable. Deterministic device-free native CTests
exercise media timelines; these checks ensure that their paths are actually
reachable from the shipped Avalonia single-file executable.
"""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


class P8WPauseAvIntegration(unittest.TestCase):
    def test_one_clock_owns_recorder_and_audio(self):
        recorder = read("src/app/recorder_session.cpp")
        source = read("src/app/audio_product_runtime.hpp")
        self.assertIn('RecordingPauseClock pause_clock', recorder)
        self.assertIn('pipeline->process_due(', recorder)
        self.assertIn('audio_runtime->service(media_now, writer)', recorder)
        self.assertIn('audio_runtime->finish(recording_stopped, writer,', recorder)
        self.assertIn('audio_stop_block_safe(', source)
        self.assertIn('microphone_.quiesce()', source)
        self.assertIn('stopped_during_pause', source)
        self.assertIn('post_resume_qpc_floor_', read(
            "src/core/audio/audio_packet_ingress.hpp"))

    def test_native_pause_and_mute_are_reachable_in_the_gui(self):
        controller = read("src/ui/Arssyut.UI/RecordingControllerWindow.axaml.cs")
        ui = read("src/ui/Arssyut.UI/MainWindow.axaml.cs")
        client = read("src/ui/Arssyut.UI/Interop/NativeBridgeClient.cs")
        bridge = read("src/bridge/native_bridge.cpp")
        self.assertIn('PauseButton.IsEnabled = true;', controller)
        self.assertIn('_nativeBridge.TogglePause()', controller)
        self.assertIn('_nativeBridge.ToggleMicrophoneMute()', controller)
        self.assertIn('HandleGlobalPauseHotkey()', ui)
        self.assertIn('RecorderTogglePause(_handle)', client)
        self.assertIn('arssyut_bridge_recorder_toggle_pause(', bridge)
        self.assertIn('request_toggle_microphone_mute()', bridge)
        self.assertIn('NativeRecorderState.Paused', controller)

    def test_idle_rejection_and_paused_mute_apply_on_resume(self):
        source = read("src/app/recorder_session.cpp")
        controller = read("src/ui/Arssyut.UI/RecordingControllerWindow.axaml.cs")
        self.assertRegex(source, r'request_toggle_pause\(\) noexcept')
        self.assertIn('state != RecorderState::Paused', source)
        self.assertIn('microphone_mute_requested_.load(', source)
        self.assertIn('audio_runtime->set_microphone_muted(', source)
        self.assertRegex(controller, r'NativeRecorderState\.Recording or\s*NativeRecorderState\.Paused')
        self.assertIn('arssyut_bridge_recorder_toggle_pause(', read(
            "tests/windows/test_native_bridge.cpp"))
        self.assertIn('arssyut_recording_pause_clock_tests', read("CMakeLists.txt"))


if __name__ == "__main__":
    unittest.main()
