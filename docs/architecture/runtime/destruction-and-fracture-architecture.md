# Destruction And Fracture Architecture

## Purpose

This document defines the destruction and fracture subsystem for Horo Engine.
It covers pre-fractured geometry, runtime fracture, debris generation,
destruction events, physics integration, audio and VFX coupling, and network
replication of destruction state.

## Destruction Model

Destruction is modeled through destructible components:

```cpp
struct DestructibleComponent {
    AssetId                fractureAsset;
    DestructibleState      state;            // Intact, Damaged, Destroyed
    float                  health;
    float                  damageThreshold;  // damage before state transition
    DestructibleBehavior   behavior;
    bool                   networkReplicated;
};
```

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

## Runtime Fracture

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

On fracture:

1. The intact mesh is hidden
2. Chunks within the damage radius are activated
3. Physics forces are applied to chunks (explosion, impact direction)
4. Debris particles are spawned
5. Audio impact/destruction sound is triggered
6. Destruction event is broadcast on the data bus

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
- Chunks that come to rest (sleep) are frozen to reduce simulation cost
- After a configurable duration, sleeping chunks may be removed (debris
  cleanup)

## Network Replication

Destruction state is replicated in multiplayer:

- Server authoritatively determines fracture events
- Destruction state changes are replicated as reliable RPCs
- Only the final state (Intact/Damaged/Destroyed) is replicated, not
  individual chunk transforms
- Debris is client-side cosmetic only (not replicated)
- Late-joining clients receive the current destruction state

## Editor Authoring

Fracture authoring tools:

- Fracture mesh import (import pre-fractured FBX with naming convention)
- Voronoi fracture generator (generate chunks from intact mesh in-editor)
- Fracture preview (play fracture animation in editor viewport)
- Chunk connectivity visualization
- Damage threshold and behavior configuration

## Feature Tiers

| Feature              | `es3`      | `dx11` / `dx12_vulkan` | `high_end` |
| -------------------- | ----------- | ------------- | ------------ |
| Pre-fractured meshes | No          | Yes           | Yes          |
| Runtime fracture     | No          | Simple        | Full         |
| Hierarchical fracture| No          | No            | Yes          |
| Debris particles     | No          | 256           | 2K           |
| Chunk count per asset| —           | 64            | 512          |
| Chunk physics        | —           | Convex        | Convex + mesh|

## Related Documents

- [Destruction Setup UI Reference](./destruction-setup.html)

- [Physics Architecture](./physics-architecture.md): fracture chunk physics
- [VFX And Particles Architecture](./vfx-and-particles-architecture.md): debris particle spawning
- [Audio Architecture](./audio-architecture.md): destruction sound events
- [Decal System Architecture](./decal-system-architecture.md): impact decals
- [Networking Architecture](./networking-architecture.md): destruction state replication
- [Scene Runtime](./scene-runtime.md): destructible component model
