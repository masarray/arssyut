# ADR-009 — Native audio clock, capture, mix and MP4 authority

- **Status:** Accepted for P7A planning
- **Date:** 2026-10-07
- **Baseline:** `main@9dc6c49cf92b47932b0f2767cf90c7e8d6a94420`
- **Scope:** architecture only; no native audio implementation is introduced by this ADR

## Context

Arssyut currently has an accepted video recorder and one canonical
`RecorderSession` media clock. The product already discovers microphone
devices and exposes System Audio / Microphone controls, but the bridge
intentionally returns `UNSUPPORTED` when those streams are requested.

The next milestone must add production audio without introducing the failure
modes common in naive recorder implementations:

- pitch/chipmunk audio caused by treating bytes as the wrong sample format/rate;
- crackles or gaps caused by slow capture callbacks or unbounded queues;
- A/V drift caused by treating independent device clocks as identical;
- memory growth proportional to recording duration;
- clipping from naive microphone + system-audio summation;
- deadlocks or long Stop/Finalize latency caused by audio workers;
- a second media clock, writer or RecorderSession authority.

The repository engineering contract already requires one recording clock,
bounded audio buffering, explicit resampling, deterministic mixing, observable
underflow/overflow/drift, fixed-capacity audio pools and long-session testing.

## Decision

### 1. One master media timebase

`RecorderSession` remains the only recording timeline authority.

All audio source timestamps are converted to the existing monotonic/QPC
100-nanosecond timebase. No audio worker owns an independent product timeline.

WASAPI packet QPC timestamps are source evidence, not a second clock. They are
mapped into the RecorderSession timeline and used to estimate per-device drift.

Device timing is not assumed perfect. Packets with timestamp errors or failed
monotonic/plausibility checks are classified with lower timestamp confidence.
Normal drift estimation uses only trusted device-QPC evidence; continuity may
fall back to frame-count extrapolation or packet-arrival estimation without
turning scheduler jitter into a new clock.

### 2. Two explicit capture sources

P7A has two independent Windows sources:

- microphone: WASAPI capture endpoint;
- system audio: WASAPI shared-mode loopback on the render endpoint.

They share a common source contract but own separate Windows objects, event
handles, worker lifetimes and counters.

Do not alias System Audio to Stereo Mix or to the selected microphone.

### 3. Event-driven capture

Audio capture workers are event-driven. They drain every available WASAPI
packet after wake-up and release endpoint buffers promptly.

Windows capture workers register with MMCSS task `Audio` and deterministically
revert that registration during teardown. `Pro Audio` is not the default:
higher priority requires measured evidence that ordinary Audio scheduling misses
the endpoint service deadline.

Capture workers do not encode, mux, format diagnostics, touch UI state or
perform expensive mixing.

Each source publishes into one fixed-capacity SPSC handoff backed by retained
packet storage. No queue grows with recording duration.

Audio samples are non-replaceable timeline data. Unlike cursor/meter state they
must never use latest-wins coalescing. When a packet is lost, the loss is
represented as a timeline discontinuity and diagnostics event; later audio is
not shifted earlier to hide the gap.

### 4. Canonical format is resolved once

A single `AudioProfile` is resolved before the hot path begins.

P7A canonical program representation:

- 48 kHz;
- stereo;
- float32 samples;
- fixed 1024-frame retained program blocks;
- explicit source channel mapping;
- no implicit reinterpretation of endpoint bytes.

External benchmark work against OBS/libobs and the Microsoft AAC contract
tightened the earlier “44.1 or 48 depending on the session” policy. P7A now
uses one 48 kHz program bus so the mix quantum, AAC duration, writer format,
drift target and diagnostics do not change from recording to recording.

A source that is already 48 kHz is not sample-rate converted. 44.1/96/other
endpoint rates are converted explicitly by the accepted high-quality
resampler. This is **no hidden/downstream format guessing**, not a promise that
arbitrary 96/192 kHz endpoint data can be stored unchanged in the current
AAC/MP4 product profile; Microsoft's native AAC encoder accepts only 44.1 or
48 kHz PCM input.

The Media Foundation AAC boundary uses 16-bit PCM because that is the native
Microsoft AAC encoder input contract. Float32 remains the mix domain until the
encoder boundary. Initial AAC target is 48 kHz stereo AAC-LC at 192 kbps when
the native encoder accepts that type.

### 5. Timestamp and sample-count correctness is non-negotiable

Pitch and duration are derived from audio **frames and sample rate**, never from
raw byte count guesses.

Every packet/block carries:

- source kind;
- source sample format;
- frame count;
- channel count/channel map;
- source device position when valid;
- source QPC timestamp in 100 ns;
- canonical media timestamp;
- WASAPI silent/discontinuity/timestamp-error flags.

Sample time and duration use rational frame-count math with wide intermediates;
rounding remainder is carried forward rather than discarded every block.

Device timestamps are validated evidence, not unquestioned truth. A compact
per-source timing-quality state distinguishes trusted device-QPC timing,
continuity reconstruction, host-QPC fallback and real discontinuity. A bad
timestamp must not reset the global recording timeline or poison the drift
servo.

### 6. Drift correction is continuous and bounded

Microphone, render endpoint and system QPC do not assume identical hardware
clock rates.

Each active audio source gets a bounded drift estimator that compares source
frame position/QPC evidence against the RecorderSession timeline.

Normal drift is corrected gradually through the source resampling ratio. P7A
must not periodically sleep, duplicate/drop large chunks, or reset timestamps
to “fix” drift.

The resampler is stateful and its internal/group delay is part of media-time
accounting. Its contract exposes consumed frames, produced frames and delay (or
equivalent phase state); output PTS may not blindly reuse input packet PTS.

Actual device discontinuities are represented as missing timeline intervals.
The mixer preserves global A/V time by inserting deterministic silence where
audio is unavailable rather than shifting later audio.

The correction range and estimator window are implementation constants that
must be selected from synthetic drift tests and real long-session evidence,
not guessed in the architecture document.

### 7. Start/Armed semantics include audio readiness

When audio is enabled, native `Armed` means the requested audio source(s) are
initialized and ready in addition to the existing video/capture readiness.

Audio capture may pre-roll while the visible 3-2-1 countdown runs.

At start commit:

- the existing RecorderSession media timestamp zero remains authoritative;
- pre-commit audio is never encoded;
- a packet crossing timestamp zero is trimmed to zero when necessary;
- if the first valid packet begins after zero, deterministic silence represents
  the real gap;
- audio does not move video timestamp zero.

### 8. Mute is a mix operation, not device restart

Microphone mute/unmute keeps the source clock and worker alive. The mix policy
applies zero gain while muted so timestamps stay continuous and mute cannot
cause restart crackles or clock re-anchoring.

### 9. One writer/mux authority

There is one MP4 writer authority for both video and AAC.

Capture workers and the audio mixer never call Media Foundation sink-writer
methods directly.

P7A may refactor the current video-only writer into an AV-aware writer, but
video-only behavior must remain a regression reference and audio-disabled
recordings must not pay a material steady-state cost.

The final product profile is H.264 video + AAC-LC audio in MP4. Initial P7A
ships one mixed AAC program track, not independent mic/system tracks.

### 10. Deterministic device-loss policy

P7A does not silently switch to a different microphone or playback endpoint
mid-recording.

A recording pins the resolved endpoint identity. Device invalidation is an
explicit source state. Arssyut may make bounded, stop-aware attempts to reopen
the **same pinned endpoint identity**; missing time contributes silence and a
successful reopen re-queries mutable format properties and explicitly
re-anchors source timing. Switching to a different new default endpoint is a
separate future capability.

Friendly name is never identity. When available on supported Windows,
`PKEY_AudioEndpoint_StableId` is the preferred durable persisted identity;
ordinary endpoint ID remains the runtime/fallback identity and stale resolution
must be explicit.

### 11. Bounded memory and ownership

All COM objects, event handles, workers, packet pools, resamplers and writer
objects have one RAII owner and deterministic teardown.

No audio data structure grows with elapsed recording time.

Stop order is explicit:

`stop request -> disarm callbacks/events -> stop producers -> drain/cut mixer
at media stop -> join workers -> release audio resources -> finalize writer`.

No detached thread is permitted.

## Quality defaults to validate

These are product targets, not implementation shortcuts:

- AAC-LC;
- stereo program output for normal screen recordings;
- 48 kHz preferred production rate;
- high-quality 44.1 <-> 48 kHz conversion only when required;
- high-quality AAC bitrate target, initially 192 kbps stereo subject to Windows
  Media Foundation capability validation;
- float32 mix domain with explicit headroom policy;
- no hard clipping in accepted dual-source fixtures.

Exact gains, resampler algorithm and drift-control constants remain gated by
P7A deterministic + real-recording evidence.

## Consequences

### Positive

- audio cannot create a second media clock;
- pitch/sample-rate bugs become contract violations rather than tuning bugs;
- microphone and system audio can be built/tested independently;
- long recording drift is measurable and correctable;
- source buffers and memory remain bounded;
- UI mute/device state never owns realtime audio truth;
- multi-thread implementation can proceed with narrow file ownership.

### Cost

- microphone-only “quick coding” is intentionally delayed until common clock,
  packet, format and writer contracts exist;
- P7A requires synthetic sample-rate/drift fixtures in addition to real Windows
  testing;
- one integration phase is required after the capture and writer lanes are
  independently green.

## References

- Microsoft WASAPI `IAudioCaptureClient::GetBuffer`: packet frame count,
  discontinuity/timestamp flags, device position and QPC timestamp.
- Microsoft `AUDCLNT_STREAMFLAGS_EVENTCALLBACK`: event-driven buffering.
- Microsoft WASAPI loopback: shared-mode render-endpoint capture.
- Microsoft Media Foundation AAC encoder: PCM16 input; 44.1/48 kHz; explicit
  sample time/duration required.
