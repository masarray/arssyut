/* Select one newest eligible published GitHub Release for BOTH Windows downloads.
   Fail closed: no CI artifacts, unrelated tags, mixed versions or guessed URLs. */
(() => {
  "use strict";
  const status = document.getElementById("release-status");
  const homeInstaller = document.getElementById("home-installer-download");
  if (!status && !homeInstaller) return;
  const dot = document.getElementById("release-dot");
  const empty = document.getElementById("no-release");
  const assetList = document.getElementById("release-assets");
  const version = document.getElementById("release-version");
  const endpoint = "https://api.github.com/repos/masarray/arssyut/releases?per_page=20";

  function showStatus(message, available) {
    if (!status) return;
    status.textContent = message;
    if (dot) dot.classList.toggle("available", available);
  }
  function showEmpty(title, message) {
    if (!empty || !assetList) return;
    assetList.hidden = true;
    empty.hidden = false;
    if (version) version.hidden = true;
    const heading = empty.querySelector("h3");
    const paragraph = empty.querySelector("p");
    if (heading) heading.textContent = title;
    if (paragraph) paragraph.textContent = message;
  }
  function approvedAsset(asset, release, expectedName) {
    if (!asset || asset.name !== expectedName ||
        asset.state !== "uploaded" || !Number.isFinite(asset.size) ||
        asset.size < 1000000 || typeof asset.browser_download_url !== "string")
      return false;
    try {
      const url = new URL(asset.browser_download_url);
      const path = "/masarray/arssyut/releases/download/" +
        encodeURIComponent(release.tag_name) + "/" + encodeURIComponent(expectedName);
      return url.protocol === "https:" && url.hostname === "github.com" &&
        url.pathname === path && !url.search && !url.hash;
    } catch {
      return false;
    }
  }
  function eligibleRelease(item) {
    if (!item || item.draft || !item.published_at ||
        !Array.isArray(item.assets) || typeof item.tag_name !== "string" ||
        !/^v?\d+\.\d+\.\d+(?:-(?:rc|beta|alpha)\.\d+)?$/i.test(item.tag_name))
      return null;
    const published = Date.parse(item.published_at);
    if (!Number.isFinite(published)) return null;
    const releaseVersion = item.tag_name.replace(/^v/i, "");
    const installerName = "Arssyut-Setup-" + releaseVersion + "-win-x64.exe";
    const portableName = "Arssyut-" + releaseVersion + "-portable-win-x64.exe";
    const installer = item.assets.find(asset => approvedAsset(asset, item, installerName));
    const portable = item.assets.find(asset => approvedAsset(asset, item, portableName));
    if (!installer || !portable) return null;
    return { release: item, installer, portable, published };
  }
  function enableRow(id, linkId, asset, description) {
    const row = document.getElementById(id);
    const link = document.getElementById(linkId);
    if (!row || !link) return;
    row.hidden = false;
    link.href = asset.browser_download_url;
    link.setAttribute("aria-label", description + ": " + asset.name);
  }

  const controller = new AbortController();
  const deadline = setTimeout(() => controller.abort(), 8000);
  fetch(endpoint, {
    signal: controller.signal,
    headers: { "Accept": "application/vnd.github+json" },
    cache: "no-store"
  }).then(async response => {
    if (!response.ok) throw new Error("GitHub API returned " + response.status);
    const releases = await response.json();
    if (!Array.isArray(releases)) throw new Error("Unexpected releases metadata");
    // GitHub's listing order is not a reliable published-date ordering.
    // A newer incomplete release must never split Installer and Portable.
    const matched = releases.map(item => (!item.draft ? eligibleRelease(item) : null))
      .filter(Boolean).sort((a, b) => b.published - a.published)[0];
    if (!matched) {
      showStatus("No complete public Windows release available", false);
      showEmpty("Windows download not available",
        "No published release contains a complete, matching Windows Setup EXE and standalone Portable EXE pair. Please check GitHub Releases.");
      return;
    }
    const { release, installer, portable } = matched;
    const preview = !!release.prerelease || /-(?:rc|beta|alpha)\./i.test(release.tag_name);
    if (homeInstaller) {
      homeInstaller.href = installer.browser_download_url;
      homeInstaller.textContent = preview
        ? "Download preview installer ↗" : "Download latest installer ↗";
      homeInstaller.setAttribute("aria-label", "Download Arssyut Setup EXE: " + installer.name);
    }
    if (!status) return;
    enableRow("installer-row", "installer-link", installer, "Download Arssyut installer");
    enableRow("portable-row", "portable-link", portable, "Download Arssyut portable EXE");
    empty.hidden = true;
    assetList.hidden = false;
    version.hidden = false;
    version.textContent = (preview ? "Preview release: " : "Stable release: ") + release.tag_name;
    showStatus(preview ? "Preview release — testing build" : "Stable Windows download available", true);
  }).catch(() => {
    showStatus("Unable to check public releases", false);
    showEmpty("Please check GitHub Releases",
      "GitHub Releases could not be checked right now (offline, rate limit or service issue). Use the official GitHub Releases page; no unverified file is linked.");
  }).finally(() => clearTimeout(deadline));
})();