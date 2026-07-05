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

- [Navigation Bake UI Reference](./navigation-bake.html)

- [Gameplay Behavior Authoring](../extensions/gameplay-behavior-authoring.md): behavior tree and state machine authoring
- [Physics Architecture](./physics-architecture.md): collision geometry for NavMesh generation
- [World Streaming Architecture](./world-streaming-architecture.md): NavMesh tile streaming
- [Scene Runtime](./scene-runtime.md): agent entity and component model
- [Concurrency And Jobs](../foundation/concurrency-and-jobs.md): parallel crowd and perception jobs
- [Debug Console And Overlays](./debug-console-and-overlays.md): AI debug visualization
