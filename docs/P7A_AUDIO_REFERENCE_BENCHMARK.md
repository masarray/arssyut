# P7A — External Audio Architecture Benchmark

**Status:** research refinement after P7A0 architecture lock  
**Date:** 2026-10-07  
**Baseline:** `main@c1ab4dc559c9e520c0bf195f6ca6124d43b47942`  
**Runtime code changed by this document:** none

This benchmark exists to avoid two opposite mistakes:

1. reinventing mature realtime-audio patterns that OBS/Windows/FFmpeg have
   already proven; and
2. copying OBS wholesale even where its general streaming/mixer graph would
   make a recorder-only product heavier than necessary.

The result is a narrower Arssyut-specific strategy: copy the invariants, not the
entire framework.

---

## 1. Systems reviewed

### OBS Studio / libobs

Relevant current patterns:

- global audio output runs on fixed blocks of `AUDIO_OUTPUT_FRAMES = 1024`;
- libobs represents/handles audio in floating-point mix buffers;
- source audio is converted through a dedicated resampler abstraction;
- audio sources carry timestamps and are synchronized before mixing;
- libobs has bounded maximum audio buffering and explicit “pending source”
  behavior when a source cannot yet satisfy the current output interval;
- Windows WASAPI sources are independent source objects, not special cases
  inside the video recorder;
- Windows device capture can use device timing, detects timing problems, and
  has reconnect/default-device notification machinery;
- current Windows WASAPI code is event/callback driven rather than a
  millisecond polling loop;
- OBS has a special loopback-silence workaround because some output devices can
  otherwise stop producing useful timing while silent;
- source monitoring is a separate output path;
- OBS exposes sync offset, multiple recording tracks and per-application audio
  capture as product capabilities.

OBS is deliberately general: scenes can contain many independent audio sources,
filters, monitoring routes, multiple mixes and streaming outputs. Arssyut P7A
does not need that source-graph complexity for two native recorder sources.

### Windows WASAPI / MMDevice

Key contracts:

- `IAudioCaptureClient::GetBuffer` reports frame count, packet flags, device
  frame position and a QPC timestamp already converted to 100 ns units;
- `AUDCLNT_BUFFERFLAGS_SILENT`, `DATA_DISCONTINUITY` and
  `TIMESTAMP_ERROR` are first-class evidence and must not be discarded;
- capture should drain packets promptly and release the endpoint buffer without
  excessive delay;
- shared-mode event-driven capture is supported through
  `AUDCLNT_STREAMFLAGS_EVENTCALLBACK`;
- loopback capture is shared-mode capture from a render endpoint;
- Windows 10+ supports event notification for active loopback capture;
- MMDevice notifications provide default-device, added/removed and device-state
  changes;
- MMCSS exists to protect deadline-sensitive multimedia work without using
  crude thread-priority hacks.

### Media Foundation AAC

Microsoft's native AAC encoder constrains the initial product profile:

- AAC-LC;
- PCM input must be 16-bit;
- input sample rate must be 44.1 kHz or 48 kHz;
- mono, stereo and 5.1 are supported;
- supported encoded byte rates for mono/stereo are 12k/16k/20k/24k bytes/s,
  corresponding to 96/128/160/192 kbps;
- every input sample requires valid, non-zero time and duration;
- one AAC output frame corresponds to 1024 PCM samples.

### Media Foundation Audio Resampler

Windows provides a native resampler/channel-mapper that accepts PCM or IEEE
float input and exposes configurable output quality and custom channel mapping.
It is a candidate for static format conversion.

### FFmpeg libswresample / SoXR

libswresample is used here as a design/reference benchmark, not as an already
approved dependency.

It provides:

- sample-rate conversion;
- channel mapping;
- soft resampling compensation through `swr_set_compensation`;
- timestamp-aware padding/trimming;
- an `async` mode for stretching/squeezing/fill/trim;
- optional SoXR high/very-high precision resampling.

Its compensation model is directly relevant to long-session clock-drift
correction, but adopting FFmpeg would add binary, dependency and licensing
surface that Arssyut must justify by measurement.

---

## 2. What Arssyut should adopt from OBS

### 2.1 Fixed output quantum

Adopt the same conceptual fixed 1024-frame output quantum.

Reasons:

- exactly matches one Microsoft AAC frame;
- 21.333 ms at 48 kHz is appropriate for recording and not latency-sensitive
  live monitoring;
- deterministic allocation and block accounting;
- simpler PTS/duration math;
- simpler meter cadence;
- simpler writer service deadlines;
- fewer partial-block corner cases.

WASAPI source packet sizes remain variable. Only the canonical mixer/program
output is fixed at 1024 frames.

### 2.2 Float processing domain

Keep source normalization/mixing in float32 and convert to PCM16 only at the
Microsoft AAC writer boundary.

This matches the proven float-mix approach used by OBS while preserving explicit
clamp/quantization control.

### 2.3 Separate source ownership

Microphone and System Audio stay separate source objects/workers behind the
same canonical packet interface.

Do not hide both inside RecorderSession.

### 2.4 Event-driven endpoint service

Use endpoint/event-driven packet service.

Do not build audio around `Sleep(1)`, UI timers or the existing video worker's
1 ms poll.

### 2.5 Explicit source conversion

Every source is normalized into one program format before mixing.

Do not allow the writer or final mixer to interpret arbitrary endpoint bytes.

### 2.6 Bounded buffering

Adopt OBS's principle that audio buffering has a hard maximum and observable
state.

Arssyut should implement a simpler fixed-capacity per-source jitter/ring
contract rather than OBS's general source-graph buffering machinery.

### 2.7 Source lifecycle/recovery as state

Treat invalidation/recovery as an explicit source state, not an exception path.

---

## 3. What Arssyut should deliberately *not* copy from OBS P7A

### 3.1 No general source graph

P7A has two recorder-owned sources. It does not need arbitrary scene audio
trees, recursive source traversal, six mixes or filter graphs.

### 3.2 No user-facing “Use Device Timestamps”

OBS exposes a Windows option to attempt device timing.

Arssyut should be smarter: timing quality is an engine responsibility.

The engine validates device timing continuously and selects the best evidence
without asking the user to understand clock domains.

### 3.3 No generic sync-offset knob as a primary fix

Manual sync offset can be a future advanced escape hatch. It must not become a
substitute for correct QPC mapping and drift correction.

### 3.4 No audio monitoring in P7A

Monitoring creates another render endpoint, latency path, feedback/echo risk and
device-deduplication problem.

It is a separate product feature.

### 3.5 No six-track recording in P7A

Initial product output stays one mixed stereo AAC program track.

Multi-track is valuable for editing, but it multiplies writer/container/UI and
source-routing complexity before the core engine is proven.

### 3.6 No automatic switch to an unrelated default endpoint mid-record

OBS can follow/reconnect devices as a streaming application.

Arssyut prioritizes deterministic recorded identity. An active recording pins
the resolved endpoint ID.

### 3.7 Do not force minimum-latency audio periods

A screen recorder does not need sub-10-ms monitoring latency.

Using the smallest possible device period/“Pro Audio” scheduling by default
increases wakeups and power/CPU pressure without improving the encoded result.

---

## 4. Refined P7A product format

### 4.1 Canonical program bus: 48 kHz stereo float32

P7A now standardizes one program bus:

- 48,000 frames/sec;
- stereo;
- float32 internal;
- fixed 1024-frame blocks.

This is a deliberate simplification after the external benchmark.

Benefits:

- one mix cadence;
- one AAC frame duration;
- one drift-control target;
- simpler diagnostics and test matrices;
- common native Windows/video endpoint rate;
- no session-dependent writer format.

Source endpoints still negotiate their native/shared-engine formats.

If a source is already 48 kHz, no sample-rate conversion occurs.

44.1 kHz, 96 kHz or other endpoint rates are converted explicitly by the
accepted high-quality resampler.

For the current Microsoft AAC product profile, arbitrary high-rate sources
cannot be stored natively because the native encoder accepts only 44.1/48 kHz.
That conversion is therefore explicit and testable rather than disguised.

A future archival/lossless profile may deliberately introduce other program
rates/codecs; it is not P7A.

### 4.2 AAC target

Initial production target:

- AAC-LC;
- 48 kHz stereo;
- 192 kbps where the native encoder accepts the requested type.

Fallback to 160/128 kbps is permitted only as an explicit capability decision
visible in diagnostics, never silently because a configuration call failed.

---

## 5. Timestamp-quality policy

A source timestamp is evidence, not unquestioned truth.

Every audio source maintains a compact timing-quality state.

Conceptual classes:

- `DeviceQpcTrusted`;
- `ContinuityReconstructed`;
- `HostQpcFallback`;
- `Discontinuous`.

Device QPC/device-position evidence is trusted only when:

- WASAPI does not report `TIMESTAMP_ERROR`;
- QPC is monotonic;
- device frame position is monotonic;
- frame-position delta is compatible with delivered frame count plus an
  explicit discontinuity;
- QPC delta is plausible against the project monotonic clock.

When one packet has bad timing:

- do not reset the whole recording timeline;
- mark the timing error;
- reconstruct the packet start from the last trusted anchor + exact frame
  count when possible.

When timing remains invalid:

- fall back to a host-QPC continuity model;
- keep diagnostics explicit;
- never feed bad timestamps into the drift servo.

A new valid anchor may return the source to trusted device timing only through a
bounded re-lock policy.

This is the Arssyut equivalent of OBS's device-timing/fallback philosophy but
without a user-facing toggle.

---

## 6. Buffer sizing strategy

Do not hard-code “64 packets because it seems safe”.

At preparation time, derive source ring/pool capacity from measured endpoint
properties:

- endpoint buffer frame capacity;
- default/shared engine period;
- maximum observed/allowed packet frames;
- accepted scheduler-stall budget;
- fixed safety margin.

Conceptually:

```text
required_slots =
  ceil(jitter_budget / endpoint_period)
  + drain_safety_slots
```

Then clamp to compile-time/product min/max and allocate once.

After Armed:

- capacity never grows;
- high-water is diagnostic-visible;
- overflow is a correctness event;
- healthy acceptance must be zero overflow.

This is adaptive **preflight sizing**, not dynamic heap growth.

---

## 7. Scheduling strategy

### Capture source workers

Use one joinable event-driven worker per active endpoint.

Recommended policy:

- shared-mode WASAPI;
- event callback;
- MMCSS `Audio` class while servicing endpoint buffers;
- drain all available packets per wake;
- return to wait immediately.

Do not use `Pro Audio` or minimum engine periods by default.

Escalating priority/period is allowed only if real stress telemetry shows missed
endpoint deadlines.

### Mixer worker

Do not poll every millisecond.

Mixer waits on:

- source publication notification;
- stop;
- an explicit mix deadline when enough source horizon should exist.

It produces 1024-frame blocks only when the canonical timeline is ready.

### Writer

The writer remains single-owner.

Media Foundation's optional audio MMCSS attributes may be used if measured AAC
processing competes with video, but priority is not a substitute for bounded
service work.

---

## 8. Loopback silence policy

OBS contains a special silent-render workaround for loopback timing/glitch
behavior on some devices.

Arssyut should **not** copy that workaround unconditionally.

P7A3 must test:

- long system silence;
- playback start after silence;
- playback stop/restart;
- QPC/device-position continuity while silent;
- event delivery while silent.

Default policy:

- silence is legitimate source state;
- no fake audio playback/keepalive stream.

Only if a supported Windows/device class reproduces the “loopback stops
advancing during silence” failure may P7A3 add a narrowly-scoped silent
keepalive, guarded by diagnostics and a regression fixture.

---

## 9. Same-endpoint recovery state machine

Refine the earlier “device loss becomes silence” rule.

Arssyut may recover the **same pinned endpoint identity** automatically.

Conceptual states:

```text
Running
   |
DeviceInvalidated
   v
RecoveringSameEndpoint
   |          |
success      timeout/permanent removal
   |          |
   v          v
Running      Lost
```

During recovery:

- program timeline continues;
- missing source contribution is silence;
- no unrelated endpoint is selected;
- drift/timestamp state is re-anchored explicitly after successful re-open;
- reconnect attempts are bounded and stop-aware.

If the selected source was “Default”, RecorderSession resolves it to an actual
endpoint ID at start and pins that ID for the session.

Following a *new* default device mid-record is future opt-in behavior.

---

## 10. Resampler decision gate

Do not choose a resampler because it is already convenient.

P7A needs two capabilities:

1. static rate/channel conversion with high fidelity; and
2. tiny smooth ratio changes for long-session device-clock compensation.

Candidates to benchmark:

### A. Media Foundation Audio Resampler

Advantages:

- native Windows dependency;
- float/PCM support;
- channel mapping;
- configurable quality;
- small deployment surface.

Open question:

- whether smooth continuous ppm-scale compensation can be implemented cleanly
  without media-type churn or a second correction stage.

### B. FFmpeg libswresample

Advantages:

- mature conversion;
- explicit `swr_set_compensation` soft drift correction;
- timestamp-aware fill/trim;
- strong reference implementation;
- optional SoXR quality.

Costs:

- new dependency/deployment footprint;
- licensing/compliance review;
- binary size;
- broader third-party update surface.

### C. small purpose-built/vendored resampler

Only acceptable if it materially beats A/B on footprint while meeting quality,
drift, allocation and maintenance requirements.

A benchmark/spike issue must select the implementation before P7A5 mixer
integration. The P7A1 API must remain implementation-independent.

---

## 11. Metering strategy

OBS demonstrates that peak/RMS information is useful to users, but metering
must not drive the hot path.

P7A engine should compute cheap source/program metrics while samples are already
in the float mixer:

- per-source sample peak;
- program sample peak;
- RMS over a fixed observation interval;
- clipping count/over-range peak.

Publication is latest-wins telemetry.

UI may sample the latest meter state at its own cadence. No audio samples are
queued for UI rendering.

No monitoring/output playback is required to provide meters.

---

## 12. Product roadmap learned from OBS

Useful features that remain future work after P7A core:

### P7B candidates

- per-application/process loopback using Windows
  `ActivateAudioInterfaceAsync` process loopback;
- separate mic/system recording tracks plus a normal-player mixed track;
- advanced source gain;
- manual sync offset escape hatch;
- audio monitoring;
- optional same-session “follow default endpoint” mode.

### Later

- filters/denoise/expander/compressor;
- AEC;
- loudness normalization;
- surround/5.1;
- lossless/archival audio;
- recoverable hybrid/fragmented container redesign if the writer roadmap
  requires it.

Do not let these features enter P7A core implementation opportunistically.

---

## 13. New P7A acceptance additions

In addition to the existing acceptance lock:

### Timing robustness

Inject:

- one bad QPC timestamp;
- repeated bad QPC timestamps;
- backward device position;
- timestamp-error flag;
- valid timing re-lock.

Expected:

- no timeline reset;
- no pitch jump;
- diagnostics identify the fallback state;
- normal drift estimator ignores invalid evidence.

### Scheduling

Measure under CPU/GPU load:

- endpoint wake-to-release p50/p95/p99;
- maximum ring high-water;
- worker deadline misses;
- CPU time per source worker;
- mixer block service p95.

### Silence

System loopback:

- 5 minutes silence;
- immediate playback start;
- stop playback;
- resume playback.

No synthetic keepalive is accepted unless this test proves it is needed.

### Recovery

Invalidate and restore the same endpoint.

Expected:

- bounded recovery attempts;
- silence across missing interval;
- same endpoint ID only;
- explicit re-anchor;
- no discontinuity in global media time.

---

## 14. Refined implementation priority

After this benchmark, the best order is:

1. P7A1 canonical format/time/block/timing-quality contracts;
2. resampler benchmark/spike interface;
3. P7A2 microphone and P7A3 loopback in parallel;
4. P7A4 AV writer in parallel;
5. select/lock resampler;
6. P7A5 mixer/drift/timing fallback;
7. P7A6 integration;
8. P7A7 soak/real acceptance.

The most important optimization is not clever SIMD on day one. It is avoiding
wrong ownership, unnecessary wakeups, format churn, queue growth and duplicated
clock logic from the beginning.

---

## External references

- OBS Studio `libobs/media-io/audio-io.h` — fixed 1024-frame audio blocks and
  audio format contracts.
- OBS Studio `libobs/obs-audio.c` — source synchronization, bounded buffering
  and pending-source behavior.
- OBS Studio `plugins/win-wasapi/win-wasapi.cpp` — WASAPI source lifetime,
  event/callback operation, device timing, reconnect and loopback-silence
  handling.
- OBS Studio Audio Sources / Audio Mixer / Multi-track guides.
- Microsoft WASAPI, `IAudioCaptureClient::GetBuffer`, MMDevice notifications,
  event-driven capture and MMCSS.
- Microsoft Media Foundation AAC encoder and Audio Resampler DSP.
- FFmpeg libswresample / `swr_set_compensation`.
