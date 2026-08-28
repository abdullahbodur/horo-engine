# ADR-024: Perception Ownership, Sense Policy and Budget Decision

- **Status**: Proposed
- **Date**: 2026-08-28
- **Supersedes**: None
- **Scope**: AI perception subsystem, sensory stimulus emission, line-of-sight query seams, update policies, time-sliced budgets, and bounded memory decay
- **Issue**: [#1321](https://github.com/abdullahbodur/horo-engine/issues/1321) ([GAI-002.1])
- **JIRA**: HORO-1321
- **Normative document**: [Navigation And AI Architecture](../architecture/runtime/navigation-and-ai-architecture.md)

## Context

Autonomous AI agents require sensory systems to detect, interpret, and react to their environment. In early game architectures and legacy implementations, perception was frequently coupled directly to rendering and audio output—scraping framebuffers, querying pixel depth or occlusion buffers, and inspecting audio mixer voice states. Such coupling creates severe architectural problems:

1. **Headless & Dedicated Server Failure**: Dedicated servers, continuous integration smoke runs, and headless simulation environments cannot execute perception if it depends on GPU contexts, display pipelines, or audio devices.
2. **Non-Deterministic Behavior**: Rendering resolutions, LOD popping, culling optimizations, audio volume attenuation curves, and hardware mixer voice limits introduce frame-rate-dependent and platform-dependent divergence into gameplay logic.
3. **Unbounded CPU Spikes**: Uncoordinated line-of-sight raycasts and all-to-all agent sensory evaluations cause quadratic scaling ($O(N^2)$) and major frame drops when agent counts increase.
4. **Memory Leaks and Dangling Handles**: Unbounded perception memory tracking unmanaged entity pointers or raw handles results in memory exhaustion and use-after-free crashes when perceived entities are despawned.

To achieve robust, high-performance, and deterministic AI simulation, Horo Engine requires a strict architecture decision that:

- Separates gameplay perception truth from rendering and audio presentation.
- Defines clear authority, timing ownership, and query seams for all built-in senses.
- Establishes a hybrid update model with strict per-tick time budgets and distance-based sensory LOD.
- Enforces bounded perception memory with deterministic linear decay, forgetting thresholds, and entity lifecycle safety.

## Decision

**Perception is an authoritative simulation subsystem owned by `PerceptionManager` in `SceneRuntime`. It operates exclusively on typed simulation stimuli and scene/physics spatial queries; it never scrapes renderer visibility, GPU depth buffers, or audio mixer buffers. Senses use a hybrid update model where instantaneous senses are event-driven and continuous senses are time-sliced across frames within bounded CPU raycast budgets. Perception memory is strictly bounded per agent, using linear decay, configurable forgetting thresholds, and weak entity tracking.**

### 1. Gameplay Truth vs Presentation Separation

```text
+--------------------------------------------------------------------------+
|                            SIMULATION LAYER                              |
|                                                                          |
|  Gameplay Systems / Emitters               Perception Subsystem          |
|  +-------------------------+ StimulusEvent +--------------------------+  |
|  | Weapon / Footstep /     |-------------->| PerceptionManager        |  |
|  | Health / Scene Emitter  | (Typed Data)  | - Sense Registries       |  |
|  +-------------------------+               | - Time-Sliced Schedulers |  |
|                                            +--------------------------+  |
|                                                          |               |
|                                                          v               |
|                                            +--------------------------+  |
|                                            | Agent Perception Memory  |  |
|                                            | - Bounded Tracked Targets|  |
|                                            | - Linear Decay / State   |  |
|                                            +--------------------------+  |
|                                                          | (Observer)    |
+----------------------------------------------------------|---------------+
                                                           |
                                                           v
+--------------------------------------------------------------------------+
|                           PRESENTATION LAYER                             |
|                                                                          |
|  Debug & Editor Visualizers                Audio & VFX Presentation      |
|  +-------------------------+               +--------------------------+  |
|  | Vision Cones / Hearing  |<--------------| Sensory Alert Cues       |  |
|  | Sensed Stimuli Overlay  |  (Read-Only)  | Dynamic Music Triggers   |  |
|  +-------------------------+               +--------------------------+  |
+--------------------------------------------------------------------------+
```

1. **Strict Presentation Independence**:
   - Perception logic **NEVER** inspects render viewports, depth buffers, GPU occlusion queries, or audio mixer voice output buffers.
   - All sensory inputs are emitted as strongly typed data structures (`StimulusEvent`, `SightStimulus`, `HearingStimulus`, `DamageStimulus`, `TouchStimulus`, `TeamStimulus`).
   - Headless servers, automated regression tests, and dedicated server processes execute identical perception logic with zero GPU or audio hardware dependencies.
2. **Unidirectional Observation**:
   - Presentation systems (editor debug overlays, gameplay HUD, spatial audio reaction cues) may observe perception state for rendering debug cones or triggering acoustic tension cues.
   - Presentation systems never feed data back into perception state.

### 2. Senses, Authorities, and Query Seams

Every built-in sense has an explicit authority, timing owner, and underlying query seam:

| Sense | Timing Owner / Dispatch Model | Authority & Source | Underlying Query Seam | Purpose |
|---|---|---|---|---|
| **Sight** | Periodic time-sliced (`PerceptionManager` tick) | `PerceptionManager` + `PhysicsWorld` | `PhysicsWorld::Raycast` / `Sweep` for LOS occlusion; `SceneRuntime` spatial index for candidates | Visual detection within FOV cone, peripheral angle, and sight radius |
| **Hearing** | Periodic time-sliced / queued stimulus drain | `PerceptionManager` + `AudioStimulusEmitter` | Distance attenuation + optional `PhysicsWorld` acoustic obstruction raycast | Acoustic detection of footsteps, gunshots, explosions, and environmental noise |
| **Damage** | Event-driven (immediate upon damage application) | `HealthSystem` / `CombatSystem` | Direct gameplay event carrying instigator entity, damage amount, and hit direction | Detection of inflicted harm, alerting agent to attacker identity/direction |
| **Touch / Proximity** | Event-driven (fixed physics tick) | `PhysicsWorld` collision/trigger dispatcher | Contact manifolds and trigger overlap events | Immediate awareness of physical contact, collisions, and proximity penetration |
| **Team / Affiliation** | Event-driven / periodic broadcast | `TeamPerceptionRelay` | Squad/faction registry and communication radius or radio channel | Shared squad awareness, target spotting distribution, and distress alerts |

1. **Physics Query Seam**:
   - Line-of-sight (LOS) validation queries `PhysicsWorld` using dedicated collision query channels (`CollisionChannel::Visibility` / `SightOcclusion`).
   - Physics queries operate against read-only spatial acceleration structures (broadphase BVH) and do not mutate physics simulation state.
2. **Scene Spatial Seam**:
   - Candidate emitter gathering queries `SceneRuntime` spatial index (octree/BVH grid) to find entities within an agent's maximum sensory radius before running detailed LOS tests.
   - Eliminates $O(N^2)$ cross-comparisons between all agents.

### 3. Update Policies, Time-Slicing, and CPU Budgets

Perception evaluation uses a hybrid execution model to maximize responsiveness while guaranteeing bounded CPU time:

1. **Hybrid Execution Model**:
   - **Event-Driven Senses (`Damage`, `Touch`, `Team`)**: Handled immediately or collected in a lock-free queue during fixed simulation ticks. Processing is $O(E)$ where $E$ is the number of events (naturally low).
   - **Continuous Periodic Senses (`Sight`, `Hearing`)**: Evaluated at scheduled intervals across multiple simulation frames, distributed evenly via time-slicing.
2. **Time-Sliced Scheduling & Per-Tick Budgets**:
   - The engine enforces hard limits per frame/tick:
     - `maxSightRaycastsPerTick`: Maximum physics LOS raycasts allowed across all agents per frame (e.g. 128 raycasts).
     - `maxPerceptionExecutionTime`: Hard execution budget cap (e.g. 1.0 ms per simulation tick).
     - `maxAgentsEvaluatedPerTick`: Maximum number of agent sight sweeps per tick.
   - Agents are partitioned into interleaved tick buckets (round-robin or priority queue).
3. **Distance-Based Sensory LOD (Level of Detail)**:
   - Evaluation frequency dynamically scales with distance from active player characters or high-priority gameplay cameras:
     - **LOD 0 (Near, < 25m)**: Full evaluation rate (e.g. 10 Hz / every 6 ticks at 60Hz).
     - **LOD 1 (Medium, 25m - 60m)**: Halved evaluation rate (e.g. 5 Hz / every 12 ticks).
     - **LOD 2 (Far, > 60m)**: Throttled evaluation rate (e.g. 1-2 Hz / every 30-60 ticks).
     - **LOD 3 (Culled / Dormant)**: Completely paused or background sweep only.

### 4. Memory Model, Decay, and Lifecycle Safety

```text
+-------------------------------------------------------------------------+
|                         AIPerceptionMemory                              |
|  +-------------------------------------------------------------------+  |
|  | Tracked Stimuli List (Bounded Ring Buffer / Fixed Array: max 16)  |  |
|  +-------------------------------------------------------------------+  |
|    | Stimulus 0: Target A | Sight | Age: 0.2s | Strength: 0.95 | Active |
|    | Stimulus 1: Target B | Sound | Age: 2.1s | Strength: 0.40 | Decaying|
|    | Stimulus 2: Target C | Damage| Age: 4.8s | Strength: 0.05 | Forgetting|
+-------------------------------------------------------------------------+
```

1. **Bounded Stimulus Storage**:
   - Each agent maintains an `AIPerceptionMemory` container with a compile-time hard cap (`kMaxTrackedStimuli = 32`) and a runtime cap (`maxTrackedStimuli`, default 16).
   - Prevents dynamic heap allocations during runtime perception ticks.
2. **Linear Decay and Forgetting**:
   - Every tracked stimulus possesses an `age` (seconds since last sensed) and normalized `strength` ($[0.0, 1.0]$).
   - While actively sensed, `strength` is refreshed to $1.0$ and `age` resets to $0.0$.
   - When sight/sound is lost, `strength` decays linearly:
     $$\text{strength}(t) = 1.0 - \text{decayRate} \cdot \text{age}$$
   - When $\text{strength} \le \text{forgetThreshold}$ (default $0.0$) or $\text{age} \ge \text{maxMemoryDuration}$, the stimulus is marked forgotten and evicted.
3. **Last Known Position & Extrapolation**:
   - Perception records store `lastKnownPosition` and `lastKnownVelocity`.
   - Behavior trees and blackboard queries access last known location rather than live target entity transforms, preventing AI agents from "cheating" through walls.
4. **Lifecycle and Weak Entity References**:
   - Perception memory stores weak generation-checked `EntityId` handles.
   - When an entity is destroyed or pooled, subsequent perception queries detect stale handles and discard the record immediately, preventing use-after-free and stale target locking.

## Ratify-or-Revise Outcomes

| Area | Current Baseline | Outcome |
|---|---|---|
| Gameplay truth vs presentation | Ad-hoc perception queries; risk of render/audio coupling | **Ratified & Enforced.** Perception operates strictly on typed simulation stimuli; zero graphics/audio presentation coupling. |
| Built-in senses & authority | Loose sense enum without explicit timing owners | **Revised.** Explicit timing owners, query seams, and dispatch policies defined for Sight, Hearing, Damage, Touch, and Team. |
| Line-of-sight query seam | Direct scene traversal | **Revised.** Bounded `PhysicsWorld` raycast seam with visibility collision channel. |
| Update policy & budget | Unbounded per-frame evaluation | **Revised.** Hybrid execution with strict per-tick raycast budgets (`maxSightRaycastsPerTick`) and time caps. |
| Sensory LOD | All agents evaluated equally regardless of distance | **Revised.** Multi-tier distance-based sensory LOD reducing update frequency for distant agents. |
| Perception memory & decay | Unbounded container, basic age float | **Revised.** Bounded fixed-capacity storage, linear decay formulas, forgetting thresholds, and weak entity tracking. |

## Consequences

- Dedicated server and headless simulation operate with 100% feature parity to client perception.
- AI behavior is strictly deterministic and decoupled from client rendering resolution, framerate, or audio mixer settings.
- CPU overhead is strictly bounded, preventing frame spikes during large-scale battles.
- Behavior trees and decision planners receive realistic last known target positions rather than cheating with perfect scene knowledge.
- Memory safety is preserved across entity destruction and scene streaming transitions.

## Rejected Alternatives

- **Scrape renderer depth buffer or GPU occlusion queries for sight.** Rejected: breaks headless/dedicated servers, introduces GPU-CPU readback stalls, and makes AI gameplay frame-rate-dependent.
- **Inspect audio mixer voice volume or bus output for hearing.** Rejected: mixes master volume/mute settings into gameplay truth, fails on headless servers, and violates audio thread lock-free invariants.
- **Synchronous all-to-all sight checks every frame.** Rejected: quadratic scaling ($O(N^2)$) causes massive CPU spikes with dozens of agents.
- **Unbounded heap-allocated stimulus memory per agent.** Rejected: creates memory fragmentation, heap allocation overhead during gameplay, and vulnerability to memory exhaustion.
- **Direct raw entity pointers in perception memory.** Rejected: leads to dangling pointers and crashes when entities are destroyed without notifying all observing agents.
