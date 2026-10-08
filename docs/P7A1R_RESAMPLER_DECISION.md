# P7A1R Resampler Decision Record

**Status:** post-merge candidate hardening in progress; P7A5 remains blocked  
**Baseline:** `main@11e70fcd4bded6c732cc1bd903abf757864b0782`  
**Tracking:** #72 / PR #80

## Product requirement

P7A5 needs one resampling authority that can provide both:

1. high-quality static conversion into the fixed 48 kHz stereo float32 program bus; and
2. smooth, bounded ppm-scale rate adjustment for long-session device-clock drift.

A backend that only satisfies (1) is not sufficient as the sole product authority.

## Media Foundation Audio Resampler result

The Windows `CLSID_CResamplerMediaObject` candidate is retained as the
lowest-deployment-surface static SRC baseline.

CI evidence exercises:

- float32 stereo 44.1 kHz -> 48 kHz;
- float32 stereo 96 kHz -> 48 kHz;
- 440 Hz pitch preservation;
- 1 kHz pitch preservation;
- bounded output-frame accounting;
- impulse response / algorithmic-delay sanity;
- explicit EOS + drain behavior;
- CPU processing timing in the benchmark output.

The transform is instantiated through `IMFTransform` and quality/channel
configuration is exposed through `IWMResamplerProps`.

## Drift-compensation finding

The public `IWMResamplerProps` control surface used by the Windows Audio
Resampler does not expose a continuous ppm/rate-compensation control equivalent
to FFmpeg/libswresample `swr_set_compensation`.

Changing media types repeatedly to approximate tiny drift corrections would
introduce format churn and reset/filter-state ambiguity. That violates P7A's
stateful-resampler, bounded-delay and no-churn requirements.

Therefore:

- **Media Foundation is acceptable as a static-SRC reference/candidate.**
- **Media Foundation is not accepted as the sole P7A5 resampler authority.**
- P7A5 must not emulate normal drift by periodic sample drop/duplication.
- P7A5 must not add a low-quality second correction stage merely to preserve a
  zero-dependency decision.

## Dependency decision

The next decision candidate is libswresample because it exposes explicit soft
compensation semantics. It is **not yet accepted as a product dependency**.

Before acceptance, the repo must measure and document:

- 44.1 -> 48 and 96 -> 48 pitch/frame accuracy;
- impulse/group delay and drain tail;
- positive and negative ppm compensation;
- hours-equivalent frame-count convergence;
- CPU and steady-state allocation behavior;
- binary/package footprint;
- Windows deployment mechanics;
- LGPL compliance/update surface.

No RecorderSession, bridge, UI, WASAPI source or AV-writer integration is
allowed to depend on libswresample until that evidence exists.

## Gate for P7A5

Issue #69 remains blocked until one backend passes both static-SRC and smooth
ppm-compensation requirements.

The purpose of this gate is to prevent a locally convenient static resampler
from forcing coarse sync correction or duplicated timing authority into the
production mixer.


## Post-merge hardening gate

A final review after PR #81 identified four evidence gaps that must be closed
before the libswresample selection is consumable by P7A5:

1. hours-equivalent repeated compensation convergence;
2. explicit impulse/group-delay and complete drain-tail evidence;
3. steady-state cost evidence without per-chunk harness allocation;
4. pinned/recorded candidate version, binary footprint and license evidence.

The follow-up hardening lane therefore requires:

- +100 ppm and -100 ppm over one hour equivalent, refreshed every 10 seconds
  (360 correction windows per direction);
- absolute accumulated frame-accounting error <= 8 frames over the hour fixture;
- chunked impulse conversion at 44.1 -> 48 and 96 -> 48 with bounded peak delay,
  pre-drain delay, complete EOS drain and post-drain delay evidence;
- fixed input/output scratch capacity across the entire hours fixture;
- elapsed processing time and process private-memory growth telemetry;
- FFmpeg candidate version exactly 9.0.2 for this decision evidence;
- vcpkg checkout commit recorded in CI;
- SHA-256 and byte size for the static swresample/avutil libraries;
- retained FFmpeg copyright/license artifact.

The benchmark remains isolated. These checks do not authorize product linkage or
installer changes. Product packaging/licensing remains a separate P7A5/P7A6
integration decision.
