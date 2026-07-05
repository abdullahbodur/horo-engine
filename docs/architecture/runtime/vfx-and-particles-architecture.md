# VFX And Particles Architecture

## Purpose

This document defines Horo Engine's visual effects runtime: particle systems,
decals, GPU particles, VFX graphs, and volumetric effects. It specifies how
effects are authored, simulated, rendered, and integrated with gameplay, audio,
and the render graph.

The goal is to provide a scalable effects architecture that supports everything
from simple one-shot bursts to large GPU-driven environmental effects, without
forcing the renderer to treat every effect as a special case.

## Scope

Covered:

- particle system data model and components
- CPU and GPU simulation paths
- VFX graph authoring and runtime evaluation
- decal projection and lifetime
- effect instancing, pooling, and culling
- sorting and transparency
- event-driven spawning
- integration with scene runtime, render graph, audio, and gameplay

Not covered:

- specific DCC tools or texture authoring
- fluid simulation (future document; not currently in scope)
- full atmospheric scattering model (only the VFX boundary is covered here; see
  [Advanced Rendering Architecture](./advanced-rendering-architecture.md))
- post-processing effects (see [Advanced Rendering Architecture](./advanced-rendering-architecture.md))

## Core Decisions

- Effects are runtime objects owned by the scene runtime, not by the renderer.
- Particle simulation may run on CPU or GPU depending on complexity, platform,
  and feature tier.
- VFX graphs are authored assets that compile into a runtime effect descriptor.
  They are not interpreted per particle.
- Decals are first-class scene objects with a defined projection volume and
  lifetime.
- Effects use the same material and render pass system as meshes; no hidden
  immediate-mode rendering from gameplay code.
- Particle spawn is event-driven or continuous. Gameplay fires events; the VFX
  system decides how to translate them into spawned instances.
- Effects are pooled. Allocation during gameplay should be avoided except during
  initial load.

## Effect System Ownership

```text
Scene Runtime
  +-- VfxWorld
      +-- EffectSystem
      +-- ParticleSimulators (CPU and GPU)
      +-- DecalManager
      +-- EffectInstancePool
      +-- VfxEventQueue
      +-- VfxRenderExtractor
```

`VfxWorld` is owned by the active scene runtime. It lives across gameplay frames
and communicates with the renderer through extracted render instances.

## Particle System

A particle system is a template that describes how particles are spawned,
simulated, rendered, and destroyed.

```cpp
struct ParticleSystemDescriptor {
    ParticleSystemId id;
    SimulationDomain domain;      // CPU or GPU
    MaxParticleCount maxParticles;
    EmitterShape shape;
    SpawnRate spawnRate;
    LifetimeRange lifetime;
    InitialVelocityRange velocity;
    InitialSizeRange size;
    InitialColorRange color;
    MaterialId material;
    RenderMode renderMode;        // Billboard, Mesh, Ribbon, Trail
    SortMode sortMode;            // None, ByDistance, OldestFirst
    CollisionMode collisionMode;  // None, Planes, SceneDepth, PhysicsWorld
};
```

### Simulation Stages

Each particle has:

- position, velocity, acceleration
- size, rotation, angular velocity
- color and opacity
- lifetime and age
- custom payload (for gameplay-defined modules)

Per-frame stages:

1. Spawn
2. Initialize new particles
3. Apply forces (gravity, wind, attraction, noise)
4. Update position/rotation/size/color
5. Collision response
6. Kill expired particles
7. Extract render instances

### CPU Simulation

CPU simulation uses a flat SoA buffer. It is the default for:

- low particle counts
- complex gameplay interactions
- platforms without compute support
- effects that need tight gameplay coupling

### GPU Simulation

GPU simulation uses compute shaders. It is the default for:

- high particle counts
- effects with simple local forces
- platforms with compute support

Rules:

- GPU particles may fall back to CPU if the feature tier lacks compute.
- CPU and GPU particle systems may coexist in the same scene.
- Gameplay reads from CPU particles; GPU particles are visual-only unless a
  read-back path is explicitly declared.

## VFX Graph

A VFX graph is an authored node graph that defines a complex effect made of
multiple emitters, forces, events, and render outputs.

```text
VFXGraph
  +-- Update Context
  |     +-- Spawn Event Handler
  |     +-- Force Field
  |     +-- Kill Condition
  +-- Output Context
        +-- Particle Renderer
        +-- Ribbon Renderer
        +-- Mesh Renderer
        +-- Decal Spawner
```

Graph nodes:

| Node | Purpose |
|---|---|
| `Emitter` | Spawns particles continuously or on event. |
| `Force` | Applies gravity, wind, vortex, attraction. |
| `Noise` | Adds turbulent displacement. |
| `Collision` | Bounce or die on collision. |
| `ColorOverLife` | Animates color/opacity by normalized age. |
| `SizeOverLife` | Animates size by normalized age. |
| `SubEmitter` | Spawns child particles on birth/death/collision. |
| `Decal` | Projects a decal at particle position or collision point. |
| `Light` | Spawns a temporary point light (bounded, tier-aware). |
| `Audio` | Triggers an audio event on spawn/collision. |

Graphs compile to a runtime descriptor. The runtime evaluates update contexts
and extracts outputs. The graph does not interpret arbitrary scripts at runtime.

## Decals

A decal is a projected texture applied to scene geometry within a bounded
volume.

```cpp
struct DecalDescriptor {
    DecalId id;
    MaterialId material;
    ProjectionMode mode;      // BoxProjection, OrientedBox
    Vec3 halfSize;
    float fadeAngle;
    float fadeOutDistance;
    float lifetimeSeconds;
    bool affectedByTimeOfDay;
};
```

Decals are placed as scene objects. They are rendered as part of the deferred or
forward decal pass using the standard material model.

Lifetime options:

- permanent until explicitly removed
- timed with optional fade
- event-driven removal

Decal atlas and deferred projection are the default. Forward decals are a
fallback for forward-only feature tiers.

## Render Modes

| Mode | Description |
|---|---|
| `Billboard` | Camera-facing quad. |
| `Mesh` | Instanced mesh particles. |
| `Ribbon` | Connected segments forming a ribbon. |
| `Trail` | Trail following a moving emitter. |
| `Decal` | Projected decal. |
| `Light` | Temporary light spawned by effect (limited, tier-aware). |

## Sorting And Transparency

Translucent particles are sorted back-to-front per camera.

- CPU particles may be sorted on CPU.
- GPU particles use GPU bitonic or radix sort.
- Additive particles do not require strict sorting.
- Opaque/masked particles participate in the opaque pass and depth testing.

Sorting must not dominate frame time. Budgets are enforced and reported.

## Culling

Effects are culled using:

- view frustum
- distance-based LOD
- screen-size thresholds
- occlusion (when available)

GPU particle systems are still budgeted even when culled; inactive systems may
sleep and skip simulation.

## Event-Driven Spawning

Gameplay spawns effects by firing events to the VFX world.

```cpp
struct VfxSpawnRequest {
    AssetId effectAsset;
    Transform worldTransform;
    std::optional<Vec3> impactNormal;
    float scale;
    VariantMap parameters;
};
```

Common events:

- `Footstep` — dust or splash at foot position
- `BulletImpact` — spark, debris, decal
- `Explosion` — fireball, smoke, shockwave
- `MuzzleFlash` — flash and smoke
- `AbilityCast` — gameplay-specific visual

The VFX system maps events to effect assets through a gameplay-defined table or
effect tags. It does not hardcode gameplay semantics.

## Audio Coupling

VFX graphs may trigger audio events:

- on spawn
- on collision
- on death
- on sub-emitter spawn

Audio events are sent to the audio world, not played directly. This preserves
the audio mixing and spatialization pipeline.

## Material Integration

Particles and decals use the same material system as meshes (see
[Material And Shader Model](./material-and-shader-model.md)).

Particle materials typically use:

- `Unlit` or `Lit` shading model
- `Translucent` or `Additive` blend mode
- flipbook/texture atlas animation
- soft particle depth fading
- vertex color modulation

Decal materials use:

- `Lit` or `Unlit`
- `Translucent` or `Masked`
- deferred projection shader

## Performance Budgets

Per effect:

- max particle count
- max active instances
- max lights spawned
- max decals spawned
- max simulation time per frame

Global budgets:

- total live particles across all CPU systems
- total live particles across all GPU systems
- total decals
- total effect draw calls
- effect GPU time

Exceeding a budget drops oldest/farthest particles or delays spawn. Diagnostics
report over-budget events.

## Feature Tier Behavior

| Tier | Behavior |
|---|---|
| `es3` | CPU particles only, no GPU simulation, limited lights, no volumetrics. |
| `dx11` | GPU particles supported, limited decals, simple volumetrics. |
| `dx12_vulkan` | Full GPU particles, many decals, compute-driven effects. |
| `high_end` | GPU events, large-scale simulations, advanced volumetrics. |

Effects must degrade gracefully: fewer particles, simpler shaders, CPU fallback.

## Asset Formats

### Particle System Asset

```json
{
  "schemaVersion": 1,
  "assetType": "particle_system",
  "simulationDomain": "CPU",
  "maxParticles": 256,
  "emitterShape": { "type": "Cone", "angle": 30, "radius": 0.1 },
  "spawnRate": { "type": "Burst", "count": 32 },
  "lifetime": { "min": 0.5, "max": 1.2 },
  "velocity": { "min": [0, 1, 0], "max": [0, 3, 0] },
  "material": "material_spark_001",
  "renderMode": "Billboard"
}
```

### VFX Graph Asset

```json
{
  "schemaVersion": 1,
  "assetType": "vfx_graph",
  "contexts": [
    {
      "name": "Update",
      "nodes": [...]
    },
    {
      "name": "Output",
      "nodes": [...]
    }
  ]
}
```

## Diagnostics And Validation

- Missing materials or textures are reported at import time.
- GPU particle systems on compute-less tiers produce a warning and fallback.
- Over-budget effects are logged with the offending asset ID.
- Infinite-lifetime particles without kill conditions are rejected.
- Decal count and light-spawn limits are enforced at runtime.

## Testing Requirements

- Unit tests for particle spawn/kill bookkeeping.
- CPU vs GPU simulation equivalence tests for simple forces.
- Sort correctness tests.
- Culling tests for off-screen and far-away effects.
- Event-to-spawn mapping tests.
- Visual regression tests for representative effects.
- Performance tests measuring particle count vs frame time.

## Related Documents

- [Particle Editor UI Reference](./particle-editor.html): emitter stack, module parameters, curve editing, and live preview panel.

- [Material And Shader Model](./material-and-shader-model.md): particle and
  decal materials.
- [Rendering Architecture](./rendering-architecture.md): render graph and pass
  extraction.
- [Advanced Rendering Architecture](./advanced-rendering-architecture.md):
  post-processing, volumetrics, and high-end feature tiers.
- [Audio Architecture](./audio-architecture.md): audio event routing.
- [Asset Pipeline](./asset-pipeline.md): effect import and cook.
