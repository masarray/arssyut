# P7A — Multi-thread execution and ownership plan

**Purpose:** allow multiple implementation threads/agents to work in parallel
without creating duplicate authorities or merge-conflict churn.

**Rule zero:** architecture docs, ADR-009 and
`P7A_EXTERNAL_AUDIO_RESEARCH.md` are merged before any P7A native
implementation branch starts.

---

## 1. Workflow

The recommended engineering sequence is:

```text
architecture / ADR / acceptance lock
        ↓
umbrella issue + narrow child issues
        ↓
contract-only core PR
        ↓
parallel isolated implementation lanes
        ↓
deterministic lane tests
        ↓
single integration lane
        ↓
failure / soak / real recording evidence
        ↓
main integration
```

Do not allow every thread to edit `RecorderSession`, the bridge and the writer
at the same time.

The highest-conflict integration files are owned by one integration lane only.

---

## 2. Dependency graph

```text
P7A0  Architecture lock (docs only)
  |
  v
P7A1  Canonical audio core contracts
  |\
  | +--------------------------+
  | |                          |
  | v                          v
  | P7A1R Resampler spike      P7A4 AV writer / AAC
  | |
  | +-------------+
  |               |
  v               v
P7A2 Microphone  P7A3 System loopback
  \               /
   +------+-------+
          |
          +---------- P7A1R selected resampler
          |
          v
 P7A5 Mixer / drift / sync
          |
          v
 P7A6 RecorderSession + bridge
          |
          v
 P7A7 Hardening / soak / acceptance
```

P7A2, P7A3, P7A4 and P7A1R can progress in parallel after P7A1 contracts are
frozen. P7A5 is blocked on the selected resampler result plus the accepted source
contracts it consumes.

---

## 3. Branch policy

Every child issue starts from the latest accepted `main` or from a specifically
named dependency merge commit.

Suggested names:

- `feat/p7a1-audio-core-contract`
- `spike/p7a1r-audio-resampler`
- `feat/p7a2-wasapi-microphone`
- `feat/p7a3-wasapi-loopback`
- `feat/p7a4-mf-av-writer`
- `feat/p7a5-audio-mix-drift`
- `feat/p7a6-recorder-audio-integration`
- `test/p7a7-audio-soak-acceptance`

Do not maintain one giant long-lived implementation branch with unrelated lane
work unless integration has actually begun.

Each PR body states:

- exact dependency commit;
- files/authority owned;
- files explicitly not touched;
- deterministic gates added;
- diagnostics added;
- real evidence still pending.

---

## 4. Lane ownership matrix

### Lane P7A1 — portable canonical audio core

**Owns conceptually**

- `src/core/audio/*`
- portable audio test files

Responsibilities:

- audio format/profile types;
- frame/time rational math;
- fixed audio block representation;
- bounded ring/pool primitives if a reusable project primitive does not already
  exist;
- drift estimator contract with trusted vs degraded timestamp evidence;
- resampler interface/contract including internal/group delay accounting;
- endpoint/timestamp-quality vocabulary shared by Windows lanes;
- channel mapping/mix math;
- synthetic fixtures.

Must not edit:

- `RecorderSession`;
- native bridge;
- Avalonia;
- WASAPI source implementations;
- Media Foundation writer implementation.

This lane should be portable and deterministic.

### Lane P7A1R — resampler / drift-compensation benchmark

**Owns conceptually**

- benchmark fixtures/prototypes isolated from product integration;
- dependency/license/size report;
- no RecorderSession or bridge code.

Responsibilities:

- compare Media Foundation Audio Resampler against libswresample/SoXR or another
  justified lightweight candidate;
- 44.1 -> 48 and 96 -> 48 quality;
- steady-state CPU and allocation;
- startup latency;
- impulse/sine/sweep fidelity;
- ability to apply smooth ppm-scale rate compensation;
- binary/deployment footprint and licensing;
- deterministic selection recommendation for P7A5.

This lane produces a decision and accepted implementation seam. It must not
smuggle a new third-party dependency into the product merely because a spike
worked.

### Lane P7A2 — WASAPI microphone source

**Owns conceptually**

- `src/platform/windows/audio/wasapi_microphone_source.*`
- microphone-source Windows tests

Responsibilities:

- endpoint activation;
- native format discovery;
- event-driven packet drain;
- COM MTA + MMCSS `Audio` worker lifetime;
- QPC/device-position capture + timestamp-quality classification;
- fixed pool/SPSC publishing;
- silent/discontinuity/timestamp-error propagation;
- device invalidation;
- deterministic teardown.

Must not edit RecorderSession, bridge, UI or MP4 writer.

### Lane P7A3 — WASAPI system loopback source

**Owns conceptually**

- `src/platform/windows/audio/wasapi_loopback_source.*`
- loopback-source Windows tests

Responsibilities mirror P7A2 but use the render endpoint in shared loopback
mode. The source must also prove no-playback silence behavior without depending
on artificial render audio and must preserve pinned endpoint identity across
default-device changes.

Must not duplicate the microphone source's common lifetime/packet code.
If shared code is required, it is introduced through an agreed common
`src/platform/windows/audio` utility seam, not copied.

Must not edit RecorderSession, bridge, UI or MP4 writer.

### Lane P7A4 — AV writer / AAC

**Owns conceptually**

- Media Foundation AV writer implementation and tests;
- writer-stage diagnostics names required by that implementation.

Responsibilities:

- optional AAC stream configuration;
- 44.1/48 kHz PCM16 input contract;
- AAC-LC bitrate configuration/fallback;
- audio sample PTS/duration validation;
- monotonic per-stream writes;
- one finalize path;
- video-only equivalence tests.

Must not implement WASAPI or drift correction.

### Lane P7A5 — mixer, resampling and drift

**Owns conceptually**

- audio engine/mixer layer built on P7A1 contracts;
- synthetic long-duration tests.

Responsibilities:

- own the fixed 48 kHz / 1024-frame `AudioProgramClock` derived from
  RecorderSession media zero;
- render source timelines into exact program intervals rather than following
  packet arrival cadence;
- align source packets to RecorderSession-compatible media time;
- high-quality required SRC;
- bounded clock-drift estimation/correction using trusted timing evidence;
- resampler group-delay/phase accounting in output PTS;
- timestamp fallback/re-lock policy and stale-packet handling;
- mono/stereo mapping;
- source gain/mute;
- headroom/clipping policy;
- fixed AAC-size output blocks;
- per-source peak/RMS/clip latest-wins meter snapshots;
- program-deadline miss diagnostics;
- start trim/silence and stop cut semantics.

Must not enumerate devices, call WASAPI directly, mutate UI or call MF writer
directly.

### Lane P7A6 — integration authority

**This is the only lane allowed to touch the high-conflict product integration
files after parallel lane work is accepted.**

Owns:

- `src/app/recorder_session.*`
- native bridge ABI/request/snapshot changes;
- managed bridge projection;
- UI capability/mute/device state required to expose accepted native audio;
- integration diagnostics plumbing;
- AV writer orchestration;
- latest-wins Mic/System meter projection only (no audio media copy into UI).

Responsibilities:

- resolve one canonical AudioProfile before workers start;
- create/own AudioEngine;
- include requested audio in Preparing/Armed readiness;
- use existing media-zero commit;
- sole caller/owner path into AV writer;
- Stop/Finalize ordering;
- bounded/fair service of ready audio blocks around due video writes;
- writer backlog/high-water diagnostics;
- explicit source failure/status projection;
- remove `UNSUPPORTED` only for actually implemented streams.

No audio capture algorithm is invented in this lane.

### Lane P7A7 — hardening / acceptance

Owns tests, fixtures, scripts and acceptance documentation.

Responsibilities:

- 10/60 minute sync fixtures;
- 100 Start/Stop cycle test;
- device-loss + same-identity recovery/default-device-change scenarios;
- timestamp-error/degraded-timing fixtures;
- no-playback loopback silence fixture;
- memory/resource high-water review;
- pitch/frequency measurement;
- MP4 stream/timestamp inspection;
- real-device matrix;
- final handoff/acceptance lock.

P7A7 may discover defects but does not silently redesign another lane. Defects
return to the owning lane with evidence.

---

## 5. Files with single-writer ownership during parallel phase

Until P7A6 begins, no parallel lane may edit these:

```text
src/app/recorder_session.hpp
src/app/recorder_session.cpp
src/bridge/native_bridge.h
src/bridge/native_bridge.cpp
src/ui/Arssyut.UI/*
docs/CURRENT_HANDOFF.md
```

The writer lane exclusively owns the writer files it introduces/renames.

The capture lanes exclusively own their source files.

This rule exists to make cherry-pick/rebase/integration predictable.

---

## 6. Shared contract freeze

P7A1 publishes the stable types/interfaces required by other lanes.

After P7A1 merges:

- P7A2/P7A3/P7A4/P7A5 branch from that exact merge commit;
- shared type changes require an explicit P7A1 follow-up PR;
- do not locally fork a slightly different packet/format type in each lane.

This prevents “almost the same” structures from becoming permanent duplicate
authorities.

---

## 7. CI policy by lane

Every lane has deterministic gates before integration.

### P7A1

- portable unit tests;
- no Windows audio device dependency;
- synthetic 44.1/48/drift/gap cases;
- bounded ring/pool tests.

### P7A2/P7A3

- Windows build;
- source lifetime tests;
- invalid-device tests;
- event/teardown tests;
- packet metadata/flag propagation;
- no full recording required yet.

### P7A4

- video-only writer regression;
- 44.1 AAC synthetic sample;
- 48 AAC synthetic sample;
- monotonic PTS/duration;
- finalized MP4 stream inspection.

### P7A5

- synthetic tone pitch;
- mixed-rate sync;
- positive/negative drift;
- mute;
- discontinuity/silence;
- clipping fixture;
- hours-equivalent accelerated frame-count math.

### P7A6

- full native recorder integration tests;
- bridge ABI tests;
- audio-disabled baseline;
- mic/system/dual requested-state behavior;
- Armed/start/stop/finalize state tests.

### P7A7

- soak and real Windows acceptance.

---

## 8. Merge order

Recommended:

1. P7A0 documentation/ADR.
2. P7A1 core contract.
3. P7A1R, P7A2, P7A3 and P7A4 can progress in parallel.
4. Lock the resampler decision from P7A1R before P7A5 product integration.
5. P7A5 after the resampler decision and enough source contracts are stable;
   portable synthetic mixer work may start earlier.
6. P7A6 only after P7A2/P7A3/P7A4/P7A5 are independently green.
7. P7A7 closes the milestone.

No child PR merges merely because another lane “needs the code”. It merges when
its own ownership contract and tests are coherent.

---

## 9. Review checklist for every P7A PR

Reviewers answer all of these:

- Does this create another media clock?
- Does this create another RecorderSession?
- Does it allocate per packet/block in steady state?
- Can any queue grow with duration?
- Can an audio callback block on UI, disk, mux or another worker?
- Are sample rate/type/channels explicit?
- Are frame count and byte count distinguished?
- Are timestamps based on canonical 100 ns/QPC?
- Are discontinuity and timestamp-error flags preserved?
- Is overflow observable and deterministic?
- Is every worker joinable with one shutdown path?
- Does the change keep audio-disabled video behavior intact?
- Are diagnostics sufficient to prove failure instead of guessing by ear?

If any answer is unclear, the PR is not ready.

---

## 10. Thread handoff template

Every implementation thread ends with:

```text
P7A lane:
Issue:
Branch / PR:
Base commit:
Head commit:
Owned files:
Contracts consumed:
Contracts changed:
Tests green:
Failure tests:
Performance/resource evidence:
Known gaps:
Blocked next lane:
Do-not-reopen decisions:
```

This handoff is posted in the issue and, once accepted, summarized in the
repository handoff document.

---

## 11. Anti-patterns

Reject these even if they make a quick demo work:

- one thread adds mic directly inside RecorderSession while another writes a
  separate system-audio engine;
- each source invents a different timestamp type;
- using `Sleep(10)` polling for audio packets;
- unbounded `std::vector`/queue of captured audio;
- resampling by skipping/duplicating arbitrary samples;
- changing sample-rate metadata without converting samples;
- writing raw 48 kHz data while declaring 44.1 kHz or vice versa;
- stopping/restarting microphone to implement mute;
- calling the sink writer from multiple realtime workers behind a mutex;
- changing video clock/cadence to follow an audio device;
- silently changing endpoint mid-record;
- judging sync only from a short listening test.

---

## 12. Multi-thread success criterion

Parallelism is successful only if the final integration is boring.

By the time P7A6 starts, microphone capture, loopback capture, writer/AAC and
mix/drift logic should already be individually deterministic and bounded. P7A6
should mostly wire accepted authorities together, not discover what an audio
packet or timestamp means.
