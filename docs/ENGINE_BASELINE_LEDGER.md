# Arssyut Engineering Baseline Ledger

This document is the historical authority map for future work.

It exists to prevent a recurring failure mode in which a later milestone
reimplements a subsystem that was already accepted, loses behavior, and then
spends another milestone recovering it.

Before changing an existing subsystem, read this ledger and the commits it
points to.

## 1. Repository lineage

### Accepted production baseline before P6

Main is intentionally frozen at:

`5c3684b6ce0dcd1037eb35c60efce17294bb6bae`
— **Lock P5E real visual acceptance baseline (#29)**.

This baseline incorporates the accepted recorder engine progression:

- PR #8 — native GUI + WGC + real H.264 MP4 vertical slice;
- PR #12 — valid DXGI sample length to Media Foundation;
- PR #14 / `fb45166ec1c8...` — P3R full ArZoom camera/click parity recovery;
- PR #20 / `b366b1c18559...` — P4R.3 native cursor reliability lock;
- PR #27 / `04d8c0814143...` — authoritative SDR BT.709 / NV12 color contract;
- PR #28 / `ee19ebffa8fe...` — screen-native ArVisual;
- PR #29 / `5c3684b6ce0d...` — real-visual P5E acceptance lock.

These are not suggestions. Later UI work must not retune or replace their
authorities incidentally.

### P6R native recorder workspace baseline

The frozen P6R branch is:

`milestone/p6r-native-functional`

at:

`b11451bd640a072d81dbd1024b4c641a76c9daac`

This branch is 73 commits ahead of the P5E main baseline and is the canonical
native recorder-workspace reference for P6UI.

### P6UI presentation branch

The active product presentation branch is:

`feat/p6ui-avalonia-shell`

It starts from the exact P6R native baseline above. P6UI is allowed to evolve
the presentation layer aggressively while preserving native authorities.

## 2. Native subsystem authority map

| Subsystem | Authority | Baseline / evidence | Rule |
| --- | --- | --- | --- |
| Capture / WGC | native C++ | P1 + PR #8 | never reimplement capture in Avalonia |
| CFR / media clock | native C++ | P1/P2 recorder session | UI observes state only |
| H.264 / MF / MP4 | native C++ | PR #8, #12, P5D.7 | UI never owns encode/finalize |
| ArZoom camera | native C++ | P3R `fb45166...` | no second camera/viewport solver |
| Click/keycap | native C++ compositor | P4R/P4R.3 | Avalonia config only |
| Native cursor | WGC | P4R.3 `b366b1c...` | do not add custom cursor compositor |
| ArVisual | native C++ | P5A-P5E, locked at `5c3684...` | do not retune during UI work |
| Source catalog | native C++ | `src/app/source_catalog.*` | Avalonia source enumeration is temporary preview only |
| Capture boundary | native `RecorderOverlay` | P6R | native is final authority |
| Smart Zoom boundary | native Region/viewport geometry | P6R.2 | must use PresentationFrameState camera values |
| Region crop | native C++ | P6R.2 geometry + compositor crop | fix the existing path; never create a second crop pipeline |
| Recording toolbar state | native session state | P6R nonblocking worker state | Avalonia controller becomes a projection via bridge |
| Settings/product visuals | Avalonia | P6UI | may evolve aggressively without engine changes |

## 3. P6R history that must be reused

P6R was not one monolithic experiment. The following sequence created useful
native infrastructure that P6UI must consume rather than rewrite.

### Boundary and recording overlay

- `2fc4041457db...` — add recorder toolbar and capture-boundary overlay;
- `113890291284...` — implement capture boundary and recording toolbar;
- `c541fb5ad883...` — expose target screen geometry;
- `f79503860d6e...` — resolve monitor/window boundary geometry;
- `8666cc23c28a...` — keep overlays independent of hidden main window;
- `f98411dde5de...` — single buffered recording-toolbar paint surface;
- `99630bb5e86b...` — cache boundary geometry;
- `8fea645999f3...` — eliminate redundant boundary repaint/window churn.

The accepted direction is a separate native overlay, not morphing the main
window and not a full-screen Avalonia input surface.

### Smart Zoom boundary synchronization

- `b8e261e760ac...` — expose deterministic camera viewport geometry;
- `2582eff1af40...` — centralize Smart Zoom boundary geometry;
- `8276d9cfe19b...` — use tested viewport geometry for capture boundary;
- `e3aabeba2b2a...` — regression test 2x contraction and edge clamping;
- `1e9adb86ca4a...` — route Region recording boundary through shared helper.

This path already made the visible recording rectangle shrink/move with the
same camera values consumed by the compositor. It must be bridged, not
reimplemented.

### Region implementation

- `9d62d5c1054f...` — persist Region editor state;
- `45e62ca657c5...` — extend native overlay for interactive Region editing;
- `17d46a90ec22...` — interactive Region boundary;
- `d9ce46d5c4d0...` — carry canonical Region crop into RecorderConfig;
- `98b10a2bddfe...` — render selected crop without a second capture path;
- `d367e3540d39...` — editable Region records actual crop;
- `c925f0bcf362...` — event-driven idle Region editing;
- `d2c5468999a7...` — canonical Region geometry contract;
- `c381cd05101f...` — virtual-screen Region mapping;
- `ade2f7eef919...` — negative-origin/crop mapping tests;
- `3eca4749c1d8...` / `fb1eb91b07b6...` — route through tested geometry + CI;
- `b11451bd640a...` — boundary pixel-identical to encoded NV12 crop.

**Important acceptance reality:** the code path exists, but real GUI acceptance
did not prove custom Region move/resize sufficiently. Therefore Region is
classified as **existing implementation requiring focused correction**, not as
a missing subsystem to rewrite.

## 4. Historical recovery lessons

The project history already documents the cost of replacement-by-assumption.

### P3 -> P3R

The first P3 integration existed but did not preserve accepted ArZoom behavior.
P3R recovered parity by pinning the upstream authority and using a thin adapter.

Lesson: **bind to the accepted authority; do not create a simplified parallel
implementation.**

### P4R.2 -> P4R.3

P4R.2 introduced a custom cursor compositor. Real validation showed native WGC
cursor behavior was more reliable. P4R.3 removed the duplicate cursor
authority.

Lesson: **one authority is better than a visually ambitious duplicate.**

### P6R -> P6UI boundary duplication

P6R already owns capture-boundary geometry and Smart Zoom synchronization.
P6UI.3A temporarily recreated boundary/source behavior in Avalonia for visual
acceptance, causing a real two-monitor input-blocking regression.

Lesson: **the Avalonia shell must project native boundary/source state rather
than becoming a second source/boundary engine.**

## 5. P6UI forward-only rules

The following rules apply from this audit onward.

1. **No engine rewrite for UI convenience.**
   P6UI changes presentation, layout, typography, materials, motion,
   accessibility and interaction grammar. Existing capture/media/compositor
   algorithms remain native.

2. **No duplicate authorities.**
   There must be only one authoritative source catalog, one Region rect, one
   camera viewport, one media clock and one capture boundary.

3. **Reuse before replacement.**
   Before writing new code for a behavior that existed in P6R, inspect the P6R
   commit/file first. If the native behavior is correct, bridge it.

4. **Fix Region in place.**
   Custom Region correction must start from `region_geometry.*`,
   `RecorderOverlay`, and the existing RecorderConfig crop path. Do not add a
   second Region capture implementation.

5. **Avalonia may move fast visually.**
   `src/ui/Arssyut.UI` may be redesigned aggressively as long as it does not
   become an engine authority.

6. **Temporary preview code has an expiry.**
   `SourcePreviewCatalog.cs` and `CaptureBoundaryWindow.*` are P6UI
   acceptance scaffolding only. They may receive bug fixes needed to keep the
   preview usable, but no new capture/camera/crop algorithms. P6UI.4 must
   replace their authority with the native bridge and then retire them.

7. **Native drift is CI-gated.**
   During P6UI.1-P6UI.3 work, commits after
   `b11451bd640a072d81dbd1024b4c641a76c9daac` must not modify native
   `src/app`, `src/core`, or `src/platform` implementation files.

8. **Bridge milestones are explicit.**
   Native modifications become legal again only inside P6UI.4+ and should be
   additive bridge/API work first. Existing accepted engine files require a
   reproduced bug, a narrow reason and regression coverage.

## 6. Forward plan

The next progression is intentionally monotonic:

```text
P6UI.3 visual acceptance
    |
    | no native changes
    v
P6UI.4A read-only native bridge
    source catalog / devices / session snapshots / configured hotkeys
    |
    v
P6UI.4B command bridge
    record / stop / config mutations through native authority
    |
    v
P6UI.4C native overlay bridge
    selected target -> RecorderOverlay
    PresentationFrameState -> Smart Zoom boundary
    existing Region editor -> canonical Region rect
    |
    v
remove Avalonia source/boundary authority
    |
    v
P6UI.5 floating controller bound to real session
```

UI polish does not have to wait for these bridge steps, but engine behavior
must always follow this ownership order.

## 7. Change-review checklist

Before modifying an existing subsystem:

- Which commit/PR currently owns this behavior?
- Is the requested behavior missing, or merely not exposed in Avalonia?
- Can the existing authority be bridged instead of rewritten?
- Is there real acceptance evidence for the current behavior?
- What deterministic gate prevents regression?
- Does the proposed code create a second source/camera/crop/timeline authority?

If the last answer is yes, redesign the change before coding.
