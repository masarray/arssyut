# P1 Native Capture Baseline

Status: **Implementation complete; direct desktop/soak acceptance pending.**

P1 establishes the real Windows video ingress and GPU presentation pipeline.
It deliberately does not create a disposable GDI/screenshot recorder and does
not write raw BGRA frames to disk.

## 1. Production path

```text
Windows.Graphics.Capture
    CreateForWindow / CreateForMonitor
        |
        v
Direct3D11CaptureFramePool::CreateFreeThreaded
        |
        | FrameArrived internal worker
        v
LatestFrameSlot (3 fixed slots, latest wins)
        |
        | FrameLease keeps WGC surface alive
        v
NativeVideoPipeline
        |
        +-> FrameScheduler (canonical 30/60 FPS timeline)
        |
        +-> capture_texture()
        |
        v
D3D11Compositor
        |
        +-> retained GPU input-copy texture
        +-> crop/scale fullscreen-triangle pass
        +-> retained output texture
        |
        v
P2 encoder boundary
```

The WGC system-relative timestamp is already QPC-based and is stored directly
in the project's canonical 100-ns `TimePoint`.

## 2. Capture ownership

`WgcCaptureSource` owns one joinable MTA thread.

The thread owns:

- WinRT apartment lifetime;
- GraphicsCaptureItem;
- free-threaded frame pool;
- GraphicsCaptureSession;
- frame/closed event subscriptions.

There is no detached worker.

`stop()` requests owner shutdown and joins the owner thread before source
state is released.

## 3. Frame callback contract

The steady-state `FrameArrived` callback performs only:

1. `TryGetNextFrame`;
2. bounded metadata extraction;
3. sequence/timestamp assignment;
4. publish into the fixed latest-frame slot;
5. bounded atomic diagnostics;
6. return.

No file I/O, logging, color analysis, encoding, CPU image conversion, or
unbounded allocation belongs in this callback.

## 4. Latest-frame coalescing

`LatestFrameSlot` has exactly three slots.

Slot states:

```text
Free -> Writing -> Ready -> Reading -> Free
```

When a newer frame arrives while an older frame is still unread, the old
`Ready` frame is retired and the new frame becomes authoritative.

A frame already leased by the video consumer is never force-released.

This means:

- capture never builds seconds of latency;
- memory cannot grow with session duration;
- the WGC surface remains alive while D3D consumes it;
- semantic frame ownership is explicit.

## 5. Resize safety

A WGC frame-pool resize is not performed while a frame from the old pool is
still leased.

On a content-size change:

1. discard stale unread frame;
2. if a consumer still owns an old frame, drop the resize frame and return;
3. a later callback retries;
4. once no old frame is in flight, close the current frame;
5. call `FramePool.Recreate`;
6. publish frames from the new geometry.

Resize work is exceptional, not steady-state work.

## 6. Canonical output scheduler

`FrameScheduler` represents output cadence as a rational frame rate.

The scheduler does not accumulate catch-up work.

If processing wakes late, it returns the latest due output slot plus an exact
`skipped_intervals` count.

This is the P2 contract for choosing a bounded duplicate/drop policy without
building a raw-frame backlog.

Tests prove exact landmarks such as:

- 60 FPS frame index 30 = 0.5 seconds;
- 60 FPS frame index 60 = 1.0 seconds.

## 7. GPU compositor

The compositor does not assume WGC frame-pool textures expose the exact shader
bind flags desired by Arssyut.

Instead:

```text
WGC texture
 -> CopyResource (GPU -> GPU)
retained shader-readable input texture
 -> crop + linear scale shader
retained BGRA output texture
```

The input texture/SRV is rebuilt only if source geometry/format changes.

The output texture/RTV is rebuilt only if requested output geometry changes.

Steady-state render creates no texture, RTV, SRV, shader, sampler, constant
buffer, or query resource.

## 8. Performance instrumentation

P1 instruments:

- capture frames received;
- unread frames replaced;
- busy-slot drops;
- source resize count;
- source close count;
- callback failures;
- maximum frame-slot pressure;
- newly rendered output frames;
- reused output frames;
- unavailable output slots;
- skipped cadence intervals.

Latency histograms are fixed-size atomic buckets.

Available quantiles:

- p50 upper bound;
- p95 upper bound;
- p99 upper bound.

Measured paths:

- WGC callback duration;
- compositor CPU submit duration;
- compositor GPU execution duration.

GPU timing uses a four-slot D3D11 timestamp/disjoint query ring and
`D3D11_ASYNC_GETDATA_DONOTFLUSH`. A frame never waits for a timing query.

## 9. Recoverable session boundary

P1 creates a real crash-recovery directory/manifest contract without inventing
a raw-frame media format.

Directory:

```text
<session-id>.arssyut-session/
    manifest.v1
```

Manifest updates use:

```text
write temporary file
-> FlushFileBuffers
-> MoveFileEx(REPLACE_EXISTING | WRITE_THROUGH)
```

The manifest contains:

- format version;
- recovery state;
- output dimensions;
- canonical frame rate;
- intended output path.

Encoded video payload is intentionally P2 work. P1 does not write uncompressed
frames to disk merely to claim it has a recorder.

## 10. Automated validation

Windows Release CI covers:

- frame-rate math and late-slot coalescing;
- crop bounds/fail-safe behavior;
- bounded latency histogram quantiles;
- frame-slot latest-wins semantics;
- repeated frame-slot churn;
- D3D11 WARP device ownership;
- GPU crop/scale on a synthetic texture;
- steady-state compositor resource reuse;
- output resize resource rebuild;
- recoverable manifest creation/update;
- terminal recovery state;
- controlled no-frame video-pipeline behavior.

## 11. Direct desktop validation probe

CI builds:

`arssyut_p1_probe.exe`

Usage:

```powershell
# 10-second smoke test, 60 FPS output cadence
.\arssyut_p1_probe.exe 10 60

# 30-minute P1 soak
.\arssyut_p1_probe.exe 1800 60

# 30-minute 30 FPS comparison
.\arssyut_p1_probe.exe 1800 30
```

The probe uses the production WGC/frame-slot/scheduler/compositor path and
prints:

- received/replaced/dropped frame counts;
- rendered/reused/unavailable output slots;
- skipped output intervals;
- compositor resource generation;
- process private-memory start/end/max/delta;
- capture callback p50/p95/p99;
- compositor CPU p50/p95/p99;
- compositor GPU p50/p95/p99.

The probe exits non-zero if no usable WGC frame is processed, the pipeline
fails, or busy-slot drops exceed its conservative smoke-test gate.

### Repeated lifecycle validation

On an interactive Windows desktop:

```powershell
1..100 | ForEach-Object {
    .\arssyut_p1_probe.exe 1 60
    if ($LASTEXITCODE -ne 0) { throw "P1 probe failed at cycle $_" }
}
```

This specifically exercises repeated D3D/WGC owner construction, start, stop,
event revocation, thread join, and teardown.

## 12. Acceptance boundary

Automated CI can prove deterministic math, resource ownership, fixed-capacity
behavior, GPU compositor correctness, and recovery-file semantics.

Hosted CI is not considered evidence for real interactive desktop capture
quality. Final P1 acceptance therefore also requires the probe on a real
Windows desktop for:

- 1080p-class 30 FPS soak;
- 1080p-class 60 FPS soak;
- repeated start/stop;
- source resize behavior;
- stable private memory after warm-up;
- no excessive busy-slot drop;
- credible callback/CPU/GPU latency.

This limitation is explicit rather than replacing real WGC validation with a
headless synthetic claim.
