# Navigation And AI Architecture

## Purpose

This document defines the navigation (NavMesh), pathfinding, tactical environment queries, and AI subsystems
for Horo Engine. It covers navigation mesh generation, runtime pathfinding,
dynamic obstacle avoidance, AI perception, behavior integration, crowd
simulation, fixed-tick simulation phase ordering, network authority roles,
and hardware-driven simulation budget profiles.

## Target Ownership And Architecture Boundaries

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

### NavigationApi: Consumer Contracts

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

### NavigationRuntime: Policy, State, And Scheduling

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

### NavigationRecastDetour: Private Execution

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

### NavigationNull: Feature Absent, Not Fake Navigation

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

## Subsystem Decoupling

Editor viewport camera navigation remains in Gui; orbit/pan/fly/focus code never depends
on NavigationApi or NavigationRuntime. A separate editor adapter may extract Horo debug
geometry for a NavMesh overlay, but navigation never reads camera/UI state or links rendering.
Gameplay owns decision graphs, perception, blackboards, and tactical scoring; navigation owns
spatial evaluation. Gameplay consumes typed admission/query interfaces, and navigation never
includes behavior nodes or script bindings. Host adapters bridge Assets, World Streaming,
and scene lifetime without making them providers' dependencies.

## Navigation Integration Contracts

### Submission, Jobs, And Publication

`NavigationCoordinator` admits owned typed requests with captured navigation-world/scene
incarnation, caller authority, cancellation, configuration, topology/obstacle revision, target
tick, and bounded output/node-expansion budgets. Admission returns `Result<NavRequestHandle>`;
closed admission or exhausted queue/result capacity returns `AdmissionRejected` with no
accepted job. The generation-checked request handle identifies a bounded result record and
correlates with a Foundation `JobId`; it is not a second task scheduler.

All background query/crowd/build work uses Foundation `JobSystem`, with exclusive scratch and
immutable input leases. Completion enqueues data, never a callback that writes live scene
state. Owner-thread `NavIntentCommit` consumes eligible results in stable request/agent order,
rechecks incarnation, handle generation, authority, cancellation, and relevant revisions, and
publishes paths/desired velocities before character locomotion. Stale results return
`StaleTopology` or `InvalidHandle` and may be resubmitted within budget; they are never applied
to replacement agents or tiles. A held path/corridor must be revalidated before later use too.

ADR-018 `OwnerThreadNextFrame` console handlers submit typed navigation commands for this
phase; they do not introduce a second mutation phase in `PreUpdate` / `DebugPhase`. Heavy
bake/export commands use asynchronous `OperationId` progress per ADR-010. The application
operation coordinator alone mutates the shared `OperationStore`; it may aggregate multiple
jobs. Per-agent tick jobs need no user-visible operation record. Main/editor/transport owners
never `Wait()`/`Join()` navigation jobs; allowed teardown waits follow ADR-010. No second
thread pool, per-backend executor, or independent OperationStore is permitted.

Deterministic fixed-tick mode cannot select results according to which worker happened to
finish. It runs bounded deterministic navigation kernels in stable agent/request order at the
declared tick, on the owning executor, without waiting on jobs. The request still completes
through scheduled publication, never inline at submission. Required topology must already be
pinned; unavailable data has a deterministic typed outcome. A provider unable to meet the
bounded deterministic kernel contract fails mode selection explicitly. Best-effort mode may
run the same kernels as jobs; pending work retains its prior valid intent or a configured safe
stop, never an incomplete new result. Completion timing is not an authority override.

Scene teardown closes admission, cancels pending work, rejects late results, then retires
resources after readers finish. Query cancellation before publication prevents result
application; cancellation after publication does not retroactively undo an applied intent.

### Obstacle And Provider State Synchronization

The owner applies bounded obstacle command batches at `NavIntentCommit` to logical
`DynamicObstacleOverlay` state and publishes an immutable obstacle revision. Carving occurs
only in the injected topology backend, on an isolated candidate. Readers pin an immutable
pair of topology and overlay revisions; no worker reads a container concurrently modified
by gameplay or by another job. Candidate topology is installed at a later safe point only
if its base revisions still match. Until ready, newly blocking obstacle intent uses a
conservative avoidance/stop overlay so agents cannot follow newly obstructed corridors.

Each mutable `dtCrowd`/tile-cache instance is exclusively owned for its batch; sharing an
instance between parallel updates is forbidden. Parallel queries lease separate query/scratch
objects against read-only mesh snapshots. Old snapshots and backend storage are retired only
after all readers release them. No blocking writer wait is introduced on the frame path.

`NavQueryCache` is bounded and thread-safe. Keys include query/filter/agent configuration and
all contributing tile/obstacle-region generations, including explored regions for negative
or partial answers. If exact dependency coverage is unavailable, a whole-navigation-world
revision is the conservative fallback. A change invalidates all affected entries; it need
not invalidate unrelated regions. Cache hits and late worker results both revalidate their
revision coverage before publication; external path users cannot bypass that check.

### Generation-Safe Navigation Identities

`CrowdAgentHandle` contains `NavigationWorldId` (a unique incarnation bound by the host to
one scene generation), slot index, and slot generation. Removing an agent increments its
generation; a generation that cannot advance retires the slot permanently. Worlds/incarnations
are never reused while references may survive. Stale, removed, or cross-world handles fail
with `NavigationErrorCode::InvalidHandle`, without modifying another agent. Request and
obstacle handles use the same lifetime rule. Provider polygon/tile references are translated
to Horo handles with topology-generation validation; a raw vendor bit pattern is not a stable
public identity. Queued operations revalidate handles on apply, not just at submission.

### World Streaming And Tile Lifetime

World Streaming is the authority for cell residency, load/eviction decisions, and global
resident-memory limits. A host-composed `IFeatureStreamingProvider` adapter decodes/stages the
ADR-023 NavigationMesh cell payload into private provider resources. Cell tile publication
or logical removal occurs with the existing `CommitDeferredLifecycleChanges` cell transaction;
`NavIntentCommit` consumes the committed topology revision and never sees partially installed
cell state. The adapter participates in rollback if any required cell provider fails.

Navigation tile streaming means requesting needed content and installing/releasing navigation
resources under that contract; it is not another cell loader. Missing tiles generate bounded
residency requests through the adapter. Denied/unavailable content produces a typed
`NoNavigationData` or explicitly partial path, not a direct I/O bypass. In-flight queries hold
leases; eviction first removes tiles from the active topology and invalidates affected paths,
then reclaims private bytes after readers retire. Results from logically evicted tiles cannot
be committed merely because a lease still keeps their memory alive.

The shared world budget accounts for cell-backed navigation resources and their retired but
still-pinned storage until actually freed. Navigation caches/scratch use explicit host-assigned
sub-budgets and do not independently charge the same allocation again. `wst.*` residency and
forced-eviction commands (ADR-018 / WST-010.8) enter the same world state machine. Standalone
scenes without World Streaming use a host asset-lifetime adapter with the same snapshot and
lease rules; neither case adds a concrete WorldStreaming dependency to NavigationRuntime.

Global locations use `WorldCoordinate64`; provider-local coordinates use SceneMath conversions
with a captured origin epoch. ADR-026 rebases translate dynamic paths/obstacles consistently.
A late result from an old origin is converted or rejected before application; rebasing alone
does not invalidate static tile topology or its stable world identity.

### Complete Third-Party Encapsulation And Allocation

The no-leak rule covers all public Horo navigation headers and all consumers outside
`NavigationRecastDetour`, including transitive includes, forward declarations, aliases,
macros, inline/template bodies, factory signatures, and exported ABI types. It covers the
entire vendor include tree (`Recast*.h`, `Detour*.h`, debug helpers, and future headers), not
an allowlist of forbidden examples such as `DetourTileCache.h` or `RecastAlloc.h`.

Required enforcement when the navigation targets are introduced:

1. Register each header under exactly one target in `cmake/HoroPublicHeaderOwnership.cmake`
   and use `horo_configure_target_header_boundary`; `NavigationCoordinator.h` belongs to
   Runtime, provider factories to their providers, and neutral contracts to Api. Reject
   unowned/duplicate headers and broad repository include roots at configure time.
2. Build the existing generated isolated public-header consumers, including Api-only and
   Null/runtime-only compositions without vendor include paths. Vendor usage requirements
   must remain PRIVATE; static-library link closure may carry implementation archives but
   must not propagate vendor headers, definitions, or options to consumers.
3. Add a CI include-boundary check over compiler dependency/include traces and compile
   commands for every non-provider translation unit. Reject any resolved path under the
   pinned vendor tree, even if reached through a renamed wrapper header. Scan public AST
   declarations/inline bodies and exported symbols for vendor declarations; vendor symbol
   visibility is private in shared builds.
4. Negative fixtures deliberately inject direct and transitive vendor headers, forward
   declarations, public aliases, and leaked include directories; each must fail the relevant
   gate. Run them with real-provider, runtime-only/no-builder, Null, and navigation-omitted
   configurations. Passing an empty scan before targets exist is not enforcement evidence.

These are implementation acceptance gates; this documentation-only ADR does not claim the
navigation-specific trace/AST/export fixtures already run in CI. Existing header ownership
and isolated consumer machinery is reused rather than replaced.

All provider allocations (persistent meshes, query scratch, crowd, tile cache, and bake
scratch) route through bounded, tracked Foundation memory domains. Install the vendor
[Recast allocation hooks](https://github.com/recastnavigation/recastnavigation/blob/main/Recast/Include/RecastAlloc.h)
and [Detour allocation hooks](https://github.com/recastnavigation/recastnavigation/blob/main/Detour/Include/DetourAlloc.h)
once per linked vendor-library instance during host activation, before any vendor allocation
or concurrent work. Additional hosts share the established routing and cannot replace it.
Their routing must be thread-safe and retain the allocating domain for cross-thread frees; hooks/domain
lifetime outlasts every allocation. Never swap hooks per scene or job. Explicit tile-cache
allocators and provider-owned C++ buffers follow the same rule. A path that cannot be routed
must be adapted or rejected during provider validation, not silently exempted. Allocation
failure returns a typed bounded failure, with no partial state publication or fallback to
untracked malloc. Failure-injection/accounting tests cover shutdown and concurrent queries.

## Navigation Mesh

### NavMesh Generation

NavMesh is generated from scene collision geometry:

- Static mesh colliders are voxelized and used to build a navigation mesh
- NavMesh generation runs as an offline asset cook step or background tooling job
- Generated NavMesh is stored as an immutable cooked `NavMeshData` asset referenced by AssetId

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

Multiple NavMesh surfaces can exist for different agent types (human, large creature, flying).

### NavMesh Asset And Cook Contract

`NavMeshData` is the immutable cooked payload/view. An authoring NavMesh definition contains
build settings, agent profiles, source geometry references, areas/off-mesh links, and a stable
Asset Registry `AssetId` assigned in its sidecar. Moving/renaming source preserves that ID.
The host's Asset Pipeline adapter registers the canonical `core.navmesh` type and its cooker,
writes immutable artifacts to `CookCatalog`, and loads them through `IAssetProvider` /
`AssetLoadService`. NavMeshData itself does not import the registry/provider implementation;
the adapter associates AssetId/type with the neutral payload and pins its lifetime.

Source definitions/settings follow `HoroProjectVersion` migration. Cooked bytes carry an
independent `navMeshFormatVersion` and the standard artifact envelope, including actual-byte
digest, sizes, and validated table ranges. Runtime rejects unsupported versions, malformed
indices/links, non-finite coordinates, bounds violations, or corrupt digests before installing
any tile. It never parses authoring JSON, migrates source, or recooks at runtime.

Cook uses the complete canonical Asset Pipeline key plus its explicit dependency-aware
extension: asset identity/type, source and metadata digests, settings/schema (including agent
profile and coordinate conventions), source/dependency geometry artifact digests in canonical
order, cooker identity/version, typed target/profile, and envelope/navmesh-format versions.
Project migration and provider/cooker format changes invalidate affected outputs. The current
dependency-free CacheKeyV1 cannot silently bake dependency-bearing meshes; this capability
requires the versioned dependency-aware extension also required by ADR-017.

Provider-specific optimized bytes, if emitted, carry an explicit provider-format version and
are private cooked sections decoded by that provider, never exposed as public vendor structs.
The host chooses a compatible payload/provider pairing or returns `UnsupportedCookedVersion`.
Cell packaging reuses the same cooked content through ADR-023's NavigationMesh payload; it
must not create competing AssetIds or an independent residency/cook authority.

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

NavMesh tile requests are driven by active-agent demand, but cell residency remains
World Streaming policy as defined above; nearby agents do not authorize a second loader.

## Pathfinding

### A* With NavMesh

Pathfinding uses A* on the NavMesh polygon graph:

```cpp
struct PathfindingRequest {
    WorldCoordinate64  start;
    WorldCoordinate64  end;
    NavMeshQueryFilter filter;
    float              straighteningThreshold;
    CancellationToken  cancelToken;
};

struct PathfindingResult {
    std::vector<WorldCoordinate64> waypoints;
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

- Dynamic obstacles are applied as a scene-scoped runtime avoidance overlay; cooked `NavMeshData` is not mutated
- Carving uses cylindrical or box-shaped cutouts
- Carving is local to the affected NavMesh tiles
- Paths are re-computed when an agent's path intersects a new obstacle

```cpp
struct DynamicObstacle {
    WorldCoordinate64  center;
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
     Evaluated at scheduled intervals across fixed simulation ticks and distributed evenly via
     time-slicing.
2. **Time-Sliced Raycast Budgets**:
   - The engine enforces hard limits per fixed simulation tick:
     - `maxSightRaycastsPerTick`: Maximum physics LOS raycasts allowed across all
       agents in one fixed simulation tick. It is profile-bounded (for example,
       at most 16 for `LowCpu` and 128 for `MediumCpu`).
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

## Environment Query System (EQS)

### Purpose And Ownership Boundary

The Environment Query System (EQS) provides composable spatial and tactical
reasoning for AI decision making (such as finding cover, establishing flanking
angles, acquiring line of sight, or selecting ambush locations) without embedding
ad hoc physics traces and NavMesh traversability checks inside behavior trees or
tick callbacks.

`EnvironmentQueryManager` (part of the Gameplay AI subsystem `HoroAI`) is the
**single authority** that orchestrates query lifecycles, stage scheduling, test
execution, score normalization, result caching, and terminal outcomes. Other
subsystems act strictly as read-only snapshot providers:

- **Navigation System**: provides NavMesh surface projection, polygon
  containment, traversability, and pathfinding distance/cost calculations.
- **Physics System**: provides collision raycasts, shape sweeps, and
  line-of-sight checks.
- **Perception System**: provides perceived candidate entities, stimulus locations,
  and sensory memory records.
- **Decision Systems (Behavior Tree / State Machine / Utility AI)**: consume
  query results asynchronously via typed task nodes; they never block fixed-tick
  simulation loops.

```text
+-------------------------------------------------------------------------+
|                       EnvironmentQueryManager                           |
|  +-------------------------------------------------------------------+  |
|  | Tick (Time-Sliced Budget: e.g. 1.5ms / max N items per frame)      |  |
|  +-------------------------------------------------------------------+  |
|         |                     |                      |                  |
|  +--------------+     +----------------+     +------------------+       |
|  | Query State: |     | Query State:   |     | Query State:     |       |
|  | Resolving    | --> | Generating /   | --> | Scoring /        |       |
|  | Context      |     | Nav Projection |     | Physics Traces   |       |
|  +--------------+     +----------------+     +------------------+       |
|                                                      |                  |
|                                              +------------------+       |
|                                              | Query State:     |       |
|                                              | Completed /      |       |
|                                              | PartialSuccess   |       |
|                                              +------------------+       |
+-------------------------------------------------------------------------+
```

### Query Templates And Item Model

Queries are defined by immutable compiled assets (`EnvironmentQueryPlan` /
`EnvironmentQueryTemplate`). Each stage possesses a unique `StageId` that remains
stable across serialization, renaming, and editor reordering.

```cpp
enum class EnvQueryItemType : uint8_t {
    Point,          // WorldCoordinate: spatial points (cover, vantage, patrol)
    Actor,          // EntityId: scene entities (targets, allies, cover objects)
    DirectionalRay, // Ray / Direction: tactical vectors (flanking, retreat angles)
    Custom          // Extensible gameplay payload with explicit typed schema
};

enum class EnvQueryContextType : uint8_t {
    Querier,                // Requesting agent entity
    Target,                 // Primary adversary / focus entity
    QuerierLocation,        // Agent world position snapshot
    TargetLocation,         // Target world position snapshot
    WorldOrigin,            // Scene world origin (0, 0, 0)
    Custom                  // Pluggable registered context provider
};

struct QueryContextSnapshot {
    EntityId           querierEntity;
    WorldCoordinate    querierPosition;
    Vector3            querierForward;
    std::optional<EntityId> targetEntity;
    std::optional<WorldCoordinate> targetPosition;
    uint32_t           sceneGeneration;
};
```

Context resolution snapshots values into an immutable `QueryContextSnapshot` at
query start. Query execution never exposes raw mutable scene pointers.

### Generators

Generators produce bounded sets of candidate items around query contexts:

- `GridGenerator`: 2D or 3D regular lattice around context with configurable
  spacing, radius, and boundary extents.
- `DonutRingGenerator`: Concentric radial rings around context with inner radius,
  outer radius, and radial step counts.
- `ConeGenerator`: Directional wedge/cone oriented along context forward vector
  with radius, arc half-angle, and angular step count.
- `NavMeshProjectionGenerator`: Projects spatial candidate points onto valid
  NavMesh surfaces, discarding off-mesh or non-traversable candidates.
- `PerceivedEntitiesGenerator`: Populates candidate items from sensory perception
  memory matching affiliation/sense filters.

All generators enforce a mandatory maximum item clamp (`maxItems`) to prevent
unbounded allocations and CPU spikes. Every candidate item receives a stable,
deterministic generation sequence index (`item_index`).

### Tests And Filters

Tests evaluate candidate items against query contexts or world providers:

- `LineOfSightTest`: Raycast or shape sweep between candidate item and context
  using Physics collision traces.
- `DistanceTest`: Euclidean, Chebyshev, Manhattan, or NavMesh path distance
  between candidate item and context.
- `DotProductTest`: Directional alignment (facing angle, field of view, flanking
  angle).
- `PathfindingCostTest`: NavMesh path length, travel cost, or reachability.
- `CoverExposureTest`: Obstacle height, stance clearance, and exposure angle
  relative to threat positions.

```cpp
enum class EnvQueryTestMode : uint8_t {
    FilterOnly,     // Discards items failing boolean condition
    ScoreOnly,      // Assigns normalized continuous score [0.0, 1.0]
    FilterAndScore  // Discards failing items, then scores survivors
};

enum class EnvQueryScoreCurve : uint8_t {
    Linear,         // (val - min) / (max - min)
    InverseLinear,  // 1.0 - (val - min) / (max - min)
    Sigmoid,        // S-curve emphasizing mid-ranges
    ThresholdStep   // Step function (0.0 or 1.0)
};
```

### Scoring Normalization And Deterministic Tie-Breaking

To prevent scoring arithmetic drift and non-deterministic behavior:

1. **Normalized Range**: All scoring tests normalize raw metrics into
   `[0.0, 1.0]` using the configured `EnvQueryScoreCurve`.
2. **Arithmetic Safety**: Non-finite numbers (`NaN`, `+Inf`, `-Inf`) and
   division-by-zero are trapped; offending items receive a score of `0.0` with
   diagnostic logging and cannot be selected as winning candidates.
3. **Total Score Weighted Sum**:
   $$\text{TotalScore} = \frac{\sum_{i=1}^{N} w_i \cdot s_i}{\sum_{i=1}^{N} w_i}$$
   where $w_i \ge 0$ is the stage weight and $s_i \in [0.0, 1.0]$ is the normalized test score.
4. **Deterministic Tie-Breaking Cascade**:
   - Primary: Highest `TotalScore`.
   - Secondary: Configured tie-breaker test score (e.g. minimum distance to Querier).
   - Tertiary: Stable item generation sequence index (`item_index`).
   Candidate ranking is 100% reproducible and independent of multi-threaded job
   scheduling or memory allocation layout.

### Time-Budgeted Asynchronous Execution

```cpp
enum class QueryResultStatus : uint8_t {
    Completed,      // All stages evaluated, candidates ranked
    PartialSuccess, // Budget exhausted; best candidate evaluated so far returned
    Cancelled,      // Cancelled by caller token or task abort
    TimedOut,       // Maximum frame/tick duration exceeded
    Aborted         // Querier, target, or scene destroyed in-flight
};

struct ScoredItem {
    EnvQueryItemType itemType;
    WorldCoordinate  position;        // Point / Actor location
    EntityId         entity;          // valid when itemType == Actor
    Vec3             direction;       // valid when itemType == DirectionalRay
    uint32_t         customTypeId{0}; // valid when itemType == Custom
    std::array<uint8_t, 16> customPayload{};
    float            totalScore;      // Normalized [0.0, 1.0]
    uint32_t         itemIndex;       // Stable generation index
};

struct QueryResult {
    QueryResultStatus         status;
    std::vector<ScoredItem>   items;           // Ranked by score and tie-breaker
    std::optional<ScoredItem> winningItem;     // Highest ranked candidate
    uint64_t                  executionTicks;  // Duration across slices
};
```

1. **Time-Sliced Execution**: Queries execute across multiple simulation ticks
   under a global per-tick time budget (e.g. `1.5ms` per frame) or candidate item
   batch limits. High-cost queries yield at stage or item boundaries and resume
   on subsequent frames.
2. **Partial-Result Fallback**: When configured, queries encountering budget
   exhaustion return `QueryResultStatus::PartialSuccess` containing the best
   valid item evaluated before yielding.
3. **Deterministic Replay**: Fixed-seed generation and index-based tie-breaking
   guarantee deterministic tactical decisions in replay and automated tests.
4. **Caching And Invalidation**:
   - Query results may be cached using a composite key:
     `CacheKey(QueryTemplateId, ContextSnapshotHash, NavMeshRevision, PhysicsRevision)` where `ContextSnapshotHash` quantizes querier/target `WorldCoordinate` to integer millimeters (raw floats would miss every moving-agent tick).
   - Results are retained in a bounded LRU cache with time-to-live (TTL).
   - Cache eviction never invalidates caller-owned immutable `QueryResult` copies.
5. **Lifecycle Safety And Cancellation**:
   - Queries track weak `EntityId` handles and scene generation counters.
   - When a querier entity, target entity, or scene is destroyed, in-flight
     queries transition immediately to `Aborted`; outstanding physics traces
     and pathfinding jobs are cancelled without dangling pointer access.


## AI Architecture And Runtime Ownership

### Subsystem Boundaries And Decoupling From Editor AI

Horo Engine strictly isolates runtime gameplay AI (`HoroAI` / `HoroEngine::AI`) from Editor LLM / Agent tooling (`AIA-001` / `HoroEditor` AI):

- **Gameplay AI Runtime (`HoroAI`)**: A high-performance, deterministic C++20 runtime linked into game clients and dedicated servers. It executes local decision graphs (Behavior Trees, Hierarchical State Machines, Utility AI, HTN/GOAP planners), spatial perception queries, pathfinding, and crowd simulation under strict per-frame CPU and memory budgets with zero unbudgeted heap allocations.
- **Editor AI Agent Tooling (`AIA-001`)**: An authoring and development assistant embedded in `HoroEditor` using external or local large language models (LLMs) and the Model Context Protocol (MCP). It operates asynchronously on human authoring requests (code generation, scene inspection, asset imports) and is never compiled into runtime game packages or dedicated servers.
- **Optional NPC LLM Dialogue Seam (`GAI-007.4`)**: Generative AI dialogue or narrative agents exist strictly as bounded, asynchronous gameplay service providers (`INpcDialogueService`). They communicate through network/async jobs with explicit fallback behaviors and never block the fixed simulation tick or hold raw pointers to ECS memory.

### AI Runtime Ownership And Scene Generation Safety

All gameplay AI runtime state is owned strictly by the active `SceneRuntime` generation:

```cpp
struct AiBrainState {
    DecisionTreeInstanceId  instanceId;
    DecisionGraphAssetId    assetId;
    ExecutionNodeIndex      currentNode;
    ExecutionStatus         status;
    TickTimestamp           lastEvaluationTick;
};

struct PerceptionMemory {
    std::vector<PerceivedStimulus> stimuli;  // bounded capacity reserved at scene activation
    std::optional<EntityId>        primaryTarget;
    WorldCoordinate64              lastKnownTargetLocation;
    TickTimestamp                  lastSeenTimestamp;
};

struct BlackboardState {
    BlackboardSchemaId             schemaId;
    uint32_t                       schemaVersion;
    std::vector<BlackboardValue>   values;   // schema-sized once at scene activation
    BlackboardRevision             revision;
};
```

`PerceptionMemory` is the scene-owned agent record. Its stimulus buffer is the
bounded `AIPerceptionMemory` store from ADR-024 (`maxTrackedStimuli` default 16,
compile-time hard cap 32). `PerceptionMemory::stimuli` reserves that cap during
scene activation and never grows during simulation; overflow follows the configured
oldest/lowest-significance eviction policy. `BlackboardState::values` is sized
exactly once from its immutable schema. Neither container allocates on fixed-tick
paths. `AIBlackboardView` is the typed access view over `BlackboardState`; example
layouts named `AIBlackboard` describe keys, not a second store.

1. **Generation Validation**: AI components (`AiAgentComponent`, `AiControllerComponent`), blackboard instances, perception memories, and task execution contexts reference entities using generation-checked `EntityId` (or `EntityRef { SceneRuntimeId, EntityId }`). Any stale handle referencing an entity destroyed in an earlier frame or previous scene generation is rejected.
2. **Scene Lifecycle Binding**:
   - Scene activation instantiates AI brains, blackboards, and perception memories from immutable schema assets.
   - Scene transition, replacement, or unload immediately cancels all pending AI background jobs and destroys all AI runtime state.
   - Play-In-Editor (PIE) executes AI logic exclusively within the isolated runtime scene clone; AI state is never written back to the authoring scene document.
3. **No Process-Global AI Singletons**: AI systems do not maintain ambient or process-global agent registries. All state queries are scoped to `SceneRuntimeAccess`.

## Fixed-Tick Simulation Scheduling And Safe Points

Gameplay AI execution is partitioned into discrete, deterministic **coarse groups** within the fixed simulation tick (ADR-021). They are not a second scheduler. ADR-022 names the fine-grained mutation phases that implement them; character locomotion commit, animation, and render extraction belong to that broader tick in [Simulation Lifecycle And Fixed-Tick Phase Ordering](#simulation-lifecycle-and-fixed-tick-phase-ordering).

```text
ADR-021 coarse group               ADR-022 fine phase
1. SystemPhase::Perception      -> PerceptionSensePoll
2. SystemPhase::BlackboardSync  -> BlackboardSync
3. SystemPhase::AiDecision      -> AiDecisionEvaluate
4. SystemPhase::AiIntentDispatch-> NavIntentCommit
                                   + typed combat/animation intent enqueue
5. SystemPhase::Gameplay        -> CharacterControllerLocomotion
                                   + IBehaviorInstance::OnFixedUpdate
                               then AnimationRigUpdate
                               then variable-rate RenderExtraction (not a fixed phase)
```

### Phase Contracts

1. **`SystemPhase::Perception` (Perception Update)**:
   - Performs spatial candidate broadphase queries and line-of-sight raycasts against physics and scene geometry.
   - Ingests sensory stimuli (sight, hearing, proximity, damage, team communication) and applies decay in `PerceptionMemory`.
   - Read-only with respect to scene ECS transforms; writes only to agent-private perception memory.
2. **`SystemPhase::BlackboardSync` (Blackboard Mutation Safe Point)**:
   - Commits batched external blackboard writes and perception stimulus reflections into blackboard keys.
   - Enforces schema validation against `BlackboardSchema`.
   - Fires registered blackboard change observers at a deterministic safe point.
   - Freezes blackboard state for the upcoming decision phase.
3. **`SystemPhase::AiDecision` (Decision Evaluation)**:
   - Evaluates high-level decision graphs (Behavior Trees, State Machines, Utility AI, Planners) using frozen blackboard and perception state.
   - Emits high-level action and navigation intents (e.g. move-to destination, attack intent, cover request).
   - Does not step physics or mutate scene topology directly.
4. **`SystemPhase::AiIntentDispatch` (Action/Navigation Intent Dispatch)**:
   - Translates decision outputs into typed intents. Navigation and steering commit in `NavIntentCommit`.
   - Combat actions and animation triggers enqueue as typed intents consumed later; they are not absorbed into `NavIntentCommit`.
5. **`SystemPhase::Gameplay` (Behavior Script Step & Locomotion)**:
   - Generic object-attached gameplay behaviors (`IBehaviorInstance::OnFixedUpdate`) and character controllers step **after** AI decision evaluation.
   - This group is not the AI decision phase.

### Scene Mutation Safe Points

AI decision nodes, behavior trees, and background tasks are strictly forbidden from mutating Scene ECS topology (creating/destroying entities, adding/removing components) directly during decision evaluation or worker job execution. All structural changes must be recorded into the `SceneCommandBuffer` and committed at the standard Scene Runtime synchronization point (`CommitDeferredLifecycleChanges`).

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
   - `AIDecisionSystem` is the sole scheduling authority in `AiDecisionEvaluate` (ADR-021 `AiDecision`). `AiControllerComponent` and eligible `BehaviorComponent` attachments are inert plan bindings discovered by that system; components do not own runners. Generic `OnFixedUpdate` behaviors still run later in `Gameplay` / `CharacterControllerLocomotion`.
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
    std::vector<WorldCoordinate64>   patrolPath;
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

## Asynchronous AI Tasks And Job System Integration

Asynchronous AI workloads—such as NavMesh pathfinding, perception visibility raycasts, and tactical environment queries—must execute through the Foundation `JobSystem`:

- **Single Task Scheduler**: AI subsystems must not instantiate private worker thread pools or bespoke background task runners.
- **Scene-Scoped Cancellation**: Every async AI job captures a `CancellationToken` bound to the active `SceneRuntime` generation. When a scene unloads or transitions, all active AI jobs are cancelled immediately.
- **Worker Thread Invariant**: Background AI jobs never mutate scene ECS components or live blackboard instances directly. Completed results (e.g. `PathfindingResult`, visibility test results) are queued into thread-safe result buffers and applied to agent memory on the main simulation thread at designated phase safe points.

## Debugging And Visualization

- NavMesh visualization overlay (walkable areas, obstacles, off-mesh links)
- Path visualization (active paths with waypoints)
- Perception visualization (sight cones, hearing radii, known stimuli)
- AI debug panel (blackboard inspector, behavior tree state, active path)
- NavMesh generation diagnostics (build time, coverage percentage)

## Simulation Lifecycle And Fixed-Tick Phase Ordering

AI simulation executes as a fixed-timestep pipeline within the engine's fixed
update loop, defined by [Runtime Lifecycle Architecture](./runtime-lifecycle.md).
Six ordered phases execute in each simulation tick; they implement the ADR-021
coarse groups. A variable-rate presentation
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
  RenderExtraction  (variable-rate presentation bridge; not a numbered phase)
        |  (Immutable presentation snapshot)
```

### Phase Contracts And Invariants

Phases 1-6 run inside the fixed simulation tick and may mutate simulation state
under the phase contracts below. `RenderExtraction` is the following
variable-rate presentation bridge, not a seventh fixed-tick phase.

1. **`PerceptionSensePoll`**:
   - Gathers all ADR-024 sensory stimuli (Sight line-of-sight candidates, Hearing events, Damage dispatches, Touch/Proximity overlaps, and Team/Affiliation events or bounded relays) into staged per-agent stimulus buffers.
   - Operates as a read-only pass over physics spatial structures and audio event queues.
   - Invariant: Does NOT mutate agent blackboard state, behavior tree node states, or world transforms.
2. **`BlackboardSync`**:
   - Ingests staged stimulus buffers into each agent's `BlackboardState`.
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

### Variable-Rate Presentation Bridge — `RenderExtraction`

This bridge is not a fixed-tick phase.

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
  ├── Client AnimationRigUpdate (Visual Blending)
  └── RenderExtraction Bridge -> Viewport Presentation
  (NO perception queries, NO blackboards, NO decision trees)
```

### Host Role Matrix

| Host Role | Authority Scope | Executed Phases | Network Replication Output |
|---|---|---|---|
| **Standalone** | Full local simulation and presentation authority. | Fixed phases 1–6, then the variable-rate extraction bridge. | None (local single process). |
| **Dedicated Server** | Authoritative simulation owner for all AI agents. | Fixed phases 1–5; phase 6 only when authoritative skeletal hitboxes require it; no presentation bridge. | Replicates entity `NetworkId`, transform, velocity, and public animation tags. |
| **Client** | Presentation-only consumer for server-replicated AI. | Client animation update plus the variable-rate extraction bridge; no authoritative AI phases for server-owned agents. | Receives replication; submits no AI authority. |

### Authority, Privacy, And Security Boundaries

- **Clients Never Run Authoritative AI**: Connected clients NEVER run `PerceptionSensePoll`, `BlackboardSync`, `AiDecisionEvaluate`, or `NavIntentCommit` for server-owned AI agents. Clients receive replicated transform, velocity, and state tags from the server.
- **Server State Privacy**: Agent perception memories (e.g. sight awareness meters, target tracking scores) and `BlackboardState` internal representations (behavior tree execution nodes, patrol indices, threat scoring matrices) are **server-private**.
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
enum class AiLodSchedulingPolicy : uint8_t {
    FullRate,
    DistanceBands,
    PriorityAged,
};

struct GameplayAiProfile {
    std::string_view profileName;
    uint32_t         maxActiveNavMeshAgents;
    uint32_t         maxDynamicObstacles;
    uint32_t         maxPerceptionQueriesPerTick;
    uint32_t         pathfindingWorkerThreads;
    uint32_t         perceptionWorkerThreads;
    bool             enableCrowdSimulation;
    bool             enableHierarchicalPathfinding;
    AiLodSchedulingPolicy lodSchedulingPolicy;
    float            highFrequencyRadius;      // Distance (m) for full-rate evaluation
    float            mediumFrequencyRadius;    // Distance (m) for 1/2 rate evaluation
    float            lowFrequencyRadius;       // Distance (m) for 1/4 rate evaluation
};
```

### Standard Engine Profiles

| Feature / Budget Parameter | `LowCpu` | `MediumCpu` | `HighCpu` | `DedicatedServer` |
|---|---|---|---|---|
| **Target CPU Threads** | 2–4 | 6–8 | 12+ | 16+ |
| **Max Active NavMesh Agents** | 64 | 512 | 2,048 | 4,096 |
| **Max Dynamic Obstacles** | 16 | 128 | 512 | 1,024 |
| **Perception Queries / Tick** | 16 | 128 | 512 | 1,024 |
| **Pathfinding Workers** | 1 | 2 | 4 | 8 |
| **Perception Workers** | 1 | 2 | 4 | 4 |
| **Crowd Simulation** | Enabled | Enabled | Enabled | Enabled |
| **Hierarchical Pathfinding** | Disabled | Enabled | Enabled | Enabled |
| **LOD Scheduling Policy** | `DistanceBands` | `DistanceBands` | `DistanceBands` | `PriorityAged` |
| **High-Frequency Radius** | 15 m | 25 m | 35 m | 0 (unused) |
| **Medium-Frequency Radius** | 35 m | 60 m | 90 m | 0 (unused) |
| **Low-Frequency Radius** | 60 m | 120 m | 180 m | 0 (unused) |

These values fully initialize every `GameplayAiProfile` field; each column name
is its `profileName`. Built-in profiles are exact defaults; larger hosts use
validated project-defined profiles rather than interpreting `+` as an unbounded
runtime value. `FullRate` disables time-slicing and ignores zero-valued radii.
`DistanceBands` requires strictly increasing positive radii. `PriorityAged`
requires zero radii and uses the weighted fair queue plus deadline aging defined
by ADR-024.

`maxPerceptionQueriesPerTick` caps all costly spatial and physics queries admitted
by `PerceptionSensePoll`. ADR-024's `maxSightRaycastsPerTick` is the LOS-raycast
subset and must not exceed the aggregate cap. Event receipt is free of this query
budget; any resulting LOS, overlap, or spatial lookup consumes one admitted query.

### AI Schedule And Profile Verification

- Lifecycle tests assert exactly six ordered fixed-tick phases and a read-only
  post-commit `RenderExtraction` bridge.
- Host-role tests prove that dedicated servers do not depend on extraction and
  that clients cannot submit authoritative AI work for server-owned agents.
- Profile fixtures initialize every field from the canonical table and reject
  invalid worker counts, radius/policy combinations, and sight-raycast budgets
  above the aggregate perception-query cap.
- Shipping replication schema tests reject private blackboard and perception
  memory fields.

### Graphics Decoupling Invariant

**Architectural Rule**: Graphics backends (`OpenGL`, `Metal`, `Vulkan`, `NullRenderer`) grant **zero** AI capacity, perception query fidelity, or gameplay authority.

- Selecting a higher-end graphics API (e.g. Vulkan vs OpenGL) does NOT increase AI agent limits or perception budgets.
- Headless dedicated servers running with `NullRenderer` operate with full CPU/memory capacity and are not artificially throttled by presentation-tier checks.
- AI budgets scale exclusively with host CPU core counts, worker thread availability, and project-configured memory limits.

## Navigation Resource Guidance

Navigation scaling depends on CPU/worker capacity and measured memory costs, not graphics
features. The canonical `GameplayAiProfile` defaults above remain the authority for AI agent
and obstacle limits. Earlier navigation-only 64/1,024/5,000+ agent and 1/8/32 MB cache figures
were illustrative, not a second set of defaults. Navigation cache, scratch, worker, and queue
limits must be finite validated host/project settings within the shared budgets; tuning
changes require workload, platform, and measurement evidence, not a new ADR.

## Navigation Boundary And Integration Verification

| Case | Required result |
|---|---|
| Api-only consumer uses SceneMath | Builds through Foundation without invented SceneMath target |
| Runtime linked without a concrete provider | Builds; host injection supplies capabilities |
| Runtime-only server without builder/graphics | Real queries/crowd work; no bake or graphics link requirement |
| Direct/transitive vendor include or public vendor type | Boundary negative fixture fails; no ABI leak |
| Concurrent queries, obstacle edits, and carving | Immutable revisions; no shared mutable vendor instance races |
| Unrelated region changes / contributing tile changes | Unrelated cache entries may survive; affected entries and late paths cannot publish stale topology |
| Agent removed, slot reused, or scene replaced during query | InvalidHandle; no mutation of replacement agent |
| Cell eviction while a query holds a lease | Logical removal immediately prevents new/late use; bytes freed only after readers retire and remain budgeted until then |
| World-cell staging fails or wst forced eviction occurs | Shared transaction/rollback and residency authority preserved |
| Null versus real-provider admission, cancellation, publication | Same phase/order contract; Null never returns fake geometry success |
| Different worker completion orders in deterministic mode | Same declared-tick output; no timing-dependent result selection |
| Queue/result capacity exhausted or shutdown begins | Typed admission rejection/cancellation; no orphan job or operation |
| Authoring geometry/settings/dependency/target changes | Correct versioned cook-key invalidation and stable AssetId |
| Cooked version/digest/indices invalid | Typed failure before tile installation |
| Provider allocation failure / destruction | Tracked bounded failure; no leaks, partial publication, or hook-lifetime violation |
| Origin rebase with a query in flight | Dynamic coordinates converted/rejected consistently; static topology identity unchanged |

These are required downstream runtime/CI tests, not tests implemented by this ADR-only change.

## Related Documents

- [ADR-013: Environment Query Ownership, Item and Scoring Model](../../adr/013-environment-query-ownership-item-and-scoring-model.md)

- [ADR-010: Job Waiting and Operation Store Ownership](../../adr/010-job-waiting-and-operation-store-ownership.md)
- [ADR-017: Prefab Asset and Spawn Contracts](../../adr/017-prefab-role-ownership-and-capability-tiers.md)
- [ADR-018: Command Dispatch and World Streaming Diagnostics](../../adr/018-command-registration-permissions-threading-and-packaged-build-policy.md)
- [ADR-023: World Index and Cell Format](../../adr/023-world-index-and-cell-format-architecture-decision.md)
- [ADR-026: Large-World Precision](../../adr/026-large-world-precision-and-floating-origin-strategy.md)
- [Asset Pipeline](./asset-pipeline.md)
- [World Streaming](./world-streaming-architecture.md)
- [Header Visibility and Ownership](../foundation/header-visibility-and-ownership.md)
- [ADR-016: Navigation Target Ownership and Dependency Boundary](../../adr/016-navigation-target-ownership-and-dependency-boundary.md)
- [Navigation Bake UI Reference](./navigation-bake.html)

- [ADR-021: Gameplay AI Ownership, Scheduling and Behavior Boundary](../../adr/021-gameplay-ai-ownership-scheduling-and-behavior-boundary.md)
- [ADR-022: AI Fixed-Tick Order, Authority and Simulation Budget](../../adr/022-ai-fixed-tick-order-authority-and-simulation-budget.md)
- [ADR-024: Perception Ownership, Sense Policy and Budget Decision](../../adr/024-perception-ownership-sense-policy-and-budget.md)
- [ADR-025: AI Decision Assets and Shared Gameplay Behavior Boundary](../../adr/025-ai-decision-assets-and-gameplay-behavior-boundary.md)
- [Runtime Lifecycle Architecture](./runtime-lifecycle.md): Fixed-tick simulation loop and interpolation model
- [Multiplayer Replication Architecture](./multiplayer-replication-architecture.md): Server authority, RPCs, and property replication
- [Networking Architecture](./networking-architecture.md): Network transport and I/O threading
- [Gameplay Behavior Authoring](../extensions/gameplay-behavior-authoring.md): Behavior tree and state machine authoring
- [Physics Architecture](./physics-architecture.md): Collision geometry and character controller locomotion
- [World Streaming Architecture](./world-streaming-architecture.md): NavMesh tile streaming
- [Scene Runtime](./scene-runtime.md): Agent entity and component model
- [Concurrency And Jobs](../foundation/concurrency-and-jobs.md): Parallel crowd and perception jobs
- [Save Game And Persistence](./save-game-and-persistence.md): durable perception-memory capture and staged restore
- [Navigation Bake UI HTML Reference](./navigation-bake.html): non-normative
  static UI reference
- [Debug Console And Overlays](./debug-console-and-overlays.md): AI debug visualization
