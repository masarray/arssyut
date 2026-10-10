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

The Download page fetches the GitHub API's **newest published GitHub release** on the visitor's browser:

https://api.github.com/repos/masarray/arssyut/releases?per_page=20

It never invents binary URLs. It only uses GitHub-hosted asset URLs from that returned release, selects an Arssyut Setup Installer (.exe) or Windows Portable (.exe) when attached, and excludes internal/test/candidate files. A draft or prerelease is not exposed as stable; a missing release or a GitHub API failure has a clear explanatory fallback. The website works with no external framework or tracking service.

**Windows preview v0.1.0-rc.1 is published as of 2026-10-10**, with a Setup EXE, portable single EXE, and SHA256SUMS. It is labeled *prerelease*, not stable. System audio and microphone recording are implemented (user confirmed); **webcam recording remains unimplemented**. Do not mislabel preview assets as stable or substitute video-only CI artifacts.

## Launch/readiness checklist

1. Check website links, favicon, viewport layout and keyboard navigation at mobile and desktop sizes.
2. Enable GitHub Pages from the repository settings and ensure the deployment succeeds.
3. Review Windows Installer / Portable artifacts before creating the first GitHub release.
4. Publish verified release assets named consistently (for example Arssyut-Setup-vX.Y.Z-win-x64.exe and Arssyut-vX.Y.Z-portable-win-x64.exe); the page will discover them automatically.
5. Verify the download links originate from GitHub Releases and independently verify the downloaded binary can install or launch on a clean Windows computer.
6. Keep the user-confirmed audio feature status accurate; mark **webcam** as the unimplemented feature. Run independent binary, installer and website acceptance before promoting the preview to stable.

The README includes links to the intended Pages URLs; until Pages is enabled, these may not resolve. The GitHub repository remains the source of truth.
