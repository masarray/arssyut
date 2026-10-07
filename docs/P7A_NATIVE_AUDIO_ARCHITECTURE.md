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

Preferred output is 48 kHz because it is standard for video-oriented workflows
and supported by the Microsoft AAC encoder.

44.1 kHz is also valid when all enabled sources can use it without unnecessary
conversion.

Policy:

1. if all enabled sources are 48 kHz -> 48 kHz, no SRC;
2. if all enabled sources are 44.1 kHz -> 44.1 kHz, no SRC;
3. if sources disagree -> 48 kHz canonical bus;
4. if an endpoint exposes another shared-engine rate -> convert explicitly to
   the resolved 44.1/48 kHz AAC-compatible bus.

This is **no avoidable resampling**, not a false promise that every arbitrary
96/192 kHz device format can be placed unchanged into the current AAC product
profile.

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

---

## 7. Buffering and backpressure

Audio is continuous semantic media. It must **not** use latest-wins coalescing.

Each source:

- one fixed-capacity packet pool;
- one fixed-capacity SPSC ring;
- capacity derived from measured endpoint period + maximum accepted scheduling
  stall, not an arbitrary “large enough” number;
- queue depth and pool high-water exposed in diagnostics.

Mixer output:

- fixed-size retained blocks;
- preferred block size aligned to AAC's 1024-sample frame contract;
- one bounded SPSC handoff to the sole writer caller.

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

A bounded estimator computes source clock-rate error relative to QPC.

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

Initial P7A is deterministic rather than “clever”:

- selected endpoint identity is frozen for the recording;
- default-device changes affect the next recording, not the active session;
- device invalidation becomes an explicit source state;
- the unavailable source contributes timeline-aligned silence;
- diagnostics/status expose the loss;
- no silent automatic switch to a different microphone/speaker mid-record.

Future hot-rebind can be a separate milestone after evidence.

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
9. device disconnect;
10. mute/unmute;
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
