# ADR-143: Terrain/Foliage Scale Budgets, Observability and Feature Boundary

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: Terrain/Foliage 1.0 core and high-end qualification workloads, active scale, resident and per-frame streaming budgets, cook throughput, editor latency, headless overhead, measurement ownership, regression gates and post-1.0 GPU-driven boundary
- **Issue**: [TRF-007.1](https://github.com/abdullahbodur/horo-engine/issues/1977)
- **Jira**: [HORO-1933](https://horo-engine.atlassian.net/browse/HORO-1933)
- **Related**: [ADR-010](010-job-waiting-and-operation-store-ownership.md), [ADR-012](012-world-streaming-partition-authority-and-subsystem-boundaries.md), [ADR-034](034-gpu-memory-and-residency-ownership.md), [ADR-038](038-gpu-scene-and-instance-data-model.md), [ADR-042](042-cpu-gpu-timestamps-and-pipeline-statistics.md), [ADR-043](043-gpu-memory-and-resource-inspection.md), [ADR-051](051-renderer-benchmark-and-regression-gates.md), [ADR-137](137-terrain-foliage-ownership-data-tier-and-lifecycle.md), [ADR-138](138-terrain-source-cooked-tile-cache-and-streaming-ownership.md), [ADR-139](139-terrain-render-extraction-material-lod-and-tier-boundary.md), [ADR-140](140-foliage-placement-baked-dynamic-state-and-eviction-ownership.md), [ADR-141](141-terrain-foliage-cross-system-ownership-and-readiness.md), [ADR-142](142-terrain-foliage-document-tool-undo-and-preview-ownership.md)
- **Normative documents**: [Terrain and Foliage Architecture](../architecture/runtime/terrain-and-foliage-architecture.md), [Metrics and Profiling](../architecture/observability/observability-performance.md), [Testing Architecture](../architecture/delivery/testing-architecture.md), [World Streaming Architecture](../architecture/runtime/world-streaming-architecture.md), [Rendering Architecture](../architecture/runtime/rendering-architecture.md)

## Context

The Terrain/Foliage architecture defines provider-neutral feature tiers, exact budget
admission and a core-1.0 CPU-selected render path. It intentionally leaves scale and
performance numbers open. Consequently, “supports large terrain,” “high density,” or
“low overhead” cannot be accepted or regressed objectively. A renderer could pass a
small scene, a cook could report cache-hit throughput, or a headless host could retain
visual work while still claiming compliance.

Feature tiers and qualification workloads answer different questions. A tier is a
resolved content/capability policy whose final numeric descriptor limits belong to
TRF-001.3. A qualification workload fixes representative content, active scale,
budgets and expected measurements. Neither implies the other, and neither may grant
hardware capability.

Performance numbers are meaningful only inside a reproducible environment cohort.
They also need non-overlapping accounting: resident Terrain bytes, renderer backing,
process resident memory and World Streaming reservations cannot be summed blindly.
Missing metrics, fallback algorithms and dropped samples must not become zero or pass.

This ADR establishes two versioned 1.0 qualification workloads and their absolute
budgets. TRF-007.2 owns concrete runtime metric descriptors and bounded diagnostic
snapshots. TRF-007.4 owns the representative scene assets and captured baseline data.

## Decision

### 1. Qualification policy coordinates existing owners

| Responsibility | Authority |
|---|---|
| Workload descriptors, absolute budgets, required measurements and pass result | Terrain/Foliage qualification policy |
| Cell demand, aggregate reservations, per-epoch work and active/retiring admission | World Streaming |
| Logical tile/cluster/instance state and neutral payload accounting | TerrainRuntime |
| GPU backing, per-view CPU plan, draw execution and native measurement | RenderFrontend/backend owners |
| Collision and navigation resources/timing | Physics and Navigation owners |
| Source edit transaction and interaction latency | Editor document/tool owners under ADR-142 |
| Cook stage timing, cache state and artifact throughput | Assets/Terrain Cook |
| Metric descriptors, aggregation, availability, retention and profiler capture | Observability |
| Cohort admission, run orchestration, baselines and regression evaluation | Benchmark/CI service |

The qualification service receives narrow immutable snapshots and typed measurements.
It owns no tile, instance, document, GPU resource, Physics body, Navigation tile, metric
store or scheduler. Workload descriptors are inert and cannot activate a backend,
reserve memory, load content or register ambient services during validation.

### 2. Scale workloads are versioned and orthogonal to feature tiers

Version 1 defines `TerrainCore1_0` and `TerrainHighEnd1_0`. These identifiers are
qualification workload families, not `TerrainFeatureTier` values, graphics API names,
product presets or runtime capability tokens. Each concrete workload descriptor also
fixes content hashes, seed, camera/input trace, target/tier plan, host mode, backend,
viewport, render profile, fixed tick, cache state and instrumentation revision.

The selected Terrain feature tier may differ between products, but a result belongs to
one workload only when the resolved plan exactly satisfies its descriptor. A smaller
scene, lower density, fewer active tiles, disabled required consumer, lower render
scale, alternate backend, warm cache or undeclared algorithm is a different workload
and cannot satisfy the gate.

### 3. Version 1 fixes exact scale and memory envelopes

All byte values are binary MiB. A terrain tile has `128 x 128` interior quads and
`129 x 129` stored height samples before separately declared seam/apron or mip data.
The workload uses square tiles; changing dimensions creates a new workload revision.

| Required envelope | `TerrainCore1_0` | `TerrainHighEnd1_0` |
|---|---:|---:|
| Simultaneously Active terrain tiles | 256 | 1,024 |
| Active foliage clusters | 1,024 | 4,096 |
| Active baked + durable + ephemeral foliage instances | 262,144 | 1,048,576 |
| TerrainRuntime steady resident logical/decoded bytes | 256 MiB | 1,024 MiB |
| Foliage steady resident logical/decoded bytes | 256 MiB | 1,024 MiB |
| Candidate/decode/upload staging allowance | 128 MiB | 512 MiB |
| Retiring old-generation allowance | 128 MiB | 512 MiB |
| Maximum aggregate Terrain/Foliage charged bytes | 768 MiB | 3,072 MiB |

“Active” means discoverable through the committed aggregate RuntimeScene/World
Streaming root with all descriptor-required readiness dimensions. Prepared, private,
candidate and retiring generations are not Active, but their bytes remain charged to
the staging or retirement allowance. Shared physical allocations use one ADR-034 charge
identity; renderer/Physics/Navigation views do not duplicate Terrain logical bytes.

The steady resident limits are both required workload capacity and hard qualification
ceilings. The fixture must reach the exact active counts without exceeding them. A
content representation that cannot fit fails admission; it cannot silently drop tiles,
instances, collision, navigation, layers, LODs or quality. Product runtime descriptors
may choose lower caps, while higher caps require a separately qualified workload/
revision and sufficient global World Streaming budget.

The existing World Streaming baseline allocates 256 MiB each to Terrain and Foliage;
therefore `TerrainCore1_0` fits its steady-state provider slices. Its 128 MiB staging
and 128 MiB retirement allowances consume the remaining general envelope only through
explicit aggregate reservation. `TerrainHighEnd1_0` requires a product/runner global
budget of at least 3,072 MiB plus all non-Terrain provider reservations; it does not
inflate the default baseline implicitly.

### 4. Per-frame streaming and owner work are capped

One `TerrainStreamingBudgetEpoch` is one committed presentation frame for an
interactive host and one fixed simulation tick for headless. Catch-up frames/ticks do
not multiply the allowance. The epoch records actual duration, but the byte cap is not
converted into an unbounded rate after a hitch.

| Streaming gate | `TerrainCore1_0` | `TerrainHighEnd1_0` |
|---|---:|---:|
| Newly committed Terrain/Foliage resident payload per epoch | 4 MiB | 16 MiB |
| Terrain owner-lane streaming work p95 per epoch | 2.00 ms | 3.00 ms |
| Concurrent tile/cluster load operations | 4 | 16 |
| Concurrent retirement operations | 4 | 16 |

Provider reads and worker decode may span epochs, but publication cannot commit more
new resident payload than the epoch cap. Temporary, decoded and upload copies remain
covered by staging reservation. The owner-work gate measures Terrain coordination,
validation, snapshot publication and receipt processing; worker I/O/decode, GPU time,
Physics and Navigation time remain separate measurements.

When a limit is reached, the manager stops admitting new units and returns bounded
backpressure. It does not preempt native work, exceed the budget, discard required
content or reinterpret the limit as a target average. A single artifact larger than the
per-epoch cap requires explicit multi-epoch staging and atomic publication after the
complete candidate is ready.

### 5. Cold deterministic cook throughput has absolute floors

Cook qualification starts with an empty Terrain cook-cache namespace, fixed source and
dependency hashes, the workload's target/tier/toolchain envelope, and no previously
published artifact reuse. It excludes source generation/import and package compression
unless the workload explicitly includes them. It includes validation, canonical tiling,
seams/mips, neutral consumer payloads, foliage placement/clustering, hashing and atomic
artifact/manifest publication.

| Cold cook gate | `TerrainCore1_0` | `TerrainHighEnd1_0` |
|---|---:|---:|
| Successfully published terrain tiles per second | at least 16 | at least 64 |
| Deterministically evaluated foliage placements per second | at least 250,000 | at least 1,000,000 |
| Failed, missing or non-deterministic output | 0 | 0 |

Rates use successful output divided by measured steady duration; failed/retried work is
not counted as throughput. Tile and placement rates are reported independently and both
must pass. A warm-cache reuse workload is diagnostic and cannot satisfy this cold gate.
Byte-identical manifest/artifact checks and peak memory/scratch budgets pass separately.

Absolute floors apply only to the versioned protected-runner cohorts attached to each
workload. Other machines report measurements and relative baselines without claiming
that a different hardware class failed the product contract.

### 6. Editor interaction has bounded latency gates

The editor workload uses ADR-142 typed operations against a resident document, with
preview resources warmed and no source save/cook publication inside the interaction
window. A standard gesture touches at most four tiles and sixteen canonical patches;
its input trace and patch contents are fixed.

| Editor latency gate | `TerrainCore1_0` | `TerrainHighEnd1_0` |
|---|---:|---:|
| Input sample to interaction-overlay publication p95 | 16.67 ms | 8.33 ms |
| Gesture release to atomic document commit p95 | 100 ms | 50 ms |
| Modal/tool/document cancellation to overlay removal p95 | 16.67 ms | 8.33 ms |
| Missed/corrupt/partial history commits | 0 | 0 |

Each process iteration performs 200 deterministic gestures after warm-up. Every sample
is retained; slow samples are not removed because they exceed the gate. Larger edits
use separate workloads and may be asynchronous. Meeting latency by shrinking the
affected closure, skipping before/after patches or publishing before validation is a
correctness failure, not a performance pass.

### 7. Headless overhead excludes visual work exactly

The headless workload uses the same logical Terrain content and descriptor-required
collision/navigation readiness but composes no render adapter. It runs 10,000 fixed
ticks after readiness and compares with the identical fixture/trace with the optional
Terrain visual dimension absent in both cases.

| Headless gate | `TerrainCore1_0` | `TerrainHighEnd1_0` |
|---|---:|---:|
| Terrain logical/streaming owner work p95 per fixed tick | 0.50 ms | 1.50 ms |
| Additional visual CPU work, GPU bytes, draw/dispatch/submit count | exactly 0 | exactly 0 |
| Terrain observability collection CPU p95 per fixed tick | 0.05 ms | 0.10 ms |
| Metric/diagnostic memory retained by Terrain | at most 4 MiB | at most 8 MiB |

Physics/Navigation work and their memory are reported by their owners and cannot be
relabeled as Terrain overhead. Process totals remain useful context but are not summed
with tagged subsystem values. A Null Render adapter is a separate explicit workload;
it cannot stand in for a host with no render adapter.

### 8. Core and high-end 1.0 require the CPU-selected render recipe

Both version-1 workloads qualify the ADR-139 core path: TerrainRuntime publishes view-
independent candidates, and RenderFrontend performs bounded CPU per-view visibility,
LOD/seam selection and direct/instanced batch construction. Compute, GPU Scene, indirect
command generation and GPU-driven foliage selection are not required or permitted in
these workload identities.

The CPU plan must meet the active scale and all absolute budgets on every interactive
backend/platform cohort declared required by the product. A backend that cannot meet a
required workload is unqualified; it does not switch recipe, tier, fixture or profile.
Expected unsupported is legal only when the product descriptor did not require that
cohort and admission returns its exact typed reason before measurement.

Post-1.0 may define a new `TerrainGpuDriven` recipe/workload revision. It must preserve
the same candidate semantics and ownership while separately freezing GPU buffer/
counter capacities, overflow behavior, synchronization, readback, shader/cook identity,
fallback and cross-backend qualification. It cannot be enabled under either version-1
workload ID or used retroactively to claim its gates. A declared CPU fallback is a
separate measured plan, never a silent response to GPU failure or saturation.

### 9. Required measurements have stable, non-overlapping semantics

Qualification consumes typed measurements through Observability/benchmark services:

- active/preparing/retiring tile, cluster and instance counts;
- Terrain and foliage logical/decoded resident, staging and retirement bytes;
- committed streaming bytes and owner work per budget epoch;
- cook tile/placement success, failure, duration and throughput;
- edit overlay, commit and cancellation latency plus history bytes/patch counts;
- headless owner/observability time and proof of zero visual resources/work;
- budget denial, stale result, cancellation, eviction and retirement-stall totals; and
- active workload, content, plan, capability and measurement schema revisions.

Counts/bytes come from owning ledgers and typed snapshots, not directory enumeration,
native API queries or parsed logs. GPU/process memory are separate observations with
their own availability/provenance. IDs, paths, coordinates, tile/cluster/instance keys,
operation IDs and native handles are prohibited metric dimensions. Detailed per-item
evidence belongs to a bounded on-demand diagnostic snapshot under TRF-007.2.

Metric unavailability is explicit. A missing required measurement, stale generation,
dropped sample, series-budget breach, invalid calibration or mismatched revision makes
the run invalid; it is never zero or pass. Metrics inform qualification and diagnosis,
not runtime control flow.

### 10. Measurement and regression follow ADR-051

Interactive steady workloads use 300 warm-up frames and at least 20 seconds of measured
frames: 1,200 frames for the 60 Hz Core cohort and 2,400 frames for the 120 Hz High-End
cohort. They use seven process iterations and retain at least 99% valid samples. Editor,
cook and headless workload windows above override operation counts while preserving
seven independent iterations unless their versioned descriptor explicitly requires a
stricter plan.

The environment cohort fixes workload revision, source/content, OS, CPU/memory/device
class, backend/driver, power/thermal policy, build/toolchain, dependency lock, viewport/
present state, effective capabilities and instrumentation. Candidate and protected-
branch baseline must match. Adaptive quality, dynamic resolution, unrelated streaming,
debug layers and profiler captures are off unless part of the workload identity.

A result must satisfy every absolute scale/budget/floor and the applicable ADR-051
relative regression/stability gate. Passing one does not waive the other. Qualification
artifacts include exact measurements, availability, fallback decisions, excluded sample
reasons, budget math and bounded diagnostics; they contain no absolute machine paths,
native handles or raw source payloads.

### 11. Failure, cancellation, replacement and shutdown remain measurable

Admission fails before work when the fixture, descriptor, capabilities, global budget,
measurement plan or cohort is incompatible. During execution, cancellation stops new
admission and retains candidates, samples and native/consumer leases through normal
acknowledged retirement. Replacement invalidates the run instead of splicing old/new
generations. Device/provider loss and stale evidence are explicit outcomes.

Shutdown cancels queued/active workloads, closes metric/diagnostic admission, drains
bounded completion records, retires submitted consumer work and then releases snapshots,
reservations and modules. A timeout reports incomplete qualification/retirement and
retains ownership; it cannot force-free or publish a partial pass.

Stable outcomes include `Passed`, `BudgetExceeded`, `ThroughputBelowFloor`,
`LatencyExceeded`, `RequiredMetricUnavailable`, `InsufficientSamples`,
`WrongRecipe`, `UnexpectedUnsupported`, `EnvironmentInvalid`, `Regression`,
`Cancelled`, `StaleGeneration`, `RetirementStalled` and `InfrastructureFailed`.

## Consequences

- “Core” and “high-end” Terrain/Foliage scale now have exact active counts, tile shape,
  memory, per-epoch bandwidth/work, cook, editor and headless expectations.
- Capability tiers remain independent; TRF-001.3 can freeze descriptor limits without
  turning benchmark workloads into product presets.
- Core/high-end 1.0 cannot depend on hidden GPU-driven work. Post-1.0 optimization must
  introduce explicit recipe/workload identity and qualification.
- The core steady-state slices align with existing World Streaming defaults, while
  staging/retirement overlap and the larger high-end envelope require explicit global
  reservation.
- Some prototypes and platforms will become explicitly unqualified until they provide
  required measurements and pass both absolute and relative gates.
- TRF-007.2 must implement low-cardinality descriptors/bounded snapshots, and TRF-007.4
  must publish immutable fixtures and protected-runner cohort manifests.

Required coverage includes exact-boundary and one-over-limit counts/bytes; checked
aggregate accounting with old/new/staging/retiring overlap; per-epoch backpressure;
cold/warm cache separation and deterministic cook floors; editor latency without lost
history; headless zero-visual proof; required metric unavailability/dropped samples;
cohort mismatch, fallback and wrong-recipe rejection; absolute plus relative gate math;
and cancellation/replacement/shutdown at every measurement stage.

## Rejected Alternatives

### Use feature tiers as benchmark profiles

Rejected because tiers express resolved product capability/quality, while workloads fix
content, scale, environment and measurements. Coupling them would let a tier name hide
different numbers or algorithms.

### Publish only target FPS or average frame time

Rejected because it hides owner cost, streaming bandwidth, tail latency, memory overlap,
cook/editor/headless behavior and correctness failures.

### Require GPU-driven culling for high-end 1.0

Rejected because it would make compute/GPU Scene/indirect execution an undeclared core
dependency and undermine backend parity. Both 1.0 workloads prove the CPU-selected path.

### Treat missing metrics as zero or skip their gates

Rejected because absence would look better than real work and reward broken
instrumentation. Required measurement unavailability invalidates the run.

### Average streaming bandwidth across long windows

Rejected because large one-frame bursts cause hitches and memory overlap even when the
long-term average is low. Admission is capped per explicit epoch.

### Compare absolute thresholds across arbitrary machines

Rejected because throughput and latency are cohort-dependent. Absolute gates belong to
versioned protected runners; other cohorts use their own explicit descriptors/baselines.

### Let a GPU recipe satisfy the existing CPU workload identity

Rejected because results would be incomparable and GPU failure could hide a recipe
switch. Recipe and fallback are explicit workload identity.
