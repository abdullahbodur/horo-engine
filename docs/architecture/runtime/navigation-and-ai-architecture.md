# Navigation And AI Architecture

## Purpose

This document defines the navigation (NavMesh), pathfinding, and AI subsystems
for Horo Engine. It covers navigation mesh generation, runtime pathfinding,
dynamic obstacle avoidance, AI perception, behavior integration, and crowd
simulation.

## Navigation Mesh

### NavMesh Generation

NavMesh is generated from scene collision geometry:

- Static mesh colliders are voxelized and used to build a navigation mesh
- NavMesh generation runs as an offline asset cook step
- Generated NavMesh is stored as a `NavMeshAsset` referenced by the scene

```cpp
struct NavMeshBuildSettings {
    float  agentRadius;
    float  agentHeight;
    float  maxSlope;
    float  stepHeight;
    float  cellSize;           // voxel size
    float  regionMinSize;
    bool   generateOffMeshLinks;
};
```

Multiple NavMesh surfaces can exist for different agent types (human, large
creature, flying).

### NavMesh Data

The runtime NavMesh is a compact data structure:

```cpp
struct NavMeshData {
    std::vector<NavMeshVertex>  vertices;
    std::vector<NavMeshPoly>    polygons;
    std::vector<NavMeshLink>    offMeshLinks;     // jump, ladder, teleport
    NavMeshTileGrid             tileGrid;
    NavMeshQueryFilter          defaultFilter;
};
```

NavMesh tiles support streaming: only tiles near active agents are loaded.

## Pathfinding

### A* With NavMesh

Pathfinding uses A* on the NavMesh polygon graph:

```cpp
struct PathfindingRequest {
    WorldCoordinate    start;
    WorldCoordinate    end;
    NavMeshQueryFilter filter;
    float              straighteningThreshold;
    CancellationToken  cancelToken;
};

struct PathfindingResult {
    std::vector<WorldCoordinate> waypoints;
    PathfindingStatus            status;      // Complete, Partial, Failed
    float                        pathLength;
};
```

- `NavMeshQueryFilter` controls which NavMesh areas are traversable
- Path straightening (string pulling) produces a compact waypoint list
- Partial paths are returned when the destination is unreachable
- Pathfinding requests are asynchronous and can be cancelled

### Hierarchical Pathfinding

For large worlds, hierarchical pathfinding is used:

- High-level graph connects NavMesh tiles
- Coarse path through tiles is found first
- Fine path within each tile is computed lazily as the agent moves
- High-level graph is regenerated when streaming loads or unloads tiles

## Dynamic Obstacles

Moving obstacles (other agents, vehicles, doors) affect navigation:

- Dynamic obstacles are applied as a scene-scoped runtime avoidance overlay; cooked `NavMeshAsset` data is not mutated
- Carving uses cylindrical or box-shaped cutouts
- Carving is local to the affected NavMesh tiles
- Paths are re-computed when an agent's path intersects a new obstacle

```cpp
struct DynamicObstacle {
    WorldCoordinate    center;
    float              radius;
    float              height;
    bool               isMoving;
    ObstaclePriority   priority;
};
```

## AI Perception

### Gameplay Truth vs Presentation Separation

The AI perception subsystem is an authoritative simulation component owned by
`PerceptionManager` in `SceneRuntime`. It evaluates what AI agents detect and
remember entirely within the simulation layer.

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
   - Perception **NEVER** scrapes render viewports, GPU depth buffers, pixel
     occlusion culling buffers, or audio mixer voice buffers for gameplay truth.
   - Headless servers, automated continuous integration tests, and dedicated
     server processes execute full perception logic without any graphics context,
     GPU, display, or audio mixer initialized.
2. **Typed Stimulus Transmission**:
   - Gameplay systems emit strongly typed data structures rather than inspecting
     presentation state.
3. **Unidirectional Presentation Observation**:
   - Debug overlays, editor gizmos, gameplay HUDs, and adaptive audio layers may
     observe perception state for visualization or audio cues, but presentation
     never mutates or feeds back into perception truth.

### Senses, Authorities, And Query Seams

Every built-in sense has an explicit authority, timing owner, and underlying
query seam:

| Sense | Timing Owner / Model | Authority & Source | Query Seam | Purpose |
|---|---|---|---|---|
| **Sight** | Periodic time-sliced (`PerceptionManager` tick) | `PerceptionManager` + `PhysicsWorld` | `PhysicsWorld::Raycast` / `Sweep` for LOS occlusion; `SceneRuntime` spatial index for candidates | Visual detection within FOV cone, peripheral angle, and sight radius |
| **Hearing** | Periodic time-sliced / stimulus queue drain | `PerceptionManager` + `AudioStimulusEmitter` | Distance attenuation + optional `PhysicsWorld` acoustic obstruction raycast | Acoustic detection of footsteps, gunshots, explosions, and environmental noise |
| **Damage** | Event-driven (immediate on hit) | `HealthSystem` / `CombatSystem` | Direct gameplay event carrying instigator entity, damage amount, and hit direction | Detection of inflicted harm, alerting agent to attacker identity/direction |
| **Touch** | Event-driven (fixed physics tick) | `PhysicsWorld` collision dispatcher | Contact manifolds and trigger overlap events | Immediate awareness of physical contact, collisions, and proximity penetration |
| **Team** | Event-driven / periodic broadcast | `TeamPerceptionRelay` | Squad/faction registry and communication radius or radio channel | Shared squad awareness, target spotting distribution, and distress alerts |

```cpp
enum class AISense : uint8_t {
    Sight     = 1 << 0,
    Hearing   = 1 << 1,
    Damage    = 1 << 2,
    Touch     = 1 << 3,
    Team      = 1 << 4,
};

struct SightStimulus {
    EntityId        targetEntity;
    WorldCoordinate position;
    float           visualStrength;
};

struct HearingStimulus {
    EntityId        emitterEntity;
    WorldCoordinate position;
    float           loudness;
    uint32_t        soundTag;
};

struct DamageStimulus {
    EntityId        instigatorEntity;
    WorldCoordinate hitLocation;
    float           damageAmount;
    uint32_t        damageType;
};

struct TouchStimulus {
    EntityId        otherEntity;
    WorldCoordinate contactPoint;
    Vector3         contactNormal;
};

struct TeamStimulus {
    EntityId        sourceTeammate;
    EntityId        targetEntity;
    WorldCoordinate lastKnownPosition;
    uint32_t        alertLevel;
};

struct StimulusEvent {
    AISense         sense;
    EntityId        sourceEntity;
    WorldCoordinate location;
    float           intensity;
    float           expirationTime;
};
```

1. **Physics Query Seam**:
   - Line-of-sight (LOS) occlusion checks execute against `PhysicsWorld` using
     dedicated query channels (`CollisionChannel::Visibility` / `SightOcclusion`).
   - LOS queries are read-only and operate against physics spatial acceleration
     structures without mutating collision state.
2. **Scene Spatial Seam**:
   - Candidate emitter gathering queries `SceneRuntime` spatial acceleration
     structures (octree / BVH) to discover potential emitters within sensory
     range before issuing detailed LOS physics traces, eliminating $O(N^2)$ scaling.

### Update Policies And Time-Sliced Budgets

Perception uses a hybrid execution model balancing immediate reactivity and
bounded compute cost:

1. **Hybrid Execution Model**:
   - **Event-Driven Senses (`Damage`, `Touch`, `Team`)**: Dispatched immediately
     when gameplay events occur or collected during the fixed simulation tick.
     Work is $O(E)$ where $E$ is the small number of emitted events.
   - **Continuous Periodic Senses (`Sight`, `Hearing`)**: Evaluated at scheduled
     intervals across frames, distributed evenly via time-slicing.
2. **Time-Sliced Raycast Budgets**:
   - The engine enforces hard limits per frame/tick:
     - `maxSightRaycastsPerTick`: Maximum physics LOS raycasts allowed across all
       agents in a single frame (e.g. 128 raycasts).
     - `maxPerceptionExecutionTime`: CPU budget cap per tick (e.g. 1.0 ms).
     - `maxAgentsEvaluatedPerTick`: Maximum agent sight sweeps per tick.
   - Agents are partitioned into interleaved tick buckets (round-robin or
     priority-scheduled).
3. **Sensory Level of Detail (LOD)**:
   - Evaluation frequency dynamically scales with distance from active player
     characters or high-priority gameplay cameras:
     - **LOD 0 (Near, < 25m)**: Full evaluation rate (e.g. 10 Hz / every 6 ticks at 60Hz).
     - **LOD 1 (Medium, 25m - 60m)**: Halved evaluation rate (e.g. 5 Hz / every 12 ticks).
     - **LOD 2 (Far, > 60m)**: Throttled evaluation rate (e.g. 1-2 Hz / every 30-60 ticks).
     - **LOD 3 (Culled / Dormant)**: Completely paused or background sweep only.

### Memory Model And Linear Decay

```cpp
struct AIPerceptionConfig {
    float          sightRadius;
    float          sightAngle;           // Peripheral vision cone half-angle
    float          hearingRadius;
    float          touchRadius;          // Contact/proximity range
    float          memoryDuration;       // Max memory lifetime in seconds
    float          decayRate;            // Linear decay rate per second
    float          forgetThreshold;      // Strength threshold for eviction
    uint32_t       maxTrackedStimuli;    // Runtime cap, <= kMaxTrackedStimuli
    PerceptionMask senseMask;            // Active sense bitmask
};

struct PerceivedStimulus {
    AISense         sense;
    EntityId        sourceEntity;
    WorldCoordinate lastKnownPosition;
    Vector3         lastKnownVelocity;
    float           strength;            // Normalized sensed strength [0.0, 1.0]
    float           age;                 // Seconds since last observed
    bool            isCurrentlySensed;   // Active line-of-sight or contact
};

struct AIPerceptionMemory {
    static constexpr uint32_t kMaxTrackedStimuli = 32; // compile-time hard cap
    std::array<PerceivedStimulus, kMaxTrackedStimuli> entries{};
    uint32_t count = 0;                 // live count, <= config.maxTrackedStimuli
    uint32_t maxTrackedStimuli = 16;    // runtime cap, default 16, max 32
};
```

1. **Bounded Stimulus Storage**:
   - Each agent maintains an `AIPerceptionMemory` container with a fixed capacity
     (`maxTrackedStimuli` default 16, compile-time hard cap 32), preventing dynamic heap allocation
     during perception ticks.
2. **Linear Decay And Forgetting**:
   - Tracked stimuli record `age` (seconds since last sensed) and normalized
     `strength` ($[0.0, 1.0]$).
   - While actively observed, `strength` is refreshed to $1.0$ and `age` resets
     to $0.0$.
   - When sight/sound is lost, `strength` decays linearly:
     $$\text{intensity}(t) = 1.0 - \text{decayRate} \cdot \text{age}$$
   - When $\text{intensity} \le \text{forgetThreshold}$ (default $0.0$) or
     $\text{age} \ge \text{memoryDuration}$, the stimulus is purged.
3. **Last Known Position & Velocity Tracking**:
   - Perception records store `lastKnownLocation` and `lastKnownVelocity`.
   - Behavior trees and blackboards query last known location rather than live
     target transforms, preventing AI agents from cheating through walls.
4. **Lifecycle And Weak Entity Safety**:
   - Perception memory stores generation-checked `EntityId` handles.
   - When an entity is destroyed or pooled, subsequent perception queries detect
     stale handles and discard the record immediately, preventing use-after-free
     and stale target locking.

## Behavior Integration

AI behavior is authored through the gameplay behavior system:

- Behavior trees drive high-level AI decision making
- State machines manage agent states (idle, patrol, combat, flee)
- Utility-based AI evaluates multiple options and selects the best action
- Blackboard stores agent knowledge (target, last known position, patrol path)

```cpp
struct AIBlackboard {
    std::optional<EntityId>  target;
    std::optional<WorldCoordinate> lastKnownTargetPosition;
    std::vector<WorldCoordinate> patrolPath;
    uint32_t                 patrolIndex;
    float                    alertLevel;
    AIState                  currentState;
};
```

Navigation commands (move-to, follow, patrol) are issued from behavior nodes
and executed by the navigation system.

## Crowd Simulation

For groups of agents, crowd simulation provides:

- Local avoidance (reciprocal velocity obstacles)
- Formation movement (line, wedge, column, circle)
- Lane formation in corridors
- Density-based speed modulation

```cpp
struct CrowdAgentConfig {
    float  neighborRadius;
    float  maxNeighbors;
    float  avoidanceRadius;
    float  maxSpeed;
    float  maxAcceleration;
    bool   useFormations;
};
```

Crowd simulation runs as a parallel job over agent groups. Agents within a
group share avoidance data; groups are independent.

## Debugging And Visualization

- NavMesh visualization overlay (walkable areas, obstacles, off-mesh links)
- Path visualization (active paths with waypoints)
- Perception visualization (sight cones, hearing radii, known stimuli)
- AI debug panel (blackboard inspector, behavior tree state, active path)
- NavMesh generation diagnostics (build time, coverage percentage)

## Feature Tiers

| Feature              | `es3`     | `dx11`      | `dx12_vulkan` | `high_end` |
| -------------------- | ---------- | ------------ | ------------- | ------------ |
| NavMesh agents       | 32         | 256          | 1K            | 5K+          |
| Pathfinding          | Async      | Async        | Job system    | Job system   |
| Hierarchical paths   | No         | Yes          | Yes           | Yes          |
| Dynamic obstacles    | 16         | 64           | 256           | 1K           |
| Crowd simulation     | No         | Simple       | Full          | Full         |
| Perception queries   | Sync       | Sync         | Parallel      | Parallel     |

## Related Documents

- [ADR-011: Perception Ownership, Sense Policy and Budget Decision](../../adr/011-perception-ownership-sense-policy-and-budget.md)
- [Navigation Bake UI Reference](./navigation-bake.html)

- [Gameplay Behavior Authoring](../extensions/gameplay-behavior-authoring.md): behavior tree and state machine authoring
- [Physics Architecture](./physics-architecture.md): collision geometry for NavMesh generation
- [World Streaming Architecture](./world-streaming-architecture.md): NavMesh tile streaming
- [Scene Runtime](./scene-runtime.md): agent entity and component model
- [Concurrency And Jobs](../foundation/concurrency-and-jobs.md): parallel crowd and perception jobs
- [Debug Console And Overlays](./debug-console-and-overlays.md): AI debug visualization
