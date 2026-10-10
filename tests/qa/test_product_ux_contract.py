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

    def test_official_favicon_branding_and_release_gates(self):
        import json
        from xml.etree import ElementTree
        favicon = ROOT / "assets/favicon"
        for name in ("favicon.ico", "favicon-16x16.png", "favicon-32x32.png",
                     "apple-touch-icon.png", "android-chrome-192x192.png",
                     "android-chrome-512x512.png", "site.webmanifest"):
            self.assertTrue((favicon / name).is_file(), name)
        manifest = json.loads((favicon / "site.webmanifest").read_text(encoding="utf-8"))
        self.assertEqual(manifest["name"], "Arssyut")
        self.assertTrue(all(not icon["src"].startswith("/")
                            for icon in manifest["icons"]))
        self.assertIn("<ApplicationIcon>", self.project)
        self.assertIn("assets\\favicon\\favicon.ico", self.project)
        self.assertIn('Link="Assets\\ArssyutLogo.png"', self.project)
        self.assertIn('Icon="avares://Arssyut.UI/Assets/favicon.ico"', self.xaml)
        self.assertIn('Source="avares://Arssyut.UI/Assets/ArssyutLogo.png"',
                      self.xaml)
        self.assertIn('Icon="avares://Arssyut.UI/Assets/favicon.ico"',
                      (UI / "SettingsWindow.axaml").read_text(encoding="utf-8"))
        readme = (ROOT / "README.md").read_text(encoding="utf-8")
        self.assertIn('src="assets/favicon/android-chrome-192x192.png"', readme)
        landing = (ROOT / "index.html").read_text(encoding="utf-8")
        for url in ("assets/favicon/favicon.ico",
                    "assets/favicon/favicon-32x32.png",
                    "assets/favicon/apple-touch-icon.png",
                    "assets/favicon/site.webmanifest",
                    "assets/favicon/android-chrome-512x512.png"):
            self.assertIn(url, landing)
        self.assertIn("Arssyut.UI.exe", landing)
        installer = (ROOT / "packaging/windows/Arssyut.iss").read_text(encoding="utf-8")
        self.assertIn("SetupIconFile=", installer)
        self.assertIn("assets\\favicon\\favicon.ico", installer)
        self.assertIn('#define AppExe "Arssyut.UI.exe"', installer)
        self.assertIn('Filename: "{app}\\{#AppExe}"', installer)
        candidate = (ROOT / ".github/workflows/windows-installer-candidate.yml").read_text(encoding="utf-8")
        self.assertIn("workflow_dispatch:", candidate)
        self.assertNotIn("release: published", candidate)
        self.assertIn("ExtractAssociatedIcon", self.workflow)
        ElementTree.fromstring(self.project)

    def test_audio_artifact_survives_merge_and_video_only_is_explicit(self):
        import re
        self.assertIn("github.ref == 'refs/heads/main'", self.workflow)
        self.assertIn("contains(github.event.pull_request.title, '[audio]')", self.workflow)
        self.assertNotIn("github.head_ref == 'feat/p7-product-audio-integration-v1'", self.workflow)
        for job_step in ("Publish INTERNAL FFmpeg-enabled Avalonia recorder",
                         "Internal preview launch smoke",
                         "Upload INTERNAL audio hardware acceptance package"):
            self.assertIn(job_step, self.workflow)
        self.assertIn("-DARSSYUT_ENABLE_PRODUCT_AUDIO=ON", self.workflow)
        self.assertIn("-p:InternalAudioPreview=true", self.workflow)
        self.assertIn("name: arssyut-AUDIO-ENABLED-TEST-win-x64", self.workflow)
        self.assertIn("name: arssyut-VIDEO-ONLY-avalonia-win-x64", self.workflow)
        self.assertIn("README_VIDEO_ONLY.txt", self.workflow)
        self.assertIn('Title="Arssyut — Enhanced Screen Recorder"', self.xaml)
        self.assertIn('Text="Enhanced Screen Recorder"', self.xaml)
        self.assertIn("Video-only build.", self.code)

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
            self.assertIn(meter + ".Height =", self.code)
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


    def test_compact_capture_original_svg_geometry_and_vertical_meters(self):
        import re
        from xml.etree import ElementTree
        ElementTree.fromstring(self.xaml)
        for mode in ("Display", "Window", "Region", "Game"):
            block = re.search(
                rf'<ToggleButton x:Name="Mode{mode}"[\s\S]*?</ToggleButton>',
                self.xaml)
            self.assertIsNotNone(block, mode)
            self.assertIn('Orientation="Vertical"', block.group())
            self.assertIn(f'Text="{mode}"', block.group())
            self.assertIn(f'Data="{{StaticResource Ms{mode}}}"', block.group())
            self.assertIn('Width="24" Height="24"', block.group())
        # The icon system uses locally bundled official 24px Google
        # Material Symbols, instead of SVG-Repo tiles plus Lucide actions.
        sources = ("MaterialSymbolsCapture.axaml", "MaterialSymbolsActions.axaml")
        registry = "".join((UI / "Design" / file).read_text(encoding="utf-8-sig")
                           for file in sources)
        for file in sources:
            ElementTree.fromstring((UI / "Design" / file).read_text(encoding="utf-8-sig"))
            self.assertIn(f'Source="/Design/{file}"', (
                UI / "App.axaml").read_text(encoding="utf-8-sig"))
        for key in ("MsDisplay", "MsWindow", "MsRegion", "MsGame",
                    "MsSettings", "MsPlay", "MsFolder",
                    "MsCamera", "MsVideo", "MsMic", "MsAudio",
                    "MsSpeaker", "MsMouse", "MsKeyboard", "MsAdvanced"):
            self.assertIn(f'x:Key="{key}"', registry)
            if key not in ("MsMouse", "MsKeyboard", "MsAdvanced"):
                self.assertIn(f'StaticResource {key}', self.xaml if key != "MsSpeaker" else
                              (UI / "MainWindow.axaml").read_text(encoding="utf-8-sig"))
        self.assertIn("Apache License", (
            ROOT / "third_party/material_symbols/LICENSE").read_text(encoding="utf-8-sig"))
        self.assertNotIn("M488.188,41.797", self.xaml)
        for name in ("SystemLevelL", "SystemLevelR",
                     "MicrophoneLevelL", "MicrophoneLevelR"):
            meter = re.search(
                rf'<Border x:Name="{name}"[^>]*>', self.xaml)
            self.assertIsNotNone(meter, name)
            self.assertIn('Height="0"', meter.group())
            self.assertIn('VerticalAlignment="Bottom"', meter.group())
        self.assertIn('Grid.Column="1" Grid.RowSpan="2"', self.xaml)
        self.assertIn('Height" Value="72"', (
            UI / "Design/ArControls.axaml").read_text(encoding="utf-8-sig"))

    def test_material_icons_preserve_command_handlers_and_dynamic_record_icon(self):
        for key in ("MsSettings", "MsPlay", "MsFolder"):
            self.assertIn(f'Data="{{StaticResource {key}}}"', self.xaml)
        for handler in ("Settings_OnClick",
                        "OpenSaved_OnClick", "ShowFolder_OnClick"):
            self.assertIn(f'Click="{handler}"', self.xaml)
        self.assertIn('x:Name="CameraPreviewPlaceholder"', self.xaml)
        # The source icon and record/stop icon are still dynamic in the code.
        self.assertIn('x:Name="SourceIcon"', self.xaml)
        self.assertIn('x:Name="RecordIcon"', self.xaml)
        self.assertNotIn('Click="RefreshSources_OnClick"', self.xaml)

    def test_settings_audio_toggle_and_post_fader_contract(self):
        settings = (UI / "SettingsWindow.axaml").read_text(encoding="utf-8-sig")
        setting_code = (UI / "SettingsWindow.axaml.cs").read_text(encoding="utf-8-sig")
        state = (UI / "Preview/SettingsPreviewState.cs").read_text(encoding="utf-8-sig")
        main = (UI / "MainWindow.axaml.cs").read_text(encoding="utf-8-sig")
        meter = (UI / "Preview/RecordedAudioMeter.cs").read_text(encoding="utf-8-sig")
        for name, handler in (("SettingsSystemAudioToggle", "SettingsSystemAudio_OnClick"),
                              ("SettingsMicrophoneToggle", "SettingsMicrophone_OnClick")):
            self.assertIn(f'x:Name="{name}"', settings)
            self.assertIn(f'Click="{handler}"', settings)
            self.assertIn(f'private void {handler}', setting_code)
        self.assertIn('Changed?.Invoke(this, EventArgs.Empty);', state)
        self.assertIn('RefreshInputLabels();', main)
        self.assertIn('_settings.SystemAudioEnabled', main)
        self.assertIn('_settings.MicrophoneEnabled', main)
        self.assertIn('audioSessionLocked:', main)
        self.assertIn('_audioSessionLocked', setting_code)
        self.assertIn('RecordedAudioMeter.PostFaderPeak(', main)
        self.assertIn('RecordedAudioMeter.PostFaderPeak(', setting_code)
        self.assertIn('Math.Clamp(gainPercent, 0, 100) / 100f', meter)
        self.assertNotIn('private static double MeterValue(', main)
        self.assertNotIn('private static double PeakPercent(', setting_code)

    def test_settings_audio_mixer_is_real_and_persisted(self):
        settings = (UI / "SettingsWindow.axaml").read_text(encoding="utf-8-sig")
        state = (UI / "Preview/SettingsPreviewState.cs").read_text(encoding="utf-8-sig")
        runtime = (ROOT / "src/app/audio_product_runtime.hpp").read_text(encoding="utf-8-sig")
        bridge = (ROOT / "src/bridge/native_bridge.cpp").read_text(encoding="utf-8-sig")
        for name in ("SystemGainSlider", "MicrophoneGainSlider",
                     "SystemMuteButton", "MicrophoneMuteButton",
                     "SystemMeterR", "MicMeterR"):
            self.assertIn(f'x:Name="{name}"', settings)
        self.assertIn("AudioMeter(", (UI / "SettingsWindow.axaml.cs").read_text(encoding="utf-8-sig"))
        self.assertNotIn("SystemLevelPattern", (UI / "SettingsWindow.axaml.cs").read_text(encoding="utf-8-sig"))
        self.assertIn("SetMixLevel(", state)
        self.assertIn("SetMixMuted(", state)
        self.assertIn("program_.mixer().set_source_config(", runtime)
        self.assertIn("config.audio_system_gain = request->system_gain;", bridge)
        self.assertIn('SnapshotDevices()', (UI / "SettingsWindow.axaml.cs").read_text(encoding="utf-8-sig"))

    def test_audio_meter_tracks_are_bounded_inside_cards(self):
        self.assertNotIn('ProgressBar x:Name="SystemLevelL"', self.xaml)
        self.assertEqual(self.xaml.count('Width="8" Height="55"'), 4)
        self.assertIn('ColumnDefinitions="2.00*,1.70*,1.05*,0.95*"', self.xaml)
        self.assertIn('HorizontalAlignment" Value="Stretch"', (
            UI / "Design/ArControls.axaml").read_text(encoding="utf-8-sig"))

    def test_webcam_preview_is_centered_and_does_not_fake_capture(self):
        from xml.etree import ElementTree
        ElementTree.fromstring(self.xaml)
        self.assertIn('x:Name="CameraPreviewFrame"', self.xaml)
        self.assertIn('Width="148" Height="83.25"', self.xaml)
        self.assertIn('x:Name="CameraPreviewPlaceholder"', self.xaml)
        self.assertIn('HorizontalAlignment="Center" VerticalAlignment="Center"', self.xaml)
        self.assertIn('x:Name="CameraPreviewImage" IsVisible="False"', self.xaml)
        self.assertIn('x:Name="CameraToggle"', self.xaml)
        self.assertIn('IsEnabled="False"', self.xaml)

    def test_persistent_preferences_are_restored_without_native_token_replay(self):
        state = (UI / "Preview/SettingsPreviewState.cs").read_text(encoding="utf-8-sig")
        store = (UI / "Preview/ProductSettingsStore.cs").read_text(encoding="utf-8-sig")
        app = (UI / "App.axaml.cs").read_text(encoding="utf-8-sig")
        self.assertIn("private const int CurrentSchemaVersion = 6;", store)
        self.assertIn("private const int ProductSchemaVersion = 5;", store)
        self.assertIn("private const int SpotlightSchemaVersion = 4;", store)
        for field in ("CaptureMode", "CaptureSourceId", "SystemAudioEnabled",
                      "MicrophoneEnabled", "MicrophoneDevice", "FrameRate",
                      "VisualStyle", "SmartZoom", "ClickHighlight",
                      "ShortcutKeys", "OutputFolder"):
            self.assertIn(field, store)
        self.assertIn("SetCaptureChoice(", state)
        self.assertIn("SetAudioPreferences(", state)
        self.assertIn("TimeSpan.FromMilliseconds(450)", app)
        self.assertIn("desktop.Exit += (_, _) => Persist();", app)
        self.assertIn("_settings.CaptureSourceId", self.code)
        self.assertIn("selected.Token", self.code)
        self.assertNotIn("MicrophoneDeviceToken { get;", state)

    def test_compact_source_and_device_details_stay_discoverable(self):
        self.assertIn('SourceSubtitle.IsVisible =', self.code)
        self.assertIn('_captureMode != PreviewCaptureMode.Display;', self.code)
        self.assertIn('ToolTip.SetTip(SourcePickerButton,', self.code)
        self.assertIn('ToolTip.SetTip(MicrophoneDeviceComboMain, _microphoneDevice);', self.code)
        self.assertIn('SourceSubtitle.IsVisible = true;', self.code)

    def test_sfx_is_mixed_on_the_existing_aac_timeline(self):
        src=(ROOT/"src/app/recorder_session.cpp").read_text(encoding="utf-8-sig")
        runtime=(ROOT/"src/app/audio_product_runtime.hpp").read_text(encoding="utf-8-sig")
        effects=(ROOT/"src/app/recorded_input_sounds.hpp").read_text(encoding="utf-8-sig")
        self.assertIn("record_click(",src)
        self.assertIn("record_keycap(",src)
        self.assertIn("recorded_cues_.reset(media_zero_100ns)",runtime)
        self.assertIn("recorded_cues_.apply(out, first_frame)",runtime)
        self.assertIn("kVoices = 24",effects)
        self.assertNotIn("PlaySound",effects)
        self.assertNotIn("waveOut",effects)

    def test_preview_is_internal_only_and_launches_directly(self):
        self.assertIn("<OutputType>WinExe</OutputType>", self.project)
        self.assertIn("'$(InternalAudioPreview)' == 'true'", self.project)
        self.assertIn("#if ARSSYUT_INTERNAL_AUDIO_PREVIEW", self.program)
        self.assertIn('"ARSSYUT_AUDIO_PREVIEW", "1"', self.program)
        self.assertIn("if (!_allowInteractionPreview && !_audioHardwarePreview)", self.code)
        self.assertIn("p:InternalAudioPreview=true", self.workflow)
        self.assertIn('Join-Path $package "Arssyut.UI.exe"', self.workflow)
        self.assertIn("name: arssyut-AUDIO-ENABLED-TEST-win-x64", self.workflow)
        self.assertNotIn("START_AUDIO_PREVIEW.cmd", self.workflow)


if __name__ == "__main__":
    unittest.main()
