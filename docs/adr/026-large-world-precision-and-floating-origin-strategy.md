# ADR-026: Large-World Precision and Floating Origin Strategy

- **Status**: Proposed
- **Date**: 2026-08-28
- **Supersedes**: None
- **Scope**: Coordinate systems, precision representations (64-bit integer, double-precision floats, camera-relative floats), floating origin rebasing transactions, platform/GPU shader performance boundaries, physics/subsystem authority, and error domains.
- **Issue**: [#1604](https://github.com/abdullahbodur/horo-engine/issues/1604) ([WST-007.1])
- **JIRA**: HORO-1604
- **Parent**: [#1524](https://github.com/abdullahbodur/horo-engine/issues/1524) ([WST-007])
- **Normative documents**:
  - [Coordinate Precision And Origin Rebasing](../architecture/runtime/coordinate-precision-and-origin-rebasing.md)
  - [World Streaming Architecture](../architecture/runtime/world-streaming-architecture.md)
  - [Rendering Architecture](../architecture/runtime/rendering-architecture.md)
  - [Physics Architecture](../architecture/runtime/physics-architecture.md)
  - [Scene Math](../architecture/foundation/scene-math.md)

---

## Context

Standard 32-bit single-precision IEEE 754 floating-point coordinates (`fp32`) provide 24 bits of significand (approximately 7.2 decimal digits of precision). In large virtual worlds, this introduces severe numerical degradation as distance from the world origin $(0, 0, 0)$ increases:

| Distance from Origin | Spatial Resolution ($\text{ulp}$) | Visual & Physical Artifacts |
|---|---|---|
| $1\,\text{km}$ ($10^3\,\text{m}$) | $\approx 0.00012\,\text{m}$ ($0.12\,\text{mm}$) | Imperceptible |
| $10\,\text{km}$ ($10^4\,\text{m}$) | $\approx 0.00098\,\text{m}$ ($0.98\,\text{mm}$) | Sub-millimeter jitter in close-up geometry |
| $100\,\text{km}$ ($10^5\,\text{m}$) | $\approx 0.0078\,\text{m}$ ($7.8\,\text{mm}$) | Noticeable vertex shaking, Z-fighting, physics contact instability |
| $1,000\,\text{km}$ ($10^6\,\text{m}$) | $\approx 0.0625\,\text{m}$ ($6.25\,\text{cm}$) | Unusable physics simulation, massive mesh jitter, broken camera controls |
| $10,000\,\text{km}$ ($10^7\,\text{m}$) | $\approx 0.5\,\text{m}$ ($50\,\text{cm}$) | Total collapse of spatial coherence |

To support large-scale open worlds, flight/space simulations, and seamless multi-region environments without compromising cross-platform compatibility (e.g., Mobile OpenGL ES / Metal / Vulkan and WebGL), Horo Engine requires an explicit, normative coordinate precision and origin rebasing strategy.

Key design constraints:

1. **Universal GPU Compatibility**: Mobile and entry-level GPUs may lack shader FP64 support or provide materially lower FP64 throughput. GPU shaders therefore remain on the supported `fp32` / `fp16` path.
2. **Physics Engine Stability**: Commercial and open-source physics engines (e.g., Jolt, PhysX, Box2D) rely on 32-bit floating point calculations for collision detection, EPA/GJK, island solving, and sleeping. Physics clusters must remain localized within low-magnitude coordinate frames.
3. **Deterministic Persistence & Replication**: Authoring tools, spatial partitioning grids, save games, and multiplayer state replication require a stable, absolute global frame of reference that never drifts when the local rendering origin shifts.
4. **Zero Velocity Discontinuities**: Origin rebasing is a coordinate frame re-indexing, not a physical displacement over time; it must not induce artificial velocity spikes, Doppler glitches, or particle recreation.

---

## Decision

**Horo Engine adopts a Hybrid Hierarchical Global Coordinate representation (`WorldCoordinate64`) paired with Floating Origin Rebasing (`CameraRelativeFloat3`) for runtime rendering and local simulation clusters:**

1. **Global World Coordinates (`WorldCoordinate64`)**:
   - The authoritative representation for global world space, spatial partitioning, persistent save state, and multiplayer network replication is a 64-bit composite coordinate: an `IntVector3` grid cell index plus an `IntVector3 cellOffsetMm` stored in integer millimeters in the half-open range `[0, cellSizeMm)` (default cell $1024\,\text{m} = 1\,024\,000\,\text{mm}$).
   - Round-trip conversion to/from world-space fixed-point 64-bit integer millimeters (`int64_t[3]`, $\pm 9.22 \times 10^{12}\,\text{m}$ range with $1\,\text{mm}$ resolution) is exact. `dvec3` / `DVec3` is a derived tooling view (53-bit significand), not canonical storage. A 32-bit `Vec3` cell offset is rejected: near a 1024 m cell edge fp32 ULP is ≈ 0.12 mm, so it cannot satisfy an exact millimeter round-trip.
2. **Floating Origin Rebasing (`CameraRelativeFloat3`)**:
   - The active runtime view and local simulation operate in a localized single-precision floating-point frame relative to a dynamic floating origin $C_{\text{origin}}$.
   - When the active camera or focal entity traverses beyond a configured rebasing threshold distance ($R_{\text{threshold}}$, default $1000\,\text{m}$) from $C_{\text{origin}}$, the engine executes a coordinated, atomic Origin Rebase Transaction.
   - The origin shift offset $\Delta_{\text{origin}} = C_{\text{new}} - C_{\text{old}}$ is calculated in 64-bit precision and broadcast as a typed `OriginRebaseEvent` at a declared frame synchronization safe point.
3. **Camera-Relative GPU Rendering**:
   - GPU shaders remain strictly 32-bit single precision (`fp32`).
   - High-precision world-to-view subtractions $(P_{\text{world}} - C_{\text{camera}})$ are evaluated on the CPU or during constant-buffer generation.
   - Vertex transformations in vertex shaders consume pre-translated camera-relative model matrices $M_{\text{view\_rel}} = V_{\text{rel}} \cdot M_{\text{rel}}$, eliminating vertex jitter and z-fighting without requiring 64-bit GPU vertex attributes or shader emulation.
4. **Local Physics and Subsystem Clusters**:
   - The active `PhysicsWorld` resides entirely in local rebased cluster coordinates.
   - On `OriginRebaseEvent`, the physics adapter shifts body spatial structures by $-\Delta_{\text{origin}}$ without modifying linear or angular velocities, contact manifold caches, or sleeping states.
   - Audio listeners/emitters, VFX particle buffers, navigation agent waypoints, and camera rigs apply $-\Delta_{\text{origin}}$ cleanly without timing or derivative artifacts.
5. **Fallible Boundaries and Typed Errors**:
   - All spatial transformations, conversions, and rebase transactions return `Horo::Result<T, Error>` under the `horo.runtime.precision` and `horo.runtime.world_streaming` error domains per [ADR-008](008-error-model-exception-boundary-and-registry.md).

---

## Ratify-or-Revise Outcomes

| Area | Prior Architecture State | Ratified / Revised Decision |
|---|---|---|
| Global World Coordinates | Implicit 32-bit `WorldCoordinate` (`Vec3`) in early sketches | **Revised.** Replaced with `WorldCoordinate64` (`IntVector3 cellIndex` + `IntVector3 cellOffsetMm`) and exact `int64_t` millimeter round-trip. fp32 offsets are local-frame only. |
| GPU Shader Precision | Undefined; potential risk of requiring `double` vertex attributes | **Ratified.** GPU shaders remain strictly `fp32`/`fp16`. Camera-relative transformation $(P_{\text{world}} - C_{\text{camera}})$ is computed on CPU during render extraction. |
| Physics Coordinate Frame | Single global scene coordinate system | **Revised.** Physics runs in a local rebased coordinate frame. Position translation during rebasing preserves velocities and sleeping states. |
| Origin Shift Notification | Informal event on generic bus | **Revised.** Formalized as a transactional two-phase protocol (`PrepareRebase` -> `CommitRebase`) with monotonic generation fencing via `OriginRebaseCoordinator`. |
| Subsystem Synchronization | Subsystems handle origin drift independently | **Revised.** Synchronized broadcast at pre-physics frame safe point; unsupported or failing adapters abort the transaction with typed `Horo::Error`. |
| Networking & Saves | Transmit local floats | **Ratified.** Saves and network authority serialize canonical `WorldCoordinate64`, decoupled from client-local floating origin states. |

---

## Authoritative Ownership

| Subsystem / Type | Ownership Responsibility | Permitted Operations | Forbidden Operations |
|---|---|---|---|
| `OriginRebaseCoordinator` | Manages active floating origin $C_{\text{origin}}$, rebasing threshold checks, two-phase rebase transactions, and monotonic `OriginGeneration` | Evaluates camera position, computes $\Delta_{\text{origin}}$, dispatches `OriginRebaseEvent` at safe points | Direct mutation of private subsystem internal caches |
| `WorldStreamingManager` | Spatial partition authority, streaming cell grid indexing, cell residency lifecycle | Converts `WorldCoordinate64` to `StreamingCellId`, queries streaming volumes | Rebasing scene geometry without coordinating with `OriginRebaseCoordinator` |
| `RenderExtraction` / `RenderFrontend` | Computes camera-relative model-view matrices $M_{\text{view\_rel}}$, populates per-view uniform buffers | Subtracts camera origin on CPU, passes 32-bit float transforms to backend | Uploading 64-bit coordinate attributes to GPU shaders |
| `PhysicsWorldAdapter` | Translates physics rigid bodies, colliders, character controllers, and query bounds by $-\Delta_{\text{origin}}$ | Spatial proxy shift, broadphase updates | Modifying velocities, waking sleeping bodies, applying forces during shift |
| `AudioSubsystemAdapter` | Translates active listener and 3D spatial emitter positions | Updates emitter coordinates | Re-triggering voices, applying Doppler shifts during origin rebase |
| `VfxSubsystemAdapter` | Translates live particle position buffers in world-space emitters | Uniform vector offset subtraction | Re-seeding emitters or resetting particle lifetimes |
| `NavigationSubsystemAdapter` | Translates dynamic obstacle overlays and active agent paths | Translates corridor waypoints | Invalidating static NavMesh tile topology |
| `SceneRuntime` (ECS) | Owns entity hierarchy, local transforms, and scene-level spatial components | Translates root entity rebased transforms | Desynchronizing entity global positions from cell partitions |

---

## Platform Cost & GPU Compatibility Analysis

### 1. GPU Shader Evaluation: `fp32` vs `fp64` vs Emulated `Double-Single`

| Strategy | Performance on Mobile (iOS/Android) | Performance on Desktop (Metal/Vulkan/D3D12) | Implementation Complexity | Architectural Fit |
|---|---|---|---|---|
| **GPU Native `fp64`** | **Unsupported / Broken** (Metal iOS & GLES lack `double` support; severe ALU penalty on desktop) | 1/4 to 1/32 FP32 ALU throughput | Low (if hardware supported) | **Rejected**: Violates mobile and console parity goals. |
| **GPU Emulated DS (`fp32` pairs)** | Multiple FP32 operations per emulated scalar and increased register pressure | Multiple FP32 operations per emulated scalar and increased register pressure | High | **Rejected**: Adds shader cost and complexity to every supported platform. |
| **Camera-Relative `fp32` (Selected)** | Uses the platform's native FP32 shader path; shifts subtraction work to extraction | Uses the platform's native FP32 shader path; shifts subtraction work to extraction | Low (CPU-side matrix subtraction) | **Selected**: Preserves broad hardware compatibility and keeps the cost measurable at one boundary. |

### 2. CPU Precision Arithmetic Costs

- Subtraction of 64-bit integer / double-precision vector coordinates on CPU occurs during **Render Extraction** (once per render instance per frame) and **Origin Rebasing** (infrequently, e.g., once every several minutes of travel).
- Implementations must benchmark coordinate conversion and camera-relative extraction with representative instance counts, release builds, and each supported CPU architecture. This decision does not prescribe an unmeasured latency target.

---

## Physics & Scene Authority

1. **Velocity and Momentum Invariants**:
   Under Galilean relativity, linear velocity $\vec{v} = \frac{d\vec{x}}{dt}$, angular velocity $\vec{\omega}$, acceleration $\vec{a}$, and forces $\vec{F}$ are invariant under spatial translation $\vec{x}' = \vec{x} - \Delta\vec{x}_0$.
   Therefore, when `OriginRebaseEvent` is dispatched:
   $$\vec{x}_{\text{body\_new}} = \vec{x}_{\text{body\_old}} - \Delta_{\text{origin}}$$
   $$\vec{v}_{\text{body\_new}} = \vec{v}_{\text{body\_old}}, \quad \vec{\omega}_{\text{body\_new}} = \vec{\omega}_{\text{body\_old}}$$
   The physics adapter modifies only internal broadphase bounding boxes and position state. No impulse is applied, contact manifolds preserve their local penetration normals, and sleeping islands remain asleep.

2. **Rebasing Safe Points**:
   Origin rebasing is strictly prohibited during:
   - Fixed-step physics integration ticks (`PhysicsWorld::Step`).
   - Active GPU command buffer recording / render pass execution.
   - Asynchronous job execution mutating spatial components.
   Rebasing occurs exclusively at the **Post-Simulation / Pre-Render Synchronization Point** on the main thread (`MainEditor` role).

---

## Fallible Results and Error Domains

All precision and origin rebasing operations conform to [ADR-008](008-error-model-exception-boundary-and-registry.md):

```cpp
namespace Horo::Runtime::Precision {

enum class PrecisionErrorCode {
    CoordinateOverflow,          // Coordinate exceeds representable 64-bit world bounds
    NonFiniteCoordinate,         // Input contains NaN or infinite floating-point values
    InvalidRebaseDelta,          // Rebase offset vector is non-finite or exceeds maximum single shift step
    ParticipantRebaseFailed,     // A registered subsystem adapter failed during Prepare or Commit
    UnregisteredParticipant,     // Attempted to interact with an invalid or dead adapter handle
    StaleOriginGeneration,       // Origin generation mismatch during transactional commit
    RebaseDuringSimulationStep,  // Attempted to trigger rebase while physics or render graph was active
    TransactionAborted           // Rebase aborted during preparation phase
};

} // namespace Horo::Runtime::Precision
```

---

## Consequences

### Positive

- **Large Supported Range**: Allows worlds spanning the documented `WorldCoordinate64` range while preserving millimeter-scale canonical storage and high local precision near the viewer.
- **Universal Hardware Parity**: Shaders execute standard 32-bit single precision on iOS, Android, macOS (Metal), Linux/Windows (Vulkan/OpenGL/D3D12), and consoles.
- **Stable Multi-User Sync & Saves**: Canonical `WorldCoordinate64` ensures world persistence and network packets never suffer from floating-origin drift or precision loss.
- **Zero Simulation Artifacts**: Physics, audio, VFX, and AI navigate origin shifts seamlessly without momentum glitches, audio pops, or visual hitching.

### Negative / Trade-offs

- **Extraction Discipline**: Render extraction code must always calculate camera-relative matrices rather than concatenating raw absolute coordinates.
- **Subsystem Registration**: Every spatial subsystem must implement an `IOriginRebaseParticipant` adapter and register with `OriginRebaseCoordinator`.

---

## Rejected Alternatives

1. **Uniform 64-bit Floating-Point Everything (`dvec3` on CPU and GPU)**:
   - *Reason for Rejection*: Supported mobile APIs and GPUs do not provide a uniform native FP64 shader contract. Emulation adds substantial ALU and register pressure that must not be imposed on every draw.
2. **Periodic Scene Reload / Level Chunk Teleportation**:
   - *Reason for Rejection*: Destroys continuity, causes massive frame hitching, breaks live physics simulations, and interrupts audio playback.
3. **Double-Single (DS) High-Low Emulation in Shaders**:
   - *Reason for Rejection*: Doubles memory bandwidth for vertex attributes and consumes excessive shader ALU registers, causing occupancy drops on low-end GPUs.
4. **Local Physics Clusters with Independent Unsynchronized Origins**:
   - *Reason for Rejection*: Creates complex boundary discontinuities when rigid bodies cross cluster boundaries; synchronizing a single global floating origin across active local clusters is vastly simpler and robust.
