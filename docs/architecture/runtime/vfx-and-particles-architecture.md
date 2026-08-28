# VFX And Particles Architecture

## Purpose

This document defines Horo Engine's visual effects runtime: particle systems,
ribbons, mesh particles, decals, GPU compute particles, VFX graphs, and
volumetric effects. It specifies how effects are authored, simulated, rendered,
and integrated with scene lifecycles, gameplay, audio, and the render graph.

The goal is to provide a scalable, deterministic, and modular visual effects
architecture that supports everything from simple one-shot gameplay bursts to
large GPU-driven environmental volumes, without allowing immediate-mode rendering
leaks or coupling scene simulation to concrete graphics backends.

## Scope

Covered:

- runtime subsystem ownership, component hierarchy, and deterministic lifecycle
- simulation domain selection policy (CPU, GPU Compute, Headless/Null)
- platform and feature tier fallback matrix
- decoupled renderer boundary and typed render extraction contracts
- pass placement, standard material pipeline integration, and camera-relative sorting
- particle system data model and emitter stages
- VFX graph asset compilation and runtime evaluation
- decal projection, lifetime, and atlas management
- event-driven spawning, pooling, and audio routing
- performance budgets, diagnostics, and regression test requirements

Not covered:

- DCC tools and texture authoring workflows
- full fluid dynamics / Navier-Stokes solvers (future subsystem)
- atmospheric scattering and sky atmosphere simulation (see
  [Advanced Rendering Architecture](./advanced-rendering-architecture.md))
- screen-space post-processing effects (see
  [Advanced Rendering Architecture](./advanced-rendering-architecture.md))

## Core Decisions

- **Subsystem Ownership**: `VfxWorld` is owned strictly by the active `SceneRuntime`.
  It is initialized at scene startup and torn down/reset deterministically on scene
  replacement or unload. No effect instances or GPU resources outlive their scene.
- **Zero Immediate-Mode Rendering**: Effects are strictly prohibited from issuing
  immediate-mode draw calls or touching graphics contexts directly. `VfxWorld`
  extracts backend-neutral render primitives during the Render Extraction phase.
- **Policy-Driven Simulation Domain**: Particle simulation domain (CPU vs. GPU Compute)
  is determined by asset configuration, gameplay interaction requirements, and active
  hardware feature tiers. Call-site ad hoc branching is forbidden.
- **Deterministic Headless Execution**: Headless servers and CI test suites use
  `NullParticleSimulator` / deterministic CPU simulation without GPU allocations or
  graphics dependencies.
- **Standard Material & Pass Integration**: Particles and decals use the standard
  PBR and unlit material model compiled via the shared material pipeline, executing
  in standard Render Graph passes (G-Buffer, Decal, Translucent Forward, Additive).
- **Camera-Relative Sorting**: Translucent particles are sorted back-to-front
  (CPU radix sort for CPU particles, GPU bitonic/radix compute for GPU particles);
  additive particles skip sorting.
- **Zero Allocation Steady State**: All particle buffers, emitter states, and effect
  instances are preallocated in pools during scene initialization, guaranteeing zero
  heap allocations during active gameplay.

## Subsystem Ownership and Lifecycle

```text
SceneRuntime
  └── VfxWorld
        ├── EffectSystem
        ├── CpuParticleSimulator
        ├── GpuParticleSimulator
        ├── NullParticleSimulator
        ├── DecalManager
        ├── EffectInstancePool
        ├── VfxEventQueue
        └── VfxRenderExtractor
```

### Component Breakdown

Every component within the runtime effects ecosystem has a single, unambiguous owner:

| Component | Owner | Responsibility |
|---|---|---|
| `SceneRuntime` | Application / Host | Owns `VfxWorld` as a major scene subsystem. Controls creation, fixed/variable simulation ticking, and destruction. |
| `VfxWorld` | `SceneRuntime` | Subsystem composition root. Coordinates simulators, decal manager, instance pools, event queues, and render extraction. |
| `EffectSystem` | `VfxWorld` | Manages effect instance life cycles, template instantiation, parameter bindings, and spatial hierarchy tracking. |
| `CpuParticleSimulator` | `VfxWorld` | Flat Structure-of-Arrays (SoA) SIMD-accelerated particle updater for gameplay-interactive, collision-coupled, or CPU-bound effects. |
| `GpuParticleSimulator` | `VfxWorld` | Manages compute dispatch generation, indirect draw arguments, and compute buffer references for high-density particle volumes. |
| `NullParticleSimulator` | `VfxWorld` | Deterministic, allocation-free simulation path for headless testing, CI, and server execution where GPU rendering is disabled. |
| `DecalManager` | `VfxWorld` | Manages active decal projection volumes, fading envelopes, dynamic bounds, and decal atlas allocations. |
| `EffectInstancePool` | `VfxWorld` | Preallocated memory pools for particle buffers, emitter states, and effect instances to guarantee zero frame-time allocations. |
| `VfxEventQueue` | `VfxWorld` | Bounded FIFO queue capturing gameplay and animation spawn events (e.g., footsteps, impacts, explosions). |
| `VfxRenderExtractor` | `VfxWorld` | Extracts immutable, backend-neutral render primitives (`VfxRenderBatch`) and submits them to the Render Graph during extraction. |

### Lifecycle and Scene Transition Invariants

1. **Initialization**: `VfxWorld` is instantiated synchronously when `SceneRuntime`
   activates. Buffer pools, emitter slots, and decal capacities are allocated based on
   the active project and feature tier settings.
2. **Simulation Phase (`Update`)**: `VfxWorld::Update(DeltaTime)` executes within
   the scene simulation loop:
   - Drains and processes pending spawn requests from `VfxEventQueue`.
   - Advances active CPU particle systems and updates decal lifetimes.
   - Generates GPU compute simulation dispatches for compute-backed systems.
3. **Extraction Phase (`ExtractRenderData`)**: Called by the renderer frontend
   strictly during the Render Extraction phase. Generates immutable snapshots of
   visible particle batches, decal volumes, and instance transforms. Simulation
   state is never modified during this phase.
4. **Teardown & Scene Replacement**: When a scene is unloaded or replaced:
   - All active effect instances, emitters, and decals are stopped immediately.
   - All pooled CPU buffers and GPU compute buffers are deterministically reset or released.
   - No effect instance, GPU handle, or callback reference may leak across the scene boundary.

## Simulation Domain Selection Policy

Domain selection is **policy-driven, asset-declared, and feature-tier constrained**.
Individual gameplay call sites cannot arbitrarily override simulation rules via raw
backend branching.

```cpp
enum class SimulationDomain : uint8_t {
    Automatic,  ///< Resolved at cook/load time based on emitter parameters and tier
    CPU,        ///< Guaranteed CPU execution; supports gameplay queries and collisions
    GPU         ///< GPU compute execution; visual-only high-density particle simulation
};
```

### Domain Selection Criteria

1. **`SimulationDomain::CPU`** is mandatory when:
   - Particles require two-way gameplay interaction (e.g., triggering game logic,
     modifying gameplay physics bodies, collecting pickup items).
   - High-precision CPU raycast or complex geometry collision response is required.
   - System particle count is low-to-medium (≤ 2,048 particles).
   - Running on low-spec hardware tiers (e.g., `es3`) or in headless/null environments.
2. **`SimulationDomain::GPU`** is selected when:
   - System particle count is high (> 2,048 particles).
   - Complex vector fields, curl noise, or GPU depth-buffer collisions are utilized.
   - Effects are purely cosmetic (visual-only, no synchronous gameplay CPU readback).
3. **`SimulationDomain::Automatic`** (Asset Default):
   - Resolved at asset cook/load time. High-count visual-only systems select GPU compute
     when supported by the active feature tier; otherwise, they degrade to CPU simulation.

### Coexistence and Synchronization Rules

- CPU and GPU particle systems can coexist seamlessly within the same scene and within
  the same composite VFX graph asset.
- Synchronization is governed by shared scene simulation time and camera transforms.
- CPU particles write to CPU-mapped dynamic vertex/instance buffers. GPU particles simulate
  in GPU compute buffers and emit indirect draw calls.
- Readback from GPU particles to CPU is prohibited in standard frame loops; gameplay
  requiring particle positional data must use CPU simulation.

### Platform & Feature Tier Fallback Matrix

| Feature Tier / Platform | Primary Domain | GPU Compute Particles | GPU Radix / Bitonic Sort | Volumetrics & Curl Noise | Fallback Behavior |
|---|---|---|---|---|---|
| **Headless / Null** (`null`) | `CPU` (Null Simulator) | Disabled | Disabled | Disabled | Full deterministic CPU dummy simulation; no GPU allocations or draw dispatches. |
| **Mobile / Low-Spec** (`es3`) | `CPU` | Disabled | Disabled | Disabled | GPU particle assets automatically fall back to CPU simulation with clamped particle caps (e.g., max 512). Complex noise disabled. |
| **Desktop Baseline** (`dx11` / `opengl4`) | `CPU` & `GPU` | Supported (Compute) | Supported (Bitonic) | Basic 3D Textures | Over-budget GPU systems throttle spawn rates or drop lowest-priority sub-emitters. |
| **High-End Desktop / Console** (`dx12_vulkan` / `metal`) | `CPU` & `GPU` | Full Compute | Full Radix/Bitonic | Full Curl Noise & Vector Fields | Unrestricted simulation with GPU indirect draw and GPU culling. |

## Renderer Boundary & Render Extraction Contract

Visual effects adhere strictly to Horo Engine's decoupled rendering architecture.
**VFX code is strictly prohibited from invoking immediate-mode draw calls or holding
backend graphics contexts.**

### Extraction Pipeline

```text
  [ VfxWorld (Scene Simulation) ]
                 │
                 ▼
  [ VfxRenderExtractor ]  (ExtractRenderData phase)
                 │
                 ├─► VfxRenderBatch (Translucent Billboard / Ribbon / Mesh)
                 ├─► VfxRenderBatch (Opaque / Masked Mesh Particles)
                 ├─► DecalRenderBatch (Deferred / Clustered Decals)
                 └─► ParticleBufferView & DynamicInstanceTransforms
                 │
                 ▼
  [ Render Graph Execution ]
         ├── G-Buffer / Depth Pre-Pass (Opaque Mesh Particles)
         ├── Decal Pass (Projected Decals)
         ├── Forward / Translucent Pass (Sorted Particles & Ribbons)
         └── Volumetric Accumulation Pass
```

### Typed Backend-Neutral Render Primitives

```cpp
enum class VfxPassKind : uint8_t {
    OpaqueMesh,
    MaskedMesh,
    TranslucentForward,
    AdditiveForward,
    DeferredDecal,
    ForwardDecal,
    VolumetricAccumulation
};

enum class VfxPrimitiveTopology : uint8_t {
    CameraFacingBillboard,
    RibbonStrip,
    InstancedMesh,
    DecalBox
};

struct ParticleBufferView {
    BufferHandle bufferHandle;
    uint32_t byteOffset;
    uint32_t particleCount;
    uint32_t stride;
};

struct VfxRenderBatch {
    VfxPassKind passKind;
    VfxPrimitiveTopology topology;
    MaterialId material;
    MeshHandle mesh;                      // Valid for InstancedMesh topology
    ParticleBufferView instanceData;      // Position, size, rotation, color, UV
    BufferHandle indirectArgsBuffer;       // Optional, for GPU indirect draw dispatches
    uint32_t indirectArgsOffset;
    AABB worldBounds;
    float sortDistance;                   // Key for translucent back-to-front sorting
    bool isCastShadowEnabled;
};

struct DecalRenderBatch {
    MaterialId material;
    Transform worldTransform;
    Vec3 halfSize;
    float fadeFactor;
    AABB worldBounds;
};
```

## Pass Placement, Material Integration, and Sorting

### Material & Shader Pipeline Integration

Particles and decals use the standard material system (see
[Material And Shader Model](./material-and-shader-model.md)):

- **Shading Models**: `Unlit`, `DefaultPBR` (lit particles/mesh particles), and
  `DecalPBR` / `DecalEmissive`.
- **Blend Modes**: `Opaque`, `Masked`, `Translucent` (alpha blend), and `Additive`.
- **Soft Particles**: Translucent particle shaders consume the scene depth buffer
  texture to evaluate soft-intersection depth fading against background geometry.
- **Flipbook Animation**: Packed sub-UV grids animated by normalized particle age.

### Pass Placement

| Primitive Type | Target Pass | Blend Mode | Depth Write | Depth Test |
|---|---|---|---|---|
| **Opaque Mesh Particles** | G-Buffer / Opaque Forward | `Opaque` | Yes | Yes |
| **Masked Mesh Particles** | Depth Pre-Pass + G-Buffer | `Masked` | Yes | Yes |
| **Deferred Decals** | Decal Pass (G-Buffer) | `Translucent` / `Decal` | No | Yes (Read-Only) |
| **Forward Decals** | Forward Lighting Pass | `Translucent` | No | Yes (Read-Only) |
| **Translucent Particles** | Translucent Forward Pass | `Translucent` | No | Yes (Read-Only) |
| **Additive Particles** | Additive Forward Pass | `Additive` | No | Yes (Read-Only) |
| **Volumetric VFX** | Volumetric Accumulation Pass | Custom Blend | No | Yes (Read-Only) |

### Camera-Relative Sorting Policy

- **Translucent Systems**: Sorted strictly back-to-front relative to the active camera.
  - **CPU Simulated**: Sorted on the CPU using multi-threaded radix sort over camera
    distance keys before writing into the extraction buffer.
  - **GPU Simulated**: Sorted via GPU bitonic or radix sort compute passes prior to
    indirect draw submission.
- **Additive Systems**: Sorting is skipped entirely, eliminating sorting overhead
  for additive glows, sparks, and energy beams.
- **Opaque Systems**: Sorted front-to-back by the Render Frontend alongside standard
  static/dynamic scene meshes to maximize early-Z rejection.

## Particle System Data Model

A particle system is a template that describes how particles are spawned,
simulated, rendered, and destroyed.

```cpp
struct ParticleSystemDescriptor {
    ParticleSystemId id;
    SimulationDomain domain;      // CPU or GPU
    uint32_t maxParticles;
    EmitterShape shape;
    SpawnRate spawnRate;
    LifetimeRange lifetime;
    InitialVelocityRange velocity;
    InitialSizeRange size;
    InitialColorRange color;
    MaterialId material;
    VfxPrimitiveTopology renderMode;
    SortMode sortMode;            // None, ByDistance, OldestFirst
    CollisionMode collisionMode;  // None, Planes, SceneDepth, PhysicsWorld
};
```

### CPU Simulation Layout (Structure of Arrays)

To maximize SIMD vectorization and cache locality, CPU particle memory is stored
as flat arrays:

```cpp
struct CpuParticleBufferSoA {
    std::vector<float> posX, posY, posZ;
    std::vector<float> velX, velY, velZ;
    std::vector<float> scaleX, scaleY;
    std::vector<float> rotZ, rotVelZ;
    std::vector<uint32_t> packedColor;
    std::vector<float> age, maxAge;
    std::vector<uint32_t> customFlags;
    uint32_t activeCount{0};
    uint32_t capacity{0};
};
```

### GPU Compute Simulation Layout

GPU particles reside in device compute buffers and are updated via compute kernels:

```hlsl
struct GpuParticleData {
    float3 position;
    float  age;
    float3 velocity;
    float  maxAge;
    float2 size;
    float  rotation;
    float  rotationVelocity;
    uint   packedColor;
    uint   flags;
};

// Ping-pong buffers:
StructuredBuffer<GpuParticleData>   CurrentParticles : register(t0);
RWStructuredBuffer<GpuParticleData> NextParticles    : register(u0);
```

## VFX Graph Asset & Compilation

A VFX graph is an authored node graph that defines a complex effect composed of
emitters, forces, turbulence, events, and render outputs.

```text
VFXGraph
  +-- Spawn Context (Rates, Bursts, Triggers)
  +-- Update Context (Forces, Turbulence, Drag, Collision, Age)
  +-- Output Context (Billboard, Ribbon, Mesh, Decal, Light, Audio)
```

Graph nodes:

| Node | Purpose |
|---|---|
| `Emitter` | Spawns particles continuously or on event. |
| `Force` | Applies gravity, wind, vortex, attraction. |
| `Noise` | Adds turbulent displacement (simplex, Perlin, curl noise). |
| `Collision` | Bounce or die on collision with planes, depth, or physics geometry. |
| `ColorOverLife` | Animates color and opacity curves by normalized particle age. |
| `SizeOverLife` | Animates size curves by normalized particle age. |
| `SubEmitter` | Spawns child particles on birth, death, or collision. |
| `Decal` | Projects a decal at particle position or collision point. |
| `Light` | Spawns a temporary point light (bounded, tier-aware). |
| `Audio` | Triggers an audio event on spawn or collision. |

Graphs compile ahead-of-time (or at asset cook time) into runtime descriptors and
optimized compute/CPU kernels. The graph runtime never interprets arbitrary scripts
per particle.

## Decals

A decal is a projected texture applied to scene geometry within an oriented bounding box.

```cpp
struct DecalDescriptor {
    DecalId id;
    MaterialId material;
    ProjectionMode mode;      // BoxProjection, OrientedBox
    Vec3 halfSize;
    float fadeAngle;
    float fadeOutDistance;
    float lifetimeSeconds;
    bool isPermanent;
};
```

- **Projection Mode**: Default is deferred projection writing to the G-Buffer.
  Forward decals use clustered/tiled decal lists on forward feature tiers.
- **Lifetime**: Supports permanent environmental decals, timed fading decals
  (e.g., bullet holes, blood splatters), and event-driven removal.
- **Atlas Management**: `DecalManager` pools and batches decal textures to minimize
  descriptor and texture switches.

## Event-Driven Spawning & Audio Coupling

Gameplay spawns effects by submitting requests to `VfxEventQueue`:

```cpp
struct VfxSpawnRequest {
    AssetId effectAsset;
    Transform worldTransform;
    std::optional<Vec3> impactNormal;
    float scale{1.0f};
    VariantMap parameters;
};
```

### Audio Routing

VFX graphs trigger audio events on spawn, collision, or sub-emitter creation. Audio
events are dispatched to `AudioWorld` via typed IDs and world positions; the VFX
subsystem never plays sounds directly, preserving the mixing and spatialization
pipeline.

## Performance Budgets and Diagnostics

### Per-Effect Budgets

- Maximum particle count per emitter.
- Maximum concurrent active instances per effect asset.
- Maximum transient point lights spawned (e.g., max 4 lights per scene effect).
- Maximum active decals per volume.

### Global World Budgets

- Total active CPU particles (default limit: 16,384 across all CPU systems).
- Total active GPU particles (default limit: 1,000,000 across all GPU systems).
- Total active decals (default limit: 256).
- Maximum render extraction time budget per frame.

Over-budget conditions drop oldest/farthest cosmetic particles, clamp spawn counts,
and emit structured diagnostics according to the observability system.

## Testing and Verification Requirements

- **Ownership & Lifecycle Tests**: Verify `VfxWorld` construction, tick, and complete
  destruction on `SceneRuntime` unload without leaking memory or GPU handles.
- **Domain Selection Policy Tests**: Verify deterministic domain resolution across
  all feature tiers (`null`, `es3`, `dx11`, `dx12_vulkan`, `metal`).
- **Null / Headless Simulation Tests**: Verify `NullParticleSimulator` produces
  deterministic, zero-graphics simulation ticks in headless test environments.
- **Render Extraction Tests**: Verify `VfxRenderExtractor` emits valid, typed
  `VfxRenderBatch` primitives with correct sort distances and bounding boxes.
- **Sorting Correctness Tests**: Verify translucent particle back-to-front sorting
  and additive sort-skipping invariants.
- **Memory Pooling Invariants**: Assert zero heap allocations during steady-state
  particle updates and burst spawning.

## Related Documents

- [ADR-011: Effect Ownership, Simulation Domain Policy and Renderer Boundary](../../adr/011-vfx-effect-ownership-simulation-domain-and-renderer-boundary.md):
  Authoritative M0 architecture decision.
- [Particle Editor UI Reference](./particle-editor.html): Emitter stack, curve editing,
  and live preview panel.
- [Material And Shader Model](./material-and-shader-model.md): Particle and decal materials.
- [Rendering Architecture](./rendering-architecture.md): Render graph and pass extraction.
- [Scene Runtime Architecture](./scene-runtime.md): Scene runtime lifecycle and ownership.
- [Advanced Rendering Architecture](./advanced-rendering-architecture.md): Volumetrics and post-processing.
- [Audio Architecture](./audio-architecture.md): Audio event routing.
- [Asset Pipeline](./asset-pipeline.md): Effect compilation and cooking.
