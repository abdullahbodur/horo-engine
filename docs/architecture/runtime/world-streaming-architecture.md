# World Streaming Architecture

## Purpose

This document defines the normative architecture for the world-streaming subsystem in Horo Engine. It establishes:

- The canonical spatial partition authority (`StreamingPartitionAuthority` / `WorldStreamingManager`)
- Crisp subsystem boundaries between World Streaming, Scene Runtime, Asset Pipeline, Feature Providers, and Editor Authoring
- Stable typed identities, monotonic generation fencing, and fallible result contracts
- The 5-state cell residency lifecycle (`Unloaded`, `Loading`, `Resident`, `Active`, `Evicting`) plus `Failed`
- Streaming volume evaluation, priority ranking, and anti-starvation mechanics
- Global and sub-allocated memory and frame-time budget enforcement
- Concurrency, cancellation, world replacement, and graceful shutdown invariants
- Server-authoritative networking and isolated editor authoring integration

---

## Subsystem Boundaries and Ownership

To prevent split-brain partition state, race conditions, and uncontrolled memory churn, the engine defines strict ownership boundaries across five cooperating subsystems:

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

### Subsystem Responsibility Matrix

| Subsystem | Primary Ownership & Responsibilities | Prohibitions & Explicit Non-Responsibilities |
|---|---|---|
| **World Streaming** (`StreamingPartitionAuthority` / `WorldStreamingManager`) | Owns spatial grid topology, coordinate quantization, streaming volume evaluation, priority ranking, cell residency state machine, generation fencing, and global budget allocation. | **Must not** directly mutate ECS archetype storage, execute gameplay systems, read raw disk files directly (must use `AssetLoadService`), or perform rendering. |
| **Scene Runtime** (`RuntimeScene`, `RuntimeSceneService`) | Owns ECS entity/component storage, transform hierarchies, system scheduling, and structural scene mutations (`CommitDeferredLifecycleChanges`). | **Must not** track spatial streaming grids, evaluate camera streaming volumes, or determine cell priority/budgets. Instantiates entities only upon transactional command batches from World Streaming. |
| **Asset Pipeline** (`AssetRegistry`, `AssetLoadService`, `AssetCooker`) | Owns cooking partitioned cell assets into chunked packages (`assets.horo`), verifying package hashes, managing I/O queues, and fulfilling asynchronous streaming asset reads bounded by `CancellationToken`. | **Must not** inspect gameplay camera locations, decide cell load priorities, or instantiate runtime ECS entities. |
| **Feature Providers** (Terrain, Foliage, Physics, Audio, PCG, Navigation) | Own subsystem-specific spatial caches and GPU/driver resources (clipmaps, instance clusters, static colliders, audio banks, PCG point caches, nav tiles) within sub-allocated memory reservations. | **Must not** trigger independent streaming cell loads, define cell boundaries, or override partition residency state. Must implement `IStreamingFeatureProvider` and respond to authority lifecycle events. |
| **Editor Authoring** (`SceneDocument`, `WorldCompositionTab`, `ViewportOverlay`) | Owns authoring documents, multi-layer level composition, cell bounds authoring, manual cell loading/pinning for editing, and offline cell baking (`BakeStreamingCells`). | **Must not** run active runtime eviction during authoring sessions. Runtime streaming simulation occurs only in explicit Play/Preview modes. |

---

## Spatial Partitioning Model

Large worlds use a deterministic, cell-based spatial partitioning model:

- The world is divided into a regular grid of streaming cells in 2D or 3D world space.
- A world dataset is identified by a `WorldPartitionId`.
- Each cell is bounded by a deterministic axis-aligned bounding box (AABB) computed from `(x, y, z)` grid coordinates and a configurable `cellSize` (meters per side, default 128.0m or 256.0m).
- World content is partitioned into **streaming layers** (e.g., persistent core, gameplay objects, decorative foliage, audio triggers).
- Foliage clusters and static primitives are **cell-aligned**: bounding boxes fit entirely within a single cell. Objects crossing cell boundaries are split during the asset bake step into per-cell chunks to prevent cross-cell load dependencies.

```cpp
namespace Horo::Runtime::Streaming {

struct StreamingCellCoord {
    int32_t x{0};
    int32_t y{0};
    int32_t z{0};

    constexpr auto operator<=>(const StreamingCellCoord&) const = default;
};

struct StreamingCell {
    StreamingCellId        id;
    WorldCoordinate        origin;          // world-space minimum bounding corner
    float                  cellSize;        // meters per side
    StreamingCellState     state;           // Unloaded, Loading, Resident, Active, Evicting, Failed
    StreamingGeneration    generation;      // active generation counter for concurrency fencing
    AssetId                cellAsset;       // baked scene chunk asset for this cell
    StreamingPriority      priority;        // cached computed priority, updated each frame
    float                  lingerTimer;     // elapsed linger time after leaving all volumes
    uint32_t               retryCount;      // remaining load retry attempts
};

} // namespace Horo::Runtime::Streaming
```

`priority` is a cached value updated per frame during volume evaluation. Storing it in `StreamingCell` allows the scheduler to sort load and eviction queues efficiently without re-evaluating every volume for every cell during scheduling.

---

## Stable Typed Identities

To eliminate string comparisons, loose maps, and numeric collisions across threads and subsystems, World Streaming uses strongly typed value wrappers:

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

struct StreamingPriority {
    float value{0.0f};
    constexpr explicit StreamingPriority(float v) : value(v) {}
    constexpr auto operator<=>(const StreamingPriority&) const = default;
};

} // namespace Horo::Runtime::Streaming
```

---

## Streaming Volumes and Priority Evaluation

Streaming volumes dictate which cells must be resident and active:

- **Camera Volume**: Spherical or frustum-aligned volume following the active camera or listener.
- **Gameplay Volumes**: Static or trigger-attached volumes dynamically activated by gameplay scripts.
- **Network Relevance Volumes**: Server-directed relevance spheres for multiplayer clients.
- **Preload Volumes**: Permanent pin volumes holding critical areas (spawn points, UI cutscene hubs) in memory regardless of camera movement.

```cpp
namespace Horo::Runtime::Streaming {

enum class StreamingVolumeType : uint8_t {
    Camera,
    Gameplay,
    NetworkRelevance,
    Preload
};

struct StreamingVolume {
    StreamingVolumeId   id;
    StreamingVolumeType type{StreamingVolumeType::Camera};
    WorldCoordinate     origin{0.0f, 0.0f, 0.0f};
    float               radius{256.0f};
    StreamingPriority   basePriority{1.0f};
    bool                unloadOutside{true}; // true = release when left; false = pin loaded
    bool                active{true};
};

} // namespace Horo::Runtime::Streaming
```

### Priority Formulation

Each frame, `StreamingPartitionAuthority` evaluates active volumes against the partition grid. For each cell overlapping at least one active volume, priority is computed as:

$$\text{priority} = \max_{v \in \text{Volumes}} \left( \frac{1.0}{\text{distance}(c, v) + \varepsilon} \times M_{\text{type}}(v) \times M_{\text{override}}(c) \right) + \text{boost}_{\text{anti-starvation}}(c)$$

Where:

- $\text{distance}(c, v)$: Euclidean distance in meters from cell center to volume origin.
- $M_{\text{type}}$: Volume type multiplier (Preload: 1.5, Camera: 1.0, Gameplay: 0.9, Network: 0.8).
- $M_{\text{override}}$: Explicit gameplay priority multiplier (0.5 to 2.0).
- $\text{boost}_{\text{anti-starvation}}$: Linear increase proportional to seconds spent waiting in the pending load queue. Prevents distant cells from starving indefinitely under continuous camera translation.

### Multi-Volume Aggregation and Linger Policy

- When multiple volumes cover the same cell, the **maximum computed priority** across all covering volumes is assigned.
- If a cell is covered by at least one volume with `unloadOutside == false` (pin), the cell is pinned and will never be scheduled for eviction.
- When all covering volumes release a cell, it enters a **linger phase** governed by `StreamingBudget::unloadLingerSeconds`. If no volume re-enters the cell before the linger timer expires, the cell is scheduled for eviction. This prevents high-frequency load/unload thrashing along cell boundaries.

---

## Residency State Machine and Concurrency Fencing

Every partition cell transitions through a deterministic, generation-fenced state machine:

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

### State Definitions and Invariants

```cpp
namespace Horo::Runtime::Streaming {

enum class StreamingCellState : uint8_t {
    Unloaded,   // No CPU/GPU resources allocated
    Loading,    // Async I/O and worker decode in flight; cancellation token active
    Resident,   // Assets loaded in memory; feature resources staged; not in ECS tick
    Active,     // Entities instantiated in Scene Runtime; physics & audio active
    Evicting,   // Entities and feature resources tearing down; references clearing
    Failed      // Terminal load failure after exhausting retries
};

} // namespace Horo::Runtime::Streaming
```

1. **`Unloaded`**: Zero runtime memory or GPU resources allocated. Cell descriptor exists in the spatial partition index.
2. **`Loading`**: Async asset I/O is scheduled with `AssetLoadService` under an active `CancellationToken`. Feature providers receive `OnCellLoading` to prepare descriptor references.
   - *Cancellation Invariant*: If covering volumes release the cell before loading completes, `CancellationToken` is cancelled immediately, in-flight I/O is aborted, and state reverts to `Unloaded`.
3. **`Resident`**: Cell asset payload is fully loaded and validated in memory. Feature providers stage data (clipmaps, instance buffers, collision geometry) via `OnCellResident`. Entities and components are prepared as candidate definitions but are **not yet inserted** into the active ECS simulation loop.
4. **`Active`**: Entities are instantiated into `RuntimeScene` via transactional batch mutation (`OnCellActive`). Physics colliders are added to the simulation world; spatial audio emitters and gameplay scripts are active.
5. **`Evicting`**: Entities are removed from `RuntimeScene` via structural batch deletion. Feature providers tear down collision meshes, audio voices, and GPU instances via `OnCellEvicting`. Once all dependencies confirm de-allocation, the cell transitions to `Unloaded`.
6. **`Failed`**: Loading encountered an unrecoverable error (e.g., corrupt package, missing asset dependency) after exhausting configured retries (`maxRetries`). Stays in `Failed` until explicit editor retry or volume cooldown trigger.

### Generation Fencing Invariant

Asynchronous workers, I/O completions, and cancellation tokens can deliver results out of order when a player rapidly crosses cell borders:

1. When a cell initiates a state transition (e.g., `Unloaded -> Loading`), the authority increments `cell.generation` and passes this `StreamingGeneration` to the asynchronous job.
2. When the asynchronous task completes, it delivers its payload alongside the captured `StreamingGeneration`.
3. The `StreamingPartitionAuthority` verifies:
   $$\text{capturedGeneration} == \text{cell.generation}$$
4. If the generations match, the state transition is committed.
5. If the generations do not match (due to cancellation, world replacement, or rapid reload), the completion payload is **immediately discarded** with zero mutation to active scene state.

---

## Feature Provider Lifecycle Contract

All streaming-aware subsystems implement `IStreamingFeatureProvider` and register with `WorldStreamingManager`:

```cpp
namespace Horo::Runtime::Streaming {

struct CellPayloadDescriptor {
    StreamingCellId        cellId;
    AssetId                sceneAsset;
    AssetId                terrainAsset;
    AssetId                foliageAsset;
    AssetId                navMeshAsset;
    AssetId                physicsAsset;
};

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

### Subsystem Feature Integration Rules

- **Terrain System**: In `OnCellResident`, fetches heightfield tiles from the asset service and constructs GPU clipmap vertex/index buffers. In `OnCellActive`, binds heightfield colliders into the physics world. In `OnCellEvicting`, unbinds colliders and releases tile buffers.
- **Foliage System**: In `OnCellResident`, decodes foliage instance clusters and populates GPU instance buffers. In `OnCellActive`, registers clusters with the render extraction graph. In `OnCellEvicting`, unregisters clusters and marks instance buffers for recycling.
- **Physics Subsystem**: In `OnCellActive`, registers static concave meshes and rigid bodies into the active `PhysicsScene`. In `OnCellEvicting`, removes bodies and triggers sleeping island re-evaluation.
- **Audio Subsystem**: In `OnCellResident`, loads localized audio soundbanks. In `OnCellActive`, spawns spatial emitters. In `OnCellEvicting`, fades out and stops active cell voices before releasing soundbanks.
- **Procedural Generation (PCG)**: In `OnCellLoading`, runs deterministic point-cloud evaluation if not baked. In `OnCellResident`, stages generated components. In `OnCellActive`, spawns PCG entities into the active scene.

---

## Global and Sub-Allocated Streaming Budgets

To guarantee smooth frame times and prevent out-of-memory crashes on memory-constrained platforms, streaming enforces hierarchical budgets:

```cpp
namespace Horo::Runtime::Streaming {

struct StreamingBudget {
    uint32_t  maxConcurrentLoads{4};
    uint32_t  maxConcurrentUnloads{4};
    size_t    maxResidentMemoryMB{1024};
    float     maxFrameTimeMS{2.0f};          // max main-thread budget for streaming work per frame
    float     unloadLingerSeconds{5.0f};     // delay before evicting cells that left all volumes
    size_t    terrainMemoryReservationMB{256};
    size_t    foliageMemoryReservationMB{256};
    size_t    physicsMemoryReservationMB{128};
    size_t    audioMemoryReservationMB{64};
};

} // namespace Horo::Runtime::Streaming
```

### Budget Accounting Invariants

1. **Sub-Allocation Hierarchy**: `terrainMemoryReservationMB`, `foliageMemoryReservationMB`, `physicsMemoryReservationMB`, and `audioMemoryReservationMB` are carved out of `maxResidentMemoryMB`.
2. **General Payload Pool**: The remainder:
   $$\text{generalPoolMB} = \text{maxResidentMemoryMB} - \sum \text{reservations}$$
   is allocated for Scene Runtime ECS entities, meshes, and textures.
3. **Internal Subsystem Eviction**: Feature providers manage internal caches within their sub-allocation. If a feature cache nears capacity, the provider evicts internal LOD levels or staging buffers without altering cell residency states.
4. **Admission Control**: `StreamingPartitionAuthority` checks both total memory and concurrency limits before transitioning an `Unloaded` cell to `Loading`. If admitting a cell would breach `maxResidentMemoryMB`, lower-priority `Resident` (inactive) or lingering cells are evicted first.

---

## World Composition and Authoring Isolation

World Composition layers organize scene content by operational domain:

- **Persistent Layer**: Always-loaded core entities (sky, sun, global audio listener, game mode rules).
- **Streaming Layers**: Spatially partitioned cell content (static geometry, terrain, props, NPC spawners).
- **Dynamic Layers**: Runtime-spawned entities (projectiles, player characters, dynamic VFX).

```cpp
namespace Horo::Runtime::Streaming {

enum class WorldLayerOwnership : uint8_t {
    WorldStreaming,       // Managed by partition authority
    GameplayScript,       // Managed by gameplay lifecycle
    NetworkReplication    // Managed by networking sync
};

} // namespace Horo::Runtime::Streaming
```

### Editor Authoring Isolation vs Runtime Streaming

- **Authoring Mode**: During level editing in `HoroEditor`, runtime cell streaming and background eviction are **disabled**. The authoring document (`SceneDocument`) retains the full level hierarchy in memory or pins requested cells to prevent unexpected eviction while authoring.
- **Offline Cell Baking**: The Build menu provides `BakeStreamingCells`. The cooker reads authored spatial layers, slices geometry and foliage clusters along cell boundaries, creates chunked package archives (`assets.horo`), and writes partition metadata.
- **Streaming Preview Mode**: An isolated viewport overlay that simulates player camera movement and color-codes cells by residency state (`Unloaded`, `Loading`, `Resident`, `Active`, `Evicting`, `Failed`) without modifying authoring documents.

---

## Networking and Server Authority

In multiplayer environments, streaming authority resides on the server:

- The server evaluates player positions and calculates cell relevance per connected peer.
- The server issues explicit cell load/unload replication commands to clients.
- Clients execute streaming requests locally as directed by server commands.
- **Per-Peer Budget Scoping**:
  - The dedicated server maintains a global world view with a higher server memory budget.
  - Clients enforce their own local `StreamingBudget` for rendering and audio assets. The server directs relevance but does not manage client-side GPU buffers.

---

## Error Handling and Diagnostics

All fallible streaming operations conform to [ADR-008](../../adr/008-error-model-exception-boundary-and-registry.md) using typed `Horo::Result<T, Error>` returns and the `WorldStreaming` error domain:

```cpp
namespace Horo::Runtime::Streaming {

enum class WorldStreamingErrorCode {
    CellNotFound,           // Cell coordinate or ID is outside partition bounds
    CellAlreadyActive,      // Activation requested for an already active cell
    CellAlreadyLoading,     // Load requested for a cell already in Loading state
    CellBudgetExceeded,     // Memory or concurrency limit prevented cell admission
    StaleGeneration,        // Asynchronous completion delivered an outdated generation
    ProviderFailed,         // A registered feature provider failed during lifecycle transition
    VolumeNotFound,         // Unregistered or invalid StreamingVolumeId
    InvalidCoordinate,      // Coordinates contain NaN, infinity, or overflow bounds
    Cancelled               // Operation cancelled due to volume exit or world teardown
};

} // namespace Horo::Runtime::Streaming
```

When a feature provider or asset load encounters a failure, the cell transitions to `Failed` state. A structured diagnostic is published to the `DiagnosticsEngine` containing `StreamingCellId`, `StreamingGeneration`, the failing provider name, and the underlying `ErrorCode`.

### Retry Policy

```cpp
namespace Horo::Runtime::Streaming {

struct StreamingRetryPolicy {
    uint32_t  maxRetries{3};
    float     retryCooldownSeconds{2.0f};
    bool      autoRetryOnVolumeChange{true};
};

} // namespace Horo::Runtime::Streaming
```

When a load fails, `retryCount` is decremented. If retries remain, the cell is re-queued after `retryCooldownSeconds`. When retries are exhausted, the cell enters `Failed`. If `autoRetryOnVolumeChange` is true, newly entering volumes reset the retry counter and trigger a fresh load attempt.

---

## Concurrency, Replacement, and Shutdown Semantics

1. **Thread Role Discipline**:
   - `StreamingPartitionAuthority::Update` runs exclusively on the main simulation/editor thread (`MainEditor` role) at a declared frame synchronization point.
   - Asset decompression, decoding, and candidate building run on `Worker` threads via `JobSystem` using cooperative `CancellationToken`.
   - ECS entity creation and destruction commit on the main scene thread during `CommitDeferredLifecycleChanges`.
2. **World Replacement**:
   - On scene transition or world reload, the partition generation increments.
   - All in-flight streaming jobs are cancelled via `CancellationToken`.
   - All `Active` and `Resident` cells are transitioned to `Evicting` -> `Unloaded`.
   - Feature providers receive bulk eviction notifications.
3. **Graceful Shutdown**:
   - Volume evaluation stops immediately.
   - Pending load queues are cleared.
   - In-flight task groups are cancelled and joined with a bounded timeout.
   - Active cells are synchronously torn down in reverse dependency order:
     $$\text{Feature Providers} \longrightarrow \text{Scene Entities} \longrightarrow \text{Partition Authority} \longrightarrow \text{Asset Pipeline}$$

---

## Cooked World Index Format (`world.index`)

The World Index manifest is the authoritative spatial directory for a streamed
world. It is cooked during asset pipeline packaging and is consumed at runtime
to establish grid boundaries, spatial hierarchies, layer metadata, streaming
volumes, and cryptographic cell checksums.

### Binary and Schema Structure

```cpp
namespace Horo::WorldStreaming {

enum class CoordinateSystem : uint8_t {
    RightHanded_YUp = 0,
    RightHanded_ZUp = 1,
    LeftHanded_YUp  = 2,
    LeftHanded_ZUp  = 3,
};

struct WorldBoundingVolume {
    Vec3 minBounds; // world-space meters (X, Y, Z)
    Vec3 maxBounds; // world-space meters (X, Y, Z)
};

struct WorldGridDimensions {
    float cellSize;             // meters per side (e.g. 64.0f, 128.0f)
    Vec3  gridOrigin;           // world-space anchor for grid coordinate (0, 0, 0)
    int32_t minGridX, maxGridX; // bounding coordinate envelope
    int32_t minGridY, maxGridY;
    int32_t minGridZ, maxGridZ;
    CoordinateSystem coordinateSystem;
    uint8_t lodLevels;          // number of spatial partition hierarchy levels
};

enum class WorldLayerFlags : uint32_t {
    None        = 0,
    Persistent  = 1 << 0, // Always resident, never unstreamed
    Optional    = 1 << 1, // Can be disabled on memory-constrained platforms
    ServerOnly  = 1 << 2, // Excluded from client streaming
    ClientOnly  = 1 << 3, // Excluded from dedicated server streaming
};

enum class WorldLayerOwnership : uint8_t {
    WorldStreaming      = 0, // managed by the streaming system
    GameplayScript      = 1, // managed by gameplay code
    NetworkReplication  = 2, // managed by the networking layer
};

struct WorldLayerDefinition {
    uint16_t layerId;
    std::string layerName;
    WorldLayerOwnership ownership;
    WorldLayerFlags flags;
    float priorityMultiplier;     // multiplier applied to cells in this layer
};

enum class StreamingVolumeType : uint8_t {
    Camera             = 0,
    Gameplay           = 1,
    NetworkRelevance   = 2,
    Preload            = 3,
};

enum class StreamingVolumeShape : uint8_t {
    Sphere     = 0,
    Box        = 1,
    ConvexHull = 2,
};

struct WorldStreamingVolumeDefinition {
    uint32_t volumeId;
    StreamingVolumeType type;
    StreamingVolumeShape shape;
    Vec3 origin;                   // world meters
    Vec3 extents;                  // radius (Sphere) or half-extents (Box)
    float priority;                // base priority weight
    bool unloadOutside;            // true = release outside; false = pin
    uint32_t layerMask;            // bitmask of layer-table ordinals 0-31
};

struct StreamingCellChecksumEntry {
    StreamingCellId cellId;        // (x, y, z, lod, layerId)
    AssetId chunkAssetId;          // logical asset ID inside horopak or wcell path
    uint64_t uncompressedSize;
    uint64_t compressedSize;
    uint32_t payloadCrc32;
    std::array<uint8_t, 32> sha256Hash;
};

struct WorldIndexManifest {
    uint32_t magic;                // 0x58444E49 ("INDX")
    uint16_t versionMajor;         // kWorldIndexVersionMajor = 1
    uint16_t versionMinor;         // kWorldIndexVersionMinor = 0
    Guid worldGuid;                // unique world asset identifier
    std::string worldName;
    WorldBoundingVolume boundingVolume;
    WorldGridDimensions gridDimensions;
    std::vector<WorldLayerDefinition> layers;
    std::vector<WorldStreamingVolumeDefinition> staticVolumes;
    std::vector<StreamingCellChecksumEntry> cellEntries;
    std::array<uint8_t, 32> manifestHash; // SHA-256 of canonical binary manifest
};

} // namespace Horo::WorldStreaming
```

These are logical models, not serialized native C++ object layouts.
`std::string`, `std::vector`, pointer size, padding, and host ABI never enter the
wire format.

### Canonical `world.index` Binary Layout (Version 1)

The binary manifest starts with this 192-byte (`0xC0`) little-endian header:

| Offset | Type | Field | Description |
|:---|:---|:---|:---|
| `0x00` | `uint32` | `magic` | `0x58444E49` (`I`,`N`,`D`,`X` bytes on disk) |
| `0x04` | `uint16` | `versionMajor` | Breaking schema version (`1`) |
| `0x06` | `uint16` | `versionMinor` | Backwards-compatible version (`0`) |
| `0x08` | `uint32` | `headerFlags` | Reserved in v1; zero |
| `0x0C` | `uint32` | `headerSize` | `0x000000C0` |
| `0x10` | `uint8[16]` | `worldGuid` | Canonical Guid bytes |
| `0x20` | `uint32` | `worldNameOffset` | String-table-relative offset |
| `0x24` | `uint32` | `worldNameSize` | UTF-8 bytes; no terminator |
| `0x28` | `float32[3]` | `minBounds` | Finite world-space meters |
| `0x34` | `float32[3]` | `maxBounds` | Finite world-space meters |
| `0x40` | `float32` | `cellSize` | Finite meters per side |
| `0x44` | `float32[3]` | `gridOrigin` | Finite world-space meters |
| `0x50` | `int32` | `minGridX` | Inclusive grid bound |
| `0x54` | `int32` | `maxGridX` | Inclusive grid bound |
| `0x58` | `int32` | `minGridY` | Inclusive grid bound |
| `0x5C` | `int32` | `maxGridY` | Inclusive grid bound |
| `0x60` | `int32` | `minGridZ` | Inclusive grid bound |
| `0x64` | `int32` | `maxGridZ` | Inclusive grid bound |
| `0x68` | `uint8` | `coordinateSystem` | `CoordinateSystem` enum |
| `0x69` | `uint8` | `lodLevels` | Hierarchy-level count |
| `0x6A` | `uint16` | `reserved0` | Zero |
| `0x6C` | `uint32` | `layerTableOffset` | Absolute, 8-byte aligned |
| `0x70` | `uint32` | `layerCount` | 32-byte entry count |
| `0x74` | `uint32` | `volumeTableOffset` | Absolute, 8-byte aligned |
| `0x78` | `uint32` | `volumeCount` | 64-byte entry count |
| `0x7C` | `uint32` | `cellTableOffset` | Absolute, 8-byte aligned |
| `0x80` | `uint32` | `cellEntryCount` | 96-byte entry count |
| `0x84` | `uint32` | `stringTableOffset` | Absolute, 8-byte aligned |
| `0x88` | `uint32` | `stringTableSize` | Exact UTF-8 byte count |
| `0x8C` | `uint32` | `reserved1` | Zero |
| `0x90` | `uint64` | `fileSize` | Exact manifest byte count |
| `0x98` | `uint8[32]` | `manifestHash` | Canonical SHA-256 |
| `0xB8` | `uint8[8]` | `reserved2` | Zero |

Fixed section entries are:

- **Layer (32 bytes):** `uint16 layerId` at `0x00`, `uint8 ownership` at
  `0x02`, zero byte at `0x03`, `uint32 flags` at `0x04`, `float32
  priorityMultiplier` at `0x08`, string-relative `uint32 layerNameOffset` and
  `layerNameSize` at `0x0C`/`0x10`, then twelve zero bytes.
- **Volume (64 bytes):** `uint32 volumeId` at `0x00`; `uint8 type`, `shape`,
  `unloadOutside`, and zero byte at `0x04`–`0x07`; `float32 origin[3]` at
  `0x08`; `float32 extents[3]` at `0x14`; `float32 priority` at `0x20`;
  `uint32 layerMask` at `0x24`; then 24 zero bytes. Mask bits address layer-table
  ordinals 0–31, not `layerId` values.
- **Cell checksum (96 bytes):** `int32 gridX/Y/Z` at `0x00`/`0x04`/`0x08`;
  `uint8 lodLevel` at `0x0C`; zero byte at `0x0D`; `uint16 layerId` at `0x0E`;
  16-byte `chunkAssetId` at `0x10`; aggregate `uint64 uncompressedSize` and
  `compressedSize` at `0x20`/`0x28`; aggregate `uint32 payloadCrc32` at `0x30`;
  zero `uint32` at `0x34`; 32-byte artifact SHA-256 at `0x38`; eight zero bytes at
  `0x58`.

Layer, volume, and cell tables sort by `layerId`, `volumeId`, and
`(layerId, lodLevel, gridZ, gridY, gridX)`; duplicates are invalid. The string
table stores NFC-normalized UTF-8 `worldName` first and layer names in layer
order, with no terminators. All section padding is zero and ranges stay within
`fileSize`.

Version 1 packs sections without discretionary gaps. `layerTableOffset = 0xC0`;
`volumeTableOffset = AlignUp(layerTableOffset + layerCount * 32, 8)`;
`cellTableOffset = AlignUp(volumeTableOffset + volumeCount * 64, 8)`; and
`stringTableOffset = AlignUp(cellTableOffset + cellEntryCount * 96, 8)`.
`fileSize` is exactly `stringTableOffset + stringTableSize`; trailing bytes are
invalid. Floats must be finite and zero uses positive-zero encoding. Guid/AssetId
fields use the Horo value type's persistent 16-byte sequence with no
platform-native GUID field swapping.

`manifestHash` is SHA-256 over exactly `fileSize` bytes after replacing bytes
`0x98`–`0xB7` with 32 zeros. A JSON mirror is a non-authoritative tooling
projection and does not participate in the hash.

---

## Cooked Cell Archive Format (`.wcell` / `HOROCELL`)

A cooked streaming cell is packaged either as a standalone `.wcell` file or as
an individual chunk within a `.horo` package archive (`horopak`).

### Fixed Header Layout (96 Bytes, Little-Endian)

All scalar types and headers are explicitly encoded in **Little-Endian (LE)**.

| Offset | Type | Field | Description |
|:---|:---|:---|:---|
| `0x00` | `uint8[8]` | `magic` | ASCII `HOROCELL` (`0x48, 0x4F, 0x52, 0x4F, 0x43, 0x45, 0x4C, 0x4C`) |
| `0x08` | `uint16` | `versionMajor` | Breaking schema version (`kCellVersionMajor = 1`) |
| `0x0A` | `uint16` | `versionMinor` | Non-breaking minor version (`kCellVersionMinor = 0`) |
| `0x0C` | `uint32` | `headerFlags` | Cell flags (Bit 0: Encrypted, Bit 1: Compressed, Bit 2: DebugInfo) |
| `0x10` | `int32` | `gridX` | Spatial grid X coordinate |
| `0x14` | `int32` | `gridY` | Spatial grid Y coordinate |
| `0x18` | `int32` | `gridZ` | Spatial grid Z coordinate |
| `0x1C` | `uint8` | `lodLevel` | Hierarchy LOD level (0 = base detail) |
| `0x1D` | `uint8` | `coordinateSystem` | `CoordinateSystem` enum |
| `0x1E` | `uint16` | `layerId` | World layer identifier |
| `0x20` | `uint32` | `compressionCodec` | `0` = None, `1` = LZ4, `2` = Zstandard |
| `0x24` | `uint32` | `payloadCrc32` | Aggregate CRC32 of decoded blocks in TOC order |
| `0x28` | `uint64` | `uncompressedSize` | Sum of entry uncompressed sizes |
| `0x30` | `uint64` | `compressedSize` | Cell-body bytes including TOC, zero padding, and compressed blocks |
| `0x38` | `uint32` | `featureTableOffset` | Must be `0x60` in version 1 |
| `0x3C` | `uint32` | `featureTableCount` | Number of entries in Feature Payload TOC |
| `0x40` | `uint8[32]` | `sha256Hash` | Canonical SHA-256 of fixed header and cell body |

The header occupies bytes `0x00`–`0x5F` inclusive (96 bytes). Fields after `0x3C` continue through the 32-byte `sha256Hash` at `0x40`, so the first byte after the header is `0x60`.

### Feature Provider Table of Contents (TOC)

Version 1 places the TOC immediately after the header at `0x60`. An array of
`featureTableCount` fixed 40-byte entries defines independently compressed
payload blocks:

```cpp
namespace Horo::WorldStreaming {

enum class FeatureProviderType : uint16_t {
    CoreEcs        = 0x0001, // Entity archetypes, IDs, hierarchy, component streams
    Terrain        = 0x0002, // Heightfield raster tiles, splatmaps, physics descriptors
    Foliage        = 0x0003, // Instanced foliage clusters and transforms
    PhysicsMesh    = 0x0004, // Static rigid body triangle meshes and convex hulls
    Audio          = 0x0005, // Soundscape emitters, ambient zones, acoustic portals
    NavigationMesh = 0x0006, // Recast NavMesh tile polygons and off-mesh links
    Destruction    = 0x0007, // Pre-fractured chunk geometries and anchor data
    CustomExt      = 0x8000, // Custom game plugin extension base
};

enum class FeaturePayloadFlags : uint16_t {
    None      = 0,
    Required  = 1 << 0, // Must be decoded; failure to decode aborts cell load
    Optional  = 1 << 1, // Can be safely skipped if feature provider is disabled/unsupported
    Encrypted = 1 << 2, // Feature payload block is encrypted
};

struct FeaturePayloadEntry {
    FeatureProviderType providerType;
    FeaturePayloadFlags flags;
    uint32_t version;             // Provider-specific payload schema version
    uint64_t payloadOffset;       // Absolute byte offset in file/chunk
    uint64_t compressedSize;      // Compressed byte count
    uint64_t uncompressedSize;    // Uncompressed byte count
    uint32_t payloadCrc32;        // Checksum of uncompressed provider payload
    uint32_t reserved;            // 8-byte alignment padding
};

} // namespace Horo::WorldStreaming
```

### Canonical Cell Body And Decode Units

Every cell contains exactly one required `CoreEcs` entry as the first TOC row;
there is no separate core block outside the table. Even an entity-empty cell
contains a valid empty-core schema block. The built-in Core ECS provider stages
it into detached `RuntimeSceneStorage`. Terrain, Foliage, Physics Mesh, Audio,
Navigation Mesh, Destruction, and custom providers use the same entry contract.

Entries sort by raw `providerType`; duplicates are invalid. Custom extensions
allocate unique IDs in `0x8000`–`0xFFFF`. Payload offsets are absolute,
non-overlapping, 8-byte aligned, and after the TOC. Inter-range padding is zero.
`WorldLayerDefinition`, `StreamingCellId`, index entries, and cell headers all
use `uint16 layerId`; cooking rejects more than 65,536 unique IDs instead of
truncating.

Exactly one of `Required` or `Optional` is set per entry and `CoreEcs` is always
required. The first block begins at
`AlignUp(0x60 + featureTableCount * 40, 8)`; each next block begins at
`AlignUp(previous.payloadOffset + previous.compressedSize, 8)`. The final block
ends exactly at `0x60 + header.compressedSize`, with no trailing body bytes.

Compression is independent per entry. With `headerFlags.Compressed`, each block
uses the header `compressionCodec` and requires one decompressor invocation.
Without it, the codec is `None` and entry compressed/uncompressed sizes match.
Entry `Encrypted` flags apply per block; the header Encrypted bit is their OR and
decryption precedes decompression. For encrypted entries, `compressedSize` is the
complete on-disk authenticated-envelope size; decrypting the security-versioned
envelope yields the codec input bytes.

The cell body is
`[featureTableOffset, featureTableOffset + compressedSize)`, including TOC,
zero padding, and compressed blocks. Header SHA-256 covers the complete artifact
range `[0x00, 0x60 + compressedSize)`, replacing header bytes `0x40`–`0x5F`
with 32 zeros while hashing. A standalone file or package chunk has exactly that
length, so the hash protects every fixed-header control field, the TOC, padding,
and blocks. The matching `world.index` cell entry must match header compressed
size, uncompressed size, CRC32, and SHA-256 before block decode.

CRC uses CRC-32/ISO-HDLC (IEEE polynomial `0x04C11DB7`, reflected
`0xEDB88320`, initial/final XOR `0xFFFFFFFF`). Each entry CRC covers exactly its
decoded bytes. Header CRC is equivalent to CRC over all decoded blocks
concatenated in TOC order without padding. The decoder may verify this aggregate
without inflating unsupported optional blocks by applying standard GF(2)
`crc32_combine` to each entry CRC and uncompressed size in TOC order.

---

## Versioning, Compatibility, and Error Handling

### Version Negotiation Rules

1. **Major Version Incompatibility**: If `header.versionMajor != kCellVersionMajor`, the cell artifact is unconditionally rejected. The engine does not perform runtime schema transmutation across major versions; assets must be re-cooked.
2. **Minor Version Forward Compatibility**: If `header.versionMinor > kCellVersionMinor`, the runtime continues decoding if and only if all unknown feature payloads have `FeaturePayloadFlags::Optional` set. If any unknown or unhandled feature payload is marked `Required`, the cell load fails.
3. **Provider Versioning**: Each feature provider inspects its own `FeaturePayloadEntry::version`. If a provider receives a version it cannot support:
   - If `Optional`, the provider skips its payload slice and logs a diagnostic warning.
   - If `Required`, the provider returns an error, triggering cell load rejection.

### Typed Error Model

World streaming errors conform to Foundation's `ErrorCode` and `Result<T, Error>` contract:

```cpp
namespace Horo::WorldStreaming {

enum class StreamingCellErrorCode : uint32_t {
    Success                   = 0,
    InvalidMagic              = 1, // Header magic is not 'HOROCELL'
    UnsupportedVersion        = 2, // Incompatible major or unsupported required minor version
    CorruptedIntegrity        = 3, // CRC32 or SHA-256 verification failed
    DecompressionFailed       = 4, // LZ4/ZSTD decompressor reported malformed data
    InvalidOffsetTable        = 5, // Payload offset or size exceeds file/chunk boundary
    RequiredProviderMissing   = 6, // Required feature provider is not registered in runtime host
    ProviderDecodeFailed      = 7, // Specific feature provider failed to parse its payload
    AllocationLimitExceeded   = 8, // Cell memory requirements exceed StreamingBudget allocation
    TruncatedData             = 9, // Unexpected end-of-stream during read
    Cancelled                 = 10,// Decode operation aborted by CancellationToken
    IoReadError               = 11,// Underlying storage or package stream I/O failure
    RequiredPayloadMissing    = 12,// CoreEcs or another format-required entry is absent
    InvalidManifest           = 13,// world.index layout, ordering, or hash is invalid
};

struct StreamingCellError {
    StreamingCellErrorCode code;
    StreamingCellId cellId;
    std::string message;
    std::vector<Diagnostic> diagnostics;
};

template <typename T>
using StreamingResult = Result<T, StreamingCellError>;

} // namespace Horo::WorldStreaming
```

---

## Decode Pipeline, Cancellation, and Failure Guarantees

Streaming cell decode executes as an asynchronous job pipeline over `HoroFoundation::JobSystem` with deterministic cancellation checkpoints and zero partial state leakage:

```text
┌─────────────────┐     Cancel?     ┌──────────────────────────┐
│  1. Async I/O   ├────────────────►│ Discard buffer           │
└────────┬────────┘                 │ Return Cancelled         │
         ▼                          └──────────────────────────┘
┌─────────────────┐   Check fail?   ┌──────────────────────────┐
│  2. Integrity   ├────────────────►│ Emit diagnostic          │
│   & Validation  │                 │ Transition to Failed     │
└────────┬────────┘                 └──────────────────────────┘
         ▼
┌─────────────────┐     Cancel?     ┌──────────────────────────┐
│ 3. Decompress   ├────────────────►│ Free staging memory      │
│  & Slice Table  │                 │ Return Cancelled         │
└────────┬────────┘                 └──────────────────────────┘
         ▼
┌─────────────────┐   Error/Cancel? ┌──────────────────────────┐
│ 4. Providers &  ├────────────────►│ Roll back provider slices│
│ ECS Instantiate │                 │ Free staging entities    │
└────────┬────────┘                 │ Zero ECS state touches   │
         ▼                          └──────────────────────────┘
┌─────────────────┐
│   5. Commit to  │ (Sync point on owner thread: swap storage candidate)
│  Runtime Scene  │
└─────────────────┘
```

### Decode Phase Guarantees

1. **Async I/O Phase**: Reads raw bytes from the archive into a scratch buffer. If `cancelToken.IsCancelled()` is signaled, the read is aborted and the buffer returned to the pool immediately.
2. **Integrity & Validation Phase**: Validates artifact length, computes SHA-256 over the fixed header with its hash field zeroed plus the exact cell body, and compares it with both header and `world.index` before trusting header controls or TOC contents. It then validates magic/version, the exact v1 TOC location, canonical provider ordering, exactly one required first `CoreEcs`, zero padding, aligned non-overlapping block ranges, aggregate sizes, and file containment. Aggregate uncompressed size and CRC are then recomputed from TOC metadata with `crc32_combine` and compared before decompression.
3. **Independent Block Decode Phase**: In canonical order, each required or supported optional entry is decrypted if needed, independently decompressed with the header codec, checked for exact decoded size, and verified against its entry CRC32. Unsupported optional entries may remain compressed and unstaged. Budget failure returns `AllocationLimitExceeded`; size/hash/CRC mismatch returns `CorruptedIntegrity`.
4. **Provider Staging Phase**: The built-in Core ECS provider creates detached `RuntimeSceneStorage`. Terrain, Foliage, Physics Mesh, Audio, Navigation Mesh, Destruction, and custom `IFeatureStreamingProvider` implementations construct resources in private candidate containers.
5. **Atomic Synchronization & Commit**: On the owner thread during `CommitDeferredLifecycleChanges`, the detached candidate is merged into the active `RuntimeScene` in a single structural transaction. If an error or cancellation occurred in phases 1–4, the candidate is discarded, feature providers drop their staged allocations, and the active scene remains completely untouched.

### Binary Format Verification Requirements

- Golden-byte fixtures verify the 192-byte index header; 32/64/96-byte index
  entries; the 96-byte cell header; and 40-byte cell TOC entries.
- Manifest fixtures cover canonical ordering, NFC strings, zero padding, hash-field
  zeroing, malformed ranges, duplicate IDs, and binary/JSON logical equivalence.
- Cell fixtures cover required first `CoreEcs`, every built-in type including
  Destruction, custom IDs, uint16 layers, independent codec calls, optional-block
  skipping, and aggregate CRC combination.
- Corruption tests independently mutate fixed-header control fields, the header
  hash field, TOC bytes, padding, compressed bytes, decoded bytes, and aggregate
  fields and assert the specified artifact-SHA/per-entry-CRC/aggregate-CRC phase.

## Large-World Coordinate Precision and Floating Origin Rebasing

World streaming in Horo Engine operates on a global coordinate grid backed by 64-bit precision, avoiding single-precision float truncation over vast distances:

- **Canonical Global Coordinates**: `Math::WorldCoordinate64` (`IntVector3 cellIndex` + `IntVector3 cellOffsetMm`) serves as the immutable coordinate authority for spatial cell boundaries, streaming volume queries, persistent level saves, and server multiplayer replication. The stored offset is integer millimeters, not fp32.
- **Floating Origin Rebasing**: When the active player/camera exceeds a configured threshold from the active floating origin ($R_{\text{threshold}} = 1000\,\text{m}$), `OriginRebaseCoordinator` executes an atomic two-phase rebase (`PrepareRebase` -> `CommitRebase`).
- **Subsystem Synchronization**: The resulting `OriginRebaseEvent` translates local simulation frames across Physics, Audio, VFX, Camera, and Navigation without velocity spikes, particle destruction, or Doppler glitching.
- **GPU Compatibility**: GPU shaders remain 32-bit `fp32` across all backends. Camera-relative transformations $(P_{\text{world}} - C_{\text{camera}})$ are computed on the CPU during render extraction.

See [Coordinate Precision And Origin Rebasing](./coordinate-precision-and-origin-rebasing.md) and [ADR-026](../../adr/026-large-world-precision-and-floating-origin-strategy.md) for the complete normative specification.

## Related Documents and ADRs

- [ADR-012: World Streaming Partition Authority and Subsystem Boundaries](../../adr/012-world-streaming-partition-authority-and-subsystem-boundaries.md): Ratified architectural decision and ratify-or-revise outcomes.
- [Scene Runtime](./scene-runtime.md): ECS entity storage, candidate preparation, and transactional structural mutations.
- [Asset Pipeline](./asset-pipeline.md): Streaming cell assets, chunked package archives, and async I/O with `CancellationToken`.
- [Terrain And Foliage Architecture](./terrain-and-foliage-architecture.md): Terrain clipmap streaming and cell-aligned foliage clusters.
- [Physics Architecture](./physics-architecture.md): Static mesh collider registration and scene binding.
- [Networking Architecture](./networking-architecture.md): Server-authoritative cell relevance and replication.
- [Concurrency And Job System](../foundation/concurrency-and-jobs.md): Job workers, cancellation tokens, and thread roles.
- [Error And Diagnostics](../foundation/error-and-diagnostics.md): Fallible `Result<T, Error>` contracts and diagnostic codes.
- [Editor Document Model](../editor/editor-document-model.md): Multi-layer authoring documents and offline cell baking.
