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

Pre-fractured meshes are authored offline:

- A source mesh is fractured using Voronoi cell decomposition
- Interior faces are generated with a configurable material
- Fracture chunks are stored as separate mesh pieces within the asset
- Chunks support physics collision (convex decomposition per chunk)
- Chunk connectivity graph defines how chunks relate

```cpp
struct FractureAsset {
    AssetId                     sourceMeshId;
    std::vector<FractureChunk>  chunks;
    FractureChunkGraph          connectivity;
    MaterialId                  interiorMaterial;
};

struct FractureChunk {
    AssetId      chunkMeshId;
    float        relativeMass;
    Vector3      centerOfMass;
    BoundingBox  localBounds;
    uint32_t     parentChunkIndex;    // for hierarchical fracture
    bool         isStructural;        // structural chunks affect stability
};
```

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

1. Destruction validates the exact cooked chunk/support closure and complete peak cost.
2. Physics and Render prepare required chunk representations privately.
3. RuntimeScene commits semantic state, intact/chunk visibility, entities and required
   bodies through one activation-ticket-scoped aggregate root.
4. Canonical gameplay/network/save events publish only after commit.
5. Optional VFX, Audio and Decal presentation requests consume the committed event.

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

## Physics Integration

Fracture chunks use the physics system:

- Each activated chunk becomes a dynamic rigid body
- Chunk collision shapes use convex decomposition (pre-computed in the
  fracture asset)
- Initial velocities are derived from the fracture event
- Physics sleep remains solver state and does not grant cleanup authority
- Gameplay-authoritative and durable chunks retire only through explicit policy and,
  where required, a successful Runtime Save/Persistent World handoff
- Cosmetic debris is VFX-owned, finite-lived and excluded from canonical chunk state

## Network Replication

Destruction state is replicated through the normal authority and replication boundary:

- The authority server commits canonical destruction commands/state
- NetworkRuntime captures versioned bounded snapshots/deltas and delivers typed apply
  commands; I/O threads do not mutate Destruction
- Late join receives health, phase, broken/support/dormant sets and ordered later deltas
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
the owner lane commits state. Stale revision/generation, authority loss, budget denial,
consumer failure or cancellation changes nothing before aggregate publication.

Runtime Save captures stable destructible/chunk identities, content/state revisions,
health, broken/support/dormant sets and required progress. Native bodies, shapes, GPU
resources, particles, voices, contacts and caches are derived/excluded. World Streaming
cannot evict the last durable state before the persistence owner accepts its handoff.

Shutdown closes admission, cancels task groups, invalidates candidates, requests exact-
generation consumer retirement and retains artifacts/reservations/modules until every
reader, job and native owner acknowledges release. A deadline reports incomplete
shutdown and never force-frees possibly referenced state.

## Verification

Required coverage includes authority/revision checks, every semantic/lifecycle
transition, deterministic contact ordering, cooked chunk closure, proof that core paths
perform no runtime geometry/collision cook, aggregate consumer rollback/replacement,
capacity and durable cleanup, save/restore, late join, authoritative/cosmetic motion,
headless/Null/all interactive backends, cancellation and repeated shutdown.

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
