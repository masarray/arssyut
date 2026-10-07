# P7A — Native Audio Foundation & A/V Sync Architecture

**Status:** Architecture lock before implementation  
**Baseline:** `main@9dc6c49cf92b47932b0f2767cf90c7e8d6a94420`  
**Authority:** `AGENTS.md` + `ADR-009-native-audio-clock-mix-mux.md`  
**Implementation policy:** no native audio coding begins until this contract and
its issue decomposition are accepted.

---

## 1. Product target

P7A adds production-quality microphone and system-audio recording without
regressing the accepted screen-recording engine.

The target is ordinary high-quality screen-recorder audio:

- correct pitch at every supported device rate;
- no chipmunk/slow audio from sample-rate or block-alignment mistakes;
- no periodic crackles from worker starvation or buffer misuse;
- no arbitrary packet gaps or repeated audio;
- stable A/V sync at start and across long recordings;
- deterministic mute/unmute;
- no clipping from naive dual-source summing;
- no queue or memory growth with recording duration;
- clean repeated Start/Stop lifetime behavior;
- clear diagnostics when the OS/device actually glitches;
- video-only recording remains materially equivalent to the current baseline.

P7A is **not** an audio-effects milestone. Noise suppression, compressor, EQ,
AEC, per-app routing and AI enhancement remain later work.

---

## 2. Current baseline and constraints

Current accepted state:

- `RecorderSession` owns the canonical recording lifecycle and media clock;
- video is CFR and H.264/MP4 through the current Media Foundation writer;
- the bridge already exposes System Audio / Microphone request flags and
  microphone device tokens;
- unsupported media inputs are currently rejected explicitly;
- microphone device discovery already exists;
- no production audio capture, mix or AAC writer path exists yet.

The following accepted authorities must not move:

| Concern | Authority |
|---|---|
| session state | `RecorderSession` |
| media timestamp zero | `RecorderSession::request_start_commit()` worker gate |
| realtime timebase | project monotonic/QPC 100 ns |
| video frame cadence | existing CFR video pipeline |
| UI state | projection only; never audio hot-path owner |
| device discovery | native Windows device catalog |
| final media writer | one native writer/mux authority |

---

## 3. End-state topology

```text
                                RecorderSession
                       one state machine + one QPC clock
                                     |
                    Preparing / Armed / Recording / Stop
                                     |
             +-----------------------+-----------------------+
             |                                               |
             v                                               v
        Video path                                      AudioEngine
  WGC -> compositor -> CFR                    +-------------------------+
             |                                |                         |
             v                                v                         v
        video sample                  Microphone worker        Loopback worker
                                               |                         |
                                               +---- fixed SPSC ---------+
                                                          |
                                                          v
                                                   AudioMixWorker
                                              format normalize / SRC
                                              QPC map / drift correct
                                                deterministic mix
                                                          |
                                                fixed AAC-size blocks
                                                          |
                                                          v
                                                 RecorderSession worker
                                              sole AV-writer caller
                                                          |
                                             H.264 + AAC-LC -> MP4
```

Important: the diagram does **not** grant UI, capture workers or the mixer
permission to call the Media Foundation sink writer directly.

---

## 4. Canonical audio data model

A packet is time-bearing media, not an arbitrary byte blob.

Conceptual fields:

```text
AudioSourcePacket
  source_id
  native_format
    sample_rate
    sample_type
    channel_count
    channel_mask
    block_align
  frame_count
  qpc_time_100ns
  device_frame_position
  flags
    silent
    discontinuity
    timestamp_error
  pool_slot / retained payload
```

A normalized mix block is separate:

```text
CanonicalAudioBlock
  media_start_100ns
  sample_rate
  frame_count
  stereo float32
  source_presence_mask
  discontinuity_mask
```

Never infer the source sample rate from buffer size. Never convert bytes using
a format different from the endpoint's negotiated format.

---

## 5. Format-resolution policy

One `AudioProfile` is resolved before the realtime path starts.

### Internal representation

- float32 mix domain;
- explicit stereo program bus for normal product recording;
- fixed retained buffers;
- exact frame counts;
- explicit channel mapping.

### Output rate

P7A uses one fixed **48 kHz stereo float32 program bus**.

This is a deliberate refinement after benchmarking OBS/libobs and the Windows
AAC stack. A fixed 48 kHz bus reduces format-state combinations and makes the
canonical 1024-frame block exactly 21.333 ms for every recording.

Source policy:

1. endpoint already 48 kHz -> no sample-rate conversion;
2. 44.1 kHz endpoint -> high-quality conversion to 48 kHz;
3. 96/192/other endpoint -> explicit conversion to 48 kHz because the current
   Microsoft AAC product path cannot encode those rates directly;
4. source negotiated format/rate remains visible in diagnostics.

There is no metadata-only “conversion”. Samples are actually converted.

### AAC boundary

Microsoft Media Foundation AAC requires 16-bit PCM input at 44.1 or 48 kHz.
Therefore float32 mix output is converted at the encoder boundary with explicit
clamp/saturation and validated frame math.

Initial quality target:

- AAC-LC;
- stereo;
- 192 kbps when supported by the selected Media Foundation path.

Fallback behavior must be explicit and diagnostic-visible.

---

## 5A. Timestamp trust and endpoint identity

External OBS/WASAPI review reinforces that endpoint timing and identity must be
explicit state, not assumptions.

Timestamp evidence tiers:

- `TrustedDeviceQpc`: WASAPI timing valid + monotonic/plausible;
- `FrameCountExtrapolated`: continuity derived from last trusted anchor and
  exact source frame count;
- `ArrivalEstimated`: monotonic read time minus packet duration, used only as
  a degraded continuity estimate;
- `Discontinuous`: continuity cannot be proven.

Only trusted device-QPC observations feed the drift estimator directly.

Friendly name is display-only. Runtime/persisted identity uses ordinary endpoint
ID and, on supported Windows, `PKEY_AudioEndpoint_StableId` when available.
Stable IDs are opaque; mutable format/name properties are always re-queried
after resolving a persisted identity.

---

## 6. Capture-worker contract

Each active source owns one event-driven worker.

### Microphone worker

Owns:

- selected capture endpoint;
- WASAPI audio client;
- capture client;
- event handle;
- source format;
- fixed packet pool;
- one SPSC publisher;
- source counters.

### System-audio worker

Owns the same contract but opens a render endpoint in WASAPI shared loopback
mode.

### Worker scheduling

Each Windows source worker:

- initializes COM MTA on its own thread;
- registers MMCSS task `Audio`;
- blocks on stop + WASAPI event (+ explicit recovery signal if required);
- never runs an active periodic sleep/poll loop;
- drains all available packets when signaled;
- reverts MMCSS and uninitializes COM during teardown.

`Pro Audio` is not the default task. Raise scheduling class only if measured
evidence shows ordinary Audio scheduling misses endpoint service deadlines.

### Worker hot path

After wake-up:

1. drain all currently available packets;
2. read frame count, flags, device position and QPC timestamp;
3. reserve one retained packet slot;
4. copy only the packet payload needed after `ReleaseBuffer`;
5. publish metadata + slot to the SPSC ring;
6. release WASAPI buffer immediately;
7. continue until empty.

Forbidden in source workers:

- AAC encoding;
- MP4 writes;
- file I/O;
- UI callbacks;
- device-list refresh;
- diagnostics JSON formatting;
- long mutex holds;
- dynamic allocation per packet;
- sample-rate guessing.

Scheduling policy:

- shared-mode event-driven WASAPI;
- one joinable worker per active endpoint;
- MMCSS `Audio` while servicing endpoint buffers;
- drain all available packets per wake, then return to wait;
- do not request minimum device periods or `Pro Audio` priority unless real
  stress telemetry proves the normal policy misses deadlines.

---

## 7. Buffering and backpressure

Audio is continuous semantic media. It must **not** use latest-wins coalescing.

Each source:

- one fixed-capacity packet pool;
- one fixed-capacity SPSC ring;
- capacity derived at preflight from endpoint buffer/period, maximum packet
  frames, an accepted scheduler-stall budget and a fixed safety margin;
- compile-time/product min/max clamps;
- no dynamic capacity growth after Armed;
- queue depth and pool high-water exposed in diagnostics.

Mixer output:

- fixed-size retained **1024-frame** blocks;
- one canonical 48 kHz stereo float32 program format;
- one bounded SPSC handoff to the sole writer caller.

The mixer is notification/deadline driven; it does not wake on a blind 1 ms
poll.

Overflow contract:

- never allocate more memory;
- increment overflow counter;
- mark timeline discontinuity;
- preserve later timestamps;
- represent lost time as silence instead of moving later samples earlier.

Healthy acceptance requires zero overflow.

---

## 8. Timeline, start and stop semantics

### Preparation

When audio is requested, `Preparing` initializes all requested endpoints,
workers, resamplers, rings and writer audio configuration.

### Armed

Native `Armed` is published only when:

- video requirements are ready;
- every requested audio source is initialized;
- required retained pools/rings are ready;
- writer audio configuration is accepted;
- source readiness has reached the contract selected for that source.

Audio can already be capturing during the visible countdown, but pre-commit
packets are not encoded.

### Commit / media zero

`RecorderSession` keeps sole authority over media zero.

For the first post-commit audio:

- packet starts before zero -> trim leading source frames;
- packet starts exactly at zero -> use directly;
- first packet starts after zero -> fill deterministic silence to the packet
  timestamp.

Never shift video to meet audio.

### Stop

Stop time is one canonical media timestamp.

The mixer cuts the logical program at that time, flushes only bounded retained
audio, then workers are joined and resources released before finalization.

No worker may continue publishing after its owner begins teardown.

---

## 9. Clock mapping and drift control

The most dangerous long-recording bug is treating two hardware clocks as one.

For every source, track:

- WASAPI device frame position;
- WASAPI QPC position in 100 ns;
- expected position on the RecorderSession media timeline;
- accumulated source frame count;
- resampler consumed/produced frame count.

A bounded estimator computes source clock-rate error relative to QPC. It
accepts only trusted timestamp evidence as direct rate observations.

The resampler is stateful: its consumed input frames, produced output frames and
internal/group delay are part of timestamp accounting. Output PTS must not
blindly reuse input packet PTS after sample-rate conversion.

Normal drift correction:

```text
nominal SRC ratio
       +
small bounded ppm correction
       =
effective ratio
```

Properties:

- slow correction;
- no sudden pitch step;
- no periodic sleeps;
- no whole-packet duplication/drop in normal drift control;
- ratio returns smoothly when error changes;
- estimator state is fixed-size.

Synthetic tests must inject known positive/negative ppm drift and prove:

- output duration remains tied to the master timeline;
- pitch stays within the accepted tolerance;
- correction remains bounded;
- no discontinuity counter increments for normal clock drift.

Exact ppm clamp/window constants are chosen only after tests.

Timestamp-quality guard:

- trusted device QPC requires no WASAPI timestamp-error flag, monotonic QPC,
  monotonic device-frame position and plausible deltas;
- one bad packet is reconstructed from the last trusted anchor + exact frame
  count when possible;
- repeated bad timing enters host-QPC fallback;
- invalid timing never enters the drift estimator;
- re-lock to device timing is explicit and bounded.

---

## 10. Mixing contract

Initial P7A output is one stereo AAC program track.

Mixer responsibilities:

- source timestamp alignment;
- resampling;
- mono/stereo channel mapping;
- mute state;
- source gain;
- explicit headroom;
- final float accumulation;
- clipping detection;
- conversion to encoder PCM at the writer boundary.

Do not hide clipping with silent hard saturation.

Exact default mic/system gains remain a calibration decision, but acceptance
must include worst-case simultaneous full-scale synthetic fixtures and prove the
selected policy does not produce uncontrolled clipping.

Microphone mute is gain=0 on the mix timeline. It does not stop/restart WASAPI.

---

## 11. Device-loss and endpoint-change policy

Initial P7A is deterministic rather than silently switching sources:

- “Default” is resolved to a concrete endpoint ID at recording preparation and
  pinned for the session;
- a different new default device affects the next recording, not the current
  session;
- device invalidation becomes an explicit source state;
- bounded, stop-aware recovery may reopen only the same pinned endpoint ID;
- the unavailable interval contributes timeline-aligned silence;
- successful same-endpoint recovery explicitly re-anchors source timing;
- diagnostics/status expose invalidation, recovery attempts and outcome.

Following a different default endpoint mid-record is future opt-in behavior.

No-playback loopback intervals are represented by the master timeline/mixer as
silence. P7A must not inject artificial audio into the user's render endpoint
merely to force loopback packets unless a reproducible supported-Windows defect
proves such a workaround necessary.

On supported Windows versions, persisted user preference should prefer
`PKEY_AudioEndpoint_StableId` where available, while retaining explicit
fallback behavior when only ordinary endpoint ID is available.

---

## 12. Writer / mux evolution

The current `MfH264Mp4Writer` is video-centric.

P7A must create one AV-aware writer authority without doing a flag-day rewrite
of unrelated video code.

Required end contract:

```text
AV writer
  configure video
  configure optional AAC
  write video sample
  write audio sample
  finalize once
```

Rules:

- one owner/caller;
- monotonic sample timestamps per stream;
- audio-disabled path remains a first-class regression mode;
- video color/H.264 policy remains unchanged;
- AAC configuration is fixed before `BeginWriting`;
- writer errors map to explicit stage/status;
- no audio source thread touches the sink writer.

The integration design may reuse the RecorderSession worker as sole writer
caller because it already runs at 1 ms cadence. Moving video submission to a
new mux thread is **not** required unless measurement proves the existing sole
caller cannot meet audio/video service deadlines.

This minimizes blast radius while preserving one writer authority.

---

## 13. Diagnostics contract

P7A diagnostics must make audio failures measurable.

Per source:

- negotiated sample rate / sample type / channels / channel mask;
- endpoint identity kind (stable/runtime/default-role) and endpoint period;
- timestamp-quality state and timing fallback transitions;
- trusted/extrapolated/arrival-estimated packet counts;
- late/stale packets discarded and timeline-gap frames;
- MMCSS registration state;
- same-device recovery attempts/successes/unavailable duration;
- resampler delay/flush frames;
- packet count;
- frame count;
- silent frame count;
- discontinuity count;
- timestamp-error count;
- SPSC depth high-water;
- packet-pool high-water;
- overflow count;
- device-invalidated count;
- source drift estimate ppm;
- effective SRC ratio min/max.

Mixer:

- output rate;
- mixed frame count;
- silence inserted frames;
- clipping/over-range events;
- mixer underrun count;
- mix queue high-water;
- muted frame count by source.

Writer:

- AAC configured rate/channels/bitrate;
- AAC samples submitted;
- audio writer backpressure/failure stage;
- first audio sample PTS;
- last audio sample end PTS.

A/V:

- first video PTS;
- first audio PTS;
- start skew us;
- current audio-vs-video end skew us;
- max absolute skew us;
- final duration difference us.

Resource:

- audio worker count;
- retained audio bytes;
- private-memory start/end/peak;
- teardown/join latency per worker.

---

## 14. Performance and memory budgets

Architecture gates:

- no dynamic allocation in steady-state source packet capture;
- no unbounded audio queue/history;
- no blocking mutex in WASAPI packet drain;
- no UI work in audio workers;
- no filesystem work in audio workers;
- no device enumeration during active capture;
- no second RecorderSession;
- no second product media clock.

Real validation targets:

- healthy mic-only/system-only/dual-source recordings have zero ring overflow,
  zero timestamp error and zero unexpected discontinuity;
- audio addition does not introduce video skipped frames or encoder
  backpressure;
- compositor/capture p95 remain materially equivalent to video-only baseline;
- retained audio memory stays constant with recording duration;
- repeated Start/Stop returns all worker/resource counts to baseline.

Long soak is mandatory.

---

## 15. Test strategy

### Portable deterministic tests

No Windows device required:

- timestamp-quality Trusted -> Extrapolated -> Trusted transition;
- invalid timestamp samples do not perturb drift estimation;
- resampler impulse/group-delay PTS accounting;
- stale packet past a closed mix interval is discarded/counted without shifting
  later media;
- frame-count -> 100 ns rational conversion;
- channel mapping;
- mono -> stereo mapping;
- float accumulation/headroom;
- clipping detection;
- 44.1 <-> 48 kHz synthetic tone SRC;
- known-rate drift injection;
- missing-packet gap -> deterministic silence;
- start trim / start silence;
- mute without timeline discontinuity;
- ring overflow policy;
- bounded pool reuse;
- stop at exact media timestamp.

### Windows deterministic tests

- mock/synthetic endpoint-format negotiation;
- Media Foundation AAC writer accepts 44.1 and 48 kHz;
- AAC sample timestamps/durations are monotonic and nonzero;
- video-only writer behavior remains green;
- AV MP4 reports expected H.264 + AAC streams.

### Real recording tests

Use controlled fixtures:

1. microphone only;
2. system audio only;
3. microphone + system audio;
4. 44.1 kHz source;
5. 48 kHz source;
6. mixed source rates;
7. silence;
8. loud dual-source clipping fixture;
9. device disconnect + same-identity recovery;
10. Windows default endpoint changes mid-session without silent retarget;
11. no-playback loopback silence interval;
12. mute/unmute;
11. 30 fps video + audio;
12. 60 fps video + audio;
13. repeated Start/Stop;
14. 10 minute recording;
15. 60 minute drift/soak recording.

Quality evidence includes waveform/frequency/duration measurements, not only
listening impressions.

---

## 16. Acceptance thresholds

These are P7A release gates unless real evidence justifies a documented change.

Healthy path:

- no unexpected crackle/drop detectable in fixture waveform;
- zero audio ring overflow;
- zero writer backpressure;
- zero unexpected WASAPI discontinuity;
- decoded test-tone frequency error <= 0.1%;
- first-program-audio vs media-zero skew <= 20 ms when the source supplies
  audio across zero;
- additional A/V drift over a 60 minute fixture <= 20 ms;
- no monotonic private-memory growth attributable to audio;
- 100 repeated Start/Stop cycles do not leak workers, COM objects, events or
  retained pool slots;
- video-only diagnostics remain materially equivalent to pre-P7 baseline.

A real OS/device discontinuity may violate the healthy-path counters, but it
must be explicit in diagnostics and must not corrupt subsequent timestamps.

---

## 17. Scope exclusions

P7A intentionally excludes:

- AI denoise;
- echo cancellation;
- automatic gain control;
- compressor/limiter product effects beyond the minimal deterministic safety
  required by the mix contract;
- EQ;
- per-application loopback;
- independent mic/system tracks;
- live monitor/playback;
- automatic mid-session endpoint switching;
- camera audio;
- pause/resume timeline semantics.

These are separate milestones after the native audio core is proven.

---

## 18. Definition of done

P7A is complete only when:

- System Audio and Microphone no longer return unsupported when valid/enabled;
- requested streams actually exist in the MP4;
- diagnostics prove rate/timestamp/buffer/drift health;
- 44.1/48/mixed-rate fixtures preserve pitch;
- 60 minute A/V sync gate passes;
- memory/resource soak passes;
- real Windows mic/system/dual-source recordings sound normal;
- video-only behavior remains accepted;
- CI and real acceptance artifacts are recorded in the repo handoff.

No individual sub-issue may declare the overall milestone complete.
