# P6R — Recorder UX Research and Product Architecture

Date: 2026-10-03

## Why P6R exists

The first P6 pass made the recorder compact, but it still treated the product as
an engineering configuration form. Direct GUI review showed two problems:

1. the recording interaction could appear frozen/blank because the same HWND was
   being restyled into a floating recorder bar while capture exclusion was
   active;
2. the product grammar was incomplete: capture mode, recording boundary,
   pause, microphone, camera and a real settings window were not first-class.

P6R resets the UX from established desktop recorder patterns rather than
inventing another layout.

## External product evidence

### Camtasia Recorder / TechSmith

TechSmith's current recorder exposes Screen Recording, Camera, Microphone and
System Audio as primary recorder toggles. The selected recording area is
highlighted directly on the desktop and can be moved/resized; microphone and
camera use explicit device dropdowns. F9 is a record hotkey and Stop is a
separate recording action.

Reference:
- TechSmith Support, "How to Record Audio and Video with Camtasia Editor"
- https://support.techsmith.com/hc/en-us/articles/39968968889101-How-to-Record-Audio-and-Video-with-Camtasia-Editor

Product implication for Arssyut:
- capture boundary is part of the recorder workspace, not diagnostics;
- mic/camera are primary inputs, not buried future cards;
- device selection belongs next to the input or in Settings.

### Bandicam

Bandicam explicitly separates Screen Recording, Fullscreen/Rectangle, Game
Recording and Device Recording modes. Its main window exposes record/stop/pause,
speaker/microphone state and webcam controls. Its screen-recording window is a
visible rectangle over the captured area.

References:
- https://www.bandicam.com/guide/video-tutorials/
- https://www.bandicam.com/guide/user-interface/
- https://www.bandicam.com/guide/settings-video/
- https://www.bandicam.com/guide/overlay-webcam/

Product implication for Arssyut:
- Display, Window/Region and Game are distinct user intents;
- recording controls remain compact and available during capture;
- pause and input state belong on the recording toolbar.

### ScreenPal

ScreenPal treats the capture area itself as an interactive surface. It can be
moved between monitors, resized, and used with screen/webcam/both modes.
Pause/Resume and Draw/Zoom are explicit recorder actions.

References:
- https://support.screenpal.com/portal/en/kb/articles/record-on-a-2nd-monitor
- https://support.screenpal.com/portal/en/kb/articles/keyboard-shortcuts-v2-0
- https://screenpal.com/tutorial/recorder-drawing-tools-overview

Product implication for Arssyut:
- the record boundary must remain visible and spatially meaningful;
- ArZoom can expose the current camera viewport through the same boundary
  surface instead of hiding that state inside the compositor.

### AnyRec

AnyRec exposes Show recording boundary, floating recording panel, Output, Sound,
Mouse and recorder preferences as distinct settings concepts.

References:
- https://www.anyrec.io/screen-recorder/guide/
- https://www.anyrec.io/downloads/screen-recorder/

Product implication for Arssyut:
- Settings must be a dedicated categorized window;
- boundary visibility and floating-panel behavior are user preferences.

### OBS Studio

OBS documents Display Capture, Window Capture, Game Capture, Video Capture
(webcam/capture device), microphone/desktop audio and explicit device selection
as separate source capabilities.

References:
- https://obsproject.com/kb/quick-start-guide
- https://obsproject.com/kb/sources-guide
- https://obsproject.com/kb/game-capture-source
- https://obsproject.com/kb/video-capture-sources

Product implication for Arssyut:
- Display, Window, Game, Audio and Camera need separate backend ownership even
  when presented through a simpler recorder UX.

## P6R product grammar

### Main recorder

The main window is a preparation surface, not the active recording controller.

It contains:
- Capture Mode: Display / Window / Region / Game;
- target selector relevant to the chosen mode;
- System Audio toggle;
- Microphone toggle + real device selector;
- Camera toggle + real device selector;
- compact REC action;
- disabled Pause affordance until pause timeline support lands;
- one Settings button.

### Capture boundary

A topmost click-through boundary represents the area that contributes to output.

For Display/Window at camera zoom 1.0 it matches the source bounds.

During Smart Zoom, the boundary maps the exact compositor camera semantics:

    viewport_width  = source_width  / camera_zoom
    viewport_height = source_height / camera_zoom

centered on camera_center_x / camera_center_y and clamped to source bounds.

The boundary is visual UX only; it does not become a second camera authority.
The values come from RecorderSnapshot, which is published by the same
PresentationFrameState used by the compositor.

### Recording toolbar

The active recording toolbar is a separate top-level overlay, not the main
window restyled in place.

Controls:
- engine state;
- elapsed time;
- Pause icon;
- Microphone icon;
- Camera icon;
- Stop icon.

The toolbar and boundary can be excluded from capture without applying capture
exclusion to the main application window.

### Settings window

Dedicated categories:
- General;
- Recording;
- Output;
- Sound;
- Camera;
- Mouse & Keystroke;
- Hotkeys.

This is configuration only. It does not create a second recorder/session state.

## Backend truth

P6R intentionally separates UX availability from backend readiness.

Already authoritative:
- Display capture;
- Window capture;
- Smart Zoom/click/key visualization;
- Pixel/Clean/Vivid visual modes;
- H.264 output.

UX implemented, backend milestone still required:
- custom Region crop/selection;
- dedicated Game capture backend;
- system audio;
- microphone capture;
- webcam composition;
- pause/resume timeline.

Until those backends land, P6R must not silently pretend they are recorded.
The UX preview blocks a recording attempt when an unsupported enabled input or
capture mode is selected.

## Freeze correction

The first P6 pass changed the main window style between overlapped and popup
inside the UI timer and joined the recorder worker from the UI completion path.

P6R removes both patterns:
- main HWND is never morphed into the toolbar;
- the toolbar is a separate overlay;
- the UI timer never blocks on thread join;
- RecorderSnapshot exposes worker completion and camera viewport state;
- completed worker join is deferred to the next safe start/close path.

This keeps the message pump responsive while recording/finalizing.

## Roadmap after P6R UX acceptance

### P6R.1 — interaction reliability
- no blocking UI join;
- separate toolbar/boundary overlays;
- F9 global record/stop;
- real device enumeration.

### P6R.2 — custom region backend

Implemented after direct GUI review:
- an editable topmost Region boundary with native move and resize hit targets;
- Region geometry stored in virtual-screen coordinates rather than local window
  coordinates;
- canonical monitor-space -> source-pixel CropRect mapping;
- native-size Region output with even-dimension NV12 alignment;
- presentation/cursor normalization against the same Region rect;
- event-driven idle editor so UI polling cannot snap the selector back.

Future refinement after real acceptance:
- optional aspect-lock/preset sizes;
- keyboard nudge/resize parity with mature rectangle recorders;
- automatic target/window snapping.

### P6R.3 — audio backend
- WASAPI system loopback;
- microphone endpoint capture;
- device selection from the P6R UI;
- independent mute/gain;
- A/V clock/sync diagnostics.

### P6R.4 — webcam backend
- Media Foundation camera source;
- selected device from P6R UI;
- retained GPU texture path;
- move/resize/shape PiP;
- no CPU full-frame composition.

### P6R.5 — pause/resume
- explicit Paused recorder state;
- canonical media-time continuity;
- no timestamp gap/drift;
- toolbar + hotkey support.

### P6R.6 — game capture
- dedicated Windows game-capture backend decision/ADR;
- efficient DirectX/OpenGL game path where technically supported;
- safe WGC/window fallback;
- game/window identity and failure handling.

P6R is not complete until the new GUI is visually accepted and the interaction
freeze regression is reproduced as fixed.


## P6R.2 visual-system and overlay follow-up

Direct GUI acceptance on 2026-10-03 rejected the mixed native-gray/owner-draw
appearance. The follow-up standardizes the recorder shell around compact dark
surfaces, restrained red recording/selection accent and Lucide-derived vector
geometry rendered natively at runtime.

The recording toolbar also moved its status/timer to one buffered parent paint
surface and caches unchanged state. Capture cadence is no longer allowed to
force full toolbar repaint work.

External evidence used for this revision:
- Bandicam Rectangle mode: moveable/resizable capture window, target selection,
  size presets and recording controls;
- ScreenPal: editable recorder boundary and explicit screen/camera modes;
- OBS: separate Display/Window/Game/Video Capture source concepts;
- Microsoft Win32: layered windows for smoothly composed shaped/animated
  overlays and UxTheme buffered painting for off-screen paint/commit.
