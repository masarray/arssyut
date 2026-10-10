/* Official GitHub release assets only. No hard-coded version or CI ZIPs. */
(() => {
  "use strict";
  const releaseListUrl = "https://api.github.com/repos/masarray/arssyut/releases?per_page=15";
  const releaseAssetPrefix = "/masarray/arssyut/releases/download/";
  const githubReleases = "https://github.com/masarray/arssyut/releases";
  const byId = id => document.getElementById(id);
  const homeInstaller = byId("home-installer-link");
  const homePortable = byId("home-portable-link");
  const homeStatus = byId("home-release-label");
  const status = byId("release-status");
  const dot = byId("release-dot");
  const empty = byId("no-release");
  const rows = byId("release-assets");
  const versionLabel = byId("release-version");

  if (!homeInstaller && !status) return;

  function renderMessage(message, active = false) {
    if (status) status.textContent = message;
    if (dot) dot.classList.toggle("available", active);
    if (homeStatus) homeStatus.textContent = message;
  }
  function fallback(title, detail) {
    renderMessage(title);
    if (rows) rows.hidden = true;
    if (!empty) return;
    empty.hidden = false;
    const heading = empty.querySelector("h3");
    const description = empty.querySelector("p");
    if (heading) heading.textContent = title;
    if (description) description.textContent = detail;
  }
  function verifiedAsset(asset, tag, filename) {
    if (!asset || asset.name !== filename ||
        typeof asset.browser_download_url !== "string") return false;
    try {
      const link = new URL(asset.browser_download_url);
      return link.protocol === "https:" && link.hostname === "github.com" &&
        decodeURIComponent(link.pathname) === releaseAssetPrefix + tag + "/" + filename;
    } catch { return false; }
  }
  function linkRow(rowId, linkId, url, label) {
    const row = byId(rowId), link = byId(linkId);
    if (!row || !link) return;
    row.hidden = false;
    link.href = url;
    link.setAttribute("aria-label", label);
  }
  const controller = new AbortController();
  const stop = setTimeout(() => controller.abort(), 8000);
  fetch(releaseListUrl, {
    cache: "no-store",
    signal: controller.signal,
    headers: { Accept: "application/vnd.github+json" }
  }).then(async response => {
    if (!response.ok) throw new Error("GitHub Releases unavailable");
    const releases = await response.json();
    if (!Array.isArray(releases)) throw new Error("Invalid GitHub release list");
    const ordered = releases.filter(item =>
      item && !item.draft && typeof item.tag_name === "string" &&
      /^v\d+\.\d+\.\d+$/.test(item.tag_name) && Array.isArray(item.assets)
    ).sort((a,b) => Date.parse(b.published_at || 0) - Date.parse(a.published_at || 0));
    if (ordered.length === 0) {
      fallback("Public release coming soon", "No published Arssyut Windows release exists yet. Check GitHub Releases for announcements.");
      return;
    }
    const release = ordered[0], tag = release.tag_name;
    const installerName = "Arssyut-Setup-" + tag + "-win-x64.exe";
    const portableName = "Arssyut-" + tag + "-portable-win-x64.exe";
    const setup = release.assets.find(asset => verifiedAsset(asset, tag, installerName));
    const portable = release.assets.find(asset => verifiedAsset(asset, tag, portableName));
    if (!setup || !portable) {
      fallback("Latest release files are incomplete", "The newest published release is missing one of its Windows EXE downloads. Check official GitHub Releases.");
      return;
    }
    const label = release.prerelease ? "Preview" : "Stable";
    const message = "Latest " + label + " · " + tag + " · Windows x64";
    renderMessage(message, true);
    if (rows && empty && versionLabel) {
      empty.hidden = true;
      rows.hidden = false;
      versionLabel.hidden = false;
      versionLabel.textContent = "Arssyut " + tag + " · " + label + " · video-only";
      linkRow("installer-row","installer-link",setup.browser_download_url,
              "Download latest Arssyut Windows setup EXE " + tag);
      linkRow("portable-row","portable-link",portable.browser_download_url,
              "Download latest Arssyut portable single EXE " + tag);
    }
    if (homeInstaller) {
      homeInstaller.href = setup.browser_download_url;
      homeInstaller.textContent = "Download Installer (.exe) ↓";
      homeInstaller.setAttribute("aria-label", "Download latest Arssyut setup " + tag);
    }
    if (homePortable) {
      homePortable.hidden = false;
      homePortable.href = portable.browser_download_url;
      homePortable.textContent = "Portable EXE ↓";
      homePortable.setAttribute("aria-label", "Download latest Arssyut portable " + tag);
    }
  }).catch(() => {
    fallback("Release check unavailable", "Please open the official GitHub Releases page to check downloads.");
    if (homeInstaller) homeInstaller.href = githubReleases;
    if (homePortable) homePortable.href = githubReleases;
  }).finally(() => clearTimeout(stop));
})();
