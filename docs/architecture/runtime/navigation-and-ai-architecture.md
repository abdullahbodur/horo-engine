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
| **Team** | Event-driven affiliation/distress updates plus periodic awareness broadcast | `TeamPerceptionRelay` | Squad/faction registry and communication radius or radio channel | Shared squad awareness, target spotting distribution, and distress alerts |

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
3. **Team Dispatch Split**:
   - Membership, faction, direct distress, and explicit target-spot events wake or
     invalidate affected agents immediately.
   - Periodic relay ticks share selected, already-committed perception facts among
     eligible teammates under bounded range/fan-out budgets. They do not read
     another agent's mutable in-progress sense evaluation.

### Update Policies And Time-Sliced Budgets

Perception uses a hybrid execution model balancing immediate reactivity and
bounded compute cost:

1. **Hybrid Execution Model**:
   - **Event-Driven Inputs (`Damage`, `Touch`, team affiliation/distress)**:
     Dispatched immediately when gameplay events occur or collected during the
     fixed simulation tick. Work is $O(E)$ where $E$ is the emitted event count.
   - **Continuous Periodic Inputs (`Sight`, `Hearing`, team awareness relay)**:
     Evaluated at scheduled intervals across frames and distributed evenly via
     time-slicing.
2. **Time-Sliced Raycast Budgets**:
   - The engine enforces hard limits per frame/tick:
     - `maxSightRaycastsPerTick`: Maximum physics LOS raycasts allowed across all
       agents in a single frame (e.g. 128 raycasts).
     - `maxPerceptionExecutionTime`: CPU budget cap per tick (e.g. 1.0 ms).
     - `maxAgentsEvaluatedPerTick`: Maximum agent sight sweeps per tick.
   - Agents use weighted fair round-robin queues. Deadline aging promotes agents
     as their LOD service interval approaches; overdue agents run
     oldest-deadline-first before normal weighted slots, preventing a sustained
     near-agent workload from starving distant queues.
   - Scene activation validates the admitted population against the configured
     service intervals and budgets. Runtime over-capacity preserves hard CPU and
     raycast limits, emits `PerceptionBudgetUnsatisfied`, and applies the configured
     deterministic admission/degradation policy rather than silently starving work.
3. **Sensory Level of Detail (LOD)**:
   - Evaluation frequency scales with distance from simulation relevance anchors
     such as active player characters and gameplay objectives. Rendering camera or
     frustum state never controls authoritative LOD on network hosts or headless
     servers:
     - **LOD 0 (Near, $[0, 25\text{m})$)**: Full evaluation rate (e.g. 10 Hz / every 6 ticks at 60Hz).
     - **LOD 1 (Medium, $[25\text{m}, 60\text{m})$)**: Halved evaluation rate (e.g. 5 Hz / every 12 ticks).
     - **LOD 2 (Far/Active, $[60\text{m}, \text{dormancyDistance})$, default endpoint 150m)**: Throttled evaluation rate (e.g. 1-2 Hz / every 30-60 ticks). Agents beyond the endpoint remain LOD2 while gameplay-pinned, inside active simulation/streaming relevance, or retaining a high-priority active stimulus.
     - **LOD 3 (Dormant)**: Entered only outside active simulation relevance (for example, an inactive streaming cell), or beyond `dormancyDistance` with no gameplay pin or high-priority stimulus. Continuous senses pause except for a bounded recheck (default 120 ticks); event-driven stimuli wake the agent immediately and recompute LOD.

### Memory Model And Linear Decay

```cpp
enum class PerceptionPersistencePolicy : uint8_t {
    PreserveMemory, // Persist durable knowledge and resume simulation-time decay
    ResetOnRestore  // Start with empty perception memory after restore
};

struct AIPerceptionConfig {
    float          sightRadius;
    float          sightAngle;           // Peripheral vision cone half-angle
    float          hearingRadius;
    float          touchRadius;          // Contact/proximity range
    float          dormancyDistance{150.0F}; // LOD2-to-LOD3 distance candidate
    float          memoryDuration{10.0F};    // Max lifetime in simulation seconds
    float          decayRate{0.1F};          // Linear decay rate, inverse seconds
    float          forgetThreshold{0.0F};    // Strength threshold for eviction
    uint32_t       maxTrackedStimuli{16};    // Runtime cap, <= kMaxTrackedStimuli
    PerceptionPersistencePolicy persistencePolicy{PerceptionPersistencePolicy::PreserveMemory};
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
   - `decayRate` is measured in inverse simulation seconds ($s^{-1}$), with a
     default of $0.1\,s^{-1}$.
   - When sight/sound is lost, `strength` decays linearly and is clamped:
     $$\text{strength}(t) = \max(0, 1.0 - \text{decayRate} \cdot \text{ageSeconds})$$
   - When $\text{strength} \le \text{forgetThreshold}$ (default $0.0$) or
     $\text{ageSeconds} \ge \text{memoryDuration}$ (default 10 simulation
     seconds), the stimulus is purged.
3. **Last Known Position & Velocity Tracking**:
   - Perception records store `lastKnownPosition` and `lastKnownVelocity`.
   - Behavior trees and blackboards query last known location rather than live
     target transforms, preventing AI agents from cheating through walls.
4. **Lifecycle And Weak Entity Safety**:
   - Perception memory stores generation-checked `EntityId` handles.
   - When an entity is destroyed or pooled, subsequent perception queries detect
     stale handles and discard the record immediately, preventing use-after-free
     and stale target locking.

## Behavior Integration And AI Decision Graphs

AI decision making is authored as dedicated graph assets that compile into flat, immutable runtime execution plans and execute within the standard scene behavior lifecycle without introducing a secondary task manager or visual scripting engine.

### Graph Asset Model And UI Independence

1. **Pure Semantic Graph Assets**:
   - Persisted graph identity is established by typed assets: `BehaviorTreeAsset` (`.horo_bt`), `StateMachineAsset` (`.horo_sm`), and `UtilityAiAsset` (`.horo_utility`).
   - Assets contain only semantic topology: stable `GraphId`, `NodeId`, `PinId`, `PropertyId`, schema versions, node type identifiers, property bindings, and blackboard key bindings.
   - Visual presentation data (node layout $(x, y)$, routing bends, comments, zoom/pan) is stored exclusively in the asset's canonical `.meta` sidecar (for example, `Guard.horo_bt.meta`) or stripped during asset cooking. `.editor_meta` is not supported.
   - Runtime modules and cooked assets have **zero dependency** on `imgui-node-editor` or editor widget libraries.

2. **Compilation To Flat Runtime Plans**:
   - Source graph assets are compiled by `DecisionGraphCompiler` into immutable runtime execution plans:
     - `CookedBehaviorTreePlan`
     - `CookedStateMachinePlan`
     - `CookedUtilityPlan`
   - A cooked plan is a contiguous array of node descriptors with precomputed integer jump offsets, child ranges, decorator masks, and blackboard index offsets.
   - Cross-paradigm call nodes reference an entry in the cooked plan's dependency table. Entries use stable asset identity and expected plan kind; they never contain runtime pointers or inline copies of another plan. Cooking rejects missing references, kind/schema mismatches, dependency cycles, and nesting beyond the configured bound.
   - Static plans are shared and read-only across all agent instances executing the asset, eliminating per-agent plan duplication and pointer chasing.
   - Per-agent dynamic state is stored in an allocation-conscious `DecisionInstanceState`. Parallel composites own bounded active branches, and each branch owns a bounded stack of cross-plan execution frames. Hot-reload and restore map state by `(DecisionGraphAssetId, DecisionNodeId)`, never by transient array index.

```cpp
struct CookedDecisionNode {
    DecisionNodeId           stableId; // persisted identity; survives recompile index shifts
    DecisionNodeTypeId       typeId;
    uint16_t                 parentIndex;
    uint16_t                 firstChildIndex;
    uint16_t                 childCount;
    uint16_t                 decoratorMask;
    DecisionNodeKind         nodeKind; // Composite, Decorator, Task, Service
    std::span<const uint8_t> staticPayload; // includes Wait duration range and other static params
};

struct DecisionSubplanRef {
    DecisionGraphAssetId assetId;
    DecisionPlanKind expectedKind;
    uint32_t requiredSchemaVersion;
};

struct DecisionExecutionFrame {
    DecisionGraphAssetId planAssetId;
    DecisionNodeId activeNodeId;
};

struct ActiveDecisionBranch {
    static constexpr uint16_t kMaxSubplanDepth = 8;
    std::array<DecisionExecutionFrame, kMaxSubplanDepth> frames{};
    uint16_t frameCount{0};
    JobHandle runningTask{};
};

struct DecisionInstanceState {
    static constexpr uint16_t kMaxActiveBranches = 8; // Parallel fan-out bound
    std::array<ActiveDecisionBranch, kMaxActiveBranches> activeBranches{};
    uint16_t activeBranchCount{0};
    // decorator memory and timers use (planAssetId, activeNodeId), never array indices
};

struct CookedDecisionPlanHeader {
    DecisionGraphAssetId                assetId;
    uint32_t                            schemaVersion;
    BlackboardSchemaId                  requiredBlackboardSchema;
    std::vector<DecisionSubplanRef>      subplanDependencies;
};

struct CookedBehaviorTreePlan {
    CookedDecisionPlanHeader             header;
    std::vector<CookedDecisionNode>     nodes;
    std::vector<uint16_t>               serviceIndices;
};
```

`CookedBehaviorTreePlan`, `CookedStateMachinePlan`, and `CookedUtilityPlan` all
begin with the same `CookedDecisionPlanHeader`; therefore any paradigm can own
validated subplan dependencies without merging or copying the referenced plan.

### Core 1.0 AI Paradigms

Horo 1.0 standardizes on three complementary decision paradigms:

#### 1. Behavior Trees (BT)

- **Composites**:
  - `Selector`: Evaluates children sequentially until one succeeds or runs.
  - `Sequence`: Evaluates children sequentially until one fails or runs.
  - `Parallel`: Evaluates all children concurrently with configurable completion policies (`RequireOneSuccess`, `RequireAllSuccess`, `RequireAllComplete`, `StopOthersOnFailure`).
- **Decorators (Conditions & Flow Control)**:
  - `Inverter`: Inverts child result (`Success` $\leftrightarrow$ `Failure`).
  - `Cooldown`: Enforces a mandatory cooldown duration between child executions.
  - `Loop`: Repeats child execution for a fixed count or while a condition is satisfied.
  - `BlackboardCheck`: Compares blackboard keys against constants, ranges, or other keys with reactive abort policies (`None`, `Self`, `LowerPriority`, `Both`).
  - `TimeLimit`: Aborts child task if execution exceeds a specified duration.
- **Tasks (Leaf Action Nodes)**:
  - `MoveTo`: Issues asynchronous pathfinding and movement requests to `NavigationSystem`.
  - `Wait`: Pauses branch execution for a fixed or randomized duration.
  - `PlayAnim`: Triggers an animation montage or clip and waits for completion.
  - `CustomTask`: User-defined native C++ or script tasks conforming to the decision task contract.
- **Services**:
  - Periodic background evaluations attached to composite or subtree nodes, updating blackboard values or environment queries while the subtree remains active.

#### 2. Hierarchical State Machines (HSM)

- `StateMachineAsset` is the canonical asset family for both flat and hierarchical state topology. HSM is the execution paradigm, not a separate persisted asset type.
- Hierarchical states with nested sub-state machines.
- Explicit entry actions, update actions, and exit actions.
- Event-triggered and condition-triggered transitions evaluated against blackboard state.
- State machines can host Behavior Trees as nested sub-state behaviors.

#### 3. Simple Utility Scoring

- Evaluates competing actions using consideration response curves (Linear, Polynomial, Logistic, Step).
- Normalized scoring $[0.0, 1.0]$ with priority weighting and multiplier aggregation.
- Utility selector selects the top-scoring action or samples from a top-tier bucketed probability distribution.

#### 4. Explicit Post-1.0 Extension Paradigms

Advanced planning and learning models are explicitly classified as **Post-1.0 Extensions**:

- **Hierarchical Task Networks (HTN)**: Domain planning with methods and compound tasks.
- **Goal-Oriented Action Planning (GOAP)**: Dynamic action graphs resolved via regression over world-state preconditions and effects.
- **Reinforcement Learning (RL) & Learned Policies**: On-device neural network inference / ML-agent decision models.
- **Conversational / Large Language Model (LLM) Decision Providers**: External generative AI NPC reasoning seams.

These paradigms will integrate via dedicated provider extension seams without breaking or modifying the 1.0 decision core.

### Runtime Task & Lifecycle Alignment

1. **Standard Execution Context**:
   - AI tasks evaluate through `BehaviorExecutionContext` (extending `BehaviorContext`), granting controlled access to scene resources, typed blackboard views, input, command buffers, and cancellation tokens.
   - `AIDecisionSystem` is the sole scheduling authority in `SystemPhase::Gameplay`. `AiControllerComponent` and eligible `BehaviorComponent` attachments are inert plan bindings discovered by that system; components do not own runners.
   - `GameplayInputAccess` is inherited for task parity with generic gameplay behaviors. It exposes only read-only semantic actions for possessed/player-controlled entities, never raw device state; NPC-only contexts receive a deterministic empty snapshot.

```cpp
enum class DecisionTaskStatus : uint8_t {
    Success = 0,
    Failure = 1,
    Running = 2,
    Aborted = 3,
};

class IDecisionTask {
public:
    virtual ~IDecisionTask() = default;
    virtual DecisionTaskStatus OnEnter(BehaviorExecutionContext&) = 0;
    virtual DecisionTaskStatus OnUpdate(BehaviorExecutionContext&, FixedDeltaTime) { return DecisionTaskStatus::Success; }
    virtual void               OnAbort(BehaviorExecutionContext&) {}
    virtual void               OnExit(BehaviorExecutionContext&, DecisionTaskStatus) {}
};
```

1. **Cooperative Asynchronous Task Execution**:
   - When a task returns `DecisionTaskStatus::Running`, it receives recurring update ticks until completion or abort.
   - Long-running async requests (e.g. `MoveTo` navigation or animation playback) capture a `CancellationToken`. On abort, the task's `OnAbort()` cancels downstream subsystem requests cleanly.

1. **Safe-Point Hot Reload And Plan Replacement**:
   - Asset compilation produces a new `CookedDecisionPlan`.
   - Running instances migrate at tick boundaries: compatible active paths retain state; incompatible edits trigger `OnAbort()` on active tasks and restart evaluation from the root node.
   - Stale or invalid plans never replace the active runtime plan and log typed compiler diagnostics.

1. **Save And Restore**:
   - The AI subsystem contributes an `IGameplayStateProvider` to `RuntimeSaveService` and captures state only at the save snapshot safe point.
   - Durable state includes plan asset/schema identity, bounded execution-frame stacks keyed by stable `DecisionNodeId`, serializable decorator/timer memory, and schema-approved blackboard values.
   - `JobHandle`, cancellation tokens, path/query handles, pointers, and cooked array indices are transient and never serialized. Restore resolves plans in staging and restarts asynchronous work only after atomic state application. Missing or incompatible plans reset the affected agent to its root with a typed diagnostic.

1. **Blackboard Storage And Typed Keys**:
   - Blackboard state is shared between behaviors and AI nodes:

`PerceptionManager` does not write `AIBlackboardView` directly. At the end of the
Perception phase it publishes immutable staged `PerceptionDelta` records.
`AIBlackboardSyncSystem` alone maps those deltas and last-known facts into
schema-approved keys during `SystemPhase::BlackboardSync`. Decision tasks and
Service nodes consume the committed view and are not alternate writers for this
seam.

```cpp
struct AIBlackboard {
    std::optional<EntityId>        target;
    std::optional<WorldCoordinate> lastKnownTargetPosition;
    std::vector<WorldCoordinate>   patrolPath;
    uint32_t                       patrolIndex;
    float                          alertLevel;
    AIState                        currentState;
};
```

Navigation commands (move-to, follow, patrol) are issued from behavior nodes
and executed by the navigation system.

### Perception Save And Restore

The AI subsystem contributes a versioned perception chunk through
`IGameplayStateProvider`. `PreserveMemory` captures durable sense/tag data,
stable source references where available, last-known position/velocity, strength,
and simulation age; `ResetOnRestore` explicitly opts ambient agents out.

Raycast queues, scheduler buckets/deadlines, raw entity generations, and in-flight
event buffers are transient. Restore resolves stable sources during staging,
retains last-known facts when a source no longer exists, republishes restored
deltas at the first `BlackboardSync`, and resumes decay from saved simulation age
without applying wall-clock offline time.

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

- [ADR-024: Perception Ownership, Sense Policy and Budget Decision](../../adr/024-perception-ownership-sense-policy-and-budget.md)
- [GAI-003.1 AI Decision Assets And Shared Gameplay Behavior Boundary](https://github.com/abdullahbodur/horo-engine/issues/1333): blackboard consumer and decision execution contract
- [Save Game And Persistence](./save-game-and-persistence.md): durable perception-memory capture and staged restore
- [Navigation Bake UI Reference](./navigation-bake.html)

- [ADR-025: AI Decision Assets and Shared Gameplay Behavior Boundary](../../adr/025-ai-decision-assets-and-gameplay-behavior-boundary.md)
- [Gameplay Behavior Authoring](../extensions/gameplay-behavior-authoring.md): behavior tree and state machine authoring
- [Physics Architecture](./physics-architecture.md): collision geometry for NavMesh generation
- [World Streaming Architecture](./world-streaming-architecture.md): NavMesh tile streaming
- [Scene Runtime](./scene-runtime.md): agent entity and component model
- [Concurrency And Jobs](../foundation/concurrency-and-jobs.md): parallel crowd and perception jobs
- [Save Game And Persistence](./save-game-and-persistence.md): durable decision-state capture and staged restore
- [Debug Console And Overlays](./debug-console-and-overlays.md): AI debug visualization
