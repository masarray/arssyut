"""P8W F2B.1/F3/F4 source regression. Hardware pixel motion still required."""
from pathlib import Path
import unittest
ROOT=Path(__file__).resolve().parents[2]
def load(path): return (ROOT/path).read_text(encoding="utf-8")
CAM=load("src/platform/windows/capture/webcam_preview_session.cpp")
GPU=load("src/platform/windows/graphics/d3d11_compositor.cpp")
HEADER=load("src/core/video/output_overlay_profile.hpp")
BRIDGE=load("src/bridge/native_bridge.cpp")
BRIDGE_HEADER=load("src/bridge/native_bridge.h")
CS=load("src/ui/Arssyut.UI/Interop/NativeBridgeClient.cs")
UI=load("src/ui/Arssyut.UI/SettingsWindow.axaml")
STATE=load("src/ui/Arssyut.UI/Preview/SettingsPreviewState.cs")
STORE=load("src/ui/Arssyut.UI/Preview/ProductSettingsStore.cs")
REC=load("src/app/recorder_session.cpp")
class OverlayContract(unittest.TestCase):
    def test_poststop_gpu_submit_is_nonblocking(self):
        self.assertIn("graphics_->immediate_context()->Flush()",CAM)
        self.assertIn("D3D11_ASYNC_GETDATA_DONOTFLUSH",CAM)
        self.assertIn("D3D11_MAP_FLAG_DO_NOT_WAIT",CAM)
        self.assertIn("recording_acknowledged_ = false",CAM)
    def test_camera_masks_one_native_shader(self):
        for p in ("Circle=0","RoundedRectangle=1",
            "DiagonalTopLeftBottomRight=2","DiagonalTopRightBottomLeft=3"):
            self.assertIn(p,HEADER)
        for phrase in ("if (mask == 0)","if (mask == 2 &&",
                "if (mask == 3 &&","webcam_params.z","webcam_style.y"):
            self.assertIn(phrase,GPU)
        self.assertEqual(GPU.count("context->Draw(3, 0);"),1)
    def test_one_gpu_logo_pass_with_png_premultiplied_alpha(self):
        self.assertIn("logo_texture : register(t4)",GPU)
        self.assertIn("premul.rgb * opacity",GPU)
        self.assertIn("PSSetShaderResources(0, 5, srvs)",GPU)
        self.assertIn("PSSetShaderResources(0, 5, null_srvs)",GPU)
        logo=load("src/platform/windows/graphics/logo_gpu_texture.hpp")
        self.assertIn("GUID_ContainerFormatPng",logo)
        self.assertIn("GUID_WICPixelFormat32bppPBGRA",logo)
        self.assertIn("D3D11_USAGE_IMMUTABLE",logo)
        self.assertNotIn("WIC",REC)
    def test_bridge_abi_matches_managed_and_profile(self):
        self.assertIn("ARSSYUT_BRIDGE_ABI_VERSION = 12",BRIDGE_HEADER)
        self.assertIn("ExpectedAbi = 12",CS)
        self.assertIn("request->camera_profile_version != 1U",BRIDGE)
        self.assertIn("config.output_overlays = overlay",BRIDGE)
        self.assertIn("set_output_overlays(config_.output_overlays)",REC)
    def test_settings_are_not_mock(self):
        self.assertNotIn("Camera compositor backend is not connected yet",UI)
        self.assertIn("PageLogo",UI)
        for tag in ("CameraMaskCombo","CameraSizeSlider","CameraMarginSlider",
                "CameraBorderSlider","CameraMirrorToggle","LogoEnabledToggle",
                "LogoPick_OnClick","LogoPlacementCombo","LogoOpacitySlider"):
            self.assertIn(tag,UI)
        self.assertIn("CurrentSchemaVersion = 7",STORE)
        self.assertIn("MixerSchemaVersion = 6",STORE)
        self.assertIn("CameraPlacement.TopRight",STATE)
        self.assertIn("LogoEnabled",STATE)
if __name__=="__main__":unittest.main()
