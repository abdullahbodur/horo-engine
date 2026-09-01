# ADR-016: Navigation Target Ownership and Dependency Boundary

- **Status**: proposed
- **Date**: 2026-08-28
- **Supersedes**: None
- **Scope**: Navigation target boundaries (`NavigationApi`, `NavigationRuntime`, `NavigationRecastDetour`, `NavigationNull`), third-party encapsulation (Recast/Detour), decoupling from editor viewport navigation and gameplay AI decision graphs, headless/server composition, and compute resource scaling
- **Issue**: [#1224](https://github.com/abdullahbodur/horo-engine/issues/1224) ([NAV-001.1])
- **Jira**: [HORO-1224](https://horo-engine.atlassian.net/browse/HORO-1224)
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

**Horo Engine establishes a four-target navigation architecture: `HoroEngine::NavigationApi` owns stable backend-neutral value types and query interfaces; `HoroEngine::NavigationRuntime` owns the scene-level coordinator, spatial query caching, dynamic obstacle overlays, and crowd job scheduling; `HoroEngine::NavigationRecastDetour` encapsulates Recast & Detour as a target-private concrete provider; and `HoroEngine::NavigationNull` represents deliberate navigation capability absence with the same scheduling contract. Public headers expose ZERO Recast or Detour symbols. Navigation runtime is strictly decoupled from editor viewport camera navigation and gameplay AI decision graphs. Navigation is optional and completely independent of rendering backends.**

### Ratify-or-revise outcomes

| Area | Prior state | Outcome |
|---|---|---|
| Target boundaries | Monolithic description under runtime | **Revised.** Split into four explicit targets: `NavigationApi`, `NavigationRuntime`, `NavigationRecastDetour`, and `NavigationNull`. |
| Third-party exposure | Ambiguous handling of Recast/Detour types | **Revised.** Public headers expose ZERO `rc*` or `dt*` types. All third-party headers/structures remain target-private inside `NavigationRecastDetour`. |
| Viewport camera navigation | Name collision with runtime agent navigation | **Ratified as decoupled.** Viewport camera controls belong exclusively to `HoroEngine::Gui` (`src/editor/.../viewport/navigation/`); runtime navigation lives under `src/runtime/navigation/` with zero dependency on UI. |
| Gameplay AI separation | AI graphs and perception mixed with navmesh | **Revised.** Behavior trees, state machines, blackboards, sensory perception, and EQS belong to `GameplayApi`/`GameplayRuntime` and consume navigation strictly through typed query interfaces. |
| Composition & headless | Unclear dependency on graphics/scene runtime | **Ratified.** NavigationApi depends on Foundation, which owns SceneMath; Runtime and providers depend on NavigationApi/Foundation, with vendor libraries private to the concrete provider. Hosts select/inject providers; headless does not imply Null. |
| Feature tier derivation | Tied to graphics tiers (`es3`, `dx11`, `dx12_vulkan`) | **Revised.** Navigation scaling is based exclusively on CPU worker pools, memory budgets, and device compute resource profiles (`Mobile / Embedded`, `Desktop / Console`, `Dedicated Server`). |

### Authoritative Target and Boundary Map

Navigation has four planned CMake targets. They are target contracts for downstream
implementation, not a claim that all four already exist in the current CMake tree.
Arrows below denote compile/link dependencies; runtime dispatch uses injected interfaces.

```text
NavigationRuntime -------> NavigationApi -------> Foundation (includes SceneMath)
NavigationRecastDetour ---> NavigationApi
NavigationNull ----------> NavigationApi

Game / Server host ------> NavigationRuntime + selected runtime provider
Cook / Bake host --------> selected mesh builder + Asset Pipeline

Host creates provider(s), then injects typed interfaces into coordinators.
NavigationRuntime never links or includes a concrete provider.
```

| Target | Direct Horo dependencies | Private vendor dependency | Public-header owner |
|---|---|---|---|
| `HoroEngine::NavigationApi` | `HoroEngine::Foundation` | None | Neutral types, requests, handles, provider interfaces |
| `HoroEngine::NavigationRuntime` | `NavigationApi`, `Foundation` | None | `NavigationCoordinator.h` |
| `HoroEngine::NavigationRecastDetour` | `NavigationApi`, `Foundation` | Selected Recast/Detour components only | Horo-only `Backends/RecastDetourProvider.h` factory/descriptor |
| `HoroEngine::NavigationNull` | `NavigationApi`, `Foundation` | None | Horo-only `Backends/NullProvider.h` factory/descriptor |

`SceneMath` is a Foundation-owned contract (`Horo/Math/SceneMath.h`), not a separate
CMake target. These dependencies provide `WorldCoordinate64`, `Vec3`, `Aabb`, and
`Quaternion`; navigation does not invent another math library. Asset, world-streaming,
and scene adapters are composed by the host; they do not add reverse dependencies
from these four navigation targets to application, Gameplay, Rendering, or GUI.

#### NavigationApi: Consumer Contracts

`include/Horo/Navigation/` contains neutral queries/paths, build settings, immutable
`NavMeshData` views, obstacle descriptions, navigation identities, typed
`NavigationErrorCode` / `Result<T>` results, and `CrowdAgentConfig` / `CrowdAgentHandle`.
Crowd types belong here because gameplay consumers need them to request agent creation
and movement. Scheduler queues, logical agent storage, and vendor crowd state do not.

Separate provider capabilities in `NavigationBackend.h`:

- `INavigationQueryBackend`: bounded spatial queries over pinned immutable topology.
- `INavigationTopologyBackend`: staged tile installation/removal and obstacle carving;
  produces a candidate topology without mutating data being queried.
- `INavigationCrowdBackend`: optional bounded crowd/avoidance batch computation.
- `INavigationMeshBuilder`: offline/background bake from owned geometry and settings.

There is no monolithic `INavigationBackend` combining all four lifecycles. A provider
may implement several capabilities, but hosts inject only those required by the use case.
Missing required capabilities fail composition; optional capabilities return `Unsupported`.
A runtime-only server composes query/topology and optionally crowd, without the builder.

#### NavigationRuntime: Policy, State, And Scheduling

`src/runtime/navigation/runtime/` owns `NavigationCoordinator`, logical obstacle/agent
state, query admission/cache policy, hierarchical search policy, and
`CrowdSimulationCoordinator`. It schedules Foundation jobs and publishes validated
results at the owning simulation phase. It never discovers concrete providers, registers
itself through a global singleton, performs vendor carving, or chooses cell residency.

`DynamicObstacleOverlay` records logical Horo obstacle changes; the topology backend
owns concrete carving and private tile-cache mutation. The crowd coordinator selects
agents, stable ordering, budgets, and target ticks; the crowd backend owns provider-specific
avoidance state and kernels. Typed snapshots/commands cross this boundary, never `dt*`
pointers. Navigation requests velocities; character locomotion owns transform/physics writes.

#### NavigationRecastDetour: Private Execution

`src/runtime/navigation/backends/recast_detour/` owns all `rc*` / `dt*` objects,
including mesh/query instances, tile caches, crowd instances, and build scratch. It
translates Horo values and typed errors at the interface boundary. Query instances/scratch
are exclusively leased per job; mutable crowd/tile-cache instances have one writer and
are never concurrently queried while being mutated.

Private runtime and build source groups are separate. A runtime-only provider composition
excludes Recast voxelization/build objects and their private link dependencies; enabling
the bake capability adds them for tools. Runtime topology/crowd support retains only its
required Detour components. This is an implementation gate, not reliance on linker dead
stripping to hide an always-linked builder.

The host activates inert provider descriptors or calls Horo-only factories and injects
owned query/topology/crowd interfaces into the coordinator. Selection happens before scene
activation. Stop admission, cancel/drain outstanding work under ADR-010, release snapshots,
then destroy providers before unloading their module; no callback or lease may outlive it.

#### NavigationNull: Feature Absent, Not Fake Navigation

`src/runtime/navigation/backends/null/` implements neutral capability absence.

| Operation | Null result |
|---|---|
| Path, projection/nearest-point, or navigation raycast | `NoNavigationData` |
| Build, install/carve topology, or create/update crowd agent | `Unsupported` |

Null never reports a successful straight-line route or pretends to evaluate geometry.
The same coordinator admits requests and publishes terminal results at the same scheduled
phase as a real provider; a cheap internal result does not invoke a caller inline. Cancellation,
capacity, stale-handle, and shutdown validation are shared. Tests needing successful paths or
scripted delays inject a separate test-only fake, not different production Null semantics.

Headless describes presentation, not navigation capability. Dedicated/headless hosts may
select Recast/Detour for real navigation, deliberately select Null for feature absence, or
omit navigation entirely. `NullRenderer` does not select `NavigationNull`.

### Strict Decoupling Rules

#### Viewport Camera Navigation vs. Runtime Agent Navigation

- **Domain Separation**: "Viewport Navigation" refers strictly to the interactive editor camera controls (first-person fly-through, turntable orbit, pan, focus, gizmo framing) located in `HoroEngine::Gui` (`src/editor/screens/workspace/panels/viewport/navigation/`).
- **Dependency Ban**: Viewport camera navigation code must never depend on `NavigationApi` or `NavigationRuntime`.
- **Debug Visualization Boundary**: When the editor visualizes the NavMesh overlay in the viewport, it extracts debug geometry (triangles, boundaries, off-mesh links) as transient render-debug primitives via the editor render extraction pipeline. Navigation never touches editor camera matrices or UI state.

#### Gameplay AI vs. Navigation Runtime

- **Domain Separation**: Gameplay AI owns decision making (Behavior Trees, Hierarchical Finite State Machines, Utility AI, Blackboard data storage) and Sensory Perception (sight cones, auditory stimuli, memory duration, Environment Query System candidate scoring).
- **Communication Direction**: AI behaviors and controllers consume navigation services exclusively by issuing typed requests (`PathfindingRequest`, `RaycastNavMeshRequest`, `FindNearestPolyRequest`) through bounded `NavigationCoordinator` admission, using a generation-checked request handle correlated with Foundation `JobId`. Tool/bake commands additionally use a coordinator-owned `OperationId`; the runtime publication rules below apply.
- **Invariance**: Navigation targets have zero dependency on behavior tree nodes, blackboard entries, or gameplay script bindings.

### Integration Contracts

The detailed contracts and verification matrix are normative in
[Navigation And AI Architecture](../architecture/runtime/navigation-and-ai-architecture.md#navigation-integration-contracts).

1. **Async and tick ownership (ADR-010/018/021/022)**: all background navigation work uses Foundation `JobSystem`. Workers consume immutable inputs and produce staged results; they never mutate ECS, blackboards, or live provider topology. `NavIntentCommit` publishes steering/query results in stable request/agent order after validating scene generation, cancellation, and topology revisions. Debug `OwnerThreadNextFrame` handlers enqueue into this phase rather than writing navigation state in an unrelated callback. Main/editor/transport threads never wait for navigation jobs. Per-agent work has typed request handles and `JobId` correlation; only user-facing operations are projected into the authoritative `OperationStore` by its application coordinator. Deterministic mode executes bounded kernels in stable order at the declared tick; best-effort mode may consume eligible worker completions, never partial results.
2. **World streaming (ADR-023, WST-010.8)**: World Streaming owns cell residency, global budgets, and eviction. A host-composed feature adapter stages NavigationMesh payloads and publishes/removes tiles in the cell transaction. Navigation owns provider resource installation and bounded query caches, and requests missing tiles through that adapter; it cannot load/evict cells independently. Query leases delay resource destruction, not logical eviction. Charges remain in the shared budget until memory is actually released. `wst.*` mutations enter the same world-streaming state machine.
3. **Handles and snapshots**: crowd handles include a navigation-world incarnation, slot index, and generation. Removal advances the generation; exhaustion retires the slot. Old/cross-world handles fail with `InvalidHandle`, without side effects. Obstacle changes are queued to the owner, which publishes immutable revisions; workers pin topology/obstacle snapshots. Cache entries and returned paths carry revisions for their contributing tiles/regions (or a conservative world revision); stale topology is never served as current.
4. **Assets and cooking (ADR-017)**: NavMeshData is a cooked immutable payload, not an authoring source. Host Asset Pipeline adapters assign/preserve AssetId, register typed artifacts in AssetRegistry/CookCatalog, and load through IAssetProvider/AssetLoadService. Source settings follow HoroProjectVersion; cooked payloads have an independent version and actual-byte integrity checks. Canonical dependency-aware cook keys include geometry, settings, target/profile, cooker and format versions. Runtime rejects incompatible bytes and never migrates or recooks them.
5. **Encapsulation and memory**: the prohibition covers every Recast/Detour header, transitive include, type, macro, inline/template body, and exported ABI symbol outside the concrete provider—not just three sample headers. Public-header ownership, isolated consumers, compile include-closure checks, export inspection, and deliberately failing leak fixtures are required when the targets land. Provider allocations, including tile/crowd/build scratch, use bounded tracked Foundation memory; no unspecified “where applicable” exception is allowed. Allocation hooks are installed once during host activation and remain valid until all provider allocations are released.

### Compute Resource Scaling (Decoupling from Rendering Tiers)

CPU/memory scaling and graphics independence are normative. Agent counts, cache sizes,
and worker counts are measured tuning decisions, not ADR constants. Earlier 64/1,024/5,000+
agent examples and 1/8/32 MB caches are withdrawn as normative limits. The subsystem's
canonical `GameplayAiProfile` defaults (ADR-022) remain unchanged; navigation validates
its share of those budgets and documents workload/platform measurements for any overrides.

## Consequences

### Positive

- **No Third-Party Pollution**: Projects, gameplay modules, and editor tools compile against narrow, stable Horo headers without exposure to Recast or Detour details.
- **Provider Interchangeability**: The navigation backend can be updated, patched, or replaced (e.g. custom voxelizers, hardware-accelerated BVH queries) without modifying public APIs or gameplay code.
- **Testability**: Automated regression tests can run with `NavigationNull` for capability-absence and scheduling validation, or with `NavigationRecastDetour` for integration tests.
- **Server/Client Parity**: Headless dedicated servers execute the identical spatial pathfinding logic as client builds with zero rendering overhead.
- **Clean Subsystem Architecture**: Clear boundaries between viewport UI navigation, runtime agent pathfinding, and AI decision graphs.

### Negative / Trade-offs

- **Translation Layer**: Translating internal provider vertex/polygon buffers into Horo value structs adds CPU work during path reconstruction. Implementations must measure that cost under representative path lengths, build modes, and target platforms before making a performance claim.
- **Multiple Targets to Maintain**: Four distinct targets in CMake instead of a single monolithic library.

## Rejected Alternatives

1. **Header-Only Recast/Detour Exposure**: Exposing Detour's `dtNavMesh` and `dtPolyRef` directly in `Horo/Navigation/`. Rejected because it locks the engine into a specific third-party library, pollutes consumer compile units with C-style macros and raw pointers, and breaks public ABI stability.
2. **Merging Navigation into Gameplay AI**: Placing NavMesh pathfinding inside `GameplayRuntime`. Rejected because navigation is a foundational spatial service used by physics, character controllers, world streaming, and level design tools that may exist without high-level AI behavior trees.
3. **Binding Navigation Capabilities to Rendering Tiers**: Retaining the legacy `es3`/`dx11`/`vulkan` feature tier table for navigation. Rejected because graphics RHI capabilities have zero architectural relationship to CPU pathfinding and dynamic avoidance capacities, and doing so breaks dedicated headless server specifications.
