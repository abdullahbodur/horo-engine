# Navigation And AI Architecture

## Purpose

This document defines the navigation (NavMesh), pathfinding, and AI subsystems
for Horo Engine. It covers navigation mesh generation, runtime pathfinding,
dynamic obstacle avoidance, AI perception, behavior integration, crowd
simulation, fixed-tick simulation phase ordering, network authority roles,
and hardware-driven simulation budget profiles.

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

## Simulation Lifecycle And Fixed-Tick Phase Ordering

AI simulation executes as a fixed-timestep pipeline within the engine's fixed
update loop, defined by [Runtime Lifecycle Architecture](./runtime-lifecycle.md).
Six ordered phases execute in each simulation tick. A variable-rate presentation
bridge runs afterward and consumes the committed snapshots:

```text
PerceptionSensePoll
        |  (Raw sensory stimuli buffers)
        v
  BlackboardSync
        |  (Committed agent knowledge & alerts)
        v
AiDecisionEvaluate
        |  (High-level action & movement intent)
        v
 NavIntentCommit
        |  (Pathfinding queries, steering & avoidance velocity)
        v
CharacterControllerLocomotion  (Physics Integration)
        |  (Committed world transforms & collision contacts)
        v
 AnimationRigUpdate
        |  (Skeletal pose evaluation)
        v
  RenderExtraction
        |  (Immutable presentation snapshot)
```

### Phase Contracts And Invariants

Phases 1-6 run inside the fixed simulation tick and may mutate simulation state
under the phase contracts below. `RenderExtraction` is the following
variable-rate presentation bridge, not a seventh fixed-tick phase.

1. **`PerceptionSensePoll`**:
   - Gathers sensory stimuli (sight cone line-of-sight traces, hearing sound events, damage triggers, proximity overlaps) into staged per-agent stimulus buffers.
   - Operates as a read-only pass over physics spatial structures and audio event queues.
   - Invariant: Does NOT mutate agent blackboard state, behavior tree node states, or world transforms.
2. **`BlackboardSync`**:
   - Ingests staged stimulus buffers into each agent's `AIBlackboard`.
   - Evaluates stimulus decay over time, updates last known target locations, adjusts agent alert levels, and processes incoming team/squad broadcast events.
   - Invariant: All blackboard mutations are completed within this phase; blackboards become read-only during subsequent decision evaluation.
3. **`AiDecisionEvaluate`**:
   - Evaluates behavior trees, finite state machines, or utility AI models against current blackboard state.
   - Selects agent actions, targets, and tactical states.
   - Invariant: Pure decision evaluation. Does NOT directly manipulate physics bodies, apply forces, or invoke rendering commands. Emits high-level movement/action intent.
4. **`NavIntentCommit`**:
   - Translates movement intent into concrete navigation actions: submits asynchronous pathfinding requests, follows active waypoints, and evaluates crowd/dynamic obstacle avoidance (RVO).
   - Computes desired kinematic horizontal/vertical velocity vectors for character locomotion.
   - Invariant: Submits desired velocity to character controllers without stepping the physics world directly.
5. **`CharacterControllerLocomotion`**:
   - Executes character controller updates within the physics tick step. Resolves collisions, ground support, slope limits, stepping, and external forces.
   - Commits authoritative entity world positions, orientations, and velocities.
   - Invariant: Transforms and velocities committed in this phase represent the authoritative simulation state for the tick.
6. **`AnimationRigUpdate`**:
   - Evaluates animation blend trees, locomotion state machines, and procedural IK based on committed locomotion velocity and active action tags.
   - Computes skeletal bone transform matrices.
   - Invariant: Evaluates presentation pose from committed simulation data; animation updates do not retroactively alter physics locomotion within the same tick.
**Variable-rate presentation bridge — `RenderExtraction`**:
   - Extracts immutable presentation snapshots from previous and current simulation ticks using interpolation factor $\alpha$.
   - Executed during variable-rate frame updates, completely decoupled from fixed-tick simulation loops.
   - Invariant: Strictly read-only presentation extraction.

## Network Authority And Host Roles

The execution of AI simulation phases is strictly governed by the host process's network role:

```text
[ Dedicated Server (Authoritative Host) ]
  ├── Phase 1: PerceptionSensePoll
  ├── Phase 2: BlackboardSync (SERVER-PRIVATE)
  ├── Phase 3: AiDecisionEvaluate (SERVER-PRIVATE)
  ├── Phase 4: NavIntentCommit & Avoidance
  └── Phase 5: CharacterControllerLocomotion (Physics Authority)
           │
           │ (Replicated Public Transforms, Velocities, Action Tags)
           ▼ [ Network Transport Layer ]
[ Client Host (Presentation-Only Peer) ]
  ├── Network Jitter Buffer & Snapshot Ingestion
  ├── Transform Interpolation (alpha)
  ├── Phase 6: AnimationRigUpdate (Client Visual Blending)
  └── Phase 7: RenderExtraction -> Viewport Presentation
  (NO perception queries, NO blackboards, NO decision trees)
```

### Host Role Matrix

| Host Role | Authority Scope | Executed Phases | Network Replication Output |
|---|---|---|---|
| **Standalone** | Full local simulation and presentation authority. | Phases 1–7 | None (local single process). |
| **Dedicated Server** | Authoritative simulation owner for all AI agents. | Phases 1–5 (Phases 6–7 optional for skeletal hitbox traces). | Replicates entity `NetworkId`, transform, velocity, and public animation tags. |
| **Client** | Presentation-only consumer for server-replicated AI. | Phases 6–7 (Transforms driven by interpolation). | Receives replication; submits no AI authority. |

### Authority, Privacy, And Security Boundaries

- **Clients Never Run Authoritative AI**: Connected clients NEVER run `PerceptionSensePoll`, `BlackboardSync`, `AiDecisionEvaluate`, or `NavIntentCommit` for server-owned AI agents. Clients receive replicated transform, velocity, and state tags from the server.
- **Server State Privacy**: Agent perception memories (e.g. sight awareness meters, target tracking scores) and `AIBlackboard` internal representations (behavior tree execution nodes, patrol indices, threat scoring matrices) are **server-private**.
- **No Private State Serialization**: Network replication protocols MUST NOT synchronize private perception data or blackboard state to clients. Only publicly observable gameplay attributes (positions, rotations, locomotion speeds, equip states, public audio cues) are sent over the wire. This prevents client-side wallhacks, radar exploits, and unnecessary bandwidth consumption.

## Simulation Execution Modes

The engine supports two explicit simulation scheduling modes for AI:

| Simulation Mode | Scheduling Contract | Primary Use Cases | Allowed Host Roles |
|---|---|---|---|
| **Deterministic Fixed-Tick** | Strict lockstep execution. Every active agent is evaluated on every fixed tick in deterministic entity-ID order. Time-slicing skips, frame-rate dependent heuristics, and random job interleavings are forbidden. | Lockstep multiplayer, replay recording and bit-identical playback, automated AI regression testing. | Standalone, Dedicated Server, Headless Test Harness |
| **Best-Effort Bounded Time-Slicing** | Distance- and significance-based Level of Detail (Simulation LOD). Agents near players update at full frequency; distant agents update at fractional rates (e.g. 1/2, 1/4 rate) with bounded maximum latency guarantees. Job queues are amortized across workers within fixed per-tick execution budgets. | High-density open-world scenes, large-scale RTS/RPG titles, single-player games exceeding per-tick CPU budgets. | Standalone, Dedicated Server |

Client hosts in networked multiplayer run neither mode for remote AI; they perform presentation-only state interpolation.

## Gameplay AI Profiles And Simulation Budgets

AI agent capacity, perception query fidelity, and pathfinding job allocations are configured through typed `GameplayAiProfile` definitions.

```cpp
struct GameplayAiProfile {
    std::string_view profileName;
    uint32_t         maxActiveNavMeshAgents;
    uint32_t         maxDynamicObstacles;
    uint32_t         maxPerceptionQueriesPerTick;
    uint32_t         pathfindingWorkerThreads;
    uint32_t         perceptionWorkerThreads;
    bool             enableCrowdSimulation;
    bool             enableHierarchicalPathfinding;
    bool             enableTimeSlicedLOD;
    float            highFrequencyRadius;      // Distance (m) for full-rate evaluation
    float            mediumFrequencyRadius;    // Distance (m) for 1/2 rate evaluation
    float            lowFrequencyRadius;       // Distance (m) for 1/4 rate evaluation
};
```

### Standard Engine Profiles

| Feature / Budget Parameter | `LowCpu` | `MediumCpu` | `HighCpu` | `DedicatedServer` |
|---|---|---|---|---|
| **Target Hardware Profile** | 2–4 CPU Threads | 6–8 CPU Threads | 12+ CPU Threads | Headless Server (Multi-Core) |
| **Max Active NavMesh Agents** | 64 | 512 | 2,048 | 4,096+ |
| **Max Dynamic Obstacles** | 32 | 128 | 512 | 1,024+ |
| **Perception Queries / Tick** | 16 | 128 | 512 | 1,024+ |
| **Pathfinding Concurrency** | Serial / 1 Worker | Job System Parallel | Job System Parallel | Job System Parallel |
| **Crowd Local Avoidance** | Simple RVO | Full RVO | Full RVO + Formations | Full RVO + Formations |
| **Hierarchical Pathfinding** | Disabled | Enabled | Enabled | Enabled |
| **Simulation LOD Policy** | Aggressive 3-Tier | Balanced 2-Tier | Full-rate Near / 2-Tier Far | Priority-Based Time-Slicing |

### Graphics Decoupling Invariant

**Architectural Rule**: Graphics backends (`OpenGL`, `Metal`, `Vulkan`, `NullRenderer`) grant **zero** AI capacity, perception query fidelity, or gameplay authority.

- Selecting a higher-end graphics API (e.g. Vulkan vs OpenGL) does NOT increase AI agent limits or perception budgets.
- Headless dedicated servers running with `NullRenderer` operate with full CPU/memory capacity and are not artificially throttled by presentation-tier checks.
- AI budgets scale exclusively with host CPU core counts, worker thread availability, and project-configured memory limits.

## Related Documents

- [ADR-022: AI Fixed-Tick Order, Authority and Simulation Budget](../../adr/022-ai-fixed-tick-order-authority-and-simulation-budget.md)
- [Runtime Lifecycle Architecture](./runtime-lifecycle.md): Fixed-tick simulation loop and interpolation model
- [Multiplayer Replication Architecture](./multiplayer-replication-architecture.md): Server authority, RPCs, and property replication
- [Networking Architecture](./networking-architecture.md): Network transport and I/O threading
- [Gameplay Behavior Authoring](../extensions/gameplay-behavior-authoring.md): Behavior tree and state machine authoring
- [Physics Architecture](./physics-architecture.md): Collision geometry and character controller locomotion
- [World Streaming Architecture](./world-streaming-architecture.md): NavMesh tile streaming
- [Scene Runtime](./scene-runtime.md): Agent entity and component model
- [Concurrency And Jobs](../foundation/concurrency-and-jobs.md): Parallel crowd and perception jobs
- [Debug Console And Overlays](./debug-console-and-overlays.md): AI debug visualization
