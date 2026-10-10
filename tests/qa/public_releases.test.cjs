"use strict";
const { test } = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

const script = fs.readFileSync(path.resolve(__dirname, "../../assets/releases.js"), "utf8");
const releaseBase = "https://github.com/masarray/arssyut/releases/download/";
function release(tag, published, prerelease = true) {
  const version = tag.replace(/^v/i, "");
  const makeAsset = (name, size) => ({
    name, state: "uploaded", size,
    browser_download_url: releaseBase + tag + "/" + name
  });
  return { tag_name: tag, published_at: published, draft: false, prerelease, assets: [
    makeAsset("Arssyut-Setup-" + version + "-win-x64.exe", 34000000),
    makeAsset("Arssyut-" + version + "-portable-win-x64.exe", 105000000),
    makeAsset("SHA256SUMS.txt", 200)
  ] };
}
async function render(releases, { homepage = false, fail = false } = {}) {
  const ids = new Map();
  function element(id) {
    if (!ids.has(id)) ids.set(id, {
      hidden: true, href: id === "home-installer-download" ? "download/" : "https://github.com/masarray/arssyut/releases",
      textContent: "", attributes: {},
      setAttribute(name, value) { this.attributes[name] = value; },
      classList: { toggle() {} },
      querySelector(selector) { return element(id + "-" + selector); }
    });
    return ids.get(id);
  }
  const document = { getElementById(id) {
    if (homepage) return id === "home-installer-download" ? element(id) : null;
    return id === "home-installer-download" ? null : element(id);
  } };
  let fetched = false;
  const fetch = async () => {
    fetched = true;
    if (fail) throw Error("Network unavailable");
    return { ok: true, json: async () => releases };
  };
  vm.runInNewContext(script, {
    document, fetch, URL, AbortController,
    setTimeout: () => 1, clearTimeout: () => {}
  });
  await new Promise(resolve => setImmediate(resolve));
  assert.equal(fetched, true);
  return element;
}
test("newest complete preview pair is selected together even when listing is unordered", async () => {
  const older = release("v0.1.0", "2026-10-09T10:00:00Z", false);
  const newer = release("v0.2.0-rc.1", "2026-10-10T10:00:00Z");
  const el = await render([older, newer]);
  assert.equal(el("installer-link").href, newer.assets[0].browser_download_url);
  assert.equal(el("portable-link").href, newer.assets[1].browser_download_url);
  assert.equal(el("release-version").textContent, "Preview release: v0.2.0-rc.1");
  assert.equal(el("release-assets").hidden, false);
  assert.equal(el("no-release").hidden, true);
});
test("incomplete newest release never mixes assets from two releases", async () => {
  const older = release("v0.1.0-rc.1", "2026-10-10T12:41:45Z");
  const newer = release("v0.1.1", "2026-10-11T09:00:00Z", false);
  newer.assets.splice(1, 1);
  const el = await render([newer, older]);
  assert.equal(el("installer-link").href, older.assets[0].browser_download_url);
  assert.equal(el("portable-link").href, older.assets[1].browser_download_url);
});
test("cross-tag URLs, wrong host, pending upload and placeholder binaries fail closed", async () => {
  const bad = release("v0.3.0", "2026-10-12T00:00:00Z");
  bad.assets[0].browser_download_url = releaseBase + "v0.2.0/" + bad.assets[0].name;
  bad.assets[1].browser_download_url = bad.assets[1].browser_download_url.replace("github.com", "not-github.example");
  const el = await render([bad]);
  assert.equal(el("release-assets").hidden, true);
  assert.equal(el("no-release").hidden, false);
  const pending = release("v0.4.0", "2026-10-13T00:00:00Z");
  pending.assets[0].state = "new";
  pending.assets[1].size = 120;
  const another = await render([pending]);
  assert.equal(another("release-assets").hidden, true);
});
test("draft releases and missing or malformed metadata never create download links", async () => {
  const draft = release("v0.1.1", "2026-10-13T00:00:00Z", false);
  draft.draft = true;
  const el = await render([draft, { tag_name: "v0.1.2", assets: [] }]);
  assert.equal(el("release-assets").hidden, true);
  assert.equal(el("installer-link").href, "https://github.com/masarray/arssyut/releases");
});
test("stable release status follows metadata; an RC tag is always preview", async () => {
  const stable = release("v0.2.0", "2026-10-14T00:00:00Z", false);
  const el = await render([stable]);
  assert.equal(el("release-version").textContent, "Stable release: v0.2.0");
  const rc = release("v0.2.1-rc.1", "2026-10-15T00:00:00Z", false);
  const other = await render([rc]);
  assert.equal(other("release-version").textContent, "Preview release: v0.2.1-rc.1");
});
test("homepage CTA matches Download installer and retains safe fallback on API failure", async () => {
  const current = release("v0.1.0-rc.1", "2026-10-10T12:41:45Z");
  const home = await render([current], { homepage: true });
  assert.equal(home("home-installer-download").href, current.assets[0].browser_download_url);
  assert.match(home("home-installer-download").textContent, /preview/i);
  const failed = await render([], { homepage: true, fail: true });
  assert.equal(failed("home-installer-download").href, "download/");
  const page = await render([], { fail: true });
  assert.equal(page("release-assets").hidden, true);
  assert.equal(page("no-release").hidden, false);
  assert.match(page("release-status").textContent, /Unable to check/i);
});
