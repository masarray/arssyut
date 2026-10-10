"""Public website contracts (offline, deterministic; no network or devices)."""
from html.parser import HTMLParser
from pathlib import Path
from urllib.parse import urlsplit
import json
import unittest

ROOT = Path(__file__).resolve().parents[2]
HOME = ROOT / "index.html"
DOWNLOAD = ROOT / "download/index.html"
JS = ROOT / "assets/releases.js"
CSS = ROOT / "assets/site.css"


class PageReferences(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.links = []
        self.ids = set()
        self.headings = []
        self.h1_count = 0
        self._heading = None

    def handle_starttag(self, tag, attrs):
        attr = dict(attrs)
        if attr.get("id"):
            self.ids.add(attr["id"])
        if tag in ("a", "link"):
            self.links.append(attr.get("href", ""))
        if tag in ("script", "img"):
            self.links.append(attr.get("src", ""))
        if tag == "h1":
            self.h1_count += 1
        if tag in ("h1", "h2", "h3"):
            self._heading = tag

    def handle_data(self, data):
        if self._heading and data.strip():
            self.headings.append((self._heading, data.strip()))

    def handle_endtag(self, tag):
        if self._heading == tag:
            self._heading = None


class PublicSiteContracts(unittest.TestCase):
    def test_html_links_are_real_files_and_paths_are_relative(self):
        for document in (HOME, DOWNLOAD):
            with self.subTest(document=document):
                self.assertTrue(document.is_file())
                html = document.read_text(encoding="utf-8")
                self.assertIn('<html lang="en">', html)
                self.assertIn('name="viewport"', html)
                self.assertIn('href="#main"', html)
                parser = PageReferences()
                parser.feed(html)
                self.assertIn("main", parser.ids)
                self.assertEqual(parser.h1_count, 1)
                for url in parser.links:
                    if not url or url.startswith(("https://", "mailto:", "#")):
                        continue
                    self.assertFalse(url.startswith("/"), url)
                    path = urlsplit(url).path
                    destination = (document.parent / path).resolve()
                    self.assertTrue(destination.is_relative_to(ROOT), url)
                    if destination.is_dir():
                        destination = destination / "index.html"
                    self.assertTrue(destination.is_file(), url)

    def test_site_uses_canonical_brand_files(self):
        homepage = HOME.read_text(encoding="utf-8")
        download = DOWNLOAD.read_text(encoding="utf-8")
        self.assertIn("assets/favicon/android-chrome-192x192.png", homepage)
        self.assertIn("assets/favicon/android-chrome-512x512.png", homepage)
        self.assertIn("../assets/favicon/favicon.ico", download)
        self.assertIn("../assets/favicon/site.webmanifest", download)
        manifest = json.loads((ROOT / "assets/favicon/site.webmanifest").read_text())
        self.assertEqual(manifest["name"], "Arssyut")
        for icon in manifest["icons"]:
            self.assertTrue((ROOT / "assets/favicon" / icon["src"]).is_file())

    def test_download_page_does_not_invent_public_binaries(self):
        html = DOWNLOAD.read_text(encoding="utf-8")
        script = JS.read_text(encoding="utf-8")
        self.assertIn('id="no-release"', html)
        self.assertIn("Public release coming soon", html)
        self.assertIn('id="release-state"', html)
        self.assertIn('aria-live="polite"', html)
        self.assertIn("releases?per_page=20", script)
        self.assertIn("browser_download_url", script)
        self.assertIn("!item.draft", script)
        self.assertIn("Preview release", script)
        self.assertIn("url.hostname === \"github.com\"", script)
        self.assertIn('assetList.hidden = false', script)
        candidate = (ROOT / ".github/workflows/windows-release-exes.yml").read_text(encoding="utf-8")
        self.assertIn("refs/heads/main", candidate)
        self.assertIn("arssyut-AUDIO-ENABLED-TEST-win-x64", candidate)
        self.assertIn("dist/release/*.exe", candidate)
        self.assertIn("source.head_sha -ne $env:GITHUB_SHA", candidate)
        self.assertIn("arssyut-RELEASE-CANDIDATE-EXEs", candidate)
        self.assertIn("publish_prerelease:", candidate)
        self.assertIn("gh release create", candidate)
        self.assertIn("--prerelease", candidate)
        self.assertIn("two executables plus SHA256SUMS", candidate)
        self.assertIn("!Array.isArray(releases)", script)
        self.assertNotIn("github.com/masarray/arssyut/actions/runs/", html)
        self.assertNotIn("releases/download/v", html)

    def test_accurate_feature_and_test_build_disclosure(self):
        readme = (ROOT / "README.md").read_text(encoding="utf-8")
        home = HOME.read_text(encoding="utf-8")
        download = DOWNLOAD.read_text(encoding="utf-8")
        self.assertIn("undergoing Windows hardware testing", home)
        self.assertIn("test builds", download.lower())
        self.assertIn("AUDIO-ENABLED-TEST-win-x64", readme)
        self.assertIn("VIDEO-ONLY-avalonia-win-x64", readme)
        self.assertIn("no stable public release", readme.lower())
        self.assertIn("src=\"assets/favicon/android-chrome-192x192.png\"", readme)
        self.assertIn("Arssyut.UI.exe", download)

    def test_mobile_accessibility_and_static_pages_deployment(self):
        css = CSS.read_text(encoding="utf-8")
        workflow = (ROOT / ".github/workflows/pages.yml").read_text(encoding="utf-8")
        self.assertIn("@media(max-width:620px)", css)
        self.assertIn("prefers-reduced-motion", css)
        self.assertIn(":focus-visible", css)
        for line in ("workflow_dispatch:", "permissions:", "pages: write",
                     "id-token: write", "upload-pages-artifact@v3",
                     "deploy-pages@v4", "cp download/index.html",
                     "cp assets/site.css assets/releases.js"):
            self.assertIn(line, workflow)
        self.assertNotIn("path: .\n", workflow)
        self.assertNotIn("release: published", workflow)


if __name__ == "__main__":
    unittest.main()
