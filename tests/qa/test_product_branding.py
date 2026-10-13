"""Regression checks for the canonical branded assets across every surface.

No image library, network access or physical Windows desktop required.
"""
import json
import re
import struct
import unittest
from html.parser import HTMLParser
from pathlib import Path
from urllib.parse import urlsplit

ROOT = Path(__file__).resolve().parents[2]
ICONS = ROOT / "assets" / "favicon"
VERSION_TOKEN = "c8f49853"


def png_dimensions(name):
    raw = (ICONS / name).read_bytes()
    if raw[:8] != b"\x89PNG\r\n\x1a\n" or raw[12:16] != b"IHDR":
        raise AssertionError(f"Invalid PNG: {name}")
    return struct.unpack_from(">II", raw, 16)


def ico_dimensions():
    raw = (ICONS / "favicon.ico").read_bytes()
    reserved, file_type, count = struct.unpack_from("<HHH", raw)
    if reserved != 0 or file_type != 1 or count == 0:
        raise AssertionError("Invalid Windows ICO file")
    dimensions = set()
    for i in range(count):
        pos = 6 + 16 * i
        if pos + 16 > len(raw):
            raise AssertionError("Truncated Windows ICO directory")
        width, height, _, _, _, _, size, offset = struct.unpack_from(
            "<BBBBHHII", raw, pos)
        dimensions.add((width or 256, height or 256))
        if not size or offset + size > len(raw):
            raise AssertionError("ICO image entry outside file")
    return dimensions


class ProductBrandingTests(unittest.TestCase):
    def test_every_canonical_image_is_valid(self):
        for name, expected in (
            ("favicon-96x96.png", (96, 96)),
            ("apple-touch-icon.png", (180, 180)),
            ("web-app-manifest-192x192.png", (192, 192)),
            ("web-app-manifest-512x512.png", (512, 512)),
        ):
            with self.subTest(name=name):
                self.assertEqual(png_dimensions(name), expected)
        variants = ico_dimensions()
        self.assertTrue(variants)
        self.assertTrue(any(w == h for w, h in variants))
        self.assertTrue(all(w == h for w, h in variants))

    def test_web_manifest_works_on_github_pages_subpath(self):
        manifest = json.loads((ICONS / "site.webmanifest").read_text(encoding="utf-8"))
        self.assertEqual(manifest["short_name"], "Arssyut")
        self.assertIn("Arssyut", manifest["name"])
        self.assertEqual(manifest["start_url"], "../../")
        self.assertEqual(manifest["scope"], "../../")
        self.assertEqual(manifest["theme_color"], "#0c1119")
        self.assertEqual({i["sizes"] for i in manifest["icons"]},
                         {"192x192", "512x512"})
        for icon in manifest["icons"]:
            self.assertFalse(icon["src"].startswith("/"),
                             "Root-absolute icons break GitHub Pages /arssyut/")
            self.assertTrue((ICONS / icon["src"]).is_file())
            self.assertEqual(icon["purpose"], "any")

    def test_pages_and_readme_use_updated_brand_icon(self):
        for path in (ROOT / "index.html", ROOT / "download/index.html"):
            page = path.read_text(encoding="utf-8")
            self.assertIn('content="Arssyut"', page)
            for file in ("favicon.ico", "favicon-96x96.png",
                         "apple-touch-icon.png", "site.webmanifest",
                         "web-app-manifest-192x192.png"):
                self.assertIn(f"{file}?v={VERSION_TOKEN}", page)
            self.assertIn(f"web-app-manifest-512x512.png?v={VERSION_TOKEN}", page)
        readme = (ROOT / "README.md").read_text(encoding="utf-8")
        self.assertIn(f"web-app-manifest-192x192.png?v={VERSION_TOKEN}", readme)

    def test_brand_patch_release_is_exact_sha_and_non_destructive(self):
        release = (ROOT / ".github/workflows/stable-v011-brand-once.yml").read_text()
        self.assertIn("v0.1.1", release)
        self.assertIn('version="0.1.1"', release)
        self.assertIn('publish_stable="true"', release)
        self.assertIn("CI_SHA", release)
        self.assertIn("main_sha", release)
        self.assertIn("source_conclusion", release)
        self.assertIn("gh workflow run windows-release-exes.yml", release)
        self.assertIn("cancel-in-progress: false", release)
        self.assertNotIn("gh release create", release)
        self.assertNotIn("gh release upload", release)

    def test_windows_exe_gui_and_setup_use_identical_brand_source(self):
        proj = (ROOT / "src/ui/Arssyut.UI/Arssyut.UI.csproj").read_text()
        for item in ("ApplicationIcon", "assets\\favicon\\favicon.ico",
                     "Assets\\favicon.ico", "Assets\\ArssyutLogo.png",
                     "web-app-manifest-192x192.png", "<Version>0.1.1</Version>"):
            self.assertIn(item, proj)
        for page in ("MainWindow.axaml", "SettingsWindow.axaml"):
            xaml = (ROOT / "src/ui/Arssyut.UI" / page).read_text()
            self.assertIn('Icon="avares://Arssyut.UI/Assets/favicon.ico"', xaml)
        main = (ROOT / "src/ui/Arssyut.UI/MainWindow.axaml").read_text()
        self.assertIn("avares://Arssyut.UI/Assets/ArssyutLogo.png", main)
        setup = (ROOT / "packaging/windows/Arssyut.iss").read_text()
        self.assertIn("SetupIconFile={#SourcePath}..\\..\\assets\\favicon\\favicon.ico", setup)
        self.assertIn("UninstallDisplayIcon={app}\\{#AppExe}", setup)
        self.assertIn('Name: "{autoprograms}\\Arssyut"', setup)
        self.assertIn('Name: "{autodesktop}\\Arssyut"', setup)
        cmake = (ROOT / "CMakeLists.txt").read_text()
        self.assertIn("enable_language(RC)", cmake)
        self.assertIn("arssyut_app_icon.rc.in", cmake)
        self.assertIn("VERSION 0.1.1", cmake)
        native = (ROOT / "src/app/arssyut_app_icon.rc.in").read_text()
        self.assertIn('ICON "@ARSSYUT_BRAND_ICON_PATH@"', native)


if __name__ == "__main__":
    unittest.main()
