# ADR-008 — Avalonia to native recorder bridge

Status: Accepted for P6UI.4

Date: 2026-10-04

## Context

The P6R native recorder already owns capture targets, Region geometry, the
recorder session, ArZoom, ArVisual, encode/mux, diagnostics and native recording
overlays.

P6UI replaces presentation with Avalonia. A temporary UI preview duplicated
source enumeration and capture-boundary behavior in C#, which caused a real
multi-monitor input-blocking regression. The bridge therefore must expose
existing native authorities rather than reconstructing them in managed code.

## Decision

Use one versioned in-process C ABI implemented by
`arssyut_native_bridge.dll`.

The bridge uses:
- one opaque native context per Avalonia application lifetime;
- one explicit ABI version;
- fixed-layout POD snapshots;
- opaque generation-scoped 64-bit source/device tokens;
- fixed-capacity UTF-16 presentation labels;
- status-code returns across the ABI boundary;
- no exceptions, STL containers, C++ classes or ownership pointers crossing the
  ABI;
- no JSON serialization or filesystem polling for live state.

The context owns the single `RecorderSession` instance used by Avalonia.

P6UI.4A exposed this context read-only. P6UI.4B extends the same ABI to:
- resolve generation-scoped source tokens inside native code;
- start Display/Window video recording through the existing `RecorderSession`;
- request Stop without blocking the UI thread;
- project Preparing/Recording/Stopping/Finalizing/Ready/Failed state back to
  Avalonia;
- expose final output and diagnostics paths;
- canonicalize frame rate, visual product mode, Smart Zoom, click and shortcut
  settings into `RecorderConfig`.

The bridge deliberately rejects Region until P6UI.4C can bind the existing
native Region editor/crop authority. Game remains unsupported until its
dedicated backend exists. System audio, microphone and camera flags are also
rejected while their real media backends are not wired. The UI must surface
that status rather than pretending those streams were recorded.

Native snapshots remain canonical:
- sources come from `enumerate_recorder_targets()`;
- source rectangles come from `recorder_target_screen_rect()`;
- microphones/cameras come from the existing native device catalog;
- recorder state comes from `RecorderSession::snapshot()`.

## Token policy

Avalonia never receives HWND, HMONITOR, camera symbolic links or microphone
endpoint IDs as authorities.

Refresh stores the full native objects in the bridge context and returns an
opaque token consisting of a snapshot generation plus item index.

A future command must resolve the token inside the same native context and
reject stale generations. C# must never rebuild a `RecorderTarget` from label
text or screen geometry.

## Command/lifecycle policy

The command bridge never creates a second recorder state machine.

Rules:
- one bridge context owns at most one native `RecorderSession`;
- a new start may replace a previous session only after
  `worker_finished=true`;
- cleanup `wait()` happens only for an already-finished worker and never while
  holding the bridge mutex;
- Stop only publishes `request_stop()`; Avalonia continues polling the native
  snapshot while finalization runs;
- opaque source tokens are resolved in native code and stale generations are
  rejected explicitly;
- expected unsupported features return structured status codes instead of
  falling back to another backend;
- output/result paths come from the same native session configuration that
  produced the recording.

## Overlay and Region policy

P6UI.4A retires the duplicate Avalonia source enumerator and capture-boundary
window.

Capture boundary, Smart Zoom viewport synchronization and Region editing remain
owned by the P6R native `RecorderOverlay` / Region geometry path. They are
reintroduced to the Avalonia product through an explicit native overlay bridge
milestone, not a second C# implementation.

## Failure behavior

If the bridge DLL is missing, incompatible or cannot initialize:
- the Avalonia shell may still launch for design/development scenarios;
- source lists remain unavailable rather than silently falling back to another
  enumeration authority;
- CI uses `--bridge-required` so packaged builds fail smoke validation when
  the bridge is absent or ABI-incompatible.

## Consequences

Positive:
- one recorder authority survives the UI migration;
- source handles and device IDs never need lossy managed reconstruction;
- command bridge work can extend the same context incrementally;
- no polling files/JSON and no duplicate capture engine;
- bridge ABI can be regression-tested independently from Avalonia visuals.

Costs:
- the native bridge DLL must be packaged beside the Avalonia executable;
- C ABI structs require explicit version/size discipline;
- P6UI.4A intentionally removes the temporary Avalonia boundary before native
  overlay integration is complete.

## Rejected alternatives

### JSON over files/pipes

Rejected for first-party in-process UI because it adds serialization,
synchronization and schema ambiguity without an isolation requirement.

### Expose C++ classes directly

Rejected because C++ ABI/runtime ownership is not a stable managed interop
contract.

### Keep Avalonia source/boundary implementations

Rejected because they create duplicate authorities and already caused a real
input regression.
