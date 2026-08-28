# ADR-011: Navigation Target Ownership and Dependency Boundary

- **Status**: Proposed
- **Date**: 2026-08-28
- **Supersedes**: None
- **Scope**: Navigation target boundaries (`NavigationApi`, `NavigationRuntime`, `NavigationRecastDetour`, `NavigationNull`), third-party encapsulation (Recast/Detour), decoupling from editor viewport navigation and gameplay AI decision graphs, headless/server composition, and compute resource scaling
- **Issue**: [#1224](https://github.com/abdullahbodur/horo-engine/issues/1224) ([NAV-001.1])
- **JIRA**: HORO-1224
- **Normative document**: [Navigation And AI Architecture](../architecture/runtime/navigation-and-ai-architecture.md)

## Context

`docs/architecture/runtime/navigation-and-ai-architecture.md` outlines the navigation mesh generation, pathfinding, dynamic obstacle avoidance, perception, and crowd simulation subsystems for Horo Engine. However, the initial specification suffered from several architectural ambiguities and boundary coupling issues:

1. **Lack of Formal Target Boundaries**: Target boundaries between backend-neutral public types, runtime coordination, and concrete third-party providers were not formally separated into isolated CMake targets and include directories.
2. **Third-Party Encapsulation Risks**: The integration of industry-standard navigation libraries such as Recast & Detour risked leaking third-party types (`rc*`, `dt*`, `dtPolyRef`, `dtCrowdAgent`, `dtTileCache`) into public headers, gameplay scripts, and scene components, creating fragile ABI boundaries and tight vendor lock-in.
3. **Conflation with Viewport Camera Navigation**: The term "navigation" in the codebase is used both for runtime spatial pathfinding (agent movement on NavMesh) and Editor Viewport camera navigation (orbit, pan, fly, focus). These two subsystems serve completely different domains and must remain strictly decoupled.
4. **Coupling with High-Level Gameplay AI**: High-level decision graphs (Behavior Trees, State Machines, Utility AI, Blackboard), sensory perception, and tactical query systems (Environment Query System) were mixed into the navigation narrative rather than positioned as independent consumers of spatial navigation queries.
5. **Coupling to Graphics / Rendering Tiers**: The original documentation derived navigation feature tiers (agent counts, dynamic obstacles, crowd simulation) from graphics hardware tiers (`es3`, `dx11`, `dx12_vulkan`, `high_end`), contradicting the engine's core architectural principle of headless/server parity and rendering independence.
6. **Headless / Dedicated Server Composition**: Dedicated game servers, headless physics/simulation workers, and test runners require spatial navigation and pathfinding without linking any graphics, RHI, or GUI modules (zero OpenGL, Metal, Vulkan, or ImGui dependencies). Conversely, games without navigation (e.g. 2D puzzle games, menu-driven titles) must be able to omit navigation targets entirely.

[NAV-001.1] resolves these architectural issues by establishing an authoritative target boundary, strict third-party encapsulation, subsystem decoupling, headless composition rules, and compute-based resource profiles.

## Decision

**Horo Engine establishes a four-target navigation architecture: `HoroEngine::NavigationApi` owns stable backend-neutral value types and query interfaces; `HoroEngine::NavigationRuntime` owns the scene-level coordinator, spatial query caching, dynamic obstacle overlays, and crowd job scheduling; `HoroEngine::NavigationRecastDetour` encapsulates Recast & Detour as a target-private concrete provider; and `HoroEngine::NavigationNull` provides a deterministic stub for headless and test environments. Public headers expose ZERO Recast or Detour symbols. Navigation runtime is strictly decoupled from editor viewport camera navigation and gameplay AI decision graphs. Navigation is optional and completely independent of rendering backends.**

### Ratify-or-revise outcomes

| Area | Prior state | Outcome |
|---|---|---|
| Target boundaries | Monolithic description under runtime | **Revised.** Split into four explicit targets: `NavigationApi`, `NavigationRuntime`, `NavigationRecastDetour`, and `NavigationNull`. |
| Third-party exposure | Ambiguous handling of Recast/Detour types | **Revised.** Public headers expose ZERO `rc*` or `dt*` types. All third-party headers/structures remain target-private inside `NavigationRecastDetour`. |
| Viewport camera navigation | Name collision with runtime agent navigation | **Ratified as decoupled.** Viewport camera controls belong exclusively to `HoroEngine::Gui` (`src/editor/.../viewport/navigation/`); runtime navigation lives under `src/runtime/navigation/` with zero dependency on UI. |
| Gameplay AI separation | AI graphs and perception mixed with navmesh | **Revised.** Behavior trees, state machines, blackboards, sensory perception, and EQS belong to `GameplayApi`/`GameplayRuntime` and consume navigation strictly through typed query interfaces. |
| Composition & headless | Unclear dependency on graphics/scene runtime | **Ratified.** Navigation targets depend only on `Foundation` and `SceneMath`; headless servers and test runners link navigation with zero rendering or GUI dependencies. |
| Feature tier derivation | Tied to graphics tiers (`es3`, `dx11`, `dx12_vulkan`) | **Revised.** Navigation scaling is based exclusively on CPU worker pools, memory budgets, and device compute resource profiles (`Mobile / Embedded`, `Desktop / Console`, `Dedicated Server`). |

### Authoritative Target and Boundary Map

```text
+-------------------------------------------------------------------------+
|                       Gameplay AI / Behavior Layers                     |
|           (Behavior Trees, State Machines, Utility AI, Blackboard)      |
+-------------------------------------------------------------------------+
                                    | (issues typed query requests)
                                    v
+-------------------------------------------------------------------------+
|                       HoroEngine::NavigationApi                         |
|   (NavMeshQuery, NavMeshPath, NavAgentProperties, NavMeshBuildSettings,  |
|    NavMeshData, DynamicObstacle, INavigationBackend, NavigationErrors) |
+-------------------------------------------------------------------------+
                                    ^
                                    | implements & coordinates
+-------------------------------------------------------------------------+
|                     HoroEngine::NavigationRuntime                       |
|   (NavigationCoordinator, Tile Streaming, Query Cache, Dynamic Overlay, |
|    Hierarchical Pathfinding, Crowd Job Scheduler, Diagnostics)          |
+-------------------------------------------------------------------------+
           |                                              |
           | binds backend                                | binds backend
           v                                              v
+-------------------------------------+        +--------------------------+
|  HoroEngine::NavigationRecastDetour |        | HoroEngine::NavigationNull|
|  (Target-Private: Recast & Detour)  |        | (Deterministic Mock/Stub |
|  - rcConfig, rcPolyMesh, rcContext  |        |  for Headless / CI Tests)|
|  - dtNavMesh, dtNavMeshQuery        |        +--------------------------+
|  - dtTileCache, dtCrowd             |
+-------------------------------------+
```

#### 1. `HoroEngine::NavigationApi` (`include/Horo/Navigation/`)

- **Type**: Interface / Model library.
- **Dependencies**: `HoroEngine::Foundation` only.
- **Responsibilities**:
  - Defines stable, immutable, backend-neutral data contracts: `NavMeshQuery`, `NavMeshPath`, `NavAgentProperties`, `NavMeshBuildSettings`, `NavMeshData`, `DynamicObstacle`, `NavMeshTileId`, `NavMeshPolyRef`, `NavQueryFilter`, `PathfindingRequest`, `PathfindingResult`, `CrowdAgentConfig`, `CrowdAgentHandle`.
  - Defines `INavigationBackend` abstract interface for pluggable spatial query and mesh generation backends.
  - Defines typed `NavigationErrors` using the standard `Horo::ErrorCode` / `Result<T>` model.
- **Encapsulation Invariant**: No header in `include/Horo/Navigation/` may include `<Recast.h>`, `<DetourNavMesh.h>`, `<DetourCrowd.h>`, or any third-party symbol.

#### 2. `HoroEngine::NavigationRuntime` (`src/runtime/navigation/runtime/`)

- **Type**: Static / Shared library.
- **Public Surface**: `Horo/Navigation/NavigationCoordinator.h`.
- **Dependencies**: `HoroEngine::NavigationApi`, `HoroEngine::Foundation`.
- **Responsibilities**:
  - `NavigationCoordinator`: Top-level runtime lifecycle coordinator for scene-scoped navigation meshes.
  - `NavQueryCache`: Thread-safe, bounded LRU query cache for repeated pathfinding and point reachability queries, invalidated deterministically upon dynamic obstacle modification or tile streaming.
  - `DynamicObstacleOverlay`: Manages runtime obstacle cutouts without mutating immutable source `NavMeshData` assets.
  - `HierarchicalPathfindingCoordinator`: High-level tile-to-tile coarse graph search paired with localized fine string-pulling.
  - `CrowdSimulationCoordinator`: Coordinates agent velocity obstacle avoidance and flocking/formation movement as batch jobs dispatched to `HoroEngine::Foundation::JobSystem`.
  - Observability contributions: Navigation query latency histograms, cache hit/miss counters, obstacle carving metrics, and profiler zones.

#### 3. `HoroEngine::NavigationRecastDetour` (`src/runtime/navigation/backends/recast_detour/`)

- **Type**: Module / Concrete Provider library.
- **Dependencies**: `HoroEngine::NavigationApi`, `HoroEngine::Foundation`, private third-party `recastnavigation`.
- **Responsibilities**:
  - Implements `INavigationBackend` using the Recast voxelization pipeline and Detour runtime query/tile-cache structures.
  - Converts Horo strongly-typed configuration structs (`NavMeshBuildSettings`) to `rcConfig` and constructs `rcPolyMesh` / `rcPolyMeshDetail`.
  - Manages `dtNavMesh`, `dtNavMeshQuery`, `dtTileCache`, and `dtCrowd` instances.
  - Translates internal Detour `dtPolyRef` and status flags (`dtStatus`) into Horo's `NavMeshPolyRef` and `Result<NavMeshPath>`.
  - Encapsulates all third-party memory allocators, ensuring allocations route through Horo's foundation memory domains where applicable.

#### 4. `HoroEngine::NavigationNull` (`src/runtime/navigation/backends/null/`)

- **Type**: Module / Deterministic Stub library.
- **Dependencies**: `HoroEngine::NavigationApi`, `HoroEngine::Foundation`.
- **Responsibilities**:
  - Implements `INavigationBackend` with deterministic, predictable stub behaviors (straight-line ray intersections, empty paths, immediate success/failure responses).
  - Used by default in unit tests, automated CI regression suites, and headless environments where pathfinding mesh evaluation is mocked or omitted.

### Strict Decoupling Rules

#### Viewport Camera Navigation vs. Runtime Agent Navigation

- **Domain Separation**: "Viewport Navigation" refers strictly to the interactive editor camera controls (first-person fly-through, turntable orbit, pan, focus, gizmo framing) located in `HoroEngine::Gui` (`src/editor/screens/workspace/panels/viewport/navigation/`).
- **Dependency Ban**: Viewport camera navigation code must never depend on `NavigationApi` or `NavigationRuntime`.
- **Debug Visualization Boundary**: When the editor visualizes the NavMesh overlay in the viewport, it extracts debug geometry (triangles, boundaries, off-mesh links) as transient render-debug primitives via the editor render extraction pipeline. Navigation never touches editor camera matrices or UI state.

#### Gameplay AI vs. Navigation Runtime

- **Domain Separation**: Gameplay AI owns decision making (Behavior Trees, Hierarchical Finite State Machines, Utility AI, Blackboard data storage) and Sensory Perception (sight cones, auditory stimuli, memory duration, Environment Query System candidate scoring).
- **Communication Direction**: AI behaviors and controllers consume navigation services exclusively by issuing typed requests (`PathfindingRequest`, `RaycastNavMeshRequest`, `FindNearestPolyRequest`) to `NavigationCoordinator` or async task interfaces.
- **Invariance**: Navigation targets have zero dependency on behavior tree nodes, blackboard entries, or gameplay script bindings.

### Headless and Dedicated Server Support

Navigation is a CPU/memory spatial service and operates entirely independently of presentation:

- Navigation modules have zero dependencies on `HoroEngine::RenderApi`, `HoroEngine::RenderFrontend`, OpenGL, Metal, Vulkan, Direct3D, or ImGui.
- Dedicated game servers link `HoroEngine::NavigationRuntime` + `HoroEngine::NavigationRecastDetour` to perform authoritative server-side pathfinding, dynamic obstacle carving, and crowd avoidance.
- Build configurations for headless servers or batch simulation tools compile cleanly without any graphics toolchain prerequisites.
- Games and tools that do not require navigation simply omit `NavigationRuntime` and `NavigationRecastDetour` from their CMake composition targets.

### Compute Resource Scaling (Decoupling from Rendering Tiers)

Navigation limits scale with CPU execution resources and memory budgets, not GPU hardware features:

| Resource Profile | Target Platform Example | Worker Threads | Active Nav Agents | Dynamic Obstacles | Query Cache Size |
|---|---|---|---|---|---|
| **Low-Power / Mobile** | Mobile, handheld consoles, embedded | 1 - 2 workers | Up to 64 | Up to 32 | 256 entries (1 MB) |
| **Standard Desktop / Console** | Mid-range PC, current-gen consoles | 4 - 8 workers | Up to 1,024 | Up to 256 | 2,048 entries (8 MB) |
| **High-Performance Dedicated Server** | Multi-core servers, high-end PC | 8+ workers | 5,000+ | 1,024+ | 8,192 entries (32 MB) |

## Consequences

### Positive

- **No Third-Party Pollution**: Projects, gameplay modules, and editor tools compile against narrow, stable Horo headers without exposure to Recast or Detour details.
- **Provider Interchangeability**: The navigation backend can be updated, patched, or replaced (e.g. custom voxelizers, hardware-accelerated BVH queries) without modifying public APIs or gameplay code.
- **Testability**: Automated regression tests can run with `NavigationNull` for fast, zero-dependency validation, or with `NavigationRecastDetour` for integration tests.
- **Server/Client Parity**: Headless dedicated servers execute the identical spatial pathfinding logic as client builds with zero rendering overhead.
- **Clean Subsystem Architecture**: Clear boundaries between viewport UI navigation, runtime agent pathfinding, and AI decision graphs.

### Negative / Trade-offs

- **Translation Layer**: Small CPU overhead for translating internal Detour vertex/polygon buffers into Horo value structs during path reconstruction (measured at negligible sub-microsecond latency).
- **Multiple Targets to Maintain**: Four distinct targets in CMake instead of a single monolithic library.

## Rejected Alternatives

1. **Header-Only Recast/Detour Exposure**: Exposing Detour's `dtNavMesh` and `dtPolyRef` directly in `Horo/Navigation/`. Rejected because it locks the engine into a specific third-party library, pollutes consumer compile units with C-style macros and raw pointers, and breaks public ABI stability.
2. **Merging Navigation into Gameplay AI**: Placing NavMesh pathfinding inside `GameplayRuntime`. Rejected because navigation is a foundational spatial service used by physics, character controllers, world streaming, and level design tools that may exist without high-level AI behavior trees.
3. **Binding Navigation Capabilities to Rendering Tiers**: Retaining the legacy `es3`/`dx11`/`vulkan` feature tier table for navigation. Rejected because graphics RHI capabilities have zero architectural relationship to CPU pathfinding and dynamic avoidance capacities, and doing so breaks dedicated headless server specifications.
