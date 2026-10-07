# P6UI.4B-A — Real Recording Acceptance Lock

**Branch:** `feat/p6ui-avalonia-shell`  
**Parent checkpoint:** `milestone/p6ui4b-command-bridge-baseline`  
**Engine authority:** frozen P6R native implementation  
**Purpose:** harden the real Avalonia -> native command path before P6UI.4C.

This milestone is intentionally narrow. It does not add a new recorder feature
or rewrite any accepted P5/P6R authority.

## 1. Automated lock

The branch must satisfy all of these before real Windows acceptance:

- protected `src/app`, `src/core` and `src/platform` trees remain unchanged
  from `b11451bd640a072d81dbd1024b4c641a76c9daac`;
- native bridge build/tests remain green;
- Avalonia interaction regression remains green;
- packaged `--bridge-required` launch smoke remains green;
- floating-controller launch smoke remains green;
- the controller's real HWND reports `WDA_EXCLUDEFROMCAPTURE` or the bounded
  `WDA_MONITOR` compatibility fallback;
- Settings controls without a current recording authority are disabled or
  explicitly staged instead of silently accepting no-op choices.

## 2. Product authority matrix

| Product intent | P6UI.4B-A behavior |
| --- | --- |
| Display / Window source | native source token + native RecorderSession |
| 30 / 60 fps | native RecorderConfig |
| Pixel Accurate / Clean Screen / Vivid Presentation | native ArVisual mode |
| Smart Zoom | native presentation config |
| Click visualization | native presentation config |
| Shortcut visualization | native presentation config |
| Output folder | native output path construction |
| Record / Stop | native bridge command |
| Finalizing / Saved | native RecorderSession snapshot |
| Open / Folder | actual native result path |
| Floating controller capture visibility | Windows capture-excluded |
| Region | blocked until existing native P6R Region bridge |
| Game | explicit unsupported |
| System audio / microphone / camera | explicit unsupported |
| Pause / Resume | disabled until explicit native paused-state milestone |

## 3. Settings truthfulness rule

A polished UI is not allowed to imply backend capability that does not exist.

During this milestone, fixed or unavailable choices are visibly locked. This is
not a product regression; it prevents preview-era controls from becoming false
product promises while the real native authorities are connected phase by
phase.

Do not implement a second backend just to make a disabled control clickable.

## 4. Real Windows evidence matrix

CI cannot substitute for these tests. Record evidence against the exact PR-head
artifact.

| # | Acceptance | Evidence | Result |
| --- | --- | --- | --- |
| 1 | Display 1 records and MP4 opens | output path + playback | pending |
| 2 | Display 2 records on real multi-monitor desktop | output path + playback | pending |
| 3 | Window source records | output path + playback | pending |
| 4 | Record -> Stop -> Stopping -> Finalizing -> Saved remains responsive | observation | pending |
| 5 | Open action opens the produced MP4 | observation | pending |
| 6 | Folder action selects the produced MP4 | observation | pending |
| 7 | repeated start/stop is stable | repeated runs | pending |
| 8 | 30 fps and 60 fps both record | diagnostics + playback | pending |
| 9 | all three visual modes record | three outputs | pending |
| 10 | Smart Zoom/click/shortcut intent reaches native recorder | diagnostics/visual evidence | pending |
| 11 | audio/mic/camera intent is rejected explicitly | UI status | pending |
| 12 | floating controller is absent from encoded Display output | playback | pending |

## 5. Closure rule

P6UI.4B-A may be called **implementation-complete** when automated gates are
green.

It may be called **accepted** only after the real Windows evidence matrix above
passes. Until then, P6UI.4C work may be prepared, but any reproduced P6UI.4B-A
recording defect has priority and must be fixed without replacing accepted
native authorities.

## 6. Next milestone

After acceptance, continue with **P6UI.4C — native overlay / Region bridge**:

```text
Avalonia selected source
        |
        v
existing native bridge context
        |
        v
P6R RecorderOverlay
        +--> Display / Window boundary
        +--> PresentationFrameState -> Smart Zoom boundary
        +--> existing Region editor -> canonical Region rect
                                      -> RecorderConfig.crop
```

No Avalonia capture-boundary window, C# crop math, second capture path, or
source identity reconstruction is permitted.
