# Handoff — Finish Arssyut Public Release, Download Sync & Webcam Status

**Date:** 2026-10-10
**Repo:** https://github.com/masarray/arssyut
**Observed main at handoff start:** `982789c35aa0bd65d440c8f6577ae621fe33769c`
**User's confirmed product status:** **System Audio and Microphone COMPLETE**; **webcam NOT implemented**.
**Scope:** Finish remaining release/website work and validate the deliverables, without regressing the successful Windows GUI/video/audio pipeline.

> Start by fetching the **latest** `main`, PRs, release list, CI jobs and artifacts again.
> These are a point-in-time checkpoint, **not** permission to reset to an
> older head. Read `AGENTS.md`, this handoff, and packaging/public-web docs.
> The older `docs/CURRENT_HANDOFF.md` preserves historical P7 planning
> and must NOT be interpreted as the current audio implementation status.

## User's non-negotiable correction

- **Audio is finished according to the user.** Remove all public copy stating
  "audio pending", "internal testing" as its capability status, or that native
  Mic/System recording has not been implemented. Preserve system audio,
  microphone, true stereo input handling, the UI's level meters, source
  enable/disable, faders, mute and Settings↔main binding. Never substitute a
  video-only reference package as the user-facing release.
- **Webcam is NOT implemented.** Say so clearly on the landing page, Download
  page and README; do not advertise a webcam recorder or fake a preview.
- Game capture is not accepted as a finished feature; do not claim it is ready
  without checking its actual capability state.
- Keep the compact Material Symbols GUI, icons sourced from
  `assets/favicon/`, stored Settings, record/stop/finalize UX, click/clack
  and all verified video features.

## Fact-checked GitHub release status at this checkpoint

GitHub already has a **published prerelease**:
https://github.com/masarray/arssyut/releases/tag/v0.1.0-rc.1

Assets (observed on the actual release, not invented):
1. `Arssyut-Setup-0.1.0-rc.1-win-x64.exe` — Inno installer (~34.8 MB).
2. `Arssyut-0.1.0-rc.1-portable-win-x64.exe` — single standalone GUI EXE (~105.3 MB), **NOT ZIP**.
3. `SHA256SUMS.txt`.

Exact download URLs:
- https://github.com/masarray/arssyut/releases/download/v0.1.0-rc.1/Arssyut-Setup-0.1.0-rc.1-win-x64.exe
- https://github.com/masarray/arssyut/releases/download/v0.1.0-rc.1/Arssyut-0.1.0-rc.1-portable-win-x64.exe

**This is a preview, not stable.** Do not claim no release exists, but also
do not mislabel prerelease as final stable. Read SHA256SUMS and check the actual
assets, target/source SHA, PE icons, installer and portable launch before
promoting a stable release. A Release asset's existence alone is not full
hardware acceptance.

## Open PR safety

**PR #102**: "Release v0.1.0: Windows Setup EXE + portable single EXE,
dynamic download sync", was found **Draft and non-mergeable**, with head
`69a3920404e7343cc7e1ef7f08878666572cb855` and an older base
`1f47de943826b6354e9e182fea459405b29143e0`.
The repo `main` has since advanced. **Do not merge PR #102 blindly**,
reset `main`, or replay obsolete audio/README changes. Inspect the diff,
rebase or reconcile only missing work onto the current tested `main`, and
close or document superseded PRs appropriately.

## Current files and release path

- `README.md` — user-facing features, current release/download guidance.
- `index.html` — landing page hero and feature grid; `#home-installer-download`.
- `download/index.html` — installer and portable buttons, FAQ, step-by-step.
- `assets/releases.js` — pulls GitHub releases dynamically using
  `https://api.github.com/repos/masarray/arssyut/releases?per_page=20`,
  filters actual GitHub asset URLs and published releases, then binds
  Installer/Portable from the **same selected release**.
- `assets/site.css` — responsive user-facing visual system.
- `.github/workflows/pages.yml` — GitHub Pages deployment.
- `.github/workflows/windows-release-exes.yml` — exact-commit release
  pipeline; produces two EXEs from the same audio-enabled GUI payload,
  SHA256SUMS, and optionally publishes a labeled prerelease.
- `packaging/windows/Arssyut.iss` — Inno recipe.
- `assets/favicon/` — canonical app, installer and site identity.
- `tests/qa/test_public_website.py` — release link and public feature text contract.
- `docs/PUBLIC_WEBSITE.md`, `packaging/windows/README.md` — deployment and packaging instructions.

## Work still required to finish the user's request

1. **Reconcile and merge public status corrections** onto the latest
   `main`: System Audio and Microphone are implemented; webcam is not.
   Ensure `index.html`, `download/index.html`, `README.md`, release
   notes, FAQ, dynamic JS fallback and documentation never disagree.
2. **Audit downloadable artifacts:** verify the setup EXE really installs
   the audio-capable Avalonia app, creates usable shortcuts, runs without
   a console, uninstalls cleanly, and that portable is a genuine
   **single EXE**, not a ZIP, with the same embedded audio-enabled payload
   and the proper `assets/favicon/favicon.ico` icon.
3. **Verify the dynamic download contract:** both buttons must refer to
   files in the SAME newest eligible published GitHub Release. Never hardcode
   a version as the website's permanent link; don't silently fetch CI
   artifacts, source-code archives, video-only binaries or unverified mirrors.
   Display `preview` vs `stable` truthfully. Test no release, empty/malformed
   assets, newer release, stale/cached browser and API rate-limit/offline
   cases. The landing CTA must sync with the Installer on Download.
4. **Verify GitHub Pages is actually deployed.** Intended links:
   https://masarray.github.io/arssyut/
   https://masarray.github.io/arssyut/download/
   The GitHub Pages API was not accessible through the current connector.
   Inspect Pages Actions logs and browse both published pages in a real
   browser. If Pages is not enabled, guide the owner to
   Settings → Pages → Build and deployment → Source: GitHub Actions
   and run the existing workflow. **Do not claim 'live' without evidence.**
5. **Run cheap QA first:** static HTML link checks, release selection JS
   tests with mocked release listings, README/landing consistency checks,
   lint/validation, existing native and Avalonia regression. Avoid
   full static-FFmpeg builds for text-only corrections. Build installer
   only when packaging or release binary changes require it.
6. **Verify acceptance before stable promotion:** real Windows launch for
   both EXEs, native System/Mic toggles and meter activity, Mic-only,
   System-only, dual-audio MP4 and volume/mute, duration and A/V sync,
   repeat Start/Stop, Stop/Finalize, plus clean install/uninstall. The
   user reports audio completed—do not reopen implementation without
   evidence of an actual regression. Capture explicit pass/fail evidence;
   no invented QA result or unverified claims.
7. **Complete release and documentation:** if stable criteria are met,
   publish a real GitHub Release with synchronized Installer EXE +
   Portable EXE + SHA256SUMS and human-friendly release notes, verify both
   public URLs download correct binaries, and confirm automatic
   website update. Otherwise leave the current preview correctly
   labelled and report the *specific* remaining blocker, without
   claiming stable or creating duplicate release assets.

## Definition of Done

- `main` contains the accurate audio/webcam status, no regressions.
- Public site and README clearly distinguish complete Mic/System recording
  from unavailable webcam recording.
- GitHub Pages is visibly live and resolves all assets (or its exact,
  documented configuration blocker is reported).
- Installer link downloads actual latest release **Setup .exe**.
- Portable link downloads actual latest release **single .exe**, not a ZIP.
- Both EXEs are sourced from the same audio-enabled CI-approved binary;
  icons, SHA256SUMS and Windows smoke/acceptance validated.
- Preview/stable badges reflect GitHub release metadata truthfully.
- PR(s) merge with expected SHA + green CI. No silent regressions, no
  rebuilding on obsolete branches and no claiming unsupported results.
- Provide the final GitHub merge SHA, checked release links, website URLs,
  test results and any remaining blockers.

**Implementation request:** Execute the fixes directly via the connected
GitHub repository. Do not stop after planning; don't manufacture helper
milestones unrelated to the release. Keep the design professional, compact,
efficient, dependency-light and user-facing.
