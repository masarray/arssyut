# P7A external audio research — OBS / WASAPI / resampler lessons

**Date:** 2026-10-07  
**Purpose:** refine P7A architecture before native implementation.  
**Scope:** research + strategy only; no runtime audio code.

## 1. Why this review exists

P7A already locked the core principles: one RecorderSession clock, bounded
event-driven sources, explicit resampling, deterministic mixing and one AV
writer authority.

This research pass compares those decisions with production patterns visible in
OBS Studio, Microsoft WASAPI/Media Foundation contracts and FFmpeg
libswresample behavior. The goal is not to clone OBS; it is to identify mature
failure handling that Arssyut should either adopt, adapt or deliberately reject.

---

## 2. OBS patterns worth keeping

### Global audio-domain normalization

OBS exposes one audio output configuration with sample rate / sample format /
speaker layout. Sources carry their own frame count + timestamp and are
resampled into the output domain when required.

Arssyut conclusion:

- keep one resolved AudioProfile/program bus;
- keep per-source native format explicit;
- normalize only at the source -> canonical bus boundary;
- avoid source-specific hidden output clocks.

### Timestamp-driven source alignment

OBS source audio carries explicit timestamps. Central audio rendering aligns
sources by those timestamps rather than assuming packet arrival order is
equivalent to media time. OBS can mark sources pending, add bounded buffering,
discard stale material and restart source timing after unrecoverable lag.

Arssyut conclusion:

- alignment belongs in one central AudioTimeline/Mixer authority;
- WASAPI producer arrival time is not media time;
- late/missing media must remain visible as timeline gaps;
- source queues may absorb a small measured scheduling window, but may never
  grow with recording duration.

### Resampler owns timing delay

OBS wraps a resampler that reports timestamp offset. FFmpeg libswresample also
exposes internal delay through `swr_get_delay()` and supports soft
compensation.

Arssyut conclusion:

- any P7A resampler contract must expose consumed frames, produced frames and
  internal/group delay;
- output PTS cannot simply equal the input packet PTS after a stateful
  resampler;
- drift correction must account for resampler phase/delay, not only change a
  nominal ratio.

### Device-timing skepticism

Current OBS WASAPI code supports `use_device_timing`. It observes
`AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR`; for non-process sources it can choose
between WASAPI device timing and a system-time-derived estimate.

Arssyut conclusion:

- device QPC timestamps are preferred evidence, not unquestionable truth;
- timestamp-error packets must not feed the drift estimator as trusted clock
  samples;
- the audio packet model needs a timestamp-confidence/source field.

### Event worker + multimedia scheduling

OBS uses an event/signal-driven WASAPI capture path and places its capture
thread in Windows MMCSS using the `Audio` task.

Arssyut conclusion:

- P7A capture workers use event-driven packet drain;
- Windows audio capture threads register with MMCSS `Audio`;
- use ordinary `Audio`, not `Pro Audio`, unless measurement proves the
  normal task cannot meet endpoint servicing deadlines;
- always call `AvRevertMmThreadCharacteristics` during deterministic teardown.

### Device invalidation is normal runtime state

OBS detects invalidated endpoints and has reconnect machinery.

Arssyut conclusion:

- device loss is not an exception/crash;
- initial P7A must never silently switch to a different endpoint;
- same-identity reactivation may be attempted asynchronously while the mixer
  contributes silence, but only when endpoint identity and negotiated format
  remain compatible;
- if the same endpoint cannot recover, the source remains unavailable and the
  session remains timestamp-monotonic.

### Audio source duplication / monitoring is complex

OBS has explicit logic to avoid duplicate audio when monitoring and capture use
the same path.

Arssyut conclusion:

- P7A intentionally has no audio monitoring/output path;
- do not introduce monitoring while building capture;
- excluding monitoring prevents feedback/double-capture complexity from entering
  the first production audio milestone.

---

## 3. OBS patterns we should not copy blindly

### Dynamic/general-purpose scene audio graph

OBS supports many source types, multiple mixers/tracks and scene graph routing.
Arssyut currently needs two product sources and one program AAC track.

Decision:

- use a small fixed source-id model for Microphone + System Audio;
- keep data structures extensible but do not import a general scene-audio graph;
- no six-mixer/multi-track abstraction in P7A.

### Large adaptive source buffering

OBS supports dynamic source buffering because arbitrary sources/plugins can
arrive with very different timing.

Arssyut knows both sources are local WASAPI endpoints.

Decision:

- use a small fixed-capacity timestamp alignment window derived from observed
  endpoint period + scheduler jitter;
- diagnostics expose actual high-water;
- never increase buffer size automatically to hide a timing bug.

### Automatic endpoint replacement

A mature broadcast application may retry/reconnect aggressively.

Decision:

- P7A may reactivate the **same selected endpoint identity** after invalidation;
- it must never move to another physical endpoint without explicit user/session
  policy;
- a Windows default-device change is observed and logged but applies to the
  next session unless the source was explicitly configured as a dynamic
  “follow default” mode in a future milestone.

### Silent-render workaround for loopback

OBS historically contains a silent-loopback workaround to prevent some output
streams from stopping during silence.

Decision:

- Arssyut's mixer clock must generate timeline silence even when loopback
  produces no packet;
- P7A should not play artificial silence into the user's render endpoint merely
  to keep capture packets flowing;
- add a no-playback/silence acceptance fixture first; only add an OS workaround
  if current supported Windows builds reproduce a real failure.

---

## 4. Timestamp confidence model

P7A source packets should classify timestamp evidence.

Suggested conceptual enum:

```text
AudioTimestampQuality
  TrustedDeviceQpc
  FrameCountExtrapolated
  ArrivalEstimated
  Discontinuous
```

### TrustedDeviceQpc

Use when WASAPI provides a valid QPC/device position without
`AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR`, and monotonic/plausibility checks pass.

This is the only tier used as direct measurement evidence by the drift
estimator.

### FrameCountExtrapolated

If a packet timestamp becomes unreliable but the source has a prior trusted
anchor and uninterrupted frame sequence, derive position from accumulated
source frames.

This preserves pitch/time without letting scheduling jitter become a fake clock.

### ArrivalEstimated

If there is no usable device timestamp/anchor, estimate packet-start time from:

`monotonic_read_time - packet_duration`.

This is continuity/failure-handling evidence, not precise drift-estimator input.

### Discontinuous

Use when Windows reports discontinuity or packet/frame continuity cannot be
proven.

The mixer inserts/holds silence according to master time and re-anchors only
when trustworthy evidence returns.

Rules:

- never silently promote estimated timestamps to trusted;
- every transition between quality tiers is counted in diagnostics;
- timestamp quality is per source, not global.

---

## 5. Endpoint identity and roles

Friendly name is display text, never identity.

Persist/transport:

- runtime endpoint ID;
- Windows 11 24H2+ `PKEY_AudioEndpoint_StableId` when available;
- friendly name separately.

Stable ID is opaque and case-sensitive; re-query mutable properties after it
resolves.

Fallback:

- if StableId is unavailable, use normal endpoint ID for the current product
  baseline and handle stale resolution explicitly.

Default roles must also be explicit:

- System Audio default: render endpoint, `eConsole`;
- Microphone default: capture endpoint policy documented explicitly (initially
  `eCommunications` only if that matches the product UX; otherwise use
  `eConsole`/configured product role consistently).

Do not let “Default” mean different role semantics in device enumeration and
recording activation.

---

## 6. Alignment/jitter policy

Add a small fixed `AudioSourceTimeline` between each source ring and the
mixer.

It tracks:

- earliest available canonical PTS;
- latest available end PTS;
- contiguous frame span;
- timestamp confidence;
- endpoint period;
- queue high-water.

Mixer consumes by master block interval, not packet arrival.

For every output interval:

- data covers interval -> resample/mix exact overlap;
- source starts late -> silence before source;
- source has a real gap -> silence in gap;
- packet arrives after its interval has already closed -> count late packet and
  discard stale media; never move it to a later time;
- source gets far ahead -> bounded ring absorbs only the measured window;
  overflow is a hard diagnostic fault.

The alignment-window capacity is chosen from endpoint-period and real scheduling
measurements during P7A2/P7A3, then locked. No adaptive growth.

---

## 7. Resampler contract refinement

A production resampler interface must report:

```text
input_frames_consumed
output_frames_produced
delay_in_input_or_common_timebase
effective_ratio
phase/remainder state
```

Requirements:

- no steady-state allocation;
- support explicit flush at stop;
- group delay included in PTS math;
- normal drift correction is soft/rate-based;
- no hard inject/drop except for an explicit real discontinuity policy;
- synthetic impulse/tone tests verify both frequency response timing and
  timestamp alignment.

Implementation choice (custom, Windows API, libswresample or another library)
is **not** selected by this document. #65/#69 should compare quality,
dependency, licensing, fixed-buffer support and drift-compensation semantics
before choosing.

Do not implement a linear-interpolation “temporary” resampler.

---

## 8. Worker scheduling refinement

Per active source worker:

- COM MTA initialized on its worker thread;
- MMCSS `Audio` registration;
- wait on stop + audio event (+ explicit reactivation signal if implemented);
- on audio event, drain all currently available WASAPI packets;
- never sleep/poll while active;
- no high-priority busy loop;
- revert MMCSS and uninitialize COM during teardown.

The mixer worker should not automatically inherit the same MMCSS class. Its
priority is selected from measurement. Capture servicing is more deadline
sensitive than non-blocking mix preparation.

---

## 9. Same-device recovery policy

P7A becomes slightly smarter than the initial architecture lock while remaining
deterministic.

On `AUDCLNT_E_DEVICE_INVALIDATED` or endpoint removal:

1. mark source unavailable;
2. stop trusting source clock;
3. mixer supplies master-timeline silence;
4. release invalid endpoint objects;
5. listen for endpoint notifications / retry the **same identity** with bounded
   backoff;
6. re-query format;
7. only resume if identity policy and format-transition policy accept it;
8. establish a new timestamp anchor;
9. expose recovery count/downtime in diagnostics.

Never:

- choose another endpoint by friendly-name match;
- silently jump to a newly selected Windows default endpoint;
- shift the program timeline when the source returns.

If reactivation cannot meet the defined compatibility contract, remain silent
until session stop.

---

## 10. New diagnostics from this research

Add to P7A diagnostics design:

Per source:

- timestamp_quality_current;
- trusted_timestamp_packets;
- extrapolated_timestamp_packets;
- arrival_estimated_packets;
- timestamp_quality_transitions;
- late_packets_discarded;
- timeline_gap_frames;
- endpoint_period_frames / 100ns;
- mmcss_registered;
- invalidation_count;
- same_device_recovery_attempts;
- same_device_recovery_successes;
- source_unavailable_duration_100ns;
- endpoint_id_kind (stable/runtime/default-role);
- resampler_delay_frames;
- resampler_flush_frames.

Mixer:

- source_alignment_lead_high_water_frames;
- source_alignment_late_high_water_frames;
- stale_source_frames_discarded.

These counters distinguish OS/device problems from Arssyut scheduling bugs.

---

## 11. Acceptance refinements

Add real/synthetic cases:

1. device timestamp error injected -> no pitch jump, estimator ignores bad
   sample;
2. source switches Trusted -> Extrapolated -> Trusted -> media remains
   monotonic;
3. delayed packet beyond closed mix interval -> counted/discarded, no time shift;
4. loopback with no playback for several minutes -> continuous silent program
   timeline without generating sound;
5. same selected endpoint unplug/replug -> silence during outage, optional
   same-identity recovery, no timeline shift;
6. Windows default endpoint changes mid-session -> active selected source does
   not silently move;
7. resampler impulse test -> group delay accounted in output PTS;
8. MMCSS registration/revert visible in debug/test hooks and no worker survives
   teardown;
9. friendly-name collision fixture -> endpoint identity remains unambiguous;
10. 96/192 kHz shared endpoint fixture -> explicit supported conversion or
    controlled rejection, never metadata/sample mismatch.

---

## 12. Research-driven implementation rule

OBS demonstrates that mature audio is not just “call WASAPI and write PCM”.
The hard problems live in:

- timestamp trust;
- source alignment;
- bounded buffering;
- resampler delay;
- device lifecycle;
- recovery;
- central mix timing.

P7A implementation should therefore remain ordered:

`core time/format contract -> independent WASAPI sources + writer -> central
timeline/mixer -> integration -> soak`.

Do not collapse these layers merely to get the first MP4 with sound.
