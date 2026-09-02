# ADR-125: VFX Transparency, Sorting and Pass Placement

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: Particle blend-class pass placement, semantic depth policy, CPU/GPU per-view sort ownership and algorithms, stable ordering, sort budgets, overload reporting and backend-neutral graph integration
- **Issue**: [VFX-004.1](https://github.com/abdullahbodur/horo-engine/issues/1752)
- **Jira**: [HORO-1709](https://horo-engine.atlassian.net/browse/HORO-1709)
- **Related**: [ADR-011](011-vfx-effect-ownership-simulation-domain-and-renderer-boundary.md), [ADR-028](028-renderer-capability-limits-and-product-profiles.md), [ADR-036](036-raster-render-path-and-quality-architecture.md), [ADR-037](037-scene-color-and-hdr-architecture.md), [ADR-038](038-gpu-scene-and-instance-data-model.md), [ADR-124](124-vfx-gpu-simulation-readback-and-compute-fallback.md)
- **Normative documents**: [VFX and Particles Architecture](../architecture/runtime/vfx-and-particles-architecture.md), [Rendering Architecture](../architecture/runtime/rendering-architecture.md), [Render Backend Parity Contract](../architecture/runtime/render-backend-parity-contract.md)

## Context

ADR-011 establishes immutable VFX extraction, renderer-owned graph construction and
per-view sorting, while ADR-036 defines the production raster recipes and keeps
baseline transparency forward shaded. The VFX architecture already names opaque,
masked, translucent and additive particle classes, but several choices remain implicit:

- whether every raster recipe maps a class to the same semantic pass/depth behavior;
- where sorted translucent and additive VFX sit relative to one another;
- which owner builds view keys and selects CPU radix versus GPU radix/bitonic work;
- how CPU and GPU sorts produce stable equivalent ordering without mutating simulation;
- what finite work/time budget applies across multiple views; and
- what the renderer does when a sort cannot be admitted or misses its frame boundary.

Leaving these choices to an effect, sequencer, shader or native backend would make
visual ordering and performance profile-dependent in unreviewable ways. This ADR
fixes semantic placement and a measurable initial budget while preserving the
frontend/backend boundary. It is an architecture contract, not a performance claim
that an implementation already meets the targets.

## Decision

### 1. The cooked output has one semantic particle class

Each `VfxRenderBatch` declares exactly one `VfxParticlePass`: `Opaque`, `Masked`,
`Translucent` or `Additive`. The class must agree with the cooked material blend
contract and primitive topology. Combined alpha-test plus alpha/additive blending is
not a fifth class; it requires a separately approved raster recipe or fails cook.

The asset declares semantic shading, blend class, soft-particle need and shadow
intent. It never stores a graph pass ID/name, attachment format, queue family, native
blend/depth state, pipeline object, backend/API enum or sort-dispatch dimensions.
Sequencer/gameplay may start or parameterize an effect but cannot rewrite its class,
pass placement or sort policy per frame.

### 2. Every class has one baseline pass and depth contract

The frontend maps VFX semantics onto the active ADR-036 raster recipe:

| VFX class | Semantic graph placement | Depth test | Depth write | Ordering |
|---|---|---|---|---|
| `Opaque` mesh | Recipe depth prepass when present, then GBuffer for compatible Deferred material or standard opaque forward pass | Enabled against the recipe's current depth convention | Enabled in the owning depth/color contract | Renderer opaque front-to-back batch policy; no VFX per-particle alpha sort |
| `Masked` mesh | Matching alpha-test depth prepass when present, then compatible GBuffer or masked forward pass | Enabled using identical cooked coverage in depth/shadow/color | Enabled for passing samples | Renderer opaque front-to-back batch policy; no translucent sort |
| `Translucent` billboard/ribbon/mesh | `VfxTranslucentForward` after opaque lighting and before baseline additive VFX | Enabled read-only; soft particles may sample the completed opaque depth | Disabled | Required stable back-to-front per view |
| `Additive` billboard/ribbon/mesh | `VfxAdditiveForward` after baseline sorted translucent VFX in scene-linear color | Enabled read-only | Disabled | No distance sort; commutative within the additive band |

Opaque/masked shadow casting uses the standard shadow pass and the same masked
coverage where applicable. Translucent/additive particles and ribbons do not cast
baseline raster shadows. Unsupported topology/material/shadow combinations require
an authored substitute or typed failure, never a silently ignored flag.

All VFX color work consumes and writes ADR-037 unexposed scene-linear ACEScg before
the output transform. `Translucent` uses coverage/opacity alpha; `Additive` adds RGB
with no coverage alpha. Neither class enters a Deferred GBuffer. Refraction, weighted
blended/OIT, distortion and other non-baseline transparency are separate capability-
validated recipes with explicit fallback; they are not aliases for these four rows.

### 3. Pass dependencies are semantic and cycle-free

The graph order relevant to particle batches is:

```text
VFX Compute once per logical GPU step
    -> per-view VFX Cull/Key Build
    -> required per-view VFX Sort
    -> standard Shadow/Depth/Opaque work for opaque/masked VFX
    -> opaque lighting / completed scene depth
    -> VfxTranslucentForward
    -> VfxAdditiveForward
    -> later scene-color consumers and output transform
```

The actual recipe may interleave compatible opaque/masked VFX with ordinary scene
work, but it preserves the semantic resources and dependencies above. A soft-particle
draw reads completed opaque depth and never creates a dependency on depth written by
that same translucent/additive pass. A previous-depth simulation collision input
remains separate from current-frame draw depth.

The frontend owns graph nodes, resources, pass merging and synchronization. Backends
translate the compiled graph and cannot reorder translucent/additive bands, choose a
different depth policy or silently fold a required sort away.

### 4. Sorting is render-view work, never simulation state

Simulation remains camera-independent except for separately declared cosmetic inputs.
For each admitted `RenderViewId`, RenderFrontend culls and builds immutable render keys
from the retained particle source, captured render origin and frozen view snapshot.
No sort writes the CPU SoA, GPU simulation state, durable particle attributes or an
effect's next-step input. Multiple views reuse the same simulation generation but own
separate index/key outputs.

The canonical back-to-front key is the tuple:

```text
finite canonical view-depth descending
+ stable VfxBatchId
+ stable ParticleSimulationId (or cooked stable GPU particle identity)
```

Depth encoding and comparison are versioned and shared by CPU/GPU fixtures. Nonfinite
depth and stale view/source generations fail the batch. Equal depths use stable IDs,
never source slot, atomic append order, worker completion or native subgroup order.
Culling and clipping cannot change the relative order of surviving equal-key items.

### 5. CPU particle sources use stable CPU radix sorting

For a CPU `ParticleDataSource`, the frontend's CPU sort preparation owns key/index
storage and schedules a stable radix sort over immutable packed frame data. VFX
simulation and `VfxRenderExtractor` provide retained particle identity/position data;
they do not receive a camera or reorder the SoA.

Ranges, scratch and result slots are admitted before the frame. Jobs operate on
disjoint ranges and publish only a complete generation at the frontend owner boundary.
A worker does not mutate graph state or block on nested work. If the complete result
is not ready by the consuming boundary, the affected translucent batch is omitted and
reported; the frame does not wait and never submits the batch unsorted as success.

Opaque/masked batches use the renderer's ordinary coarse front-to-back batch ordering.
Additive VFX bypasses per-particle distance key generation and sort entirely. It does
not consume a translucent sort-key reservation merely to preserve simulation order.

### 6. GPU particle sources use an explicit stable compute sort plan

For a GPU source, RenderFrontend emits backend-neutral `VfxGpuSortPlan` work after the
producing simulation generation and key-build/cull work, before the translucent draw.
The plan records stable key layout, item count/capacity, scratch, algorithm, view,
source/result generations and graph resource usages.

The baseline qualified algorithms are:

- stable bitonic sort for admitted visible counts up to and including 2,048, padding
  with explicit non-draw sentinel keys; and
- stable radix sort above 2,048, using a versioned digit width/pass order and stable
  scatter/reduction contract.

The frontend selects from the effective capability/profile and admitted plan. The
effect does not choose a backend kernel, and the backend does not substitute another
algorithm after admission. If only one algorithm is implemented/qualified, its exact
count/scratch limits are effective capabilities; unsupported required work follows
the declared batch fallback instead of silently changing semantics.

GPU completion order, subgroup width and queue assignment cannot affect the canonical
tuple. Backends own native pipelines/dispatch/barriers, but equivalent plans produce
the same ordered stable IDs under the declared numeric key contract.

### 7. Additive is exempt; translucent is never knowingly unsorted

`Additive` is commutative within its dedicated scene-linear band, so baseline skips
strict per-particle sorting. Stable batch submission identity remains for diagnostics,
captures and reproducible graph construction, but it is not a visual depth order.

`Translucent` and ribbons require a complete back-to-front index generation per view.
`None`, age order, source order or approximate native order cannot override that
requirement. If a different transparency technique is desired, the asset selects a
separately cooked and capability-admitted OIT/stochastic recipe; it does not weaken
the sorted-alpha contract.

An unadmitted, failed or late translucent sort results in deterministic omission of
the whole affected batch (lowest declared cosmetic priority first when resolving
capacity). Partial prefixes and unsorted fallback are forbidden because they make the
same asset blend differently according to timing. Required visual content may instead
fail/suspend the owning effect according to its authored policy.

### 8. Sort work has hard ceilings and a measured frame target

`VfxQualityPolicy` contains an immutable `VfxSortBudget` captured by the frame plan.
Initial defaults aggregate all VFX views in one rendered frame:

| VFX profile | CPU sorted keys hard ceiling | GPU sorted keys hard ceiling | CPU/GPU time targets | Aggregate target |
|---|---:|---:|---:|---:|
| Headless/test | 0 | 0 | 0.00 ms / 0.00 ms | 0.00 ms |
| Low visual budget | 8,192 | 65,536 | 0.25 ms / 0.35 ms | 0.60 ms |
| Desktop baseline | 16,384 | 262,144 | 0.35 ms / 0.65 ms | 1.00 ms |
| High visual budget | 32,768 | 524,288 | 0.50 ms / 1.00 ms | 1.50 ms |

These are finite product defaults and qualification targets, not measured claims or
permission to schedule until wall-clock expiry. Project/host profiles may select other
positive finite values through the normal validated policy path. Hard key, scratch,
dispatch and byte ceilings are admission limits. Time targets are measured outcomes;
GPU timestamps are delayed and optional only where the active profile has not claimed
native time qualification.

CPU duration is the sum of VFX key-build/sort job execution spans; GPU duration is the
timestamp interval covering only VFX key-build/sort compute passes, excluding unrelated
queue wait. Aggregate duration is their conservative sum, so overlap does not hide
cost. Measurements retain build mode, platform, backend/device/driver, profile, view
set, key counts and algorithm; a target is not reported as qualified without that
evidence.

View groups consume the aggregate ceiling in frozen `RenderViewPlan` priority followed
by stable batch priority/ID. Correlated required views such as an XR stereo set admit
or omit a batch as one group; budget pressure never renders sorted alpha in only one
required eye. The resolver admits only complete batches and records any omitted
candidate. It never drains the full ceiling independently for each eye, reflection,
editor viewport or snapshot reuse.

### 9. Over-budget behavior is bounded and observable

Predicted hard-ceiling overflow is handled before scheduling: omit the lowest-priority
cosmetic translucent batch, select its authored non-sorted substitute/OIT recipe if
already admitted, or fail/suspend required visual content. The active frame never
allocates extra scratch, truncates a batch, changes profile or stalls for capacity.

Measured CPU, GPU or aggregate target overrun does not attempt to preempt submitted
native work. It emits `VfxSortBudgetExceeded` evidence containing frame/view IDs,
profile/policy/capability generations, domain, algorithm, key count, scratch/copy
bytes, measured CPU/GPU duration and target/overage when available. Counters are
complete; detailed diagnostics are rate-limited through the observability contract.
Unavailable GPU timing is explicitly unavailable, never recorded as zero.

Repeated overruns may inform an explicit later quality-policy request, but cannot
silently change sort algorithm, particle domain, view coverage or active batch order.
Any new policy is re-admitted and published at a safe point with its own generation.

### 10. Failures identify the batch, view and owner boundary

Cook rejects `VfxBlendClassMismatch`, `VfxSortPolicyInvalid`, unsupported shadow/
topology combinations and backend/pass declarations in effect data. Frontend
validation returns `VfxSortCapacityExceeded`, `VfxSortKeyInvalid`,
`VfxSortCapabilityUnavailable`, `StaleVfxGeneration` or `VfxSortResultLate` without
publishing a partial index generation.

Every failure/omission carries effect/emitter/batch, source/view/frame generations,
requested blend/sort semantics, algorithm/capability/policy revisions and the failed
limit. Native backend text may be attached as bounded private evidence, but callers do
not parse it or branch on API enums. Device loss and cancellation preserve retained
resource lifetime and return credits only after the renderer's normal retirement.

### 11. Qualification covers classes, views, algorithms and budgets

Required implementation evidence includes:

- every class across Forward, Clustered Forward+ and Deferred recipes, verifying
  semantic pass placement, depth test/write and matching masked coverage;
- scene-linear alpha/additive composition, soft-particle completed-depth reads and no
  GBuffer or current-depth dependency cycle for baseline transparency;
- CPU stable-radix fixtures proving no SoA mutation, equal-key identity ties and full
  omission on worker failure/late completion;
- GPU bitonic fixtures at 0, 1, 2,047, 2,048 and its first unsupported/next path count,
  plus radix boundaries, sentinel padding, randomized completion and subgroup widths;
- matching CPU/GPU ordered stable IDs for canonical key fixtures on each shipped
  backend, with nonfinite/stale inputs rejected;
- additive scheduling zero per-particle sort work and translucent refusing None/age/
  source/native ordering;
- multi-view aggregate admission in frozen priority order, snapshot reuse without
  duplicate sort, atomic required-view groups, exact hard ceilings and one-key/one-
  byte/one-dispatch overflow;
- delayed GPU timestamps, unavailable timing, CPU/GPU/aggregate target overrun and
  complete/rate-limited `VfxSortBudgetExceeded` evidence, including workload, build,
  platform, backend/device/driver and algorithm identity; and
- native GPU smoke evidence per shipped backend. Null/fakes validate plan, ordering,
  limits and delayed results but cannot prove native time or driver behavior.

## Consequences

### Positive

- Every baseline particle blend class has one reviewable pass and depth contract.
- CPU/GPU sorting produces stable per-view indices without contaminating simulation.
- Additive effects avoid unnecessary strict sort work.
- Hard work ceilings and explicit millisecond targets make overload testable/reportable.
- Effect and sequencer data remain free of graph/backend implementation details.

### Costs

- Frontend needs versioned key layouts, stable CPU/GPU algorithms and admitted scratch.
- Multiple views consume separate index/key storage under one aggregate frame budget.
- Whole-batch omission is visually noticeable but avoids incorrect partial/unsorted alpha.
- Shipped profiles require per-backend timing and ordering qualification.

## Rejected Alternatives

### Let effect assets name render passes or native blend/depth state

Rejected because recipe and backend details would leak into content and become stale
when the active raster family changes. Assets declare semantic output requirements.

### Sort particle simulation storage in place

Rejected because view-dependent ordering would corrupt camera-independent CPU/GPU
state, conflict across views and change deterministic simulation identity.

### Submit translucent particles unsorted when sort work is late

Rejected because blend output would depend on worker/device timing. The complete batch
is omitted or follows an already admitted authored transparency substitute.

### Sort additive particles back-to-front by default

Rejected because the baseline additive band is commutative and strict sorting consumes
per-view work without changing its defined composition.

### Let each backend choose bitonic, radix or native ordering

Rejected because algorithm/scratch/capability resolution belongs to the frontend plan
and native ordering is not a stable cross-backend semantic.

### Use elapsed time as the only hard limiter

Rejected because CPU clocks and delayed GPU timestamps cannot safely preempt bounded
frame work. Counts/bytes/dispatches are hard admission ceilings; time is a measured
qualification target with explicit overrun evidence.
