# ADR-137: Terrain and Foliage Ownership, Data, Tier and Lifecycle

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: Terrain/foliage public and runtime module ownership, data strata, typed identity and revisions, capability tiers, scene/streaming/render/physics/navigation boundaries, mutable state, threading, replacement, cancellation and shutdown
- **Issue**: [TRF-001.1](https://github.com/abdullahbodur/horo-engine/issues/1935)
- **Jira**: [HORO-1891](https://horo-engine.atlassian.net/browse/HORO-1891)
- **Related**: [ADR-008](008-error-model-exception-boundary-and-registry.md), [ADR-010](010-job-waiting-and-operation-store-ownership.md), [ADR-012](012-world-streaming-partition-authority-and-subsystem-boundaries.md), [ADR-027](027-renderer-resource-identity-and-descriptors.md), [ADR-028](028-renderer-capability-limits-and-product-profiles.md), [ADR-034](034-gpu-memory-and-residency-ownership.md), [ADR-085](085-physics-shape-authoring-cook-and-runtime-boundary.md), [ADR-105](105-navigation-asset-and-scene-ownership-boundary.md), [ADR-108](108-dynamic-overlay-carving-and-tile-rebuild-policy.md)
- **Normative documents**: [System Design](../architecture/foundation/system-design.md), [Terrain and Foliage Architecture](../architecture/runtime/terrain-and-foliage-architecture.md), [World Streaming Architecture](../architecture/runtime/world-streaming-architecture.md), [Rendering Architecture](../architecture/runtime/rendering-architecture.md), [Physics Architecture](../architecture/runtime/physics-architecture.md), [Navigation and AI Architecture](../architecture/runtime/navigation-and-ai-architecture.md)

## Context

The current Terrain and Foliage architecture describes heightfield vectors, fixed
component arrays, backend-named tiers, streaming, GPU buffers, collision, navigation,
runtime spawning and editor tools in one document. It does not establish which target
owns the public contract or runtime state, which data is authored versus cooked versus
live, or how a dataset becomes active and retires across dependent subsystems.

That ambiguity invites several unsafe implementations: Scene or gameplay could mutate
cooked tile memory directly; Terrain could retain native GPU/physics/navigation
handles; the renderer could become terrain identity authority; workers could publish
partially prepared tiles; or an API-specific tier name could silently select a lower
content path. Automatic “furthest” or “oldest” instance deletion on a cap breach would
also convert capacity pressure into unreported gameplay/editor data loss.

Terrain and foliage span several existing authorities. World Streaming owns cell
admission and aggregate reservations. Assets/Pipeline own import and cook publication.
Scene owns scene binding and lifecycle. Render, Physics and Navigation own their
realized resources. Editor documents own authored mutations. A foundation decision is
needed so those integrations exchange typed immutable data without making the generic
Foundation target depend on any of them.

This ADR establishes the module/data/lifecycle baseline. TRF-001.2 will finalize stable
dataset/tile/type/instance identity derivation. TRF-001.3 will ratify exact v1 numeric
bounds, tier tables and shared descriptor validation. Later TRF decisions own cooked
tile/cache/streaming representation, render extraction, foliage state/eviction,
cross-system readiness and editor tooling without changing the authorities below.

## Decision

### 1. Terrain is a runtime vertical slice, not generic Foundation or a renderer feature

Horo introduces two primary targets:

```text
HoroEngine::TerrainApi
HoroEngine::TerrainRuntime
```

`TerrainApi` owns backend-neutral public values, typed IDs/revisions/handles, immutable
descriptors and narrow command/query/snapshot interfaces. It depends only on Foundation
and the narrow Assets identity/contracts its public declarations use. It contains no
editor, Scene implementation, renderer, physics, navigation, world-streaming, platform
or native provider type.

`TerrainRuntime` owns live terrain/foliage dataset generations, logical tile/cluster
state, runtime mutation admission, snapshot publication, internal reservation use and
coordinated preparation/retirement. It depends on `TerrainApi`, Assets and explicit
backend-neutral ports needed for host-composed integration. It never selects or creates
a concrete renderer, physics/navigation provider, filesystem adapter or editor service.

The application/host composition root constructs the runtime with narrow capabilities
and registers inert metadata before activation. Module descriptors do not scan assets,
register globals, create jobs/resources or inspect a service locator. Dedicated/headless
hosts may compose TerrainRuntime without a render integration.

Feature-specific cook, render, physics, navigation, world-streaming and editor adapters
depend toward the owning public contracts and are composed explicitly. Exact adapter
target splits may be introduced with the implementation tickets, but cannot reverse a
dependency into Foundation, Assets, Render, Physics, Navigation or Scene.

### 2. Each lifecycle/data responsibility has one authority

| Responsibility | Authority | Explicit non-authority |
|---|---|---|
| Authored terrain source, foliage definitions, paint/sculpt commands and undo | Editor document/application operation | TerrainRuntime and UI never edit document bytes directly |
| Import, deterministic cook, derived artifacts and atomic publication | Asset Pipeline plus Terrain cooker | Runtime, renderer and editor viewport cannot cook on demand |
| Stable asset identity and immutable byte leases | Assets | Terrain does not discover files or own package/cache paths |
| Scene binding, entity lifetime and aggregate scene activation | RuntimeScene | Terrain does not own entity hierarchy or scene transition authority |
| Live dataset/tile/cluster generations, logical runtime overlays and snapshots | TerrainRuntime | Gameplay, renderer and World Streaming cannot mutate them directly |
| Cell demand, priority, aggregate CPU/GPU/staging reservation and cell commit barrier | ADR-012 World Streaming authority | Terrain cannot run a second global streaming scheduler or oversubscribe its slice |
| Terrain/foliage GPU resources, GPU culling, draw execution and deferred GPU retirement | Render frontend/backend under ADR-027/034 | Terrain never exposes or retains native GPU handles/fences |
| Installed terrain/foliage collision shapes/bodies and physics-thread lifetime | Physics | Terrain supplies typed cooked descriptors and observes readiness only |
| Installed navigation topology/tiles and query consistency | Navigation | Terrain/hole tools cannot mutate live NavMesh memory directly |
| Gameplay meaning such as harvest/destruction/placement authorization | Product gameplay authority | Terrain owns spatial instance state but cannot invent gameplay facts |
| UI, overlays, diagnostics and tools | Presentation adapters over application/Terrain capabilities | Widgets and MCP/CLI handlers own no domain state |

Each cross-system operation has an owning result and generation. Notifications publish
only after the authority commits. A bus message, worker callback, render draw or
physics contact cannot become a terrain command or lifetime owner by itself.

### 3. Authored, cooked, scene, runtime and extraction data are distinct strata

The data flow is one-way unless an explicit authoring command starts a new cook:

```text
authoring source documents/assets
  -> validated deterministic Terrain cook
  -> immutable target/tier-keyed cooked dataset and feature payloads
  -> typed Scene binding
  -> TerrainRuntime generation and immutable snapshots
  -> render/physics/navigation/streaming integration packets
```

Authoring data may contain editable height samples, layer weights, masks, splines,
foliage rules and manually placed instances. It is not a runtime buffer and remains
owned by the document/asset operation transaction.

Cooked data is immutable, versioned, bounded and keyed by source revisions, target,
selected Terrain feature tier, cooker/toolchain identity and all semantic inputs. A
dataset manifest references independent terrain-tile, foliage-cluster, collision,
navigation-source and rendering payloads with exact sizes/hashes and compatibility.
World Streaming may package these as Terrain/Foliage feature blocks but does not
reinterpret their private schemas.

The Scene stores a typed binding to a cooked dataset plus authored transform/scope and
required feature policy. It does not embed decoded tile arrays, GPU buffers, physics
bodies or a mutable pointer to TerrainRuntime.

Runtime owns immutable published dataset/tile/cluster snapshots and separately
versioned overlays for admitted live changes. Cross-system integrations receive
bounded descriptors/views plus leases valid for one captured generation. No public
contract returns an owning `std::vector` of a whole heightfield or exposes mutable
storage shared across worker, renderer, physics, navigation and editor threads.

Render packets, physics shape-install descriptors, navigation build/source snapshots
and streaming cost/readiness reports are ephemeral projections. They cannot be written
back as authored/cooked truth or used as stable identity.

### 4. Identity, revision, handle and lease types are non-interchangeable

The contract reserves distinct strong types:

```cpp
struct TerrainDatasetId;
struct TerrainTileId;
struct FoliageTypeId;
struct FoliageClusterId;
struct FoliageInstanceId;

struct TerrainRuntimeHandle {
    TerrainRuntimeSlot slot;
    TerrainRuntimeGeneration generation;
};

struct TerrainSnapshotRevision {
    TerrainContentRevision content;
    TerrainResidencyRevision residency;
    TerrainMutationRevision mutation;
    TerrainCapabilityRevision capability;
};
```

Stable IDs identify durable authored/cooked meaning; runtime handles identify one live
incarnation; revisions identify immutable state snapshots; leases retain memory/native
dependencies. An `AssetId`, entity ID, streaming cell ID, native resource, array index,
world coordinate or display name cannot substitute for one of these types.

TRF-001.2 owns exact stable-ID namespaces, derivation, serialization and tombstone/
reuse policy. Until ratified, implementations may not publish an ad hoc hash/index or
persist runtime slot/generation values. Zero/unknown/stale/wrong-owner/wrong-generation
values fail with typed errors.

Content revision changes when immutable semantic dataset content is replaced. Residency
revision changes when published tile/cluster availability changes. Mutation revision
changes when an admitted runtime overlay commits. Capability revision changes when the
effective plan changes. Consumers compare the applicable fields rather than one vague
“terrain dirty” flag.

### 5. Feature tiers are provider-neutral product policy

Terrain uses four ordered preference profiles:

```cpp
enum class TerrainFeatureTier : std::uint8_t {
    Baseline,
    Standard,
    High,
    Ultra,
};
```

The names do not encode OpenGL, Metal, Vulkan, D3D12, console generations or shader
models. A renderer backend, device class or build configuration cannot select a Terrain
tier by string comparison. The product profile requests an allowed ordered tier set
and declares required versus optional terrain/foliage features.

`TerrainEffectiveCapabilities` intersects:

- cooked variants available for the exact dataset/target;
- TerrainRuntime implementation support;
- World Streaming CPU/GPU/staging/count reservations;
- effective Render operations/formats/limits for visual paths;
- Physics and Navigation provider capability for required grounded data; and
- host mode, product policy and runtime-mutation/editor permissions.

The resolved immutable plan records selected tier, exact finite numeric limits,
enabled algorithms, cooked variant identities, provider capability revisions, required
feature outcomes and every permitted fallback. Profiles express quality/scale policy;
they do not grant capability.

Baseline guarantees only the product-declared minimal cooked terrain/foliage behavior
that the current host requires. Standard, High and Ultra may request successively more
layers/LOD/instances, GPU culling, impostors, wind or mutation features, but no name
implies a concrete number or algorithm until TRF-001.3's versioned table is ratified.
Every resolved numeric field is explicit and finite; values such as “10M+”, zero-as-
unlimited and compile-time API labels are invalid.

A required feature with no compatible implementation/cooked variant fails plan or
scene activation. Optional fallback occurs only through the product's declared ordered
alternatives and records a typed reason. Horo never silently clamps layer/instance
count, drops authored instances, switches GPU work to CPU, removes collision or chooses
a different renderer/provider merely to make a tier appear successful.

### 6. Dataset activation is detached preparation plus atomic publication

Aggregate lifecycle is exhaustive:

```cpp
enum class TerrainRuntimeState : std::uint8_t {
    Absent,
    Preparing,
    Prepared,
    Active,
    Replacing,
    Suspended,
    Retiring,
    Failed,
};
```

The Terrain owner lane validates a Scene binding and effective plan, reserves bounded
work/resources and creates one operation-owned task group. Workers decode/validate
immutable candidates using captured asset/registry/capability revisions. They perform
no live-world mutation and call no renderer/physics/navigation object directly.

Prepared candidates contain owned/leased Horo data and integration descriptors. The
application/runtime coordinator arranges dependent prepare operations and publishes
the aggregate generation at the declared Scene safe point only after every required
participant is Prepared. Publication is a no-fail pointer/handle swap over already
validated state. Optional unavailable participants are explicit in readiness; they are
never missing by accident.

Failure or cancellation before publication retires the candidate and preserves the
old Active generation. Cancellation after publication is an explicit unload/replacement,
not rollback. A replacement keeps the old generation readable until all Scene,
streaming, render, physics, navigation, job and snapshot leases retire. Slot numbers,
cell coordinates or equal assets cannot make stale work valid for the new generation.

Suspension stops new mutation/streaming admission and captures policy-owned residency;
it does not destroy state or imply device resources survive. Resume revalidates
capabilities and may prepare a replacement plan/generation. Fatal owner failure closes
admission and remains observable; it cannot silently publish an empty flat terrain.

### 7. Readiness is multi-dimensional and generation checked

A single `loaded`/`ready` boolean cannot represent cross-system state:

```cpp
struct TerrainReadinessSnapshot {
    TerrainRuntimeHandle terrain;
    TerrainSnapshotRevision revision;
    TerrainReadinessState logical;
    TerrainReadinessState streaming;
    TerrainReadinessState visual;
    TerrainReadinessState collision;
    TerrainReadinessState navigation;
    TerrainReadinessState mutation;
};
```

Each dimension is `NotRequested`, `Preparing`, `Ready`, `Unavailable`, `Failed`,
`Suspended` or `Retiring` with a typed reason and captured provider revision. Required
dimensions gate aggregate publication or cell activation according to product/scene
policy. Optional visual absence is legal for headless hosts; required collision or
navigation cannot be treated as ready because terrain pixels exist.

World Streaming commits cell residency only after required Terrain/Foliage provider
payloads and their required integration participants are prepared under the same cell/
terrain generations. Renderer, Physics and Navigation publish their own readiness to
the coordinator; Terrain records the immutable projection but cannot forge it.

TRF-005 will refine cross-system barriers and partial-cell readiness. That follow-up
cannot replace typed dimensions with one boolean or let a dependent subsystem publish
against a stale terrain generation.

### 8. Mutable state uses commands and separate overlays

Authored sculpt/paint/hole/spline and baked foliage changes go through application-
owned document commands/transactions, update source revisions and trigger an explicit
recook/publication flow. Runtime does not write back to source files or mutate an
immutable cooked artifact.

Runtime deformation and dynamic foliage are independent optional capabilities. A
typed command captures terrain generation, expected mutation revision, bounded region/
instance count, gameplay authority, lifetime and overflow policy. The owner validates,
prepares a copy-on-write/overlay candidate and atomically advances mutation revision at
a safe point. Stale expected revision, unsupported mutation, over-budget work or lost
authority fails without partial change.

TerrainRuntime owns dynamic foliage spatial/instance state after command commit;
gameplay owns why a spawn/destruction is authorized and any persistent gameplay fact.
An instance lifetime uses a typed finite/explicit-persistent policy, never `float 0`
as a sentinel. Capacity exhaustion returns a typed result. Automatic oldest-first or
furthest-first deletion is forbidden unless TRF-004 defines and the product explicitly
selects a deterministic policy with outcome/replication/persistence semantics.

A terrain hole/deformation does not directly modify Physics or Navigation. It publishes
new typed source/overlay evidence; ADR-108 conservative exclusion and explicit rebuild/
recook capabilities govern grounded collision/navigation replacement. Visual success
alone cannot make gameplay ground authoritative.

### 9. Threading follows owner lanes and immutable handoff

TerrainRuntime has one declared mutable owner lane aligned with runtime/Scene safe
points. Command admission may enter through bounded MPSC ingress; only the owner changes
lifecycle, revisions, handle tables, resident logical state and published snapshots.

Workers perform bounded decode, validation, placement, CPU culling preparation and
candidate building inside owned task groups with cancellation. They capture values and
leases, never references to shorter-lived Scene/components/containers. Worker completion
enqueues generation-tagged evidence; callbacks never mutate live Terrain state.

Render extraction reads an immutable Terrain snapshot/lease and emits Horo RenderApi
data. Native GPU creation/draw/retirement stays on renderer-owned threads. Physics and
Navigation consume prepared descriptors through their own owner queues/safe points.
Editor UI, CLI and MCP call shared application capabilities and observe snapshots; no
transport or ImGui thread writes terrain arrays.

Frame-hot extraction/culling iteration uses preallocated/contiguous bounded data. It
performs no asset I/O, cook, arbitrary allocation, global service lookup, blocking wait
or cross-thread native call. Slow consumers observe revision notifications and query
the owning bounded store rather than receiving complete tile/instance payloads on a bus.

### 10. World Streaming and Terrain account every resource once

ADR-012 World Streaming owns aggregate cell admission, priority, linger, queue slots
and CPU/GPU/staging/retired-resource reservation. Terrain/Foliage budgets are slices,
not additional allowances. Terrain estimates each feature payload before load and must
obtain a reservation before allocation/decode/provider preparation.

TerrainRuntime owns allocation/eviction policy only inside its granted slice and for
disposable non-activation-critical local detail. It cannot run an independent camera
priority queue for cell residency, evict a pinned/required Active cell, borrow another
feature's reservation or allocate first and report growth later. It requests additional
reservation or a typed fallback/eviction decision from the authority.

Renderer GPU backing follows ADR-034: World Streaming/Terrain and Renderer project one
shared charge identity instead of counting the same physical allocation twice or not
at all. GPU/native retirement, upload/staging overlap, shared resources and frames in
flight remain charged until the owning renderer confirms release. Physics/navigation
resources follow equivalent lease/retirement accounting in their owners.

TRF-001.3 owns exact default/hard per-tier counts and bytes. Unknown estimates are never
zero/unlimited. Overflow-safe arithmetic, simultaneous old/new replacement generations,
staging copies and delayed retirement all participate in admission.

### 11. Failure, cancellation, replacement and shutdown preserve the last valid state

Stable error categories include:

```text
terrain.unavailable
terrain.capability.unsupported
terrain.tier.unsatisfied
terrain.descriptor.invalid
terrain.identity.invalid
terrain.generation.stale
terrain.revision.stale
terrain.capacity.exceeded
terrain.budget.denied
terrain.prepare.failed
terrain.integration.failed
terrain.cancelled
terrain.retirement.stalled
terrain.shutdown.incomplete
```

Errors preserve operation, terrain generation/revision, tile/cluster/instance identity
when safe, required capability/tier and nested provider cause. They do not expose native
handles, pointers, unbounded asset paths/data or provider-specific control codes.

Partial initialization rolls back only candidate-owned state in reverse dependency
order. The previous Active generation remains published. A failed optional participant
is `Unavailable`/`Failed` according to the resolved plan; it does not disappear from the
snapshot. Retry is a new bounded preparation against newly captured revisions and
never mutates the failed candidate in place.

Shutdown is idempotent and reverse ordered:

1. close commands, new loads and snapshot subscriptions;
2. cancel/yield owned task groups and invalidate candidate generations;
3. detach Scene/world-streaming admissions and request render/physics/navigation
   retirement for exact installed generations;
4. retain datasets, payloads, provider/module dependencies and snapshots until worker/
   native/consumer leases acknowledge release; and
5. release Assets/reservations and publish Absent only after all required retirement.

A deadline can report `terrain.shutdown.incomplete` but cannot force-free memory or
native dependencies still referenced by possible work. Renderer device loss, world
unload and process shutdown use the same generation/retirement rules; none may clear a
global terrain singleton because no such singleton exists.

### 12. Headless, Null and provider diversity do not change ownership

Terrain's logical/cooked/streaming state is renderer independent. A dedicated server
may require collision/navigation and no visual readiness. A cook/validation host may
load descriptors with no live Scene. An editor preview may require visual extraction
but use isolated preview/world generations.

Null Render means visual capability absence or a deterministic Horo-only extraction
fixture according to the host plan; it does not claim GPU culling/draw success. Missing
Physics/Navigation capability is explicit and fails when the scene marks it required.
No Terrain “Null” backend invents flat ground, empty foliage, collision or successful
streaming to satisfy callers.

OpenGL, Metal, Vulkan and D3D12 consume the same RenderApi terrain/foliage projection.
Native resource/command formats stay private, and different capabilities resolve via
the immutable product plan rather than backend-name branches in Terrain. CPU/GPU
algorithm alternatives must have equivalent declared semantic outcomes where required;
quality/performance differences are explicit.

### 13. Migration and qualification precede implementation claims

The current documentation-only structs and backend-named tier table are not a stable
runtime ABI or serialized format. They migrate to the typed source/cooked/scene/runtime
strata and provider-neutral plan. No compatibility shim preserves both a mutable whole-
heightfield public struct and the new leased tile model.

Before implementation/publication, TRF-001.2/001.3 must freeze ID encoding, descriptor
schemas, exact limits and validation. TRF-002 through TRF-006 must define artifacts,
render/foliage/cross-system/editor details consistently. Any later public header is
assigned to exactly one target and receives public-header consumer coverage.

Required evidence includes:

- target/dependency tests proving TerrainApi/Runtime and integration adapters expose no
  native backend, editor, ImGui, provider or global-service dependency;
- compile-time/type tests separating stable IDs, runtime handles, revisions and leases,
  with zero/stale/wrong-owner/wrong-generation failures;
- malformed/oversized/version-skewed authored/cooked/scene descriptors and no mutable
  whole-dataset buffer escaping the owning boundary;
- every requested tier against cooked/runtime/render/physics/navigation capability
  matrices, required failure and each explicitly permitted fallback reason;
- candidate failure/cancellation at every prepare participant and safe-point boundary,
  proving the old Active generation remains intact and no partial readiness publishes;
- replacement under worker, snapshot, cell, GPU, physics and navigation leases with no
  early reuse/free or stale publication;
- runtime deformation/dynamic foliage stale revision, capacity, authority and overflow
  cases with no implicit instance deletion or cooked/source mutation;
- world-streaming reserve/growth/pressure/eviction and simultaneous old/new/staging/
  retirement accounting with no double charge or oversubscription;
- headless, editor preview, dedicated server, Null Render and every interactive renderer
  consuming the same Horo contracts without silent capability substitution;
- owner-thread/worker/native-thread affinity, bounded ingress/backpressure, no frame-hot
  allocation/I/O/blocking and deterministic snapshot publication; and
- partial activation, suspension/resume, device loss, world unload, repeated shutdown
  and retirement timeout with all dependencies retained until safe release.

## Consequences

- Terrain/Foliage becomes an explicit backend-neutral runtime vertical slice with one
  public API owner and one live-state owner instead of leaking across Scene/Render.
- Authored, cooked, scene, runtime and extraction data cannot be mistaken for mutable
  aliases of the same heightfield/instance storage.
- Provider-neutral feature tiers resolve from real cooked/provider/budget capability;
  required content fails visibly and optional fallback is reviewable.
- Generation/revision/readiness snapshots prevent stale worker or dependent-system work
  from publishing into a replacement terrain.
- World Streaming, Render, Physics and Navigation keep their existing authority while
  Terrain coordinates through typed descriptors, leases and commit evidence.
- Runtime mutation, replacement and shutdown require more explicit state and tests, but
  preserve the last valid generation and prevent silent data loss/early native free.

## Rejected Alternatives

### Put Terrain/Foliage in `HoroEngine::Foundation`

Rejected because Foundation cannot depend on Assets, Scene, World Streaming, Render,
Physics or Navigation. Terrain is a vertical runtime feature built on foundational
types, not a universally lower-layer primitive.

### Implement Terrain inside each renderer backend

Rejected because authored/cooked identity, streaming, collision, navigation, gameplay
mutation and headless behavior are not GPU-backend concerns. Backends only realize
Horo RenderApi work.

### Expose a mutable `std::vector<float>` heightfield as the public runtime model

Rejected because ownership, bounds, revision, tiling and cross-thread lifetime are
undefined, and consumers could mutate data behind active GPU/physics/navigation state.

### Use `es3`, `dx11`, `dx12_vulkan` and `high_end` as Terrain tiers

Rejected because API names mix providers with product quality, group unequal backends,
do not describe actual device/implementation/cooked capability and encourage branching
or silent fallback by string.

### Clamp layer/instance/LOD counts silently to the selected tier

Rejected because authored meaning/data can disappear without a result. Required limits
fail; optional reduction needs an explicit cooked/product fallback with diagnostics.

### Let Terrain own native GPU, physics and navigation resources

Rejected because those systems own native thread affinity, device/provider generation,
deferred retirement and recovery. Terrain holds Horo leases/readiness projections only.

### Let World Streaming own Terrain tile semantics and internal cache policy

Rejected because it owns cell admission/global budget, not provider payload schema,
terrain LOD meaning or mutations. Terrain operates within its granted slice.

### Publish each dependent participant as soon as it prepares

Rejected because observers could see visual terrain without required collision or a
new tile with old navigation. Aggregate required readiness publishes atomically.

### Mutate cooked terrain in place for sculpting or runtime deformation

Rejected because it breaks deterministic assets, cache identity and active-reader
lifetime. Authoring recooks; runtime changes use separate revisioned overlays.

### Evict oldest or furthest foliage automatically on capacity overflow

Rejected because presentation order is not necessarily gameplay/persistence priority.
Admission fails unless an explicit deterministic TRF-004 policy owns the disposition.

### Use one global current Terrain singleton

Rejected because editor preview, PIE, scene replacement, streaming generations and
multi-world/server hosts need scoped owners and stale-handle rejection.

### Force-free terrain dependencies at a shutdown deadline

Rejected because worker/native callbacks and frames in flight may still reference them.
A typed incomplete result preserves lifetime truth instead of causing use-after-free.
