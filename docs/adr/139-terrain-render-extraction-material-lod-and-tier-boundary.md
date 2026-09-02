# ADR-139: Terrain Render Extraction, Material, LOD and Tier Boundary

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: Terrain/Foliage render-candidate extraction, render-resource realization, material/permutation admission, per-view visibility and LOD ownership, Terrain-tier/render-profile interaction, 1.0 CPU baseline, post-1.0 GPU-driven recipes, replacement, cancellation, device loss and shutdown
- **Issue**: [TRF-003.1](https://github.com/abdullahbodur/horo-engine/issues/1947)
- **Jira**: [HORO-1903](https://horo-engine.atlassian.net/browse/HORO-1903)
- **Related**: [ADR-027](027-renderer-resource-identity-and-descriptors.md), [ADR-028](028-renderer-capability-limits-and-product-profiles.md), [ADR-034](034-gpu-memory-and-residency-ownership.md), [ADR-035](035-shader-source-and-intermediate-representation.md), [ADR-036](036-raster-render-path-and-quality-architecture.md), [ADR-038](038-gpu-scene-and-instance-data-model.md), [ADR-137](137-terrain-foliage-ownership-data-tier-and-lifecycle.md), [ADR-138](138-terrain-source-cooked-tile-cache-and-streaming-ownership.md)
- **Normative documents**: [Terrain and Foliage Architecture](../architecture/runtime/terrain-and-foliage-architecture.md), [Rendering Architecture](../architecture/runtime/rendering-architecture.md), [Material and Shader Model](../architecture/runtime/material-and-shader-model.md), [LOD and Culling Architecture](../architecture/runtime/lod-and-culling-architecture.md)

## Context

ADR-137 makes Terrain/Foliage a backend-neutral runtime vertical slice and assigns all
native GPU resources and execution to Renderer. ADR-138 defines immutable dataset/tile
artifacts, decoded residency and generation replacement. The current Terrain document
still describes extraction as pushing draw commands into opaque/transparent arrays,
selects LOD by camera distance inside Terrain, and describes foliage indirect draws as
if they were mandatory. Those statements do not define the boundary between logical
resident content and view-dependent presentation.

Several authorities meet at that boundary. Terrain knows tile topology, semantic layer
bindings, baked LOD error data, foliage type/instance membership, holes, mutations and
residency. Material/Shader cooking knows legal shader interfaces and permutations.
World Streaming decides which cells are resident. RenderFrontend knows active views,
raster recipe, effective device capabilities, GPU budgets, visibility history, native-
resource generations and pass execution. A private backend only translates an admitted
plan.

If Terrain emits commands or owns per-view GPU state, it becomes backend-dependent and
can disagree with the render graph. If Renderer treats resident content as permission to
choose arbitrary layers or LODs, it can violate Terrain seams and authored requirements.
If profile names or backend names select features implicitly, missing variants can
silently drop layers, foliage or visual quality. If the 1.0 contract requires compute,
indirect draws or GPU Scene, Null/headless and baseline interactive compositions cannot
validate the same public boundary.

This ADR assigns render-candidate, material, resource, visibility, LOD and tier/profile
ownership. TRF-003.2 freezes the exact Terrain material-layer and permutation model;
TRF-003.3 freezes concrete GPU resource and snapshot schemas. This decision constrains
those contracts without prematurely declaring their layouts implemented.

## Decision

### 1. Terrain publishes render meaning; Renderer owns presentation

| Responsibility | Authority |
|---|---|
| Resident tile/cluster/instance membership, semantic surface/layer bindings, holes, baked LOD topology/error, mutation state and immutable snapshot lifetime | TerrainRuntime |
| View-independent projection from an immutable Terrain snapshot into bounded Horo render candidates | Terrain render-extraction adapter |
| Material/shader authoring schemas, semantic validation and deterministic cooked permutation requests | Material/Shader domain through Assets Cook |
| Product raster recipe, per-view visibility/LOD plan, RenderObject/GPU-resource mapping, batching, passes, uploads and deferred GPU retirement | RenderFrontend |
| Global cell residency, aggregate streaming reservation and cell activation/eviction | World Streaming |
| Native buffer/image/pipeline/descriptor/command realization and API synchronization | Selected private render backend |
| Gameplay visibility, targeting, collision, navigation and authoritative distance rules | Their owning gameplay/Physics/Navigation systems, never render results |

The Terrain adapter depends only on TerrainApi and RenderApi value contracts. It does
not include backend headers, query a native device, register a renderer, select a raster
recipe, allocate a GPU resource, compile a shader, submit a pass or wait for GPU work.
Its descriptor construction and registration remain inert until the application host
explicitly composes the adapter.

RenderFrontend cannot edit Terrain residency, mutation, material-layer meaning or LOD
topology. It may consume only an immutable leased generation and returns typed readiness,
pressure and retirement evidence through the composed integration boundary.

### 2. Extraction publishes a bounded view-independent candidate snapshot

The logical boundary is equivalent to:

```cpp
struct TerrainRenderExtractionSnapshot {
    TerrainRuntimeHandle terrain;
    TerrainSnapshotRevision terrainRevision;
    TerrainContentRevision content;
    TerrainRenderSchemaVersion schema;
    TerrainCoordinateFrame coordinates;
    TerrainRenderRequirements requirements;
    BoundedSpan<TerrainTileRenderCandidate> tiles;
    BoundedSpan<FoliageRenderCandidate> foliage;
    TerrainSnapshotLease lease;
};
```

Each candidate carries stable Terrain identity, exact source generation/revisions,
camera-relative-convertible bounds/transform inputs, neutral cooked geometry/artifact
references, material-layer-set or material binding identities, baked LOD descriptors and
seam/neighbor compatibility, hole/visibility semantics, shadow/depth/motion requirements,
and finite cost estimates. Foliage candidates also carry type, cluster and bounded baked/
dynamic instance ranges with stable logical instance identity.

Candidates do not contain:

- native handles, addresses, descriptor indices, command buffers, fences or API enums;
- mutable Terrain pointers, editor state, source paths or provider-owned byte pointers;
- selected per-view LOD, visible lists, indirect arguments or occlusion results;
- compiled pass order, pipeline state objects or backend shader modules; or
- gameplay visibility/collision/navigation truth.

The snapshot is view-independent. Multiple compatible views may consume the same leased
snapshot without advancing Terrain state. Extraction performs no file I/O, source decode,
asset cook, blocking shader/pipeline build or unbounded allocation. Capacity is reserved
before iteration; overflow rejects the frame/view candidate with a typed result rather
than truncating or silently dropping tiles/instances.

Terrain stable identities are not `RenderObjectId`. RenderFrontend derives stable,
generation-checked render-object identities from the Terrain runtime/generation and
tile/subobject or foliage-instance identity, preserving distinct subobjects and retiring
old mappings under ADR-038.

### 3. Residency, visibility and LOD are independent facts

World Streaming residency means neutral Terrain content and required cell consumers are
prepared/Active. It does not mean a tile is visible in a particular view. RenderFrontend
owns per-view frustum, distance, occlusion and presentation classification because those
depend on the active camera, output, raster recipe, history and effective render plan.
Render results never evict a cell or become gameplay/streaming truth.

Terrain Cook/Runtime owns the legal representation set:

- canonical LOD identities and geometric-error/transition data;
- seam, skirt, morph and neighbor-compatibility rules;
- material/layer/hole semantics preserved at each representation;
- exact resident/available artifacts and their generation; and
- required versus optional representation/fallback declarations.

RenderFrontend selects one admitted representation per view from that finite set. The
selection uses a versioned `TerrainRenderLodPolicy` with camera projection, viewport,
screen-space error thresholds, hysteresis/transition state, effective profile limits and
finite work/memory budgets. Distance alone is not the identity or full policy.

Adjacent selections must satisfy the candidate's seam/neighbor constraint. A transition
may use only declared cooked skirts, morph data or dual-LOD cross-fade. Renderer cannot
invent geometry, change a Terrain seam signature or independently clamp a neighbor. If
no compatible required selection is resident/admitted, the view returns explicit
`Preparing`, `Unavailable` or `Failed` readiness under product policy; it does not render
a crack, an unrelated tile, flat ground or nothing while reporting success.

Per-view LOD/visibility may emit bounded, generation-tagged demand/pressure observations
to the application/World Streaming authority for future frames. It cannot synchronously
load/evict tiles or mutate the current snapshot while planning a frame.

### 4. Material semantics and renderer realization remain separate

Terrain authoring/cook owns the ordered semantic layer set, weight/hole encodings,
material-function references, required blend features and legal per-tile layer masks.
Material/Shader cook validates those inputs and emits explicit target artifacts plus
Horo reflection for a finite permutation family. TRF-003.2 owns the exact key, layer
limits and blend rules.

At minimum, a Terrain material permutation is distinguished by declared shader/material
identity, pass, neutral Terrain vertex/payload layout, required layer/blend/hole/wind/
shadow/motion feature set and target artifact identity. Product profile names are
admission preferences, not hidden compile flags. A profile-only numeric parameter remains
a parameter; a byte/code-affecting choice must appear in the declared permutation or
artifact key.

RenderFrontend resolves material bindings and selects only cooked permutations that
satisfy the active raster recipe, candidate requirements, effective capabilities/formats/
limits and exact Terrain content generation. The private backend realizes the selected
shader/pipeline and bindings; it never chooses another layer count, disables holes,
changes blend semantics or substitutes a fallback shader.

Opaque, Masked, TransparentSorted and TransparentAdditive are material/render
classifications, not guesses based on “terrain”, “trunk” or “leaf”. Masked foliage stays
in depth-writing masked passes; genuinely translucent authored foliage follows the
declared transparency recipe. Extraction preserves classification requirements and the
frontend maps them into ADR-036 passes. No candidate is rendered twice or moved between
classes by backend convenience.

Missing required textures, reflection, target variants, layer capacity, pass
compatibility or bindings fail preparation before publication. Optional fallback is
legal only when the authored/cooked material and product plan declare the exact alternate
variant and observable reason.

### 5. Terrain tier and render product profile are independent axes

ADR-137 `TerrainFeatureTier` (`Baseline`, `Standard`, `High`, `Ultra`) describes a
provider-neutral Terrain content/runtime preference and its finite layer/LOD/instance/
work limits. ADR-028 `RenderProductProfile` uses similar labels for renderer recipe
preference. Equal spelling does not make the values interchangeable, ordered together or
implicitly convertible.

The application composition resolves a generation-scoped `TerrainRenderPlan` from:

- the requested Terrain tier and its exact finite resolved limits/features;
- the requested renderer profile and resolved ADR-036 raster recipe;
- the selected Terrain cooked dataset/material/permutation variants;
- TerrainRuntime, RenderFrontend and backend effective capability revisions;
- active view/output requirements and host mode; and
- World Streaming plus renderer CPU/GPU/staging/retirement budgets.

The result records both requested and selected axes, every exact variant/algorithm,
numeric capacity, revision, required predicate and fallback edge/reason. A Terrain tier
cannot grant compute/indirect/bindless support. A renderer profile cannot increase a
Terrain layer/instance limit, discard required Terrain content or select a different
dataset. Backend/API names and Debug/Release builds select neither axis.

Fallback traverses only a declared edge with a cooked compatible result. It may lower an
optional visual recipe while retaining all required Terrain/material semantics. It cannot
silently drop layers, holes, shadow/collision agreement, instances or required quality;
switch renderer/backend; invent a runtime shader; or alter World Streaming residency.

### 6. The 1.0 baseline is bounded CPU planning with neutral draw batches

Core 1.0 requires no Terrain-specific compute shader, GPU Scene residency, GPU-driven
culling, Hi-Z, indirect-count draw, bindless material access, mesh shader, virtual
texture or native multi-draw facility.

The 1.0 path is:

1. TerrainRuntime publishes one bounded immutable candidate snapshot.
2. RenderFrontend performs deterministic per-view CPU frustum/distance tests and LOD/
   seam selection using preallocated work storage.
3. RenderFrontend resolves cooked material permutations/resources and emits bounded
   backend-neutral direct or instanced draw batches into the compiled raster graph.
4. The selected backend translates validated batches and owns native execution.

CPU planning is presentation-only and may run in bounded jobs over frame-owned storage;
the frontend owns final deterministic merge/order and capacity checks. Normal extraction
and planning perform no blocking asset I/O or native resource creation. Foliage may use
renderer-owned instancing, but the public contract does not require indirect execution.

The Null backend validates candidate, plan, pass, capacity, fallback and lifetime
contracts with deterministic summaries. It claims no raster image, native culling or GPU
performance qualification. A headless/server host may omit the render adapter entirely;
Terrain logical/streaming/collision/navigation readiness remains valid without visual
readiness.

### 7. GPU-driven Terrain/Foliage is an explicit post-1.0 recipe

Post-1.0 may add a renderer-owned GPU-driven recipe using ADR-038 GPU Scene or a dedicated
renderer-internal Terrain projection, compute frustum/distance/occlusion/LOD selection,
Hi-Z, compaction and generated indirect batches. That recipe requires a separate accepted
contract, implemented backend operations, cooked shader/material variants, finite GPU
buffers/counters, overflow policy, synchronization, observability and cross-backend
qualification.

Even in that recipe:

- Terrain publishes the same semantic candidate boundary and owns no native work;
- RenderFrontend owns logical GPU records, per-view work, selected LOD, generated batches
  and resource generations;
- the backend owns only native translation/resources/synchronization;
- GPU visibility and LOD results do not become gameplay or streaming truth;
- normal frames perform no same-frame CPU readback or GPU wait; and
- overflow never writes beyond capacity or silently loses required candidates.

GPU unavailability may select the 1.0 CPU path only when the product plan declares it,
the required content fits its finite limits, and compatible cooked variants exist. A
required GPU-driven product fails admission instead of pretending CPU equivalence.

### 8. Resource preparation and aggregate publication are transactional

Terrain render readiness is a joint generation fact, not “decoded tile exists”. For each
candidate generation, RenderFrontend validates the snapshot/schema, effective plan,
material/permutation artifacts and peak allocation estimates before staging resources.
CPU preparation may occur on workers; native creation/upload and registry publication
occur through renderer-owned queues and `RenderSafePoint` rules.

Candidate resources are detached from the Active set. Failure, cancellation, stale
Terrain/content/capability/view evidence, budget denial or device loss before publication
destroys only candidate-owned work and preserves the prior valid generation. World
Streaming/Terrain aggregate commit accepts visual readiness only for the exact requested
generation and plan.

Terrain snapshot/provider-byte leases remain alive until upload no longer reads them.
Published render resources retain exact material/artifact dependencies. In-flight frames,
GPU Scene/draw work and backend submissions pin old generations; Terrain replacement or
cell eviction cannot force-free them. Render acknowledges retirement only after GPU
completion and resource-dependency release, allowing Terrain/World Streaming to release
their corresponding shared charge/lease.

### 9. Replacement, device loss and shutdown never cross ownership

Terrain content, mutation, residency, material, renderer-profile, view/output and device
changes each create a new tagged candidate/plan generation. They never patch values
visible to an in-flight frame. Exact-compatible immutable artifacts may be reused only
when their full Terrain/material/render signatures match; a same tile or material name is
insufficient.

On device loss, RenderFrontend invalidates native resource generations and rebuilds from
its declared reconstruction sources under ADR-027/034. Terrain remains logical truth and
may lend the same still-valid snapshot/artifacts; it does not recreate native objects or
pretend visual readiness. Failed reconstruction reports visual failure while independent
Terrain/Physics/Navigation state follows its own policy.

Shutdown closes new extraction/resource admission, cancels or drains renderer-owned
candidate work, rejects late generations, stops new frame use and retires native state on
the required owner thread. The render adapter releases Terrain snapshots only after its
CPU and upload reads end. A deadline may report incomplete renderer retirement; it cannot
force Terrain or Renderer to release possibly referenced state.

### 10. Errors and observability preserve both domains

The boundary returns typed errors with safe context such as Terrain runtime/content/
snapshot revisions, tile/cluster/type identity, render plan/profile/capability revision,
view/output generation, material/permutation/artifact identity, required feature, budget
and nested provider cause. Stable categories include:

```text
terrain.render.snapshot_incompatible
terrain.render.candidate_capacity_exceeded
terrain.render.required_representation_unavailable
terrain.render.seam_plan_unsatisfied
terrain.render.material_variant_missing
terrain.render.material_binding_incompatible
terrain.render.profile_plan_unsatisfied
terrain.render.resource_budget_denied
terrain.render.stale_generation
terrain.render.device_lost
terrain.render.retirement_incomplete
```

Metrics use bounded registered dimensions and distinguish candidate/visible/drawn counts,
CPU planning time, LOD selections/transitions, material/variant batches, resource bytes,
upload/backpressure, fallback reason and retirement depth. Tile/instance/material IDs,
paths and native handles do not become metric dimensions. Detailed per-item evidence is
available only through bounded explicit diagnostics/captures and never drives policy.

### 11. Verification proves the boundary and both recipe classes

Required coverage includes:

- compile/dependency tests proving Terrain and extraction public contracts expose no
  backend/native header, handle, command or service-locator dependency;
- deterministic bounded extraction from immutable snapshots, multi-view reuse, capacity
  failure and stale Terrain/content/mutation/residency/capability generation rejection;
- strong separation of Terrain identity, RenderObject/resource identity, material/
  permutation identity, view visibility and World Streaming residency;
- material classification, layer/hole/blend requirements, pass mapping and missing/
  incompatible required variant failures without silent fallback;
- Terrain-tier × render-profile × raster-recipe × cooked-variant × effective-capability
  matrices, including declared fallback reasons and forbidden implicit conversions;
- CPU 1.0 frustum/distance/LOD/seam planning with direct/instanced neutral batches, finite
  workspaces, deterministic merge/order and no indirect/compute dependency;
- optional post-1.0 GPU recipe admission, overflow, synchronization and declared CPU
  fallback behavior without same-frame readback or gameplay authority;
- adjacent-tile LOD compatibility, transition/hysteresis, unavailable representations and
  view/output/history changes without cracks or mixed generations;
- candidate failure/cancellation and replacement at validation, upload, prepare, aggregate
  commit, in-flight frame and deferred-retirement boundaries;
- World Streaming/ADR-034 shared charge accounting for staging, active, old/new and
  retiring resources without double counting or early release;
- device loss/reconstruction, backend replacement, project/world close, repeated shutdown
  and incomplete-retirement reporting; and
- Null/headless/dedicated-server plus every interactive backend composition, with native
  visual/performance qualification only on supported hardware lanes.

## Consequences

- TerrainRuntime has a stable, backend-neutral render-candidate boundary that remains
  valid for CPU, future GPU-driven, Null and no-renderer hosts.
- RenderFrontend becomes the sole owner of per-view visibility, LOD selection, draw/pass
  planning and GPU resource lifetime while respecting Terrain-authored legal choices.
- Terrain feature tiers and renderer product profiles can evolve independently without
  string coupling or silent capability claims.
- The 1.0 implementation has a concrete bounded CPU/direct-or-instanced path and does not
  inherit optional GPU Scene/compute/indirect requirements.
- Post-1.0 GPU-driven work can optimize the same semantic boundary but requires explicit
  recipe, budget, fallback and qualification contracts.
- Resource overlap during replacement is visible and budgeted; old Terrain and renderer
  generations may live longer until all frame/upload/native leases retire.
- TRF-003.2 and TRF-003.3 must specify exact material/permutation and GPU-resource/schema
  details without moving the authorities fixed here.

## Rejected Alternatives

### Let Terrain emit backend or render-graph draw commands

Rejected because Terrain would own pass ordering, native capability assumptions and GPU
lifetime. Terrain emits immutable semantic candidates; Renderer compiles execution.

### Push Terrain candidates directly into generic opaque/transparent arrays

Rejected because this loses Terrain revisions, seam/LOD constraints, material-layer
requirements and aggregate readiness. A typed bounded Terrain candidate snapshot is the
handoff.

### Let Terrain select LOD and visibility from camera distance

Rejected because selection is per view and depends on projection, output, raster recipe,
history and renderer budgets. Terrain owns legal representations and seam rules.

### Treat residency, visibility and gameplay relevance as the same boolean

Rejected because they have different owners and timelines. Renderer visibility cannot
evict World Streaming cells or authorize gameplay.

### Assume all foliage leaves are translucent

Rejected because Masked and true translucent materials have different depth, sorting and
raster-recipe behavior. Authored/cooked material classification is authoritative.

### Map Terrain Baseline/Standard/High/Ultra directly to renderer profiles

Rejected because the domains have independent capabilities, limits, required content and
fallback edges. Equal labels do not imply equal values or conversion.

### Choose Terrain features from `opengl`, `metal`, `vulkan` or `d3d12`

Rejected because API identity grants no effective feature, format, limit, cooked variant
or budget. The aggregate typed plan uses actual capabilities and content.

### Require GPU culling and indirect draws for core 1.0 Terrain/Foliage

Rejected because it would exclude the bounded baseline, Null and some qualified devices.
Core 1.0 uses CPU planning plus neutral direct/instanced batches.

### Read GPU visibility/LOD back to drive streaming or gameplay

Rejected because normal-frame synchronization would stall and presentation results are
not authoritative simulation facts. Only delayed bounded observations may inform future
owner decisions.

### Compile a missing Terrain shader variant during frame execution

Rejected because it creates unbounded frame-hot work and non-reproducible packaged
behavior. Required variants are cooked/prepared; missing required artifacts fail.

### Reuse a renderer resource because tile or material names match

Rejected because content, schema, material, permutation, device and capability generations
may differ. Reuse requires exact compatible signatures and leases.

### Free render resources immediately when Terrain evicts or replaces a tile

Rejected because uploads, frames and GPU submissions may still reference them. Renderer
owns deferred retirement and acknowledges release after completion.
