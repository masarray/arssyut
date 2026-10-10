# Public website and downloads

Arssyut's lightweight public website lives in the repository:

- Home: [index.html](../index.html)
- Download page: [download/index.html](../download/index.html)
- Shared styles: [assets/site.css](../assets/site.css)
- Release lookup: [assets/releases.js](../assets/releases.js)
- Official icons: [assets/favicon/](../assets/favicon)

The intended GitHub Pages URLs are:

- https://masarray.github.io/arssyut/
- https://masarray.github.io/arssyut/download/

**Publishing requires GitHub Pages to be enabled.** In GitHub open **Settings → Pages → Build and deployment → Source: GitHub Actions**. The [Public Website workflow](../.github/workflows/pages.yml) deploys when website files change on the default branch or through the manual **Run workflow** action. Do not claim the website is live until the deployment reports success and the URL can be opened.

The deployment stages only website HTML, CSS, JavaScript and the existing icon directory. It does not expose C++ source code or artifacts as website downloads, and it does not compile the Windows application.

## How download links work

Both the landing page and Download page use a single shared script to query the **latest published public release** on each visit:

https://api.github.com/repos/masarray/arssyut/releases?per_page=15

It never invents binary URLs. It only uses GitHub-hosted asset URLs from that returned release, selects matching Arssyut Setup (.exe) and Portable single-file (.exe) binaries from the latest published tag. Public prereleases are labelled **Preview**, stable releases **Stable**, and drafts/internal/test files never appear as stable downloads. On network failure, the buttons fall back to the GitHub Releases page rather than a stale binary URL. The website works with no external framework or tracking service.

The download page shows **Public release coming soon** when no release exists. Public video-only previews may be labelled explicitly, but audio-enabled stable remains blocked by real-device recording acceptance. Do not turn GitHub Actions internal audio acceptance artifacts into public stable links.

## Launch/readiness checklist

1. Check website links, favicon, viewport layout and keyboard navigation at mobile and desktop sizes.
2. Enable GitHub Pages from the repository settings and ensure the deployment succeeds.
3. Review Windows Installer / Portable artifacts before creating the first GitHub release.
4. Publish verified release assets named consistently (for example Arssyut-Setup-vX.Y.Z-win-x64.exe and Arssyut-vX.Y.Z-portable-win-x64.exe); the page will discover them automatically.
5. Verify the download links originate from GitHub Releases and independently verify the downloaded binary can install or launch on a clean Windows computer.
6. Keep audio-enabled release claims gated until real-device stereo and A/V checks pass.

The README includes links to the intended Pages URLs; until Pages is enabled, these may not resolve. The GitHub repository remains the source of truth.
