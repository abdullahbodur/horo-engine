# ADR-011: World Streaming Partition Authority and Subsystem Boundaries

- **Status**: Proposed
- **Date**: 2026-08-28
- **Supersedes**: None
- **Scope**: World spatial partitioning, cell residency lifecycle, subsystem boundaries (World Streaming, Scene Runtime, Asset Pipeline, Feature Providers, Editor Authoring), budget enforcement, and cancellation/shutdown contracts
- **Issue**: [#1528](https://github.com/abdullahbodur/horo-engine/issues/1528) ([WST-001.1])
- **Normative document**: [World Streaming Architecture](../architecture/runtime/world-streaming-architecture.md)

## Context

`docs/architecture/runtime/world-streaming-architecture.md` defines a cell-based streaming model for large open worlds. It introduces spatial partitioning, asynchronous cell loading and unloading, streaming volumes, priority calculations, budget sub-allocations, and server-authoritative multiplayer streaming.

However, as the engine scales toward M0 architecture baselines and M1/M2 implementation milestones ([WST-001]), the boundary between World Streaming and adjacent engine subsystems requires strict, unambiguous specification:

- **Partition Authority**: The subsystem owning the 2D/3D spatial partitioning grid, coordinate-to-cell spatial indices, streaming volume queries, residency state machine, and total memory/frame-time budgets must be canonically designated. Without a single partition authority, feature subsystems (e.g., Terrain, Foliage, Physics, Audio) or Scene Runtime risk independently evaluating cameras or triggering uncontrolled asset loads, causing duplicate I/O, budget thrashing, and race conditions.
- **Subsystem Boundary Isolation**:
  - *Scene Runtime* owns ECS entity and component lifecycles, transform hierarchies, and system ticks via `RuntimeScene`. It must not own spatial partitioning or volume evaluation.
  - *Asset Pipeline* owns asset cooking, chunked archive packaging (`assets.horo`), deduplication, and asynchronous I/O with `CancellationToken`. It must not track camera frustums or game streaming logic.
  - *Feature Providers* (Terrain, Foliage, Physics, Audio, PCG, Navigation) own feature-local caches, spatial data structures, and device buffers. They must participate in streaming by subscribing to typed lifecycle events and operating within sub-allocated budgets rather than running independent spatial paging loops.
  - *Editor Authoring* owns scene document persistence, multi-layer authoring, and offline partition baking. It must not experience runtime streaming thrash or background cell eviction while an author is actively editing a partitioned level.
- **Residency Lifecycle Precision**: The previous 3-state runtime baseline (`Loading`, `Loaded`, `Unloading`) is insufficient for complex engine composition. Loading payloads from disk into memory, staging feature-local buffers, and activating entities into the tickable ECS simulation are distinct phases. A 5-state lifecycle (`Unloaded`, `Loading`, `Resident`, `Active`, `Evicting`) plus `Failed` is required to decouple background asset staging from active scene simulation.
- **Typed Identity and Concurrency Fencing**: Async I/O completions, worker task cancellations, and rapid player movement across cell boundaries can produce out-of-order state transitions. Strongly typed identities (`StreamingCellId`, `WorldPartitionId`, `StreamingVolumeId`, `StreamingLayerId`) and monotonic `StreamingGeneration` counters are necessary to fence stale async completions and eliminate race conditions.
- **Error Model and Fallible Operations**: Public and internal streaming APIs must conform to [ADR-008](../adr/008-error-model-exception-boundary-and-registry.md) using typed `Result<T, Error>` with a dedicated `WorldStreaming` error domain, returning explicit diagnostic codes without hiding partial state.

[WST-001.1] requires a formal architectural decision to establish the canonical partition authority, ratify subsystem boundaries, codify the residency state machine, and define failure, cancellation, replacement, and shutdown lifecycles.

## Decision

**`WorldStreamingManager` (operating as the `StreamingPartitionAuthority`) is the sole canonical authority for world spatial partitioning, streaming volume evaluation, cell residency state transitions, and global streaming budget enforcement. Adjacent subsystems (Scene Runtime, Asset Pipeline, Feature Providers, Editor Authoring) interact with World Streaming across explicit, typed boundaries via typed IDs, lifecycle observer contracts, and fallible `Result<T, Error>` returns. Stale asynchronous completions are fenced via monotonic `StreamingGeneration` counters.**

---

### Ratify-Or-Revise Outcomes

| Area | Current State | Outcome |
|---|---|---|
| **Partition Authority** | Implicitly distributed across streaming volumes and cell queries | **Ratified and consolidated.** `StreamingPartitionAuthority` is the single source of truth for grid topology, cell coordinates, volume queries, and budget enforcement. |
| **Residency State Machine** | 3 active states (`Loading`, `Loaded`, `Unloading`) + `Failed` | **Revised to 5 canonical states.** `Unloaded`, `Loading`, `Resident`, `Active`, `Evicting` (+ `Failed`). Staging (`Resident`) is explicitly decoupled from simulation activation (`Active`). |
| **Generation Fencing** | Unversioned cell requests; cancellation relies solely on token | **Revised.** Added monotonic `StreamingGeneration` per cell/partition. Async worker completions must match the active generation or be discarded without mutating state. |
| **Subsystem Boundaries** | Feature subsystems (Terrain, Foliage) have ad hoc budget mentions | **Ratified and formalized.** All feature subsystems (Terrain, Foliage, Physics, Audio, PCG, Navigation) implement `IStreamingFeatureProvider` and respond to lifecycle events within sub-allocated budgets. |
| **Scene Runtime Integration** | Integrated via generic scene model handoff | **Formalized.** Scene Runtime exposes transactional structural command batches (`Create`/`Destroy`). World Streaming submits entity definitions without owning ECS storage or systems. |
| **Asset Pipeline Integration** | Async I/O with `CancellationToken` | **Ratified.** Asset Pipeline serves chunked streaming packages (`.horopkg`/`assets.horo`). Cell loads use bounded I/O queues and cooperative cancellation. |
| **Editor Authoring Boundary** | Authoring layers and baking commands mixed with runtime preview | **Formalized.** Authoring documents retain full unstreamed level geometry during edits. Runtime streaming is disabled during authoring; preview is an explicit isolated mode. |
| **Typed Error Model** | Ad hoc boolean or void returns in early drafts | **Formalized.** All fallible operations return `Result<T, Error>` with stable `ErrorCode` values under the `WorldStreaming` error domain per ADR-008. |

---

### Subsystem Ownership and Boundary Map

```
┌──────────────────────────────────────────────────────────────────────────────────┐
│                             Application Composition Root                         │
│                    (Owns lifetime, configures budgets, wires hosts)              │
└────────────┬─────────────────────────────────┬─────────────────────────────────┬─┘
             │                                 │                                 │
             ▼                                 ▼                                 ▼
┌─────────────────────────┐       ┌─────────────────────────┐       ┌─────────────────────────┐
│     Asset Pipeline      │       │ World Streaming System  │       │      Scene Runtime      │
│     (HoroAssets)        │       │      (HoroRuntime)      │       │      (HoroRuntime)      │
│  - Chunked cell packages│◄──────┤StreamingPartitionAuth.  ├──────►│  - RuntimeScene storage│
│  - Async I/O & Cooker   │  I/O  │- Spatial grid & volumes │ Scene │  - ECS entity & comp.   │
│  - CancellationToken    │ Tokens│- Residency state machine│Batch  │  - Systems & transforms │
└─────────────────────────┘       │- Budget & priority mgmt │       └─────────────────────────┘
                                  └────────────┬────────────┘
                                               │
                                 Lifecycle     │ Events (Loading, Resident,
                                 Observer      │ Active, Evicting, Unloaded)
                                               ▼
                         ┌──────────────────────────────────────────┐
                         │            Feature Providers             │
                         │       (IStreamingFeatureProvider)        │
                         ├────────────────────┬─────────────────────┤
                         │ Terrain: Clipmaps  │ Physics: Static Body│
                         │ Foliage: Instances │ Audio: Emitters     │
                         │ PCG: Point Caches  │ NavMesh: Tile Grids │
                         └────────────────────┴─────────────────────┘
```

The table below defines the strict ownership rules across subsystems:

| Subsystem | Primary Responsibility | Explicit Boundaries & Prohibitions |
|---|---|---|
| **World Streaming** (`StreamingPartitionAuthority` / `WorldStreamingManager`) | Owns spatial partitioning grid, volume spatial queries, priority sorting, cell residency state machine, generation fencing, and global memory/time budget allocation. | **Must not** directly mutate ECS archetype tables, execute gameplay systems, read raw disk files directly (must use `AssetLoadService`), or perform rendering. |
| **Scene Runtime** (`RuntimeScene`, `RuntimeSceneService`) | Owns ECS entity/component storage, transform hierarchy, system scheduling, and structural scene mutations (`CommitDeferredLifecycleChanges`). | **Must not** track spatial streaming grids, evaluate camera streaming volumes, or determine cell priority/budgets. Instantiates entities only upon transactional command batches from World Streaming. |
| **Asset Pipeline** (`AssetRegistry`, `AssetLoadService`, `AssetCooker`) | Owns cooking partitioned cell assets into chunked packages (`assets.horo`), verifying package hashes, managing I/O queues, and fulfilling asynchronous streaming asset reads bounded by `CancellationToken`. | **Must not** inspect gameplay camera locations, decide cell load priorities, or instantiate runtime ECS entities. |
| **Feature Providers** (Terrain, Foliage, Physics, Audio, PCG, Navigation) | Own subsystem-specific spatial caches and GPU/driver resources (clipmaps, instance clusters, static colliders, audio banks, PCG point caches, nav tiles) within sub-allocated memory reservations. | **Must not** trigger independent streaming cell loads, define cell boundaries, or override partition residency state. Must implement `IStreamingFeatureProvider` and respond to authority lifecycle events. |
| **Editor Authoring** (`SceneDocument`, `WorldCompositionTab`, `ViewportOverlay`) | Owns authoring documents, multi-layer level composition, cell bounds authoring, manual cell loading/pinning for editing, and offline cell baking (`BakeStreamingCells`). | **Must not** run active runtime eviction during authoring sessions. Runtime streaming simulation occurs only in explicit Play/Preview modes. |

---

### Residency State Machine

Every partition cell tracked by the `StreamingPartitionAuthority` transitions through a deterministic, generation-fenced lifecycle:

```
                  ┌──────────────────────────────────────────────────┐
                  │                                                  │
                  ▼                                                  │
 ┌─────────────────────────────────┐                                 │
 │            Unloaded             │                                 │
 └────────────────┬────────────────┘                                 │
                  │ [Volume overlap & budget admitted]               │
                  ▼                                                  │
 ┌─────────────────────────────────┐                                 │
 │             Loading             │                                 │
 └───────┬─────────────────┬───────┘                                 │
         │                 │                                         │
         │ [Payloads ready]│ [I/O failure / Retries exhausted]       │
         ▼                 ▼                                         │
 ┌───────────────┐ ┌───────────────┐                                 │
 │   Resident    │ │    Failed     │                                 │
 └───────┬───────┘ └───────┬───────┘                                 │
         │                 │ [Manual retry / Volume change cooldown] │
         │ [Activate]      └─────────────────► (To Loading)          │
         ▼                                                           │
 ┌───────────────┐                                                   │
 │    Active     │                                                   │
 └───────┬───────┘                                                   │
         │ [Volume left + linger expired / Memory pressure]          │
         ▼                                                           │
 ┌───────────────┐                                                   │
 │   Evicting    │                                                   │
 └───────┬───────┘                                                   │
         │ [Deactivated & resources freed]                           │
         └───────────────────────────────────────────────────────────┘
```

#### State Definitions and Invariants:

1. **`Unloaded`**: Zero runtime memory or GPU resources allocated. Cell descriptor exists in the spatial partition index.
2. **`Loading`**: Async asset I/O is in flight via `AssetLoadService` under an active `CancellationToken`. Feature providers receive `OnCellLoading` to prepare descriptors.
   - *Cancellation Rule*: If covering volumes release the cell before loading finishes, `CancellationToken` is cancelled immediately, in-flight I/O is aborted, and state reverts to `Unloaded`.
3. **`Resident`**: Cell asset payload is fully loaded and validated in memory. Feature providers stage data (e.g., terrain clipmaps, foliage buffers) via `OnCellResident`. Entities and components are prepared as immutable candidate definitions but are **not yet inserted** into the active ECS tick loop.
4. **`Active`**: Entities are instantiated into `RuntimeScene` via transactional batch mutation (`OnCellActive`). Physics colliders are added to the simulation world; spatial audio emitters and gameplay scripts are active.
5. **`Evicting`**: Entities are removed from `RuntimeScene` via structural batch deletion. Feature providers tear down collision meshes, audio voices, and GPU instances via `OnCellEvicting`. Once all dependencies confirm de-allocation, the cell transitions to `Unloaded`.
6. **`Failed`**: Loading encountered an unrecoverable error (e.g., corrupt package, missing asset dependency) after exhausting configured retries (`maxRetries`). Stays in `Failed` until explicit editor retry or volume cooldown trigger.

---

### Typed Identity, Contracts, and Fencing

#### 1. Typed Identities
All partition structures use strongly typed value wrappers to eliminate string parsing and primitive obsession:

```cpp
namespace Horo::Runtime::Streaming {

struct WorldPartitionId {
    uint64_t value{0};
    constexpr explicit WorldPartitionId(uint64_t v) : value(v) {}
    constexpr bool IsValid() const noexcept { return value != 0; }
    constexpr auto operator<=>(const WorldPartitionId&) const = default;
};

struct StreamingLayerId {
    uint32_t value{0};
    constexpr explicit StreamingLayerId(uint32_t v) : value(v) {}
    constexpr bool IsValid() const noexcept { return value != 0; }
    constexpr auto operator<=>(const StreamingLayerId&) const = default;
};

struct StreamingCellCoord {
    int32_t x{0};
    int32_t y{0};
    int32_t z{0};
    constexpr auto operator<=>(const StreamingCellCoord&) const = default;
};

struct StreamingCellId {
    WorldPartitionId   partition{0};
    StreamingLayerId   layer{0};
    StreamingCellCoord coord{};
    constexpr auto operator<=>(const StreamingCellId&) const = default;
};

struct StreamingGeneration {
    uint64_t value{1};
    constexpr explicit StreamingGeneration(uint64_t v) : value(v) {}
    constexpr StreamingGeneration Next() const noexcept { return StreamingGeneration{value + 1}; }
    constexpr auto operator<=>(const StreamingGeneration&) const = default;
};

struct StreamingVolumeId {
    uint64_t value{0};
    constexpr explicit StreamingVolumeId(uint64_t v) : value(v) {}
    constexpr bool IsValid() const noexcept { return value != 0; }
    constexpr auto operator<=>(const StreamingVolumeId&) const = default;
};

} // namespace Horo::Runtime::Streaming
```

#### 2. Generation Fencing Invariant
Every asynchronous operation captures the target `StreamingCellId` and its current `StreamingGeneration`. When an asynchronous worker completes:
- The authority checks `cell.currentGeneration == capturedGeneration`.
- If equal, the state transition (`Loading -> Resident`, etc.) commits.
- If not equal (due to rapid cancel/re-request cycles or world replacement), the worker completion is safely discarded with zero mutation to active scene state.

#### 3. Feature Provider Lifecycle Contract
All streaming-aware subsystems implement `IStreamingFeatureProvider`:

```cpp
namespace Horo::Runtime::Streaming {

class IStreamingFeatureProvider {
public:
    virtual ~IStreamingFeatureProvider() = default;

    [[nodiscard]] virtual std::string_view GetProviderName() const noexcept = 0;

    virtual void OnCellLoading(
        StreamingCellId cellId,
        StreamingGeneration generation,
        const CellPayloadDescriptor& payload,
        Foundation::CancellationToken cancelToken) = 0;

    virtual void OnCellResident(
        StreamingCellId cellId,
        StreamingGeneration generation) = 0;

    virtual void OnCellActive(
        StreamingCellId cellId,
        StreamingGeneration generation) = 0;

    virtual void OnCellEvicting(
        StreamingCellId cellId,
        StreamingGeneration generation) = 0;

    virtual void OnCellUnloaded(
        StreamingCellId cellId,
        StreamingGeneration generation) = 0;
};

} // namespace Horo::Runtime::Streaming
```

---

### Error Handling and Diagnostics

All fallible operations return `Horo::Result<T, Error>` governed by ADR-008. The dedicated `WorldStreaming` error domain defines stable error codes:

| Error Code | Meaning | Recovery / Lifecycle Action |
|---|---|---|
| `CellNotFound` | Cell coordinate or ID is outside the configured partition bounds. | Request rejected immediately; volume query ignores invalid coordinates. |
| `CellAlreadyActive` | An activation request was submitted for a cell already in `Active` state. | No-op; returns current status. |
| `CellAlreadyLoading` | Load requested for a cell already in `Loading` state. | No-op; returns current status. |
| `CellBudgetExceeded` | Memory or concurrency budget exhausted; cannot admit new cell load. | Request deferred to next frame queue evaluation; anti-starvation boost applied. |
| `StaleGeneration` | An asynchronous completion payload arrived with an outdated generation. | Completion discarded without state mutation. |
| `ProviderFailed` | A feature provider (e.g., Terrain or Physics) failed to stage or activate a cell. | Cell transitions to `Failed` state; diagnostic emitted with provider context. |
| `VolumeNotFound` | Attempted mutation or query on an unregistered `StreamingVolumeId`. | Query returns empty cell set; operation returns error. |
| `InvalidCoordinate` | Coordinates contain NaN, infinity, or overflow partition bounds. | Input rejected with validation diagnostic. |
| `Cancelled` | Streaming operation was aborted due to volume departure or world teardown. | In-flight I/O cancelled; resources released. |

---

### Concurrency, Replacement, and Shutdown Semantics

1. **Thread Role Discipline**:
   - `WorldStreamingManager` evaluates volumes, calculates priorities, and issues state transitions exclusively on the `MainEditor` / main simulation thread at explicit frame synchronization points.
   - Heavy decoding, package decompression, and asset deserialization execute on `Worker` threads via the `JobSystem` using cooperative `CancellationToken`.
   - Structural ECS entity creation/deletion commits on the main scene thread during `CommitDeferredLifecycleChanges`.
2. **World Replacement**:
   - When a new world is loaded or a scene transition occurs, the active partition generation is incremented.
   - All in-flight streaming jobs are cancelled via their tokens.
   - All `Active` and `Resident` cells are batched into `Evicting` and transitioned to `Unloaded`.
   - Feature providers receive bulk eviction notifications.
3. **Graceful Shutdown**:
   - Streaming volume evaluation halts immediately.
   - All pending load queues are cleared.
   - In-flight task groups are cancelled and joined with a bounded timeout.
   - Active cells are synchronously torn down in reverse dependency order: Feature Providers -> Scene Entities -> Partition Authority -> Asset Pipeline.

---

## Consequences

### Positive
- **Single Source of Truth**: Eliminates conflicting spatial queries and duplicate streaming implementations across subsystems.
- **Race Safety**: Monotonic `StreamingGeneration` counters fence asynchronous completions, eliminating race conditions during fast movement or world transitions.
- **Clean Subsystem Decoupling**: Scene Runtime, Asset Pipeline, Feature Providers, and Editor Authoring have crisp, non-overlapping responsibilities.
- **Staging vs Simulation Separation**: The 5-state lifecycle prevents frame-time hitches by allowing background staging (`Resident`) prior to synchronized entity activation (`Active`).
- **Standardized Error Handling**: Conforms strictly to ADR-008 with typed error codes and diagnostics.

### Negative / Trade-offs
- **Registration Overhead**: Feature subsystems must register lifecycle observers and manage local sub-budgets.
- **State Complexity**: Moving from 3 to 5 lifecycle states requires strict state transition validation and thorough test coverage across all edge cases.

---

## Rejected Alternatives

1. **Decentralized Streaming (Feature Subsystems Poll Cameras Directly)**:
   - *Rejected*: Letting Terrain, Foliage, Physics, and Audio independently track cameras and stream assets leads to uncoordinated I/O thrashing, double-counted memory budgets, and split-brain partition state.
2. **Embedding Spatial Partitioning in Scene Runtime**:
   - *Rejected*: `RuntimeScene` is an ECS simulation container. Coupling spatial grids and volume evaluation directly to ECS archetypes introduces circular dependencies and prevents headless or non-streamed scenes from reusing the ECS engine cleanly.
3. **Immediate Entity Activation on I/O Completion**:
   - *Rejected*: Activating entities directly from worker I/O completion threads violates ECS main-thread synchronization rules and causes frame-time spikes. The `Resident` state staging barrier is mandatory.
4. **String-Based Cell and Layer Identifiers**:
   - *Rejected*: String identifiers cause heap allocations, slow hash lookups in frame-hot loops, and risk spelling drift. Strongly typed 64-bit IDs and coordinate structs are mandatory.
