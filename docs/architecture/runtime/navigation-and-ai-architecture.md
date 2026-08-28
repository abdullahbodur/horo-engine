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

### Perception System

AI agents perceive the world through configurable senses:

```cpp
struct AIPerceptionConfig {
    float  sightRadius;
    float  sightAngle;             // peripheral vision cone half-angle
    float  hearingRadius;
    float  proximityRadius;        // touch/contact range
    float  memoryDuration;         // how long sensed stimuli are remembered
    PerceptionMask senseMask;      // which senses are active
};

enum class AISense : uint8_t {
    Sight     = 1 << 0,
    Hearing   = 1 << 1,
    Proximity = 1 << 2,
    Damage    = 1 << 3,
    Team      = 1 << 4,           // team communication
};
```

Stimuli are registered by gameplay systems (sight from physics/scene line-of-sight queries,
hearing from audio events, damage from health system) and delivered to
listening agents.

### Perception Queries

Agents query their perception state:

```cpp
struct PerceivedStimulus {
    AISense            sense;
    WorldCoordinate    location;
    EntityId           sourceEntity;
    float              intensity;
    float              age;           // seconds since perceived
    bool               isVisible;     // currently in line of sight
};
```

## Behavior Integration And AI Decision Graphs

AI decision making is authored as dedicated graph assets that compile into flat, immutable runtime execution plans and execute within the standard scene behavior lifecycle without introducing a secondary task manager or visual scripting engine.

### Graph Asset Model And UI Independence

1. **Pure Semantic Graph Assets**:
   - Persisted graph identity is established by typed assets: `BehaviorTreeAsset` (`.horo_bt`), `StateMachineAsset` (`.horo_sm`), and `UtilityAiAsset` (`.horo_utility`).
   - Assets contain only semantic topology: stable `GraphId`, `NodeId`, `PinId`, `PropertyId`, schema versions, node type identifiers, property bindings, and blackboard key bindings.
   - Visual presentation data (node layout $(x, y)$, routing bends, comments, zoom/pan) is stored exclusively in sidecar authoring metadata (`.meta` / `.editor_meta`) or stripped during asset cooking.
   - Runtime modules and cooked assets have **zero dependency** on `imgui-node-editor` or editor widget libraries.

2. **Compilation To Flat Runtime Plans**:
   - Source graph assets are compiled by `DecisionGraphCompiler` into immutable runtime execution plans:
     - `CookedBehaviorTreePlan`
     - `CookedStateMachinePlan`
     - `CookedUtilityPlan`
   - A cooked plan is a contiguous array of node descriptors with precomputed integer jump offsets, child ranges, decorator masks, and blackboard index offsets.
   - Static plans are shared and read-only across all agent instances executing the asset, eliminating per-agent plan duplication and pointer chasing.
   - Per-agent dynamic state is stored in an allocation-conscious `DecisionInstanceState`. Parallel composites require a bounded set of active node indices (not a single cursor). Hot-reload maps instance state by stable `DecisionNodeId`, never by array index.

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

struct DecisionInstanceState {
    static constexpr uint16_t kMaxActiveNodes = 8; // Parallel fan-out bound
    std::array<uint16_t, kMaxActiveNodes> activeNodeIndices{};
    uint16_t activeNodeCount{0};
    JobHandle runningTask{};
    // decorator memory and timers are indexed by stableId, not by live array index
};

struct CookedBehaviorTreePlan {
    DecisionGraphAssetId                assetId;
    uint32_t                            schemaVersion;
    BlackboardSchemaId                  requiredBlackboardSchema;
    std::vector<CookedDecisionNode>     nodes;
    std::vector<uint16_t>               serviceIndices;
};
```

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
   - Zero duplicate task managers or redundant component lifecycles: AI evaluation is driven within the engine's standard fixed-step scheduling (`SystemPhase::Gameplay`).

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

1. **Blackboard Storage And Typed Keys**:
   - Blackboard state is shared between behaviors and AI nodes:

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

- [Navigation Bake UI Reference](./navigation-bake.html)

- [ADR-025: AI Decision Assets and Shared Gameplay Behavior Boundary](../../adr/025-ai-decision-assets-and-gameplay-behavior-boundary.md)
- [Gameplay Behavior Authoring](../extensions/gameplay-behavior-authoring.md): behavior tree and state machine authoring
- [Physics Architecture](./physics-architecture.md): collision geometry for NavMesh generation
- [World Streaming Architecture](./world-streaming-architecture.md): NavMesh tile streaming
- [Scene Runtime](./scene-runtime.md): agent entity and component model
- [Concurrency And Jobs](../foundation/concurrency-and-jobs.md): parallel crowd and perception jobs
- [Debug Console And Overlays](./debug-console-and-overlays.md): AI debug visualization
