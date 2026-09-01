# ADR-042: CPU/GPU Timestamps and Pipeline Statistics

- **Status**: Proposed
- **Date**: 2026-09-01
- **Supersedes**: None
- **Scope**: Renderer timing clocks, query plans, delayed results, pipeline statistics and observability projection
- **Issue**: [RND-017.2](https://github.com/abdullahbodur/horo-engine/issues/434)
- **Jira**: [HORO-434](https://horo-engine.atlassian.net/browse/HORO-434)
- **Related**: [ADR-018](018-command-registration-permissions-threading-and-packaged-build-policy.md),
  [ADR-027](027-renderer-resource-identity-and-descriptors.md),
  [ADR-033](033-presentation-and-display-ownership.md),
  [ADR-034](034-gpu-memory-and-residency-ownership.md),
  [ADR-039](039-ray-tracing-capability-and-abstraction.md),
  [ADR-040](040-reconstruction-frame-generation-and-latency-providers.md)
- **Companion decisions**: [ADR-028: Renderer Capability, Limits and Product Profiles](028-renderer-capability-limits-and-product-profiles.md), [ADR-041: Backend-Neutral Renderer Diagnostics Model](041-backend-neutral-renderer-diagnostics-model.md)
- **Normative documents**: [Metrics And Profiling](../architecture/observability/observability-performance.md), [Rendering Architecture](../architecture/runtime/rendering-architecture.md), [Render Backend Parity](../architecture/runtime/render-backend-parity-contract.md)

## Context

Horo defines frame metrics and calibrated profiler timelines but does not yet
define who places renderer timestamps, which clock domains may be compared, how
native queries are budgeted, or how delayed GPU results retain their source frame.
Pipeline-statistic queries differ by backend and can materially perturb rendering.
Treating unsupported counters as zero or adding overlapping CPU/GPU durations
would produce plausible but false performance data.

GPU query results are asynchronous. Reading them in the frame that records them
can stall the CPU or GPU, while reusing a query slot before its submission retires
corrupts results. Device loss, timestamp wrap, frequency uncertainty, queue clock
differences and out-of-order completion all require explicit validity state.

This ADR defines the common measurement plan, clock/calibration model, query
lifetime, result schema and observability projection. It does not define the
Performance tab, graph/resource inspector, external capture tools, benchmark
thresholds, reference images, driver workarounds or packaged qualification gates.

## Decision

### 1. Measurement is observational and uses three signal paths

Renderer measurement cannot influence gameplay, graph correctness, quality,
dynamic resolution, fallback or presentation policy. Horo separates:

| Path | Use | Retention |
|---|---|---|
| Always-on renderer metrics | Low-cost real-frame CPU/GPU totals and fixed counters. | Bounded `MetricsStore` aggregates. |
| Explicit profiler capture | CPU scopes, per-queue/pass GPU zones and correlation detail. | Bounded capture session and manifest. |
| Pipeline-statistic sample/capture | Qualified hardware invocation/primitive counters for selected non-overlapping scopes. | Bounded sampled metrics or capture payload. |

ADR-041 events report availability, invalidation, saturation, failure and recovery.
They do not carry one timing/statistic payload per frame. `Result<T, Error>` remains
the control-flow contract for configuring or requesting instrumentation. Metrics
and captures never become resource/feature admission authority.

Instrumentation is described by one immutable `RendererInstrumentationPlan`:

```cpp
struct RendererInstrumentationPlan {
    RendererInstrumentationPlanId id;
    RendererInstrumentationGeneration generation;
    RendererInstrumentationChannels channels;
    RendererInstrumentationBudgets budgets;
    RendererSamplingPolicy sampling;
    RendererMeasurementFallbackPolicy fallback;
    EffectiveCapabilitiesRevision capabilities;
    ProductProfileId productProfile;
    DeviceGeneration device;
};
```

The frontend resolves requested channels against effective backend support,
product/build policy, native query limits, frames in flight and finite memory.
Missing support for a required request returns a typed error before graph work.
Optional policy may select only a declared `frame_totals`, `cpu_only` or `off`
instrumentation fallback and records why. It never changes rendering features.

### 2. CPU timing uses one process monotonic domain

All CPU intervals use the process `steady_clock` domain already owned by
Observability. Wall/UTC time labels a session but is never used for duration,
ordering or CPU/GPU correlation. A measurement records begin/end monotonic ticks,
clock identity/resolution and owner thread or logical task context.

Canonical real-frame CPU intervals are non-overlapping meanings rather than a
sum formula:

| Interval | Boundary |
|---|---|
| `frame.cpu` | Host admission of one real frame through completion of CPU submission work for that frame; excludes later presentation wait. |
| `render.prepare.cpu` | Frontend graph/plan preparation owned by that real frame, including admitted worker completion but excluding unrelated queue wait. |
| `render.submit.cpu` | Owner-thread backend encoding/submission calls for the real frame. |
| `present.cpu` | CPU time inside the ADR-033 present operation, reported separately from any display/pacing estimate. |
| named CPU profiler zone | Explicit nested scope during an armed capture; not promoted into an always-on metric automatically. |

Worker work retains task/operation and real-frame IDs. Overlapping workers are not
added into `frame.cpu`; profiler views show their intervals separately. A cancelled
or failed frame may publish a result with `outcome`, but it never increments the
successful real-frame count. Synthetic presentation has a separate presentation
interval and cannot create `frame.cpu` or `render.submit.cpu` samples.

Dotted interval IDs (`frame.cpu`, `render.submit.cpu`, `present.cpu`) are the
canonical measurement names inside the instrumentation plan. Observability
metrics in Section 7 are their published projections: `frame.cpu` duration of a
successful real frame becomes `engine.frame.cpu_time`, the qualified GPU span
becomes `engine.frame.gpu_time`, and `present.cpu` (or a separately identified
presentation estimate) becomes `engine.frame.present_time`. They are one
measurement with two names, not two clocks.

### 3. GPU clocks are per device and queue until calibration proves more

Native GPU timestamps are opaque ticks. A backend publishes a generation-scoped
clock description only after validation:

```cpp
struct GpuClockCalibration {
    DeviceGeneration device;
    RenderQueueId queue;
    GpuClockDomainId domain;
    uint64_t epoch;
    double secondsPerTick;
    uint8_t validBits;
    MonotonicTimePoint cpuReference;
    GpuTimestamp gpuReference;
    Duration maximumCorrelationError;
    MonotonicTimePoint calibratedAt;
    Duration validityWindow;
};
```

`secondsPerTick` must be finite and positive. `validBits` defines modular wrap;
the backend unwraps only intervals shorter than the unambiguous half-range. An
interval crossing a reset, disjoint period, invalid epoch or unrepresentable wrap
is invalid, not clamped.

Two timestamps in the same validated clock domain may produce a duration. A CPU
and GPU timestamp, or timestamps from different GPU domains/queues, share a
timeline only when one calibration generation relates them and carries a finite
error bound. The default maximum accepted correlation error is 250 microseconds;
product/profile policy may tighten it but cannot exceed one millisecond. Above the
bound, per-domain durations may remain valid while cross-domain ordering/overlap is
`UnavailableCalibration`.

Calibration expires after the backend-declared window, and no later than 10
seconds for a detailed capture without refresh. Device recreation/loss, queue
replacement, timestamp frequency/reset/disjoint notification or measured drift
beyond the admitted error invalidates it immediately. Recalibration publishes a
new epoch; old results retain their old epoch and are never relabeled.

Canonical GPU intervals are half-open `[begin, end)` query pairs. Negative or
zero-after-invalid-wrap durations are rejected. A frame total is the earliest
qualified begin to latest qualified end only within one correlated GPU domain;
it is a span, not a sum of pass durations. Across uncorrelated queues Horo reports
per-queue spans and no fabricated whole-frame total. Queue overlap remains overlap.

### 4. Graph placement and query ownership are explicit

`RenderFrontend` compiles stable `RendererMeasurementScopeId` values into the
render graph. Scope identity comes from registered static descriptors such as
frame, queue, pass kind or bounded named group; resource/asset/user strings cannot
create dynamic scopes on the frame path.

The graph compiler validates:

- begin/end placement on one queue and one real-frame graph generation;
- supported timestamp stages for the selected queue and operation;
- non-overlapping pipeline-statistic scopes unless the backend explicitly reports
  qualified nested-query semantics;
- unique query indices and checked capacity for every in-flight plan generation;
- pass culling/merging preserving or explicitly invalidating requested scope
  semantics; and
- no measurement scope around work that can migrate to an undeclared queue.

The backend owns native query pools/heaps/buffers and private API commands. Those
native heaps and readback buffers are ADR-027 resident resources charged under
ADR-034: they use `Pending`/`Ready`/`Retiring`/`Retired`/`Failed` and
`ResourceOperationId` for copies. The frontend-owned query **ring slot** is not a
public `ResourceHandle`. It is a plan-scoped pool index into that Ready heap,
owned until the corresponding GPU completion token retires. Slot reuse is the
same completion barrier as ADR-027 `Retiring` → `Retired`, without giving every
timestamp pair a named public handle. No backend allocates a query pool or
readback buffer opportunistically mid-frame. Capacity is reserved before frame
admission.

The baseline always-on plan reserves one GPU begin/end pair for every graphics,
compute or transfer queue that participates in the admitted real-frame graph and
has effective timestamp support for the required stages. A required whole-frame
plan rejects a participating queue without that support. Optional policy records
the queue as unavailable and publishes only explicitly partial per-queue coverage;
it never labels the observed subset as whole-frame GPU time.

[ADR-039](039-ray-tracing-capability-and-abstraction.md) AS build, update,
compaction, copy and ray dispatch are ordinary graph work. If they participate in
the real-frame graph they receive the same timestamp scopes. A dedicated
compute/copy queue used for AS work is a participating queue under this rule:
ADR-028 compute does not imply that queue or timestamp support on it. Enabling
ray tracing cannot silently break a required whole-frame GPU timing plan. Either
the AS queue has effective timestamps, the required plan fails admission, or an
optional plan publishes partial per-queue coverage and never calls it
whole-frame GPU time. Ray-specific profiler zones are named scopes, not a second
query model.

Detailed timing is
off until requested and defaults to at most 128 GPU scopes per real frame, with a
hard maximum of 512.
Pipeline statistics default to at most 32 scopes on one sampled real frame, with a
hard maximum of 128. Every plan also validates:

- `framesInFlight * queriesPerFrame` using checked arithmetic;
- at most 8 result batches pending per queue by default and 32 at the hard limit;
- at most 4 MiB persistent query/readback storage by default and 32 MiB hard limit;
- one-second minimum between automatic pipeline-statistic sample frames by
  default; and
- no runtime budget growth or catch-up burst after a skipped sample.

Profiles may lower these limits. Increasing them requires a new validated plan at
[ADR-018](018-command-registration-permissions-threading-and-packaged-build-policy.md)
`CommandThreadPolicy::RenderSafePoint`. Pipeline statistics are disabled by default in Shipping;
detailed GPU scopes require an allowed profiler/diagnostics profile. Always-on
frame totals remain optional when the backend cannot implement timestamps.

### 5. GPU results are delayed, generation-tagged and never polled by stalling

Each real-frame submission receives a query allocation from a ring slot that is
owned until the corresponding GPU completion token retires. Normal rendering never
waits for query availability, calls device idle, maps an in-use buffer or performs
synchronous readback. The backend stages native resolve/copy work in the graph when
required and checks completed slots only at bounded collection points.

Collected data enters this backend-neutral envelope:

```cpp
struct RendererMeasurementBatch {
    RendererMeasurementBatchId id;
    RendererInstrumentationGeneration plan;
    DeviceGeneration device;
    EffectiveCapabilitiesRevision capabilities;
    RealRenderFrameId sourceFrame;
    GraphExecutionId graph;
    RenderQueueId queue;
    GpuClockDomainId clock;
    uint64_t clockEpoch;
    uint32_t resultAgeInRealFrames;
    MeasurementBatchStatus status;
    std::span<const GpuTimingSample> timings;
    std::span<const PipelineStatisticSample> statistics;
};
```

Batches may complete out of submission order. Stores index them by source real
frame and generation, never by “current frame”. A synthetic presentation frame may
reference its bracketing real-frame measurements but owns no render query batch.

Status is one of `Valid`, `Partial`, `NotReady`, `Unsupported`, `DisabledByProfile`,
`InvalidCalibration`, `Disjoint`, `Overflow`, `Cancelled`, `StaleGeneration` or
`DeviceLost`. Every sample has an explicit validity bit/reason; absence is not zero.
`Partial` identifies exactly which registered scopes/statistics are valid.

A not-ready batch remains pending without blocking. At the plan's maximum pending
age, eight subsequent real frames by default and at most 32, it is retired as
`NotReady` with a bounded ADR-041 aggregate. Query slots are not reused until GPU
completion proves safety even when publication age expires. Capacity pressure
drops optional new measurement admission or follows the declared instrumentation
fallback; it does not drop/render-stall the real frame.

Cancellation suppresses publication but retains query/readback ownership through
completion. Device loss marks unresolved batches lost, invalidates calibration and
releases native objects through the device-loss retirement contract. Stale
completion cannot enter a new device/plan generation. Shutdown stops new
instrumentation, retires/cancels batches, unregisters collection callbacks on the
owner thread and then releases pools; it is idempotent after partial setup.

### 6. Pipeline statistics use a qualified canonical set

Horo exposes only counters whose semantics the backend can map and qualify:

| `PipelineStatistic` | Canonical count |
|---|---|
| `InputAssemblyVertices` | Vertices consumed by qualified input-assembly work. |
| `InputAssemblyPrimitives` | Primitives consumed by qualified input-assembly work. |
| `VertexShaderInvocations` | Vertex shader invocations, not unique vertices. |
| `ClippingInputPrimitives` | Primitives submitted to the clipping stage. |
| `ClippingOutputPrimitives` | Primitives emitted by the clipping stage. |
| `FragmentShaderInvocations` | Qualified fragment shader invocations according to the Horo semantic revision. |
| `ComputeShaderInvocations` | Total logical compute shader invocations, not workgroups. |
| `TaskShaderInvocations` | Task/amplification shader invocations when exactly representable. |
| `MeshShaderInvocations` | Mesh shader invocations when exactly representable. |

Each effective capability entry includes statistic identity, semantic revision,
supported queue/stage/scope combinations, counter bit width, nesting support and
maximum simultaneous counters. A native statistic with different granularity,
clipping/raster rules or invocation meaning is unavailable to that canonical ID;
the backend cannot relabel an approximation.

Counter deltas use modular arithmetic only within the declared bit width and
unambiguous range. Overflow, native disjoint/reset and begin/end mismatch invalidate
the sample. Nested or overlapping scopes cannot be summed. Counts from sampled
frames are labeled samples and are not extrapolated into unsampled totals. CPU
submission counters such as draw calls/triangles remain separate estimates and
must identify their source; they cannot masquerade as hardware pipeline statistics.

Null may execute synthetic, explicitly fixture-labeled statistics for contract
tests. It reports no hardware support and cannot qualify native semantics or cost.

### 7. Publication uses bounded metrics and profiler contracts

The renderer measurement collector validates a completed batch, converts ticks to
seconds with the batch's calibration epoch and publishes through pre-registered
Observability handles. It never performs dynamic metric lookup on a render thread.

Always-on descriptors include the Observability names of the Section 2
intervals (`frame.cpu` → `engine.frame.cpu_time`, and the GPU/present
counterparts):

| Metric | Kind/unit | Meaning |
|---|---|---|
| `engine.frame.cpu_time` | Histogram/seconds | Successful real-frame `frame.cpu` interval. |
| `engine.frame.gpu_time` | Histogram/seconds | Qualified real-frame GPU span; unavailable when no trustworthy total exists. |
| `engine.frame.present_time` | Histogram/seconds | CPU present operation or separately identified presentation estimate, never both combined. |
| `renderer.measurement.dropped` | Counter/count | Results rejected by bounded admission, labeled by fixed reason. |
| `renderer.measurement.age` | Histogram/count | Real-frame delay from source to publication. |

Allowed low-cardinality dimensions are fixed values such as `queue_class`,
`outcome`, `source` and `synthetic_or_real`; backend/device/frame/pass IDs are log
or profiler context, not metric dimensions. Product/backend identity already lives
in the session manifest and does not create dynamic series.

Detailed pass timing and per-scope pipeline counters go to an armed profiler
capture or bounded inspection snapshot. A sampled aggregate metric may use only a
registered finite `PipelineStatistic` and scope-class dimension with descriptor
`maxSeries`; it records sample count and sampling cadence. It never writes one
persistent metric row per frame/pass.

Persistent summaries use the existing versioned `metrics.jsonl` aggregate format.
Profiler payloads record plan ID/generation, source frame range, clock domains,
calibration epochs/error bounds, unavailable/partial intervals, sampling cadence,
query budgets and instrumentation overhead metadata. A viewer does not align CPU
and GPU tracks when calibration is invalid.

External telemetry remains an application-composed Observability exporter. The
renderer performs no network access, retry, authentication or vendor telemetry.
Export preserves availability/sample/correlation error and cannot turn missing
values into zero. High-cardinality pass/statistic detail is local capture data by
default and requires explicit allowlisting for any external export.

### 8. Failure, diagnostics and overhead remain observable

Configuration distinguishes `TimestampUnsupported`, `StatisticUnsupported`,
`InvalidClockDescription`, `InvalidInstrumentationPlan`, `QueryBudgetExceeded`,
`NestedScopeUnsupported`, `CalibrationUnavailable` and `ProfileDisabled` typed
results. Runtime collection distinguishes the batch statuses above. Optional
unavailability publishes one state/aggregate through ADR-041 and an Observability
availability revision, not one warning each frame.

Every capture/inspection result reports whether instrumentation was enabled,
scope/query counts, sampling ratio, pending age, query/readback bytes and dropped
or invalid samples. Performance claims compare like-for-like instrumentation
plans. Pipeline statistics and detailed scopes are acknowledged as perturbing;
benchmark gates either disable them or use a separately qualified measurement
configuration.

No diagnostic or measurement failure silently changes frames in flight, present
mode, async-compute placement, render scale, pass selection or backend. A required
measurement request fails explicitly. An optional request may degrade only its
instrumentation plan while rendering continues unchanged.

## Rejected Alternatives

| Option | Decision and reason |
|---|---|
| Read GPU queries in the recording frame | Rejected: may block normal CPU/GPU progress; collect only after completion. |
| Treat CPU and GPU timestamps as one clock | Rejected: domains, drift and correlation uncertainty differ. |
| Sum pass or queue durations into frame GPU time | Rejected: work overlaps and nested scopes double count; use a qualified span or separate queues. |
| Report unsupported/not-ready statistics as zero | Rejected: zero is a valid measurement; availability is explicit. |
| Create metric series per pass/resource/frame | Rejected: unbounded cardinality belongs in bounded captures/snapshots. |
| Enable pipeline statistics continuously in all builds | Rejected: cost and native support vary; use sampled/profile-gated plans. |
| Let backends invent scope names or statistic semantics | Rejected: frontend descriptors and canonical semantic revisions preserve parity. |
| Grow query pools after saturation | Rejected: hides memory/work growth; resolve a new finite plan or drop optional measurement admission. |
| Give every timestamp pair a public ADR-027 handle | Rejected: native heaps follow ADR-027; ring slots are plan-scoped indices behind GPU-completion retirement. |
| Exempt ADR-039 AS queues from whole-frame timestamp rules | Rejected: a participating queue without timestamps fails a required plan or is labeled partial, never silent. |
| Use timing metrics to drive rendering correctness/policy | Rejected: observability is not an admission or quality authority. |
| Upload detailed measurements from the backend | Rejected: export, privacy and network policy belong to process Observability/Application composition. |

## Migration And Verification

Existing CPU frame timers map to the canonical process monotonic intervals and
registered metric handles. Existing `supports timestamp queries` booleans become
typed reported/implemented/effective operation plus limit records. No backend
advertises a statistic until its exact Horo semantic mapping and native fixture are
qualified. Presentation and synthetic-frame metrics adopt ADR-033/040 identities.

RND-017.9 consumes immutable delayed batches/snapshots; it does not poll native
queries. RND-017.11 defines benchmark thresholds using fixed instrumentation plans
and availability requirements. RND-017.12 records native backend/device/profile
qualification evidence. ADR-041 supplies all availability/failure event identity.

Tests must cover:

- clock description validation, finite tick periods, bit-width wrap, half-range,
  reset/disjoint and calibration epoch expiry;
- same-domain duration, cross-queue/CPU correlation with error bounds, drift above
  threshold and no fabricated total for uncorrelated queues;
- exact CPU interval boundaries, worker overlap and separate present/synthetic
  presentation timing;
- graph scope begin/end, pass culling/merge, queue/stage placement, nesting rules
  and checked query-capacity arithmetic;
- graphics, compute and transfer queue participation, required unsupported-queue
  rejection and optional partial coverage that never becomes whole-frame time;
- ADR-039 AS/compute-copy queues as participating queues under the same
  timestamp rule;
- `frame.cpu` publishing as `engine.frame.cpu_time` and counterparts;
- default/hard scope, pending-batch, storage and sampling limits, plus no catch-up
  burst or runtime growth;
- delayed and out-of-order completion retaining source frame, plan/device/clock
  generations and result age;
- not-ready expiration without query-slot reuse before GPU completion, native
  query heaps following ADR-027 retirement while ring slots stay plan-scoped,
  saturation without render stall and declared instrumentation-only fallback;
- each canonical pipeline statistic against native fixtures, unsupported semantic
  mappings, counter overflow and no sampled-frame extrapolation;
- typed unsupported/required failure, optional availability revision, bounded
  ADR-041 aggregation and recovery;
- metric cardinality/availability, profiler manifests, aggregate persistence,
  redaction/export allowlists and no missing-to-zero conversion;
- cancellation, stale completion, device loss/recreation, partial initialization,
  callback removal and repeated shutdown; and
- Null synthetic schedule fixtures separated from native clock/statistic/performance
  qualification.

## Consequences

CPU/GPU timing and pipeline statistics now have explicit clocks, semantic identity,
finite query ownership and delayed-result behavior. Metrics cannot fabricate zero
or whole-frame totals, and detailed instrumentation cannot silently become
unbounded frame work. The cost is per-backend query/calibration adapters,
generation-tagged pools/readback, fixed descriptor maintenance and native semantic
qualification. This ADR enables no inspector UI, external profiler, benchmark gate
or telemetry upload by itself.
