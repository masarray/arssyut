"""Preserve working native Audio UI and direct EXE launch, besides Avalonia smoke."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]
UI = ROOT / "src/ui/Arssyut.UI"


class UXContract(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.xaml = (UI / "MainWindow.axaml").read_text(encoding="utf-8-sig")
        cls.code = (UI / "MainWindow.axaml.cs").read_text(encoding="utf-8-sig")
        cls.program = (UI / "Program.cs").read_text(encoding="utf-8-sig")
        cls.project = (UI / "Arssyut.UI.csproj").read_text(encoding="utf-8-sig")
        cls.workflow = (ROOT / ".github/workflows/ci.yml").read_text(encoding="utf-8-sig")

    def test_capture_workflow_and_saved_actions_preserved(self):
        for name in ("RecordButton", "SavedActions", "SystemAudioToggle",
                     "MicToggle", "MicrophoneDeviceComboMain", "CameraToggle"):
            with self.subTest(name=name):
                self.assertIn(f'x:Name="{name}"', self.xaml)
        for mode in ("Capture Display", "Capture Window", "Capture Region"):
            self.assertIn(f'AutomationProperties.Name="{mode}"', self.xaml)
        for handler in ("Record_OnClick", "OpenSaved_OnClick"):
            self.assertIn(f'Click="{handler}"', self.xaml)

    def test_audio_toggle_handlers_and_native_flags_preserved(self):
        for control, handler in (("SystemAudioToggle", "SystemAudioToggle_OnClick"),
                                 ("MicToggle", "MicToggle_OnClick")):
            self.assertRegex(self.xaml,
                rf'<ToggleSwitch\s+x:Name="{control}"[^>]*Click="{handler}"')
            self.assertIn(f"void {handler}(", self.code)
        self.assertRegex(self.code,
            r'if \(_systemAudioEnabled\)\s*flags \|=\s*NativeStartFlags\.SystemAudio;')
        self.assertRegex(self.code,
            r'if \(_session\.MicrophoneEnabled\)\s*flags \|=\s*NativeStartFlags\.Microphone;')
        self.assertIn("_microphoneDeviceToken,", self.code)
        self.assertIn("Windows default playback", self.xaml)

    def test_audio_tooltips_match_actual_build_capabilities(self):
        # Internal hardware-test UI must not claim that its *working* recorder
        # backend is pending. Public builds still stay explicitly disabled.
        self.assertNotIn("System audio backend is pending", self.xaml)
        self.assertNotIn("Microphone recording backend is pending", self.xaml)
        self.assertNotIn("selection activates with the microphone backend", self.xaml)
        self.assertIn("ToolTip.SetTip(SystemAudioToggle,", self.code)
        self.assertIn("ToolTip.SetTip(MicToggle,", self.code)
        self.assertIn("ToolTip.SetTip(MicrophoneDeviceComboMain,", self.code)
        self.assertIn("if (_audioHardwarePreview)", self.code)
        self.assertIn("Record sound from the Windows default playback device.", self.code)
        self.assertIn("Record audio from the selected microphone.", self.code)
        self.assertIn("Audio controls are a visual preview; no audio is recorded.", self.code)
        self.assertIn("Microphone recording is unavailable in this build.", self.xaml)
        self.assertIn("System audio recording is unavailable in this build.", self.xaml)

    def test_stereo_input_meter_and_quiet_primary_ui(self):
        for meter in ("SystemLevelL", "SystemLevelR",
                      "MicrophoneLevelL", "MicrophoneLevelR"):
            self.assertIn(f'x:Name="{meter}"', self.xaml)
            self.assertIn(meter + ".Value =", self.code)
        self.assertIn("NativeMethods.AudioMeter(", (
            UI / "Interop/NativeBridgeClient.cs").read_text(encoding="utf-8-sig"))
        self.assertIn("arssyut_bridge_audio_meter", (
            ROOT / "src/bridge/native_bridge.cpp").read_text(encoding="utf-8-sig"))
        for noise in ("Screen · game · camera recorder", "Desktop playback",
                      "Narration input", "PiP backend pending",
                      "native engine ready ·"):
            self.assertNotIn(noise, self.xaml)
        self.assertIn("StatusDetail.IsVisible = false;", self.code)

    def test_mic_meter_reads_wasapi_packets_and_stops_before_recorder(self):
        bridge = (ROOT / "src/bridge/native_bridge.cpp").read_text(encoding="utf-8-sig")
        self.assertIn("preview_mic.try_pop(lease)", bridge)
        self.assertIn("accumulate_input_peak(", bridge)
        self.assertIn("preview_mic.stop();", bridge)
        self.assertIn("opts.queue_capacity = 32;", bridge)
        self.assertNotIn("meter->microphone_channels,\n                meter->microphone_left", bridge)

    def test_preview_is_internal_only_and_launches_directly(self):
        self.assertIn("<OutputType>WinExe</OutputType>", self.project)
        self.assertIn("'$(InternalAudioPreview)' == 'true'", self.project)
        self.assertIn("#if ARSSYUT_INTERNAL_AUDIO_PREVIEW", self.program)
        self.assertIn('"ARSSYUT_AUDIO_PREVIEW", "1"', self.program)
        self.assertIn("if (!_allowInteractionPreview && !_audioHardwarePreview)", self.code)
        self.assertIn("p:InternalAudioPreview=true", self.workflow)
        self.assertIn('Join-Path $package "Arssyut.UI.exe"', self.workflow)
        self.assertIn("name: arssyut-INTERNAL-audio-acceptance-win-x64", self.workflow)
        self.assertNotIn("START_AUDIO_PREVIEW.cmd", self.workflow)


if __name__ == "__main__":
    unittest.main()
