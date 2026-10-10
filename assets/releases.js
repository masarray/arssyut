/* Arssyut: show the newest published release, clearly labeling previews.
   Downloads always use original GitHub Release asset URLs, never Actions artifacts. */
(() => {
  "use strict";
  const status = document.getElementById("release-status");
  const homeInstaller = document.getElementById("home-installer-download");
  if (!status && !homeInstaller) return;
  const dot = document.getElementById("release-dot");
  const empty = document.getElementById("no-release");
  const assetList = document.getElementById("release-assets");
  const version = document.getElementById("release-version");
  const repoPath = "/masarray/arssyut/releases/download/";
  const endpoint = "https://api.github.com/repos/masarray/arssyut/releases?per_page=20";

  function showStatus(message, available) {
    if (!status) return;
    status.textContent = message;
    dot.classList.toggle("available", available);
  }
  function showEmpty(title, message) {
    if (!empty || !assetList) return;
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
    if (!response.ok) throw new Error("GitHub API returned " + response.status);
    const releases = await response.json();
    if (!Array.isArray(releases)) throw new Error("Unexpected releases metadata");
    // Newest published release with the exact approved Windows binary assets,
    // including explicitly labeled preview releases; never use Actions artifacts.
    const release = releases.find(item =>
      item && !item.draft && Array.isArray(item.assets) &&
      item.assets.some(asset => officialAsset(asset) &&
        /^arssyut.*(setup|installer).*\.exe$/i.test(asset.name)) &&
      item.assets.some(asset => officialAsset(asset) &&
        /^arssyut.*portable.*win[-_.]?x64\.exe$/i.test(asset.name)));
    if (!release) {
      showStatus("No published Windows release yet", false);
      showEmpty("Public release coming soon",
        "There are no published Windows downloads yet. Installer and standalone portable EXE links will appear as soon as a verified release is published.");
      return;
    }
    const assets = release.assets.filter(officialAsset);
    const installer = best(assets, /arssyut.*(setup|installer).*\.exe$/i);
    const portable = best(assets, /^arssyut.*portable.*win[-_.]?x64\.exe$/i);
    if (homeInstaller && installer) {
      homeInstaller.href = installer.browser_download_url;
      homeInstaller.textContent = release.prerelease
        ? "Download preview installer ↗" : "Download latest installer ↗";
      homeInstaller.setAttribute("aria-label", "Download latest Arssyut Setup EXE");
    }
    if (!status) return;
    const hasInstaller = enableRow("installer-row", "installer-link", installer, "Download Arssyut installer");
    const hasPortable = enableRow("portable-row", "portable-link", portable, "Download Arssyut portable EXE");
    if (!hasInstaller && !hasPortable) {
      showStatus("Latest public release has no Windows packages", false);
      showEmpty("Windows packages not available",
        "An official release exists, but there is no eligible Windows installer or portable ZIP attached yet. Check GitHub Releases for details.");
      return;
    }
    empty.hidden = true;
    assetList.hidden = false;
    version.hidden = false;
    version.textContent = (release.prerelease ? "Preview release: " : "Latest release: ") + release.tag_name;
    showStatus(release.prerelease ? "Preview release — testing build" : "Official public download available", true);
  }).catch(() => {
    showStatus("Unable to check public releases", false);
    showEmpty("Please check GitHub Releases",
      "The release service could not be reached right now. Follow the official GitHub Releases link to check whether a download is available.");
  }).finally(() => clearTimeout(deadline));
})();
