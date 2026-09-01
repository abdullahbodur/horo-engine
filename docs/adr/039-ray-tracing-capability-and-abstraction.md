# ADR-039: Ray Tracing Capability and Abstraction

- **Status**: Proposed
- **Date**: 2026-09-01
- **Supersedes**: None
- **Scope**: Acceleration structures, ray queries, ray pipelines, limits, scheduling and fallback
- **Issue**: [RND-015.1](https://github.com/abdullahbodur/horo-engine/issues/412)
- **Jira**: [HORO-412](https://horo-engine.atlassian.net/browse/HORO-412)
- **Related**: [ADR-011](011-vfx-effect-ownership-simulation-domain-and-renderer-boundary.md),
  [ADR-018](018-command-registration-permissions-threading-and-packaged-build-policy.md),
  [ADR-027](027-renderer-resource-identity-and-descriptors.md),
  [ADR-028](028-renderer-capability-limits-and-product-profiles.md),
  [ADR-034](034-gpu-memory-and-residency-ownership.md),
  [ADR-036](036-raster-render-path-and-quality-architecture.md),
  [ADR-038](038-gpu-scene-and-instance-data-model.md)
- **Normative documents**: [Rendering Architecture](../architecture/runtime/rendering-architecture.md), [Advanced Rendering Architecture](../architecture/runtime/advanced-rendering-architecture.md)

## Context

The renderer currently has a single transitional `supportsRayTracing` boolean and
advanced-rendering prose that assumes acceleration-structure updates, a dedicated
ray pipeline, shader tables and denoising arrive together. Native APIs do not
provide that bundle uniformly. A device may support acceleration structures and
inline/intersection queries but not Horo's dedicated ray-pipeline path; update,
compaction, custom primitives, motion instances, callable shaders and limits also
vary independently.

Treating “ray tracing” as one capability would let profiles enable unavailable
operations, make Metal/Vulkan/D3D12 adapters expose false equivalence, and force
effects to invent hidden fallbacks. Acceleration structures also introduce large
scratch/result allocations, asynchronous build/compaction, GPU Scene generation
dependencies and in-flight retirement that must obey the existing graph, resource
and residency contracts.

This ADR owns backend-neutral ray operation capabilities, acceleration-structure
and shader-table abstractions, graph scheduling and fallback policy. It does not
choose reflection/GI/shadow algorithms, denoisers, material shading models, GPU
Scene identity or native API baselines.

## Decision

### 1. Ray support is a capability vector, never one boolean

ADR-028's reported/implemented/effective separation applies to ray work. The
effective snapshot represents these independently:

| Capability | Meaning |
|---|---|
| `AccelerationStructureBuild` | Build bottom-level (BLAS) and top-level (TLAS) structures from admitted triangle/AABB geometry. This is the prerequisite for all other ray operations. |
| `AccelerationStructureUpdate` | Update/refit a structure built with an admitted update policy while topology/format constraints remain satisfied. It does not permit arbitrary rebuild-in-place. |
| `AccelerationStructureCompaction` | Query compacted size and copy to a smaller result through asynchronous graph work. |
| `InlineRayQuery` | Traverse an admitted TLAS from a supported existing shader stage and handle candidates/results in that shader model. |
| `RayPipeline` | Create a dedicated ray-dispatch pipeline with ray-generation, miss and hit groups and dispatch it through graph work. |
| `CustomIntersection` | Intersect AABB/custom geometry using an admitted intersection shader/function route. Triangle traversal does not imply it. |
| `AnyHit`, `CallableShader`, `IndirectRayDispatch`, `MotionInstances`, `StructureSerialization` | Optional operations represented separately and never inferred from `RayPipeline` or a native marketing tier. |

Features are typed values with dependency closure. `InlineRayQuery` and
`RayPipeline` require acceleration-structure build plus their actual
shader/binding/synchronization requirements, but neither implies the other. Update and
compaction require the corresponding build flags and implemented copy/query path.
Serialization is device/driver compatibility-scoped cache acceleration only; it
is never the sole reconstruction source or a portable asset.

The snapshot includes named finite limits with units and provenance, including:

- maximum BLAS geometries/primitives, TLAS instances and instance-mask width;
- maximum recursion depth, payload/attribute bytes, callable depth where relevant,
  shader groups and dispatch dimensions;
- scratch/result/update size and address/offset alignment requirements;
- shader-table record/section alignment, stride and maximum table/record sizes;
- supported vertex/index formats and complete buffer usage/address predicates;
- supported instance/geometry flags, build modes and shader stages for inline query;
- maximum concurrent build/compaction work admitted by product memory/work budgets.

Zero never means unknown or unlimited. If a native concept does not map to a Horo
operation, it remains private diagnostic data. Contradictory feature/limit records
reject effective snapshot publication. Every resource/plan request checks the
complete capability, format, alignment, shader, memory and cooked-variant predicate.

The transitional `supportsRayTracing` boolean is not consumed by new plans. It is
removed when typed capabilities and backend translation land together; during
migration it may only report a conservative summary for diagnostics.

### 2. Native APIs map to Horo operations without false parity

Concrete adapters query native facts after device creation and translate only
implemented routes:

- **Vulkan** separately validates/enables the acceleration-structure
  extension/features and the ray-query and ray-tracing-pipeline extension/feature groups,
  including their dependencies, properties and shader stages. Vulkan's
  [specification](https://registry.khronos.org/vulkan/specs/latest-ratified/pdf/vkspec.pdf)
  distinguishes BLAS/TLAS and triangle/AABB geometry; extension presence without
  enabled features, device-address/format support and a Horo implementation grants
  nothing.
- **D3D12** queries the selected adapter's `D3D12_RAYTRACING_TIER` through
  `D3D12_FEATURE_DATA_D3D12_OPTIONS5`, as documented by
  [Microsoft](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_feature_data_d3d12_options5),
  then intersects the tier's operations with Horo's compiled DXIL, state-object,
  dispatch, update and synchronization implementations. Feature level, Agility
  version or “DirectX 12 Ultimate” does not grant a Horo ray capability.
- **Metal** may expose acceleration structures and intersector/intersection-query
  routes through render or compute pipelines and intersection function tables.
  Apple's [ray-tracing documentation](https://developer.apple.com/documentation/metal/ray-tracing-with-acceleration-structures)
  explicitly models acceleration structures, intersectors/queries and intersection
  function tables. The adapter maps those actual operations; it does not pretend
  Metal owns a Vulkan/DXR-shaped dedicated ray pipeline or shader table.
- **OpenGL 4.1** reports no initial ray operations. Vendor extensions or compute
  emulation do not silently alter that baseline.
- **Null** validates descriptors, capability resolution, graph/lifecycle schedules
  and errors without claiming native traversal, performance or image evidence.

Equal backend standing means equal Horo semantics for every advertised operation,
not identical feature sets or native object models. A product/effect can require
`InlineRayQuery` while permitting no `RayPipeline`, or require both. Diagnostics
show reported, implemented and effective operation sets and the exact restricting
rule; backend IDs never appear in portable scene/material requirements.

### 3. The frontend owns ray-scene projection and typed AS identity

`RenderFrontend` owns a `RayScene` projection for an admitted GPU Scene/device
generation. GPU Scene remains the render-object/transform/resource identity owner
under ADR-038. `RayScene` owns only geometry-to-BLAS mapping, TLAS instance
projection, acceleration-structure generations, build metadata and leases.

Typed process-local handles distinguish bottom and top structures:

```cpp
struct BottomLevelAsHandle { RenderFrontendId owner; uint32_t slot; uint32_t generation; };
struct TopLevelAsHandle    { RenderFrontendId owner; uint32_t slot; uint32_t generation; };
```

They follow ADR-027 `Pending`/`Ready`/`Retiring`/`Retired`/`Failed` state,
non-wrapping generation, dependency pins and deferred retirement. They are not
native addresses, GPU Scene slots, asset IDs or serializable values. Native
device addresses/descriptors stay inside the backend realization and are
invalidated by recreation. There is no `released` state.

A versioned `RayGeometryDescriptor` admits bounded arrays of:

- triangle geometry with generation-checked vertex/index resources, explicit
  offsets/strides/counts/formats and finite object-space bounds; or
- AABB geometry with an explicit stride/count/bounds and a required compatible
  custom-intersection variant.

Geometry declares opacity/any-hit policy, transform source and build preference.
Buffer descriptors must include the admitted AS input/device-address usages;
bounds and checked byte ranges must fit. Mixed triangle/AABB support, index
absence, transform buffers and update eligibility are validated explicitly. No
adapter reads mesh memory based on a guessed vertex layout or source C++ struct.

Hit shading uses the same material identities as raster, not a third ID:

| Identity | Owner | Role in RayScene |
|---|---|---|
| `MaterialId` | Scene conversion (ADR-027) | Cooked material-table key and classification source |
| `MaterialBindingId` | GPU Scene record (ADR-038) | Packed resident texture/sampler/buffer binding |
| `RayHitGroupId` | Cooked ray-pipeline group table | Logical closest-hit/any-hit/intersection group |

`RayHitGroupId` is derived at projection time from the GPU Scene record's
`MaterialId`, `MaterialBindingId` and ADR-036 `RenderClassification` plus the
admitted pipeline's group table. Closest-hit/any-hit shaders bind the same
`MaterialBindingId` resources as the raster path. Native SBT/intersector records
pack only derived identifiers; they are not serialized as a material identity.

`RayInstanceDescriptor` references a `Ready` BLAS plus a stable GPU Scene
instance, finite origin-relative transform, mask, front-face/culling policy and
the derived `RayHitGroupId`. TLAS generations carry exact GPU Scene, origin,
mesh/resource table, `MaterialBindingId` and hit-layout generations. Removed or
changed instances cannot remain reachable in newly published TLAS work, while old
frames retain old leases.

[ADR-011](011-vfx-effect-ownership-simulation-domain-and-renderer-boundary.md)
VFX particle, decal and volume batches do not participate in `RayScene`. They
occupy no GPU Scene slots (ADR-038) and therefore no BLAS/TLAS instances. Ray-
traced reflections, shadows, AO and GI do not see smoke, fire, sparks or other
VFX unless a later ADR admits a separate VFX acceleration-structure path. Raster
and screen-space fallbacks continue to composite VFX through ADR-011/037.

ADR-036 `Masked` geometry is admitted to RayScene only when `AnyHit` (or an
equivalent admitted inline/intersector route that evaluates the same
deterministic coverage) is effective for that recipe. Missing that operation
omits the Masked instance from the TLAS; it is never coerced to `Opaque`.
Coercing Masked to opaque would solidify foliage and cutouts in ray shadows.
`Opaque` and `ForwardOnlyOpaque` use opaque geometry flags. `TransparentSorted`
and `TransparentAdditive` do not contribute to RayScene unless an effect
declares a later optional transmissive recipe.

### 4. Build, update, compaction and publication are explicit graph work

Creating an AS first obtains backend build-size/alignment facts through a bounded
owner-thread request, reserves result/scratch/upload/residency budgets, and creates
a pending Horo generation. Size queries are not allocation and cannot be cached
across incompatible descriptors/device generations. Backend-reported sizes are
checked for overflow, policy limits and alignment before reservation.

The render graph represents AS build/update/copy as typed passes with declared:

- geometry/instance input reads and lifetime pins;
- scratch/result reads/writes and aliasing exclusion;
- source structure for update/compaction;
- queue/stage/access requirements and completion dependency; and
- scene/device/descriptor/build generations plus cancellation ownership.

Backends encode those validated passes and barriers privately. No feature system
calls a native build command, inserts a manual barrier, blocks for GPU idle or
reuses scratch concurrently outside its declared lifetime.

AS build, update, compaction and copy may run on a dedicated compute or copy
queue only when that queue is an independently effective ADR-028 capability. A
compute feature does not imply an asynchronous compute queue. Graphics-queue-only
devices schedule the same typed passes on the graphics queue. Queue choice is
part of the graph pass, not an adapter-local performance switch. Completion is a
graph/fence dependency consumed at
[ADR-018](018-command-registration-permissions-threading-and-packaged-build-policy.md)
`CommandThreadPolicy::RenderSafePoint`; it is not a CPU idle wait.

Static BLAS prefers trace-optimized build and may compact when implemented and
budgeted. Dynamic/deforming BLAS declares fast-build/update policy. An update is
legal only when the original generation allowed it and the native/Horo invariant
set remains unchanged: geometry count/type, formats and topology fields required
by that route. Otherwise stage a rebuild and publish a new generation. “Refit” is
not permission to change arbitrary index/vertex layout.

Compaction size results arrive asynchronously. The original ready generation
remains valid while a compacted candidate is allocated/copied; publication swaps
the RayScene mapping atomically and retires old storage after consumers finish.
No frame-hot CPU readback/wait is introduced. Compaction is skipped or deferred
with a recorded reason when overlap exceeds budget; it cannot discard the only
valid structure.

TLAS batches consume one immutable GPU Scene generation. Instance
create/update/remove and origin rebase stage a complete coherent TLAS candidate or an admitted
update that preserves all required invariants. New TLAS, dependent shader tables
and effect plans publish together at
[ADR-018](018-command-registration-permissions-threading-and-packaged-build-policy.md)
`CommandThreadPolicy::RenderSafePoint`. Failure/cancellation
keeps the last good compatible generation or disables only an explicitly optional
effect through a new resolved plan.

### 5. Ray queries and ray pipelines use distinct shader contracts

`InlineRayQuery` is an operation in a declared existing shader stage/variant. The
shader manifest records required query features, TLAS binding, ray/candidate flags,
payload/result schema, maximum per-invocation query work where product policy can
bound it, and fallback variant. It does not create a hidden ray-generation stage or
inherit ray-pipeline recursion/payload limits that do not apply.

`RayPipeline` uses a versioned logical descriptor containing:

- ray-generation, miss, closest-hit, optional any-hit/intersection and optional
  callable entry-point IDs from cooked target artifacts;
- hit-group composition and stable logical group IDs;
- payload/attribute schema IDs and byte sizes;
- maximum recursion/callable depth and required features; and
- resource/binding layout, local-record parameter schema and fallback identity.

ADR-035 compiles/reflects actual target artifacts and validates every entry point,
stage, payload/attribute size, resource and logical group. A Horo pipeline is
created only on targets with an implemented equivalent route. Metal intersector
variants may implement an effect through `InlineRayQuery`; they are not relabeled a
`RayPipeline` merely to share high-level effect code.

Shader-table data is a backend-neutral logical `RayDispatchTable` of group IDs and
typed local arguments. The frontend validates group existence, record counts,
parameter schema, resource generations and finite total size. A private adapter
packs native shader identifiers/handles, addresses, strides and section alignments
from the effective limits. Portable code never writes native shader identifiers,
assumes DXR/Vulkan section order, or serializes a native table. Metal intersection
function tables are private realizations of the relevant logical binding contract,
not proof that all SBT sections exist.

Every ray dispatch declares dimensions, pipeline/query variant, TLAS, dispatch
table where required, graph resources and maximum admitted work. Checked products
and device limits bound dimensions and records. Indirect dispatch is unavailable
unless independently effective and implemented. Missing groups, stale TLAS/table,
payload mismatch or excessive recursion/dimensions fail before encoding.

### 6. Effects own algorithms; plans own explicit fallback

Ray tracing is optional for all built-in Baseline, Standard, High and Ultra
profiles. Ultra may prefer individually enabled ray effects; it does not require a
global ray tier. Each reflection, shadow, AO or GI recipe declares:

- exact AS/query/pipeline/custom-intersection/denoiser capabilities and limits;
- geometry/material coverage, update frequency and finite memory/work budgets;
- cooked shader/table and output/history schemas;
- quality/temporal/noise assumptions and required cross-backend evidence; and
- an ordered raster/screen-space/probe/baked fallback, or a required-feature
  failure when semantic fallback is not allowed.

Capability resolution happens before AS allocation and graph execution. An effect
may choose an inline-query implementation on one backend and a ray-pipeline
implementation on another only when both are separately qualified to the same
declared effect/output semantics and the resolved plan reports the route. It cannot
change routes frame-to-frame due to timing or memory pressure.

Missing optional support compiles/publishes a new explicit fallback plan and
diagnostic; it does not silently omit rays, lower recursion, substitute geometry,
disable any-hit, change denoising, switch backend or claim the requested effect is
active. Required content returns a typed admission failure. Denoising is an effect
dependency with its own resources/history/fallback; `RayPipeline` does not imply a
denoiser.

Ray visibility is presentation data. Traversal order, floating-point intersection
and implementation details are not cross-device deterministic gameplay truth.
Physics, AI sight, audio occlusion, networking and save data use their owning
CPU/deterministic query contracts and never synchronously wait for ray traversal or AS
readback.

### 7. Memory, lifecycle and diagnostics follow common owners

AS result, scratch, staging, compaction overlap, shader-table and effect resources
are charged to finite ADR-034 reservation classes. RayScene retains source GPU
Scene/mesh/material/pipeline generations and releases them only after build and
trace consumers complete. Scratch may alias only when graph lifetime and backend
requirements prove non-overlap. Normal frames never wait for idle or synchronous
post-build readback.

Device loss invalidates native AS addresses, compacted blobs, shader identifiers,
tables, pipelines, pending size/build work and every RayScene device generation.
Recovery revalidates effective capabilities and rebuilds from retained GPU Scene,
mesh/cooked shader descriptors where leases remain valid. Optional effects may
resolve to fallback while rebuilding; required effects keep the scene/output
unready with a typed reason. Stale completions cannot publish into the replacement.

Scene close stops admission, cancels queued CPU preparation, suppresses new ray
plans, retires in-flight dispatch/build generations, releases dependency pins and
then destroys CPU mapping state. Frontend/device shutdown applies that order to
every RayScene and is idempotent after partial initialization.

Stable errors cover unsupported operation/stage/geometry/format, malformed or
stale descriptor/handle/generation, invalid update, missing shader group/variant,
payload/attribute/table-layout mismatch, limit/alignment overflow,
queue/budget/residency exhaustion, cancellation, device loss and shutdown. Diagnostics include
bounded logical scene/AS/effect/group IDs, requested/effective operations, relevant
limits/usage, driver restriction IDs and selected fallback without native handles,
addresses, shader binaries or geometry payloads.

## Rejected Alternatives

| Option | Decision and reason |
|---|---|
| One `supportsRayTracing` flag | Rejected: AS, inline query, dedicated pipeline, update, compaction and shader operations vary independently. |
| Make DXR or Vulkan's object model the Horo API | Rejected: leaks native stages, tables, addresses and false parity into Metal and portable effects. |
| Emulate `RayPipeline` with Metal compute and call it equivalent automatically | Rejected: an effect may qualify an inline/intersector route, but capability names preserve actual scheduling/shader semantics. |
| Require ray tracing for Ultra | Rejected: profiles are preferences and all built-in effects retain declared non-ray recipes. |
| Build/update acceleration structures directly from feature code | Rejected: bypasses graph synchronization, residency, cancellation and generation lifetime. |
| Treat GPU Scene slots or device addresses as TLAS identity | Rejected: they are generation-scoped projections and cannot survive slot/device recreation. |
| Update/compact AS in place while prior frames trace it | Rejected: mixes generations and creates races. Publish candidates and retire old storage by completion. |
| Serialize native AS or shader tables as portable cooked assets | Rejected: device/driver addresses and identifiers are not portable or sufficient reconstruction sources. |
| Use ray traversal for gameplay line of sight or physics | Rejected: presentation latency and nondeterministic hardware traversal cannot own authoritative simulation. |
| Silently turn off any-hit, recursion, geometry or denoising under pressure | Rejected: changes effect semantics. Resolve an explicit fallback plan or fail required admission. |
| Treat Masked geometry as Opaque when `AnyHit` is missing | Rejected: solidifies cutouts in ray shadows. Omit Masked from RayScene or fail the required recipe. |
| Put ADR-011 VFX into RayScene/TLAS | Rejected: VFX is not GPU Scene and would appear in reflections/shadows without an admitted VFX AS path. |
| Assume a dedicated async compute queue for AS builds | Rejected: ADR-028 compute does not imply that queue. Declare it or run on graphics. |

The capability-vector and graph model require more descriptors, validation and
per-backend translation than a boolean. They prevent unsupported combinations,
preserve honest backend semantics and make effect fallback and memory behavior
reviewable.

## Migration And Verification

The current `RenderBackendCapabilities::supportsRayTracing` becomes diagnostic-only
during migration and is deleted when typed operation/limit snapshots are consumed.
No existing backend becomes ray-capable from this ADR. Vulkan/D3D12/Metal delivery
must add reported queries, implementation declarations, driver restrictions,
effective translation and native qualification together.

| Delivery | Required implementation evidence |
|---|---|
| RND-015.2 / #413 | Typed BLAS/TLAS descriptors, size queries, graph build/update/compaction, RayScene projection, residency and lifecycle tests. |
| RND-015.3 / #414 | AS/ray graph scheduling, scratch/alias/barrier/queue policy and bounded memory accounting. |
| RND-015.4 / #415 | Inline ray-query shader/binding contract and qualified effect/fallback fixtures. |
| RND-015.5 / #416 | Dedicated ray-pipeline, logical dispatch-table/SBT packing and target reflection validation. |
| RND-015.9 / #420 | Denoising/history integration and cross-route temporal/output qualification. |

Tests must cover:

- every reported/implemented/effective operation combination, dependency closure,
  absent/zero/malformed limits and driver restrictions;
- triangle/AABB descriptors, checked ranges/formats/alignments, opacity/custom-
  intersection flags and stale mesh/GPU Scene/origin/material generations;
- BLAS/TLAS build, legal/illegal update, rebuild, asynchronous compaction, exact
  capacity, scratch alias/non-alias, cancellation and rollback;
- slot/resource/device replacement while builds/traces are in flight, last-good
  retention, scene close, device loss/rebuild and repeated shutdown;
- inline-query stage/variant/result schemas independently from ray-pipeline tests;
- ray-generation/miss/hit/any-hit/intersection/callable group validation,
  payload/attribute/recursion bounds and native dispatch-table alignment/packing;
- multi-view TLAS reuse and divergence, origin rebase, removed-instance suppression
  and no GPU visibility readback into gameplay;
- Masked geometry omitted without `AnyHit` rather than coerced to Opaque;
- VFX batches remaining off RayScene/TLAS;
- AS work on graphics versus independently effective compute/copy queues;
- optional fallback and required failure under each missing operation, shader,
  denoiser, memory and geometry-coverage predicate; and
- equivalent effect output fixtures for every advertised native route with stated
  image/noise/temporal tolerances. Null proves contracts only; Vulkan/D3D12/Metal
  each require compatible hardware/driver evidence for advertised operations.

## Consequences

Effects can request precise ray operations without assuming a backend-native
bundle. AS and shader-table lifetime now compose with GPU Scene, graph, residency,
device recovery and explicit fallback. The cost is a richer capability snapshot,
separate query/pipeline shader routes, substantial memory/lifecycle validation and
native qualification. This ADR enables no ray effect or backend capability by
itself.
