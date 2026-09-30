# P0 Foundation Baseline

Status: **Accepted baseline — 2026-09-30.**

P0 exists to make every later recorder milestone use the same production
ownership model from the beginning. It deliberately does **not** implement a
temporary screenshot recorder.

## State owners established

### Session

`SessionStateMachine` is the sole canonical recording lifecycle state owner.

### Time

`MonotonicClock` maps platform monotonic time to canonical 100-ns ticks.
On Windows it is backed by QueryPerformanceCounter.

### Realtime semantic events

`SpscRing<T, N>` is a fixed-capacity single-producer/single-consumer event
channel. Overflow is explicit; capacity cannot grow.

### Replaceable realtime state

`LatestAtomic<T>` implements bounded latest-wins semantics for small
lock-free atomic values. It is intentionally not used as a generic resource
container.

Large/non-trivial resources such as D3D capture frames will receive a dedicated
P1 bounded slot with explicit COM/GPU lifetime ownership.

### Diagnostics

`Diagnostics` contains bounded atomic counters only. Formatting, files, JSON,
and network operations remain outside realtime paths.

### Graphics

`D3D11Device` owns the D3D11 device and immediate context through RAII COM
ownership.

Production default: `HardwareOnly`.

CI-only deterministic graphics validation: `WarpForTesting`.

WARP is not an automatic production fallback.

## Failure contracts

P0 uses compact `StatusCode + detail` values. Expected failures do not require
exception control flow.

The D3D11 boundary reports the HRESULT bit pattern through the numeric detail
field without allocating a formatted error string in the platform boundary.

## Deterministic tests

The P0 test suite checks:

- Result success/failure semantics;
- exact fixed-capacity SPSC overflow and FIFO behavior;
- latest-wins coalescing semantics;
- bounded diagnostic counters/max observation;
- monotonic clock non-regression;
- valid and invalid session transitions;
- D3D11 resource ownership using the deterministic WARP test driver.

## P0 completion gates

P0 is accepted only when:

- Windows x64 Release configures and builds;
- all CTest tests pass;
- GitHub CI is green;
- source provenance/license baseline exists;
- no detached worker/thread or unbounded queue exists;
- no placeholder CPU screenshot recorder was introduced.

Performance measurements that require capture/render/encode begin in P1/P2;
P0 itself contains no frame hot path yet.


## Acceptance evidence

- PR: #2 — `P0: establish production recorder foundation`
- Squash merge: `9cf1e1cd770cefb17b4dbbb3ee76efcad4a3c1af`
- Windows CI: Release configure/build succeeded with MSVC 19.51 / VS 2026.
- CTest: 2/2 tests passed.
- Post-merge `main` workflow: success.
- Intentional speculative cache-line padding was removed after MSVC warning;
  it may return only if later benchmark evidence justifies it.
