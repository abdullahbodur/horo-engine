# ADR-038: GPU Scene and Instance Data Model

- **Status**: Proposed
- **Date**: 2026-09-01
- **Supersedes**: None
- **Scope**: Persistent render-object identity, GPU instance records, bounded updates and lifecycle
- **Issue**: [RND-014.1](https://github.com/abdullahbodur/horo-engine/issues/401)
- **Jira**: [HORO-401](https://horo-engine.atlassian.net/browse/HORO-401)
- **Related**: [ADR-010](010-job-waiting-and-operation-store-ownership.md),
  [ADR-011](011-vfx-effect-ownership-simulation-domain-and-renderer-boundary.md),
  [ADR-012](012-world-streaming-partition-authority-and-subsystem-boundaries.md),
  [ADR-018](018-command-registration-permissions-threading-and-packaged-build-policy.md),
  [ADR-026](026-large-world-precision-and-floating-origin-strategy.md),
  [ADR-027](027-renderer-resource-identity-and-descriptors.md),
  [ADR-034](034-gpu-memory-and-residency-ownership.md),
  [ADR-036](036-raster-render-path-and-quality-architecture.md)
- **Normative documents**: [Rendering Architecture](../architecture/runtime/rendering-architecture.md), [Advanced Rendering Architecture](../architecture/runtime/advanced-rendering-architecture.md)

## Context

GPU-driven rendering requires persistent instance data for culling, LOD and
generated draws. Existing architecture only says “scene GPU buffer with instance
data” and uses frame-owned `RenderInstance` arrays. It does not define persistent
identity, record contents, update ordering, slot reuse, origin rebasing, resource
dependencies, device loss or the boundary between ECS truth and a GPU projection.

Without one model, implementations could use entity indices as GPU addresses,
persist native descriptor indices in scene data, rewrite every record from every
view, race resource destruction, or expose delayed GPU visibility as gameplay
truth. Large streamed scenes also need bounded create/update/remove publication;
an unbounded full upload on a frame-hot path is not an acceptable fallback.

This decision owns the persistent GPU Scene projection and its CPU shadow state.
It does not choose culling algorithms, meshlet format, bindless layout, material
schema, world-streaming policy or gameplay entity identity. Those systems consume
or feed this contract through typed seams.

## Decision

### 1. GPU Scene is a frontend-owned presentation projection

`RenderFrontend` owns one `GpuScene` per admitted `(SceneRuntimeId,
SceneIncarnation, frontend/device generation)`. Multiple compatible views share
that scene and publish view constants separately. The scene runtime owns gameplay
entities/components and emits immutable render-object state; Asset/Material owners
own persistent assets and cooked data; the resource registry owns resident mesh,
material and texture generations. `GpuScene` owns only:

- stable process-local render-object-to-slot mapping;
- a bounded CPU shadow of admitted logical instance records;
- GPU buffers, free/retired slot state and publication generations; and
- staged delta/upload work required to project render state to the device.

Backends allocate and copy native memory through ADR-027/034 contracts but do not
own instance policy or inspect scene/ECS storage. `GpuScene` never mutates ECS,
selects streaming cells, loads assets, chooses materials, changes LOD policy or
returns visibility as gameplay truth. Gameplay, networking, save data and AI must
not query GPU Scene or wait for its readback.

The CPU-driven raster fallback consumes immutable render snapshots through its
declared recipe; it does not masquerade as a successfully published GPU Scene. A
recipe may explicitly prefer GPU Scene and fall back to CPU submission when all
content/feature/budget requirements permit. Required GPU-driven content fails with
a typed result if the model cannot be admitted.

[ADR-011](011-vfx-effect-ownership-simulation-domain-and-renderer-boundary.md)
`VfxRenderBatch`, decal and volume contracts remain a parallel presentation path.
They write unexposed ACEScg scene color under ADR-037 and do not occupy
`GpuSceneInstanceId` slots, reuse GPU Scene free-lists, or treat GPU Scene
visibility as emitter simulation truth. GPU Scene records are scene-extracted
mesh/material renderables; VFX simulation ownership stays on ADR-011.

### 2. Source identity and GPU slot identity are distinct

Render extraction supplies a `RenderObjectId`, a Horo value composed from the
scene runtime/incarnation, a generation-checked source object identity and a stable
render-subobject key. The subobject key distinguishes multiple renderables emitted
by one entity without relying on extraction order. `RenderObjectId` is stable for
that logical renderable across frames and view reuse. Despawn/recreate, prefab
replacement or scene reincarnation produces a new generation; stale updates cannot
target the replacement.

`RenderObjectId` is process-local presentation identity. It is not serialized,
replicated, derived from a pointer, truncated entity index, array position, asset
ID or backend descriptor address. Save/load and network systems reconstruct scene
truth and extraction issues new render identities.

GPU Scene maps it to a typed `GpuSceneInstanceId`:

```cpp
struct GpuSceneInstanceId {
    RenderFrontendId frontend;
    GpuSceneId scene;
    uint32_t slot;
    uint32_t generation;
};
```

The exact public/internal placement is downstream implementation work, but the
identity semantics are normative. Generation is non-wrapping within a scene
incarnation. Exhaustion retires the slot permanently or requires a new scene
generation; it never aliases. Shaders consume a validated bounded slot/index for
the current published generation, not the full CPU identity. CPU validation occurs
before any slot enters a graph plan.

A removed slot stops participating in newly compiled/published visibility and draw
work immediately. It is not returned to the free list until every frame, upload,
culling result, indirect buffer and read-only debug lease that can reference the
old generation has completed. Reuse increments generation and initializes the
complete record before visibility. Free-list order is deterministic.

### 3. Logical instance records are versioned and backend-neutral

The CPU shadow record contains Horo values, never native pointers/handles:

```cpp
struct GpuSceneInstanceRecord {
    GpuSceneInstanceId id;
    RenderObjectId source;
    SceneTransform currentTransform;
    SceneTransform previousPublishedTransform;
    LocalBounds localBounds;
    MeshHandle mesh;
    MaterialBindingId material;
    RenderClassification classification;
    InstanceFlags flags;
    uint32_t visibilityMask;
    uint32_t lodPolicyIndex;
    RenderStateRevision revision;
    MotionState motion;
    ScenePresentationEpoch lastTransformChangeEpoch;
};
```

This is a logical contract, not a C++ binary layout to `memcpy` into GPU memory.
Target layouts are explicit versioned shader/reflection schemas under ADR-035.
They define fixed-width fields, packing, table indices and buffer segmentation;
schema identity participates in pipelines, artifacts and GPU Scene generations.
Backends cannot add private semantic fields or reinterpret padding.

The minimal production projection provides:

- current and previous **published** local-to-scene transforms for motion;
- finite local bounds and enough derived origin-relative bounds for culling;
- generation-checked mesh and material-table references;
- explicit opaque/masked/forward-only/transparent classification from ADR-036;
- visibility/layer masks, shadow/selection flags and a typed LOD policy index; and
- source/record/motion/origin generations required to reject stale consumers.

“Previous” means the transform used by the last successfully published rendered
generation, not every simulation update received. Coalescing multiple updates
therefore retains the last visible transform and newest admitted transform.
Teleport, first publication, reactivation after an invalid gap, skeleton/topology
discontinuity and an explicit no-motion flag set `MotionState::Reset`; shaders do
not infer zero velocity from coincident matrix bytes.

One frontend-owned `ScenePresentationEpoch` groups all views of the same scene
frame. A transform delta admitted for that epoch moves the prior current transform
to `previousPublishedTransform`, installs the new current transform and records
the epoch. All views in the group consume that same pair. In later epochs with no
transform update, `lastTransformChangeEpoch` tells the shader/packing plan to use
current as effective previous, so stale motion is not repeated and the frontend
does not rewrite every static record. A view never advances history independently.

Mesh/material references identify resident Horo generations and pin them while a
published record or in-flight draw can consume them. The target projection may use
compact mesh/material/binding table indices, including a bindless table when
admitted, but those indices are derived generation-scoped values. They are not
asset identity, cannot be serialized into scene content and are invalidated on
table/device recreation. Missing, pending or incompatible required dependencies
keep a create/update staged or fail it by declared policy; no dangling index or
silent default material is published.

### 4. Large-world transforms use one scene-origin generation

The CPU shadow retains canonical high-precision scene transform authority from
[ADR-026](026-large-world-precision-and-floating-origin-strategy.md). GPU transform
and bounds records use finite camera-safe `float32` values relative to one
`GpuSceneOrigin` and carry its generation. Each view converts its camera into the
same origin before culling and shading; per-view camera-relative matrices are
derived without rewriting stable identity or using a different world origin per
backend.

An origin rebase projects a committed ADR-026 `OriginRebaseEvent`. GPU Scene is
an `IOriginRebaseParticipant`: it does not choose when to rebase. It stages a new
origin generation and a bounded rebuild/translation of every affected published
transform and derived bound. The new instance buffers, view constants, culling
inputs, motion policy and graph plan publish atomically at
[ADR-018](018-command-registration-permissions-threading-and-packaged-build-policy.md)
`CommandThreadPolicy::RenderSafePoint`. Old frames retain the old origin/buffers.
Rebase never mixes view constants from one generation with instance data from
another. Motion resets or applies the mathematically equivalent old/new-origin
correction explicitly; the origin shift itself cannot produce object velocity.

If rebuilding all active records cannot fit the configured staging/update budget,
the coordinator begins early enough to stage over bounded work while the old
generation remains active, or returns a typed capacity/deadline result. It does not
partially expose a rebased scene. Scene Runtime remains the origin-rebase
authority; GPU Scene only projects the committed change.

### 5. Extraction emits ordered delta batches

Render extraction compares stable render state against its last acknowledged
projection and emits immutable `GpuSceneDeltaBatch` values. A batch carries
scene/incarnation, extraction snapshot revision, expected base GPU Scene generation,
origin generation, stable sequence and bounded create/update/remove operations.

Operations are typed:

- `Create` supplies the complete logical record and dependency generations.
- `Update` supplies expected source/instance generation, prior record revision,
  a typed dirty mask and replacement values for those fields.
- `Remove` supplies source/instance generation and final source revision.
- bulk cell activation/removal is a bounded collection of those operations, not a
  provider-owned mutation of GPU buffers.
  [ADR-012](012-world-streaming-partition-authority-and-subsystem-boundaries.md)
  remains the cell-admission and `BeginRetire`/`PollBarrier` authority. GPU Scene
  is a reader of committed cell residency: it emits create/remove for admitted or
  retiring cells and holds generation leases that `PollBarrier` must observe. It
  cannot evict a cell, expand a cell budget, or keep a retired cell's GPU records
  after the barrier completes.

Within a batch, operation order is stable by source identity and field category.
Duplicate/conflicting operations, non-monotonic revisions, unknown removes,
create-on-live identity and update-after-remove are rejected with typed diagnostics.
The producer may pre-coalesce updates only when the acknowledged base, last
published transform and remove/create boundaries remain preserved. The consumer
never resolves ambiguity with arrival order or hash-map iteration.

Snapshots remain independently usable by the CPU-driven path. Producing a delta
does not transfer scene ownership or allow GPU Scene to hold component-pool
pointers. Extraction keeps enough acknowledgement state to rebuild a complete
candidate after device loss, queue loss or an explicitly requested resync.

### 6. Admission, upload and publication are bounded transactions

Any permitted producer thread may submit an owned batch to a finite frontend
queue and receive a typed operation identity/result. Admission validates envelope
size, scene/device/schema generations and cancellation ownership without touching
native memory. Queue-full returns backpressure; it never drops removes or labels an
unpublished update successful.

CPU validation/coalescing and packed upload preparation may run as bounded
[ADR-010](010-job-waiting-and-operation-store-ownership.md) jobs over owned
immutable data with explicit cancellation and scene/device leases. Only the
host-declared render-capable owner applies slot allocation, resource pins, upload
reservations, graph-visible buffer changes and publication at ADR-018
`CommandThreadPolicy::RenderSafePoint`. Worker code cannot map native GPU memory,
reuse slots, publish table indices or outlive its scene/device/cancellation
leases. The render owner does not GPU-idle or mapped-readback-wait for a
same-frame histogram, culling result or upload completion; subsequent consumers
declare graph/fence dependencies on the published generation.

CPU shadow records, GPU instance/compact tables, staging/upload arenas and
in-flight generations are renderer-owned
[ADR-034](034-gpu-memory-and-residency-ownership.md) reservations. Admission
reserves them before realize. Mesh/material pins are
[ADR-027](027-renderer-resource-identity-and-descriptors.md) generation-checked
handles; upload/copy completion uses `ResourceOperationId`, not a native fence
exposed to extraction. Budget denial is a typed ADR-008/034 result.

Budgets separately bound queued batches/operations/bytes, CPU shadow records,
active/retired slots, dirty records per publication, upload bytes, staging memory,
resource-table references, rebuild work and in-flight generations. Capacity growth
is an explicit staged replacement outside frame-hot execution and charges both old
and new storage until retirement under ADR-034. No vector/buffer growth, full-scene
sort, blocking wait or unbounded retry occurs during graph execution.

One transaction validates all operations/dependencies, reserves candidate slots
and pins, packs target records and schedules uploads. It publishes a new immutable
`GpuSceneGeneration` at `RenderSafePoint` only when all required buffers/tables
are ready. Failure or cancellation rolls back candidate slots/pins/staging and
retains the last good generation. A failed remove never revives destroyed
gameplay state: extraction resyncs or the affected record is suppressed from new
plans until consistency is restored and diagnosed.

When work exceeds one frame's allowed upload budget, accepted transactions remain
pending and make bounded progress. The old published generation remains coherent.
Product policy defines maximum visual latency/deadline and whether optional GPU
Scene can fall back by compiling a new CPU recipe; it cannot render a half-applied
transaction or silently discard updates to meet a frame budget.

### 7. Consumers retain published generation leases

GPU culling, LOD selection, meshlet expansion, shadow work, generated draws,
ray-structure inputs and debug views consume an immutable published GPU Scene
generation plus view/configuration generations. Each declares exact buffers,
schema, origin, resource-table and access requirements to the render graph. No
consumer caches a raw mapped pointer or assumes a slot remains live after its
lease.

Culling/compaction output carries its source GPU Scene and view generations.
Indirect draw admission rejects mismatches before native execution.
Counter/argument capacities are bounded; overflow returns the recipe's explicit diagnosed
policy and never writes past storage. GPU results are presentation data. Optional
asynchronous diagnostics/readback are bounded and generation tagged; gameplay
cannot synchronously read them.

Different views can select different LOD/culling results over the same instance
records. Per-view visibility, selected LOD, sort keys and generated commands live
in view work buffers rather than mutating the persistent base record. Every view
in one scene presentation epoch uses the same current/effective-previous pair;
only owner admission of a transform delta changes history state.

### 8. Recovery, shutdown and errors are explicit

Device loss invalidates every GPU buffer, compact table index, pending upload and
published device generation. The CPU shadow and immutable extraction state are
reconstruction sources only while their scene/resource leases remain valid.
Frontend recovery creates a new device/GPU Scene generation, revalidates every
mesh/material dependency and republishes a complete candidate before GPU-driven
work resumes. It never preserves native addresses or accepts stale completions.

Scene teardown closes admission, cancels queued/preparation work, suppresses new
plans, retires published generations after GPU completion, releases pins and then
destroys CPU shadow/mapping state. Frontend/device shutdown performs the same order
for every scene and is idempotent after partial initialization. A producer holding
an old scene ID receives `SceneClosed`/`StaleSceneGeneration`, not access to a new
scene in the same slot.

Stable error categories cover invalid/stale source identity, malformed delta,
generation/revision conflict, non-finite transform/bounds, missing dependency,
schema/layout incompatibility, queue/capacity/upload/residency exhaustion,
cancelled/stale operation, origin mismatch, device loss and shutdown. Diagnostics
include bounded scene/source/instance IDs, expected/actual generations, operation
and field category, limit/usage and selected fallback rule without dumping complete
instance buffers or private scene data.

## Rejected Alternatives

| Option | Decision and reason |
|---|---|
| Re-upload every visible instance every view/frame | Rejected: duplicates work across views, loses persistent identity and makes large-scene cost unbounded. |
| Use ECS entity index as the GPU slot | Rejected: ECS reuse/lifetime and render subobjects do not match GPU retirement or multi-scene identity. |
| Store native GPU addresses or descriptor indices in scene components | Rejected: leaks backend/device generations into gameplay and cannot survive table/device recreation. |
| Let each backend own its GPU Scene model | Rejected: culling, updates, fallback and diagnostics would diverge; backends only realize validated buffers/copies. |
| Make GPU Scene authoritative for transforms or visibility | Rejected: publication latency, culling heuristics and device loss cannot define gameplay truth. |
| Update records in place while frames consume them | Rejected: races prior frames and mixes transform/resource/origin generations. Publish immutable generations and retire them by completion. |
| Reuse removed slots immediately | Rejected: in-flight culling, indirect and debug work can still reference the old slot. Reuse waits for all leases and increments generation. |
| One view-specific camera-relative instance buffer | Rejected as the persistent authority: multiplies updates and cannot serve multiple views. Use one scene origin plus per-view constants/work buffers. |
| Drop/coarsen deltas when the upload queue is full | Rejected: creates silent visual/state divergence. Apply backpressure or an explicit whole-recipe fallback. |
| Serialize GPU Scene in saves or cooked worlds | Rejected: it contains process/device/projection identity. Serialize scene/assets and reconstruct the projection. |
| Put ADR-011 VFX particles into GPU Scene slots | Rejected: VFX pass kinds, skip-sort additive and simulation ownership would be rewritten as scene-mesh instances. |

The selected model retains a CPU shadow and overlapping immutable generations,
which increases memory. In exchange it bounds updates, protects in-flight work,
supports multi-view reuse and device reconstruction, and keeps presentation state
outside gameplay authority.

## Migration And Verification

The current frame-owned `RenderInstance` arrays remain the CPU baseline and source
for initial extraction migration. They must gain stable `RenderObjectId` and
revision semantics before persistent deltas are enabled. Existing primitive mesh
caches and viewport backend registries remain migration sources, not GPU Scene
identity authorities. No current backend advertises GPU-driven capability merely
because it can allocate an instance buffer or issue an indirect draw.

| Delivery | Required implementation evidence |
|---|---|
| RND-014.1 / #401 | Typed GPU Scene registry, logical/packed schemas, slot generations, CPU shadow, transactional delta admission and lifecycle tests implement this decision. |
| RND-014.2 / #403 | Generated draw/dispatch baseline consumes bounded scene/view generations and rejects stale argument buffers. |
| RND-014.3 / #402 | Per-view frustum/distance culling over immutable generations with bounded compact output and CPU fallback. |
| RND-014.4+ / #404 onward | Hi-Z, compaction, LOD, bindless, meshlets and streaming integration consume generation/schema leases without redefining instance identity. |

Focused tests must cover:

- stable subobjects, despawn/recreate, scene reincarnation, generation exhaustion,
  stale create/update/remove and deterministic free-list reuse;
- complete/partial record validation, non-finite transforms/bounds, dirty masks,
  duplicate/reordered deltas and coalesced previous-published motion;
- mesh/material replacement, pending/missing dependencies, pin/retirement order,
  table/schema/device generation changes and no dangling derived index;
- exact capacity, queue/upload backpressure, multi-frame staging, cancellation at
  every phase, rollback and last-good generation retention;
- two or more views sharing records but producing independent LOD/visibility,
  with no duplicate transform-history advancement;
- origin rebase of a committed ADR-026 event while frames are in flight, atomic
  old/new generation use, motion reset/correction and bounded full-candidate
  staging;
- ADR-012 cell activate/retire as bounded create/remove with PollBarrier leases;
- remove/reuse while culling/indirect/debug leases are in flight, overflow and
  mismatched generated-draw admission;
- scene close, device loss during every phase, complete rebuild, stale completion,
  partial initialization and repeated shutdown;
- VFX batches remaining on the ADR-011 path rather than GPU Scene slots;
- RenderSafePoint publication without same-frame GPU idle/readback; and
- CPU fallback and required-GPU failure with explicit diagnostics. Native backend
  tests prove upload/barrier/retirement behavior; Null tests prove the state model.

## Consequences

GPU-driven features now share one stable instance identity, record vocabulary and
publication lifecycle. Multiple views can reuse persistent scene data; updates,
rebases, resource replacement and device loss have bounded transactional behavior.
The cost is a CPU shadow, generation/acknowledgement state, overlapping buffers
during publication and strict resource pinning. This ADR defines contracts only;
it does not implement GPU culling or advertise GPU-driven rendering support.
