/* Arssyut: public downloads come ONLY from non-prerelease GitHub Releases.
   No hard-coded asset URLs, drafts, Actions ZIPs, or pre-release binaries. */
(() => {
  "use strict";
  const status = document.getElementById("release-status");
  if (!status) return;
  const dot = document.getElementById("release-dot");
  const empty = document.getElementById("no-release");
  const assetList = document.getElementById("release-assets");
  const version = document.getElementById("release-version");
  const repoPath = "/masarray/arssyut/releases/download/";
  const endpoint = "https://api.github.com/repos/masarray/arssyut/releases/latest";

  function showStatus(message, available) {
    status.textContent = message;
    dot.classList.toggle("available", available);
  }
  function showEmpty(title, message) {
    assetList.hidden = true;
    empty.hidden = false;
    const heading = empty.querySelector("h3");
    const paragraph = empty.querySelector("p");
    if (heading) heading.textContent = title;
    if (paragraph) paragraph.textContent = message;
  }
  function officialAsset(asset) {
    if (!asset || typeof asset.name !== "string" ||
        typeof asset.browser_download_url !== "string") return false;
    if (/internal|acceptance|candidate|preview|debug|experimental|(^|[-_. ])test([-_. ]|$)/i.test(asset.name))
      return false;
    try {
      const url = new URL(asset.browser_download_url);
      return url.protocol === "https:" &&
        url.hostname === "github.com" &&
        url.pathname.startsWith(repoPath);
    } catch {
      return false;
    }
  }
  function best(assets, expression) {
    return assets.find(item => expression.test(item.name)) || null;
  }
  function enableRow(id, linkId, asset, description) {
    if (!asset) return false;
    const row = document.getElementById(id);
    const link = document.getElementById(linkId);
    row.hidden = false;
    link.href = asset.browser_download_url;
    link.setAttribute("aria-label", description + ": " + asset.name);
    return true;
  }

  const controller = new AbortController();
  const deadline = setTimeout(() => controller.abort(), 8000);
  fetch(endpoint, {
    signal: controller.signal,
    headers: { "Accept": "application/vnd.github+json" }
  }).then(async response => {
    if (response.status === 404) {
      showStatus("No stable public release yet", false);
      showEmpty("Public release coming soon",
        "There are no published stable downloads yet. When the first release is ready, the installer and portable links will appear here automatically.");
      return;
    }
    if (!response.ok) throw new Error("GitHub API returned " + response.status);
    const release = await response.json();
    if (!release || release.draft || release.prerelease ||
        typeof release.tag_name !== "string" || !Array.isArray(release.assets))
      throw new Error("Unexpected release metadata");
    const assets = release.assets.filter(officialAsset);
    const installer = best(assets, /arssyut.*(setup|installer).*\.exe$/i);
    const portable = best(assets, /arssyut.*(portable|windows|win[-_.]?x64|x64).*\.zip$/i);
    const hasInstaller = enableRow("installer-row", "installer-link", installer, "Download Arssyut installer");
    const hasPortable = enableRow("portable-row", "portable-link", portable, "Download Arssyut portable ZIP");
    if (!hasInstaller && !hasPortable) {
      showStatus("Latest public release has no Windows packages", false);
      showEmpty("Windows packages not available",
        "An official release exists, but there is no eligible Windows installer or portable ZIP attached yet. Check GitHub Releases for details.");
      return;
    }
    empty.hidden = true;
    assetList.hidden = false;
    version.hidden = false;
    version.textContent = "Latest stable version: " + release.tag_name;
    showStatus("Official public download available", true);
  }).catch(() => {
    showStatus("Unable to check public releases", false);
    showEmpty("Please check GitHub Releases",
      "The release service could not be reached right now. Follow the official GitHub Releases link to check whether a download is available.");
  }).finally(() => clearTimeout(deadline));
})();
