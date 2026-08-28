# ADR-011: Effect Ownership, Simulation Domain Policy and Renderer Boundary

- **Status**: Proposed
- **Date**: 2026-08-28
- **Supersedes**: None
- **Scope**: Runtime VFX subsystem ownership, simulation domain selection policy, platform fallback matrix, render extraction boundary, pass placement and sorting
- **Issue**: [#1749](https://github.com/abdullahbodur/horo-engine/issues/1749) ([VFX-001.1])
- **JIRA**: HORO-1706
- **Normative document**: [VFX And Particles Architecture](../architecture/runtime/vfx-and-particles-architecture.md)

## Context

Visual effects (particle systems, ribbon emitters, mesh particles, decals, and volumetric effects) span gameplay simulation, physics interaction, compute dispatching, and graphics presentation. Without explicit architectural boundaries, effects tend to introduce severe architectural debt:

1. **Immediate-mode rendering anti-patterns**: Gameplay or effect scripts issuing immediate-mode draw calls directly into graphics contexts, breaking render graph scheduling, pass reordering, and multi-threaded rendering.
2. **Ambiguous subsystem ownership**: Effects persisting across scene transitions or living in global singletons, leading to memory leaks, dangling scene references, and non-deterministic state.
3. **Ad hoc simulation domains**: Simulation code branching dynamically on loose flags at call sites, making CPU versus GPU particle behavior non-reproducible and error-prone across hardware tiers.
4. **Backend-leaking abstractions**: Exposing native GPU buffer handles or API-specific compute shaders directly to gameplay and scene systems.

Ticket [#1749](https://github.com/abdullahbodur/horo-engine/issues/1749) ([VFX-001.1]) requires a definitive architecture decision that establishes:

- The authoritative owner and deterministic lifecycle of `VfxWorld` and its subsystem components.
- A policy-driven, reproducible simulation domain selection model with explicit hardware/platform fallback matrices.
- A backend-neutral render-extraction contract that integrates with the standard material and Render Graph pass system.

## Decision

**`VfxWorld` is owned exclusively by the active `SceneRuntime`, initialized at scene startup, ticked during the scene simulation phase, and deterministically reset/destroyed on scene unload or replacement. Simulation domain selection (CPU vs. GPU compute) is policy-driven, asset-defined, and feature-tier constrained. Visual effects never use immediate-mode rendering; `VfxWorld` extracts typed, backend-neutral render primitives (`VfxRenderBatch`, `ParticleBufferView`, `DynamicInstanceTransform`) during the Render Extraction phase for submission to standard Render Graph passes.**

### Ratify-or-revise outcomes

| Area | Prior / Baseline State | Outcome |
|---|---|---|
| **VFX World Ownership** | Loosely placed under Scene Runtime | **Ratified and Formalized.** `VfxWorld` is strictly owned by `SceneRuntime`. Its lifetime is bounded exactly by the scene lifecycle. |
| **Lifecycle & Teardown** | Implicit teardown on scene change | **Ratified.** Reset and teardown are deterministic; pools, queues, and compute allocations are reclaimed synchronously on scene replacement. |
| **Simulation Domain** | Ad hoc CPU/GPU distinction | **Revised to Policy-Driven.** Domain is determined by asset descriptor rules, gameplay coupling requirements, and platform feature tiers with deterministic fallback. |
| **Headless / Null Simulation** | Undefined headless behavior | **Formalized.** Headless and test environments use deterministic, allocation-free `NullParticleSimulator` / CPU simulation without GPU dependencies. |
| **Renderer Boundary** | Conceptually separated | **Ratified and Strictly Enforced.** Zero immediate-mode rendering. `VfxRenderExtractor` emits immutable backend-neutral render batches during the Render Extraction phase. |
| **Material & Shader Pipeline** | Mixed custom/standard shader models | **Ratified.** Particles and decals use the standard PBR and unlit material model compiled via the shared shader pipeline. |
| **Sorting & Transparency** | Described in high-level terms | **Formalized.** Camera-relative distance sorting is CPU-radix for CPU systems and GPU bitonic/radix compute for GPU systems; additive particles skip sorting. |

---

### Authoritative Ownership & Component Breakdown

Every component within the runtime effects ecosystem has a single, unambiguous owner:

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

#### Lifecycle and Scene Transition Rules

1. **Initialization**: `VfxWorld` is instantiated when `SceneRuntime` activates. Preallocated instance and particle buffer pools are established according to scene/project quality configurations.
2. **Simulation Phase**: `VfxWorld::Update(DeltaTime)` executes within the scene simulation loop:
   - Processes all queued `VfxEventQueue` events.
   - Updates CPU particle systems and decal timers.
   - Encodes GPU compute simulation dispatches for compute-backed systems.
3. **Extraction Phase**: `VfxWorld::ExtractRenderData(RenderWorldSnapshot&, const SceneView&)` is called strictly during the Render Extraction phase. No simulation state is mutated during extraction.
4. **Teardown & Scene Replacement**: When a scene is unloaded or replaced:
   - All active effect instances, emitters, and decals are stopped immediately.
   - All pooled buffers, GPU compute resources, and event queues are deterministically reset/released.
   - No effect instance, GPU handle, or callback reference may leak into the replacement scene or host runtime.

---

### Simulation Domain Selection Policy and Fallback Matrix

Domain selection is **policy-driven, asset-declared, and feature-tier constrained**. Individual gameplay call sites cannot arbitrarily override simulation rules via raw backend branching.

#### Domain Selection Criteria

1. **`SimulationDomain::CPU`** is mandatory when:
   - Particles require two-way gameplay interaction (e.g., triggering game logic, modifying gameplay physics bodies, collecting pickup items).
   - High-precision CPU raycast/geometry collision response is required.
   - System particle count is low-to-medium (≤ 2,048 particles).
   - Running on low-spec hardware tiers (e.g., `es3`) or in headless/null environments.
2. **`SimulationDomain::GPU`** is selected when:
   - System particle count is high (> 2,048 particles).
   - Complex vector fields, curl noise, or GPU depth-buffer collisions are utilized.
   - Effects are purely cosmetic (visual-only, no synchronous gameplay CPU readback).
3. **`SimulationDomain::Automatic`** (Asset Default):
   - Resolved at asset cook/load time. High-count visual-only systems select GPU compute when supported by the active feature tier; otherwise, they degrade to CPU simulation.

#### Coexistence Rules

- CPU and GPU particle systems can coexist seamlessly within the same scene and within the same composite VFX graph asset.
- Synchronization is governed by shared scene simulation time and camera transforms.
- CPU particles write to CPU-mapped dynamic vertex/instance buffers. GPU particles simulate in GPU compute buffers and emit indirect draw calls.

#### Platform & Feature Tier Fallback Matrix

| Feature Tier / Platform | Primary Domain | GPU Compute Particles | GPU Radix / Bitonic Sort | Volumetrics & Curl Noise | Fallback Behavior |
|---|---|---|---|---|---|
| **Headless / Null** (`null`) | `CPU` (Null Simulator) | Disabled | Disabled | Disabled | Full deterministic CPU dummy simulation; no GPU allocations or draw dispatches. |
| **Mobile / Low-Spec** (`es3`) | `CPU` | Disabled | Disabled | Disabled | GPU particle assets automatically fall back to CPU simulation with clamped particle caps (e.g., max 512). Complex noise disabled. |
| **Desktop Baseline** (`dx11` / `opengl4`) | `CPU` & `GPU` | Supported (Compute) | Supported (Bitonic) | Basic 3D Textures | Over-budget GPU systems throttle spawn rates or drop lowest-priority sub-emitters. |
| **High-End Desktop / Console** (`dx12_vulkan` / `metal`) | `CPU` & `GPU` | Full Compute | Full Radix/Bitonic | Full Curl Noise & Vector Fields | Unrestricted simulation with GPU indirect draw and GPU culling. |

---

### Renderer Boundary and Render Extraction Contract

Visual effects adhere strictly to Horo Engine's decoupled rendering architecture. **VFX code is strictly prohibited from invoking immediate-mode draw calls or holding backend graphics contexts.**

#### Render Extraction Pipeline

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

#### Typed Render Primitives

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
    MeshHandle mesh;                      // Valid for InstancedMesh
    ParticleBufferView instanceData;      // Position, size, rotation, color, UV
    BufferHandle indirectArgsBuffer;       // Optional, for GPU indirect draw
    uint32_t indirectArgsOffset;
    AABB worldBounds;
    float sortDistance;                   // Key for translucent back-to-front sorting
    bool isCastShadowEnabled;
};
```

---

### Pass Placement, Material Pipeline, and Sorting

1. **Standard Material Pipeline Integration**:
   - Particle and decal shaders are authored and compiled via the engine's standard material system (refer to [Material And Shader Model](./material-and-shader-model.md)).
   - Shading models supported: `Unlit`, `DefaultPBR` (lit particles/mesh particles), and `DecalEmissive`/`DecalPBR`.
   - Blend modes: `Opaque`, `Masked`, `Translucent` (alpha blend), and `Additive`.
   - Soft particles: Translucent particle shaders consume the scene depth buffer texture to evaluate soft-intersection depth fading.

2. **Pass Placement**:
   - **Opaque / Masked Mesh Particles**: Inserted into the standard G-Buffer (deferred) or Depth Pre-Pass / Opaque Forward pass. Writes depth.
   - **Deferred Decals**: Rendered into the G-Buffer during the decal pass (modifying Albedo, Roughness/Metallic, and Normal attachments).
   - **Forward Decals**: Rendered in the forward lighting pass for forward-only tiers.
   - **Translucent Particles & Ribbons**: Inserted into the Translucent Forward Pass. Depth-tested against scene depth, no depth write.
   - **Additive Particles**: Inserted into the Additive Forward Pass. Depth-tested, no depth write, order-independent.

3. **Sorting Policy**:
   - **Translucent Systems**: Sorted back-to-front relative to the active camera.
     - **CPU Simulated**: Sorted on the CPU using multi-threaded radix sort over camera distance keys before writing into the extraction buffer.
     - **GPU Simulated**: Sorted via GPU bitonic or radix sort compute kernels prior to indirect dispatch.
   - **Additive Systems**: Sorting is skipped entirely, eliminating sorting overhead for additive glows and sparks.
   - **Opaque Systems**: Sorted front-to-back by the Render Frontend alongside standard static/dynamic scene meshes to maximize early-Z rejection.

---

## Consequences

### Positive

- **Architectural Purity**: Elimination of immediate-mode draw calls guarantees compatibility with multi-threaded rendering and future render graphs.
- **Backend Neutrality**: `VfxWorld` operates with zero knowledge of concrete graphics APIs (Vulkan, Metal, OpenGL, D3D12, Null).
- **Deterministic Lifecycles**: Subsystem ownership by `SceneRuntime` guarantees clean teardown, eliminating cross-scene leaks.
- **Predictable Performance**: Memory pooling guarantees zero heap allocations during steady-state gameplay updates and particle spawning.
- **Cross-Platform Scalability**: Explicit feature tier matrices allow high-end platforms to utilize massive compute particle simulations while ensuring graceful degradation to CPU simulation on lower tiers and headless servers.

### Negative / Tradeoffs

- Render extraction introduces a lightweight data-packing step to format instance buffers into backend-neutral views.
- GPU particle simulation requires compute shader support; platforms lacking compute capability must adhere to CPU particle budget limits.

---

## Rejected Alternatives

- **Immediate-Mode Rendering from Gameplay / Effect Components**: Rejected because it violates pass ordering, prevents multi-threaded extraction, breaks render graph compilation, and couples gameplay code to the active graphics backend.
- **Renderer-Owned VFX Systems**: Rejected because particle simulation requires access to scene transforms, gameplay physics, and spatial queries. The renderer must remain a consumer of immutable snapshots.
- **Ad Hoc Per-Emitter Backend / Domain Switching**: Rejected because call-site branching produces non-reproducible visual artifacts, race conditions, and unmaintainable platform code.
- **Dedicated Proprietary Particle Shading System**: Rejected in favor of the standard PBR / unlit material pipeline to ensure consistent lighting, shadows, and asset pipeline integration across the engine.
