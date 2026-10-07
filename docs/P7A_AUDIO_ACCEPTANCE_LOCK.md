# P7A — Audio acceptance lock

This document defines evidence required before P7A may be called complete.

A green build is necessary but not sufficient.

---

## 1. Baseline preservation

Before enabling audio:

- record the same video-only fixture from current `main`;
- preserve H.264 profile/rate-control/color behavior;
- preserve frame pacing;
- preserve capture/compositor p95;
- preserve zero/near-zero skipped-frame and encoder-backpressure behavior;
- preserve Start/Armed/ACTION semantics.

Audio-disabled P7A must not materially regress the accepted video recorder.

---

## 2. Synthetic quality fixtures

### Tone / pitch

Required decoded checks:

- 440 Hz;
- 1 kHz;
- multi-tone;
- 44.1 kHz source;
- 48 kHz source;
- mixed source rates;
- 96 kHz source converted to the 48 kHz program bus.

Decoded tone frequency error target: <= 0.1%.

Fail on:

- octave/pitch error;
- chipmunk/slow audio;
- duplicated/dropped periodic waveform sections;
- incorrect decoded duration from sample-rate metadata mismatch.

### Silence

- all-zero/silent WASAPI packets preserve time;
- silence does not create false discontinuities;
- AAC output remains timestamp-monotonic.

### Gap

Inject one known missing packet interval.

Expected:

- discontinuity counter increments;
- exact missing interval becomes silence;
- all later audio remains at original media timestamps;
- video does not shift.

### Drift

Inject positive and negative source clock error.

Expected:

- bounded smooth correction;
- no periodic sample chunk drop/duplication;
- final media duration remains tied to master clock;
- pitch tolerance remains inside gate.

### Timestamp-quality fallback

Inject:

- one WASAPI timestamp-error packet;
- repeated timestamp errors;
- backward device-frame position;
- implausible QPC jump;
- valid timing after fallback.

Expected:

- global media clock never resets;
- one bad packet is reconstructed from continuity when possible;
- repeated bad timing enters explicit fallback state;
- bad timing does not enter drift estimation;
- re-lock is bounded and diagnostic-visible;
- no audible pitch jump.

---

## 2A. Timestamp confidence / resampler-delay gate

Inject degraded timing without changing sample content:

- one packet with timestamp error;
- short run of extrapolated timestamps;
- return to trusted device timing;
- delayed stale packet after its mix interval has closed;
- resampler impulse with nonzero internal delay.

Required:

- audio remains monotonic;
- no pitch step;
- bad timestamp evidence does not perturb drift estimate;
- timestamp-quality transition counters match the fixture;
- stale media is discarded/counted, never shifted later;
- resampler group delay is reflected in output PTS;
- return to trusted timing does not create a discontinuous correction.

---

## 3. Start / Armed / ACTION

With audio enabled:

- requested audio endpoint initializes during Preparing;
- visible 3-2-1 may overlap audio preparation;
- Armed is not published until requested audio source contract is ready;
- no pre-commit audio is encoded;
- commit timestamp remains RecorderSession media zero;
- source audio crossing zero is trimmed precisely;
- late first packet creates timeline-aligned silence;
- ACTION does not wait for an unrelated second audio-start phase.

Measure:

- audio prepare latency;
- commit-to-first-audio-frame;
- first audio PTS;
- first video PTS;
- initial A/V skew.

Healthy source-across-zero target: <= 20 ms A/V start skew.

---

## 4. Real Windows matrix

Test at minimum:

### Microphone only

- common USB microphone;
- built-in microphone array when available;
- mute/unmute;
- quiet speech;
- loud speech;
- silence;
- unplug/device invalidation.

### System audio only

- browser/video playback;
- music;
- silence/no active playback for several minutes (program timeline remains
  continuous without emitting artificial render sound);
- endpoint at 44.1 kHz where available;
- endpoint at 48 kHz;
- playback endpoint invalidation;
- 5 minutes of system silence followed by immediate playback;
- playback stop/resume after silence;
- event/timestamp continuity during silence.

### Dual source

- microphone speech over music/video;
- high-level simultaneous content;
- mute mic while system continues;
- system silence while mic continues;
- start/stop while both sources are active.

### Video combinations

- 30 fps;
- 60 fps;
- Display;
- Window;
- Region;
- Pixel Accurate;
- Clean Screen;
- Vivid Presentation.

---

## 5. Long-session sync

Run controlled A/V sync fixtures:

- 10 minutes;
- 60 minutes.

Measure the same identifiable audiovisual transient near start and near end.

Release target:

- start skew <= 20 ms for healthy source-across-zero fixture;
- additional drift over 60 minutes <= 20 ms;
- timestamps monotonic;
- no increasing queue depth;
- no repeated discontinuity pattern.

Do not accept “sounds approximately synced” as evidence.

---

## 6. Crackle / dropout gate

Healthy path requires:

- source ring overflow = 0;
- mix output overflow = 0;
- unexpected WASAPI discontinuity = 0;
- timestamp error = 0;
- audio writer backpressure = 0;
- source packet loss = 0.

Any nonzero value must be explained by a deliberate fault-injection or a real
OS/device event captured in evidence.

Waveform inspection must show no periodic zero-length holes, repeated blocks or
single-sample spikes attributable to Arssyut.

---

## 7. Mix / clipping gate

Dual-source fixture must prove:

- source channel mapping is correct;
- mono mic does not become one-sided;
- stereo system audio preserves left/right;
- no integer wrap;
- no uncontrolled hard clipping;
- mute produces exact timeline continuity;
- gain changes, if exposed, do not restart capture.

Exact product gain defaults are accepted only after matched real recordings.

---

## 8. Scheduling / deadline gate

Under concurrent CPU/GPU stress, record:

- endpoint event wake -> packet release p50/p95/p99;
- source-worker CPU time;
- mixer block service p95/p99;
- source ring/pool high-water;
- missed endpoint deadlines;
- video skipped/backpressure deltas.

Default shared-mode + MMCSS `Audio` must remain healthy. Do not promote the
workers to `Pro Audio` or minimum-period scheduling unless this evidence shows
the normal policy fails.

---

## 9. Memory / resource lifecycle

### Recording soak

At least 60 minutes with dual audio.

Also record:
- timestamp-quality transition count;
- same-device recovery counters;
- MMCSS registration/revert state;
- resampler delay/flush frames;

Required:

- packet pool capacity constant;
- SPSC capacity constant;
- retained audio bytes constant;
- no source-worker count growth;
- no COM/event handle growth attributable to repeated packets;
- private-memory behavior plateaus after warm-up.

### Start/Stop soak

100 cycles:

`Start -> Armed -> commit -> short record -> Stop -> Ready`.

After final cycle:

- worker count returns to baseline;
- pool slots all returned;
- endpoint clients released;
- event handles released;
- writer finalized once per session;
- no stale callback touches a destroyed owner.

---

## 10. Device failure gate

Fault cases:

- microphone unplug/replug of the same physical endpoint;
- output device invalidated/reappears;
- endpoint removed before start;
- Windows default endpoint changes during an active session;
- two endpoints share the same friendly name;
- stale device token;
- format negotiation failure;
- AAC configuration failure.

Requirements:

- no crash/deadlock;
- no silent switch to unrelated endpoint or a newly changed default;
- same selected endpoint may recover only through explicit identity resolution;
- friendly-name collisions cannot select identity;
- explicit diagnostic/source state;
- timeline remains monotonic;
- video remains recoverable;
- Stop/Finalize completes.

---

## 11. MP4 validation

Final file must verify:

- H.264 video stream exists;
- one AAC program stream exists when audio requested;
- no AAC stream when audio disabled;
- expected sample rate/channels/bitrate metadata;
- nonzero and monotonic sample durations;
- playable in normal Windows player + another independent player/editor;
- audio/video durations are coherent;
- seeking works;
- finalization does not hang.

---

## 12. Diagnostics evidence required in issue closure

Attach or paste:

- exact commit;
- CI run;
- artifact id/digest;
- requested sources;
- negotiated native formats;
- resolved AudioProfile;
- packet/frame/discontinuity/overflow counters;
- drift ppm min/max;
- resample ratio min/max;
- initial/final A/V skew;
- AAC configuration;
- memory start/end/peak;
- worker teardown counters;
- MP4 stream inspection result.

---

## 13. Closure rule

P7A can close only after:

1. deterministic tests green;
2. Windows release CI green;
3. mic-only real recording accepted;
4. system-only real recording accepted;
5. dual-source real recording accepted;
6. 60-minute drift gate accepted;
7. memory/resource soak accepted;
8. video-only regression accepted.

Short demos do not close the milestone.
