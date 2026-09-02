# Destruction And Fracture Architecture

## Purpose

This document defines the destruction and fracture subsystem for Horo Engine.
It covers pre-fractured geometry, runtime fracture, debris generation,
destruction events, physics integration, audio and VFX coupling, and network
replication of destruction state.

[ADR-144](../../adr/144-destruction-ownership-authority-state-and-runtime-geometry-boundary.md)
is the foundation contract. Destruction is a scene-scoped runtime domain with one
semantic state authority, pre-cooked core-1.0 geometry, provider-neutral tiers and
aggregate Scene/Physics/Render publication. Runtime mesh cutting is post-1.0 only.
[ADR-145](../../adr/145-destruction-source-chunk-geometry-collision-and-cook-ownership.md)
defines source normalization, canonical DFR chunk/interior/connectivity artifacts,
solver-neutral collision inputs, Assets cache/publication and separate Physics/Render
derived products.
[ADR-146](../../adr/146-destruction-runtime-activation-physics-cleanup-and-rollback.md)
defines the runtime transaction over those products. Commands and post-step contact
evidence enter at bounded Destruction safe points; Physics prepares exact pre-cooked
chunk bodies privately; RuntimeScene alone exposes semantic, entity, Physics and Render
changes through one aggregate commit. Sleep or visibility never grants cleanup
authority, and rollback preserves the old root only before that commit.
[ADR-147](../../adr/147-destruction-event-and-cosmetic-consumer-ownership.md)
defines what happens afterward. Destruction appends bounded typed facts to its committed
journal; an application-owned dispatcher maps them to gameplay, VFX, Decal, Audio and
accessibility requests at destination safe points. Consumers own mapping realization,
admission, playback/simulation and native lifetime and cannot change DFR success.
[ADR-148](../../adr/148-fracture-document-generator-undo-and-preview-ownership.md)
specializes editor ownership. A persistent fracture asset document owns working source,
typed operations, bounded history and dirty state; generator workers produce detached
candidates; Assets owns durable source/cooked publication; high-fidelity preview uses an
isolated runtime/Physics world and never production state.
[ADR-149](../../adr/149-destruction-persistence-replication-streaming-and-authority.md)
defines canonical save/network reconstruction, explicit server authority, snapshot-first
late join and no-loss streaming handoff. DFR retains semantic sets/revision/seed;
Physics owns paired active-chunk motion; Persistent World owns dormant storage; World
Streaming alone decides residency.

## Ownership

`DestructionApi` owns backend-neutral identities, revisions, descriptors, commands,
results and immutable snapshots. `DestructionRuntime` owns live scene-scoped health,
semantic phase, broken/support membership, command admission, transition planning and
canonical events. RuntimeScene owns entity/binding lifetime and aggregate activation;
Physics and Render own bodies/shapes and render/GPU state; Assets/Destruction Cook own
source, deterministic artifacts and publication; gameplay owns damage permission.

Physics contacts, replicated records, scripts, UI, Audio, VFX and Decals never mutate
destruction state directly. They provide typed evidence/commands or consume post-commit
snapshots/events. Native handles and mutable buffers do not cross the public boundary.

## Destruction Model

Scene components carry stable binding and authored policy, not live mutable state:

```cpp
struct DestructibleSceneBinding {
    DestructibleId destructible;
    AssetId fractureAsset;
    DestructionPolicyId policy;
    DestructionFeatureRequirements requiredFeatures;
};
```

`DestructionWorld` owns `Intact`, `Damaged` or `Fractured` semantic phase plus exact
health, broken chunk set and support state under one monotonic state revision.
`Fractured` means at least one cooked transition committed; exact membership cannot be
inferred from the phase. The independent runtime lifecycle is `Absent`, `Preparing`,
`Prepared`, `Active`, `Replacing`, `Suspended`, `Retiring` or `Failed`.

## Pre-Fractured Geometry

Pre-fractured meshes are authored and cooked offline through ADR-145:

- Assets owns immutable normalized source bytes, dependency scheduling, physical cache,
  package storage and atomic publication
- Destruction Cook owns fracture recipes, deterministic generation/import validation,
  canonical chunk/exterior/interior geometry and connectivity
- The canonical DFR artifact owns solver-neutral convex input with stable chunk/subshape
  identities, not native Physics shapes
- Physics separately cooks solver/profile/platform-specific immutable shape artifacts
  and owns runtime shape storage/retirement
- Mesh/Render separately lower canonical chunk geometry and own GPU resources

```cpp
struct CanonicalFractureArtifact {
    FractureArtifactId artifact;
    FractureArtifactRevision revision;
    AssetId sourceMesh;
    FractureRecipeId recipe;
    FractureChunkTable chunks;
    FractureConnectivityGraph connectivity;
    FractureGeometryTables geometry;
    FractureCollisionInputTable collisionInputs;
};
```

Stable chunk identity is not an array index, source/display name, cache path, render
submesh or native shape ID. DFR owns canonical interior classification, winding,
material slots and UV policy. Render cannot reclassify faces, and Physics cannot change
chunk membership while realizing collision.

The DFR cook fingerprint includes normalized source/recipe/dependency digests,
algorithm/version/seed, coordinate/tolerance/repair policy, interior/material/UV and
connectivity rules, selected tier/limits and artifact/toolchain schemas. Physics and
Render add their own native target fingerprints to the accepted DFR artifact identity;
a solver/backend upgrade invalidates its derived product without changing DFR topology.

## Runtime Pre-Cooked Fracture

Runtime fracture can be triggered by:

- **Impact**: Collision with sufficient force/momentum
- **Damage**: Accumulated damage reaches threshold
- **Explosion**: Radial damage with falloff
- **Script**: Explicit fracture command from gameplay

```cpp
struct FractureEvent {
    FractureEventType    type;
    WorldCoordinate      impactPoint;
    Vector3              impactDirection;
    float                damage;
    float                radius;
};
```

Every trigger becomes an authority-, generation- and revision-checked typed command.
Physics contacts are immutable evidence consumed after the Physics step; callbacks do
not fracture objects directly. On a successful pre-cooked transition:

1. Destruction validates command/evidence generation and computes the exact cooked
   direct-detach plus unsupported-chunk closure and complete peak cost.
2. Physics and Render prepare required chunk representations privately under one
   transition ticket; ordinary queries still resolve the previous root.
3. Physics publishes prepared bodies into ticket-scoped routing only at its pre-step
   safe point, without making them public.
4. RuntimeScene commits semantic state, intact/chunk visibility, entities and required
   bodies through one activation-ticket-scoped aggregate root.
5. Canonical destruction facts append to the bounded queryable journal only after
   commit.
6. The application dispatcher maps committed facts to gameplay/network/save adapters
   and optional VFX, Audio, Decal and accessibility requests at owner safe points.

Failure or cancellation before commit preserves the intact/previous generation. Old
representations remain leased and charged until every consumer acknowledges retirement.

Core 1.0 never cuts, booleans, voxelizes, remeshes or triangulates source geometry; it
never creates interior surfaces, convex decomposition or mass properties at runtime.
Those products must exist in the validated fracture artifact. Missing/incompatible
content returns a typed failure instead of runtime generation or visual-only fallback.

## Debris System

Small debris particles are handled by the VFX system:

```cpp
struct DebrisSettings {
    AssetId     debrisMeshId;        // small generic debris mesh
    uint32_t    minParticleCount;
    uint32_t    maxParticleCount;
    float       initialVelocity;
    float       lifetime;
    float       fadeOutDuration;
    AssetId     impactDecalMaterial; // scorch/damage decal at impact point
};
```

Debris particles use a lightweight physics simulation (no collision between
debris particles, only against world geometry). They fade out after their
lifetime and are recycled from a debris pool.

## Hierarchical Fracture

Large structures fracture in stages:

1. Initial fracture: surface chunks break off
2. Structural collapse: when enough structural chunks are removed, the
   remaining chunks lose support
3. Secondary fracture: falling chunks fracture on impact with the ground

The chunk connectivity graph determines structural dependency:

```cpp
struct FractureChunkGraph {
    std::vector<std::vector<uint32_t>> adjacency;   // chunk index → neighbor indices
    std::vector<float>                 supportWeight; // how much this chunk supports others
};
```

Core 1.0 support loss uses deterministic graph reachability, not a stress solver.
After directly selected chunks are removed, the planner traverses the remaining cooked
graph from immutable anchors in stable chunk/neighbor order. Every remaining chunk not
reachable from an anchor joins the detach set. The complete result must fit active
limits or the transition fails atomically; it is never truncated. Damage accumulation
and threshold rules are separate from this closure step.

## Physics Integration

Fracture chunks use the physics system:

- Each activated chunk becomes a dynamic rigid body
- Chunk collision shapes use convex decomposition (pre-computed in the
  fracture asset)
- Initial velocities are derived from the fracture event
- Bodies prepare privately from the exact DFR/Physics shape artifact and become query-
  visible only with the aggregate RuntimeScene commit
- Contact callbacks collect bounded immutable evidence; they do not mutate Destruction,
  Scene structure or body topology
- Physics sleep remains solver state and does not grant cleanup authority
- Gameplay-authoritative and durable chunks retire only through explicit policy and,
  where required, a successful Runtime Save/Persistent World handoff
- Cosmetic debris is VFX-owned, finite-lived and excluded from canonical chunk state

## Event And Consumer Integration

`DestructionWorld` publishes immutable facts with stable world/destructible generation,
state revision, transition ticket, tick, kind and occurrence identity. Facts describe
committed damage, chunk activation, support loss, dormancy, reactivation or availability;
they do not name effect graphs, decal materials, audio media/voices or callbacks.

The application composition root owns the cooked semantic binding table and one
session-scoped `DestructionEventDispatcher`. It reads the bounded Destruction journal by
cursor after aggregate commit, fans out in cooked order and copies fixed-capacity values
into destination-owned queues. Required gameplay/accessibility delivery capacity is
reserved before the source transition commits. Optional cosmetic capacity failure is a
typed destination outcome and never rolls back canonical state.

VFX owns effect mapping realization, queue/pool admission, simulation and Render
extraction. The Decal presentation owner owns projection, attachment, lifetime and
retirement. Audio owns cue/media readiness, sample scheduling, voices, mixing and device
callbacks. Gameplay adapters may submit later domain commands but cannot reenter the
source commit. Every layer derives its request identity from the DFR occurrence, binding
generation, destination and layer ordinal so retries/reload cannot duplicate effects.

`EngineDataBus` may publish a coalesced journal-revision notification, not the required
fact payload. Save restore and late join reconstruct canonical state and do not replay
historical effects by default; prediction uses a separate cosmetic occurrence namespace.

## Network Replication

Destruction state is replicated through the normal authority and replication boundary:

- The authority server alone commits canonical destruction commands/state; autonomous
  and simulated clients hold replicas and cannot gain authority from contact/visibility
- NetworkRuntime captures versioned bounded snapshots/deltas and delivers typed apply
  commands; I/O threads do not mutate Destruction
- Save/network snapshots bind exact artifact content/chunk-table identity, semantic
  revision, deterministic seed, health/phase and broken/active/supported/dormant sets
- Active authoritative chunk motion is paired Physics-owned canonical state keyed by
  stable DFR chunk identity; native solver state is never serialized
- Late join receives a full semantic/Physics snapshot at one revision/tick, then applies
  a bounded contiguous delta stream through aggregate activation
- Gameplay-authoritative chunk motion uses existing Physics/network state replication;
  Destruction does not create a second transform stream
- Cosmetic chunks/debris may simulate client-side but cannot affect gameplay, saves or
  canonical hashes

## Editor Authoring

Fracture authoring tools:

- Fracture mesh import (import pre-fractured FBX with naming convention)
- Voronoi fracture generator (generate chunks from intact mesh in-editor)
- Fracture preview (play fracture animation in editor viewport)
- Chunk connectivity visualization
- Damage threshold and behavior configuration

Each asset opens as one persistent `FractureAssetDocument` rooted at stable asset and
accepted source revision. The document owns working recipe/source/graph intent, typed
operation execution, history, dirty/saved state and derived candidate/preview status.
Panels, inspectors, tree/graph views and viewport gizmos own presentation/input only and
cannot mutate source, history or artifact publication directly.

Import and procedural generation capture an immutable exact-revision input snapshot and
write a bounded detached candidate in a document-owned cancellable operation. Completion
does not dirty or publish. Only an explicit accept operation may atomically apply the
candidate's authored semantic patch and record exact before/after history. Undo/redo
replays those patches or immutable source-section checkpoints; it never reruns the
generator or stores production cooked/native products.

Source save publishes through Assets and alone advances saved state. Production cook is
a separate Assets-owned ADR-145 generation. Preview cook is transient and activates the
ordinary DFR/RuntimeScene/Physics/Render contracts inside a generation-fenced
`FracturePreviewSession`; its Physics world, events, persistence-disabled state and
resources are distinct from production. Preview observations write back only through a
new explicit document operation.

## Feature Tiers

Tiers are provider-neutral product preference profiles. They do not name a graphics API,
platform, solver or device and do not grant capability:

| Feature family | `Baseline` | `Standard` | `High` |
|---|---|---|---|
| Pre-cooked fracture | Bounded one-level | Larger staged graph | Larger qualified hierarchy |
| Support/collapse | Minimal cooked policy | Richer cooked policy | Hierarchical cooked policy |
| Physics | Pre-cooked dynamic convex chunks | Same ownership, larger limits | Same ownership, qualified larger limits |
| Presentation effects | Optional bounded cosmetic requests | Increased declared limits | Highest qualified declared limits |
| Runtime geometry generation | Unavailable in core 1.0 | Unavailable in core 1.0 | Unavailable in core 1.0 |

DFR-001.3 freezes exact chunk/depth/body/event/byte/work limits. Resolution intersects
product request, cooked variants, runtime, World Streaming, Physics, Render and host
capabilities. Required unsupported content fails; fallback is only an explicitly ordered
product choice with a typed reason. Runtime geometry is a separate post-1.0 capability,
not an automatic `High` feature.

## Lifecycle, Persistence And Shutdown

Destruction builds transitions as detached candidates with exact affected chunks,
consumer requirements and peak reservations. Workers validate immutable artifacts only;
the owner lane commits state. Physics and Render may publish prepared resources only to
ticket-scoped private routing. Stale revision/generation, authority loss, budget denial,
consumer failure or cancellation changes nothing before aggregate publication; private
resources retire after owner-safe readers drain.

Aggregate commit is the rollback boundary. Before it, failure preserves the complete old
root. After it, restore, dormancy or recovery is a new authorized command/revision rather
than an in-place rewind. A required consumer loss exposes typed suspended/failed
availability and never fabricates an intact state.

Runtime Save captures stable destructible/chunk identities, content/state revisions,
health, broken/support/dormant sets and required progress. Native bodies, shapes, GPU
resources, particles, voices, contacts and caches are derived/excluded. World Streaming
cannot evict the last durable state before the persistence owner accepts its handoff.
Sleeping, old or invisible chunks are not cleanup-eligible by implication. An explicit
bounded policy chooses canonical dormancy, and required durable state must be accepted
before live Scene/Physics/Render representations retire through another aggregate commit.
Persistent World stores dormant DFR semantic snapshots by stable world/cell/destructible
and exact content/revision identity, but does not interpret them. World Streaming owns
residency and may retire the last live copy only after exact-revision handoff succeeds.
Restore and later cell activation apply saved/dormant state before the aggregate root;
content mismatch requires a registered stable-chunk migration or typed rejection.

Shutdown closes admission, cancels task groups, invalidates candidates, requests exact-
generation consumer retirement and retains artifacts/reservations/modules until every
reader, job and native owner acknowledges release. A deadline reports incomplete
shutdown and never force-frees possibly referenced state.

## Verification

Required coverage includes authority/revision checks, every semantic/lifecycle
transition, deterministic contact ordering, cooked chunk closure, proof that core paths
perform no runtime geometry/collision cook, aggregate consumer rollback/replacement,
capacity and durable cleanup, save/restore, late join, authoritative/cosmetic motion,
headless/Null/all interactive backends, committed fact ordering, journal gaps, consumer
dedup/overload independence, typed editor operations, exact history, stale generator
work, preview isolation, cancellation and repeated shutdown.

## Related Documents

- [Destruction Setup UI Reference](./destruction-setup.html)

- [Physics Architecture](./physics-architecture.md): fracture chunk physics
- [VFX And Particles Architecture](./vfx-and-particles-architecture.md): debris particle spawning
- [Audio Architecture](./audio-architecture.md): destruction sound events
- [Decal System Architecture](./decal-system-architecture.md): impact decals
- [Networking Architecture](./networking-architecture.md): destruction state replication
- [Scene Runtime](./scene-runtime.md): destructible component model
- [Asset Pipeline](./asset-pipeline.md): source, cook, cache and artifact publication
- [Save Game And Persistence](./save-game-and-persistence.md): canonical destruction
  state capture and durable dormancy
- [ADR-144](../../adr/144-destruction-ownership-authority-state-and-runtime-geometry-boundary.md):
  module/state authorities, command ordering, cooked activation, tiers, persistence,
  replication and post-1.0 runtime geometry boundary
- [ADR-145](../../adr/145-destruction-source-chunk-geometry-collision-and-cook-ownership.md):
  normalized source/recipe inputs, canonical DFR chunk/interior/connectivity artifact,
  solver-neutral collision inputs and separate Physics/Render derived products
- [ADR-146](../../adr/146-destruction-runtime-activation-physics-cleanup-and-rollback.md):
  command and contact safe points, deterministic support loss, private Physics body
  preparation, aggregate publication, cleanup, rollback, replacement and shutdown
- [ADR-147](../../adr/147-destruction-event-and-cosmetic-consumer-ownership.md):
  committed fact identity/journal, application dispatch, destination safe points,
  deduplication and gameplay/VFX/Decal/Audio ownership
- [ADR-148](../../adr/148-fracture-document-generator-undo-and-preview-ownership.md):
  persistent document, typed operations, detached generation, exact bounded history,
  Assets publication and isolated preview ownership
- [ADR-149](../../adr/149-destruction-persistence-replication-streaming-and-authority.md):
  canonical save/network state, server authority, paired Physics motion, late join,
  durable streaming handoff and compatibility
