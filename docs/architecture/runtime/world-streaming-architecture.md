# World Streaming Architecture

## Purpose

This document defines the world-streaming subsystem for Horo Engine. It covers
spatial partitioning of large worlds, asynchronous cell loading and unloading,
streaming volumes, priority calculation, budget coordination with terrain and
foliage, cell lifecycle (including error and retry), world composition, editor
integration, and networking authority.

## Streaming Model

Horo Engine uses a cell-based world-streaming model:

- The world is divided into a grid of streaming cells
- Each cell contains scene objects, terrain tiles, and foliage clusters
- Foliage clusters are **cell-aligned**: a cluster's bounding box fits within
  a single cell. Clusters that would span cell boundaries are split during
  the foliage bake step, producing a cluster per cell. This ensures no
  cluster is gated on two cells' load decisions.
- Cells are loaded and unloaded based on streaming volumes (camera, gameplay
  relevance, network relevance)
- Streaming operations are asynchronous and cancellable

```cpp
struct StreamingCellId {
    int32_t  x;
    int32_t  y;
    int32_t  z;
    uint8_t  lod;     // 0 = base resolution
    uint16_t layerId; // layer index from WorldIndexManifest
};

struct StreamingCell {
    StreamingCellId   id;
    WorldCoordinate   origin;          // world-space cell origin
    float             cellSize;        // meters per side
    StreamingCellState state;          // Unloaded, Loading, Loaded, Unloading, Failed
    AssetId           cellAsset;       // baked scene asset or wcell package chunk for this cell
    StreamingPriority priority;        // cached computed priority, updated each frame
};
```

`priority` is a **cached computed value** updated each frame during volume
evaluation. It is stored in the cell struct to allow the streaming scheduler
to sort the load queue without re-evaluating every volume for every cell
every tick.

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
    uint32_t layerId;
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
    uint32_t layerMask;            // bitmask of layers affected by this volume
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
    uint32_t schemaVersion;        // kWorldIndexCurrentVersion = 1
    Guid worldGuid;                // unique world asset identifier
    std::string worldName;
    WorldBoundingVolume boundingVolume;
    WorldGridDimensions gridDimensions;
    std::vector<WorldLayerDefinition> layers;
    std::vector<WorldStreamingVolumeDefinition> staticVolumes;
    std::vector<StreamingCellChecksumEntry> cellEntries;
    std::array<uint8_t, 32> manifestHash; // SHA-256 of all preceding fields
};

} // namespace Horo::WorldStreaming
```

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
| `0x24` | `uint32` | `payloadCrc32` | CRC32 checksum of uncompressed payload |
| `0x28` | `uint64` | `uncompressedSize` | Total uncompressed payload byte count |
| `0x30` | `uint64` | `compressedSize` | Total compressed payload byte count on disk |
| `0x38` | `uint32` | `featureTableOffset` | Byte offset to Feature Payload TOC from file start (`0x60` when TOC immediately follows this header) |
| `0x3C` | `uint32` | `featureTableCount` | Number of entries in Feature Payload TOC |
| `0x40` | `uint8[32]` | `sha256Hash` | SHA-256 cryptographic hash of compressed payload |

The header occupies bytes `0x00`–`0x5F` inclusive (96 bytes). Fields after `0x3C` continue through the 32-byte `sha256Hash` at `0x40`, so the first byte after the header is `0x60`.

### Feature Provider Table of Contents (TOC)

Immediately following the 96-byte fixed header (offset `0x60`, or another location named by `featureTableOffset`), an
array of `featureTableCount` entries defines the sub-payload slices:

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
2. **Integrity & Validation Phase**: Verifies magic bytes `HOROCELL`, endianness, schema version compatibility, and validates that all TOC offsets and sizes are strictly contained within the on-disk bounds. Compares SHA-256 of the **compressed** payload against the cell header and `WorldIndexManifest`. Any compressed-integrity mismatch immediately returns `StreamingCellErrorCode::CorruptedIntegrity` without executing decompression. Uncompressed `payloadCrc32` is **not** compared in this phase.
3. **Decompression & Slice Extraction Phase**: Decompresses the core ECS block and feature payload slices into bounded staging memory. Memory allocation is verified against the available `StreamingBudget`. If over-budget, returns `AllocationLimitExceeded`. After successful decompression, compares CRC32 of the **uncompressed** payload against `payloadCrc32` in the cell header and `WorldIndexManifest`. Each `FeaturePayloadEntry::payloadCrc32` is compared after that provider slice is decompressed. CRC mismatch returns `StreamingCellErrorCode::CorruptedIntegrity`.
4. **Feature Provider & ECS Staging Phase**: Staged memory slices are dispatched to registered `IFeatureStreamingProvider` instances (Terrain, Foliage, Physics Mesh, Audio, NavMesh). Providers construct runtime-ready resources in private candidate containers. ECS entities are constructed in a detached `RuntimeSceneStorage` candidate.
5. **Atomic Synchronization & Commit**: On the owner thread during `CommitDeferredLifecycleChanges`, the detached candidate is merged into the active `RuntimeScene` in a single structural transaction. If an error or cancellation occurred in phases 1–4, the candidate is discarded, feature providers drop their staged allocations, and the active scene remains completely untouched.

## Streaming Volumes

Streaming volumes define which cells should be loaded:

- **Camera volume**: Cells within a radius of the active camera
- **Gameplay volumes**: Pre-defined volumes attached to gameplay triggers
- **Network relevance volumes**: Cells relevant to the local player in
  multiplayer
- **Preload volumes**: Always-loaded cells for critical areas

```cpp
struct StreamingVolume {
    StreamingVolumeType type;
    WorldCoordinate     origin;
    float               radius;
    StreamingPriority   priority;
    bool                unloadOutside;
};
```

`unloadOutside` (bool): When `true`, cells that are inside this volume's
radius must remain loaded, but this volume does **not** prevent a cell from
unloading if all other volumes have also released it. When `false`, the
volume acts as a **pin**: cells within its radius are held loaded regardless
of other volumes' state. This allows "camera" volumes to release cells when
the camera moves away while "preload" volumes permanently pin critical
cells.

The streaming system evaluates all active volumes each frame and computes a
union of required cells. Cells not in the union but currently loaded are
scheduled for unloading after a configurable linger duration.

**Linger duration** is a global setting stored in `StreamingBudget` (not per
volume or per cell):

```cpp
struct StreamingBudget {
    uint32_t  maxConcurrentLoads;
    uint32_t  maxConcurrentUnloads;
    size_t    maxResidentMemoryMB;
    float     maxFrameTimeMS;        // time budget for streaming work per frame
    float     unloadLingerSeconds;   // delay before unloading cells that left all volumes
    size_t    terrainMemoryReservationMB;
    size_t    foliageMemoryReservationMB;
};
```

This prevents rapid load/unload cycles when the camera oscillates near a
cell boundary.

## Cell Lifecycle

```
                 ┌──────────────────────────┐
                 │                          │
                 ▼                          │
Unloaded ──→ Loading ──→ Loaded ──→ Unloading ──→ Unloaded
               │                                    ▲
               │        (max retries)               │
               └──→ Failed ─────────────────────────┘
```

**States**:

- **Unloaded**: No resources allocated
- **Loading**: Scene asset is being read from disk; terrain and foliage data
  are requested from the asset pipeline via async I/O with cancellation
  tokens
- **Loaded**: All objects and components are instantiated in the scene
  runtime. The cell remains in this state until all volumes release it and
  the linger duration expires.
- **Unloading**: Objects are being removed; references are cleaned up. Once
  complete, transitions to Unloaded.
- **Failed**: Loading encountered an unrecoverable error after exhausting
  retries. The cell stays in Failed until explicitly retried (editor
  command, streaming volume re-evaluation after a configurable cooldown).

**Retry policy**:

```cpp
struct StreamingRetryPolicy {
    uint32_t  maxRetries;             // default 3
    float     retryCooldownSeconds;   // delay before re-queuing a Failed cell
    bool      autoRetryOnVolumeChange;// re-attempt when a new volume covers this cell
};
```

When a `Loading` cell fails, the streaming system decrements a retry counter
for that cell. If retries remain, the cell is re-queued after the cooldown.
If retries are exhausted, the cell enters `Failed` state and a diagnostic
is emitted. When `autoRetryOnVolumeChange` is true, a Failed cell is
automatically re-queued (with a fresh retry counter) when a streaming volume
newly covers it.

Transitions are driven by `StreamingRequest` objects queued into the job
system:

```cpp
struct StreamingRequest {
    StreamingCellId  cellId;
    StreamingAction  action;          // Load or Unload
    StreamingPriority priority;
    CancellationToken cancelToken;   // from asset pipeline: job system CancellationToken
};
```

The `cancelToken` is derived from the asset pipeline's async I/O contract:
all asset read operations for this cell share a single token. If the cell
transitions to `Unloading` before the load completes, the token is
cancelled, aborting in-flight I/O and releasing partial resources.

## Priority And Budget

### Priority Calculation

Streaming priority is computed as a weighted sum each frame:

```
priority = (1.0 / (distance + epsilon))
         * volumeTypeMultiplier
         * explicitOverrideMultiplier
         + antiStarvationBoost
```

where:

| Factor | Description |
| ------ | ----------- |
| `distance` | Meters from cell center to nearest active streaming volume origin. Closer cells score higher. `epsilon` prevents division by zero. |
| `volumeTypeMultiplier` | Camera = 1.0, Gameplay = 0.9, Network = 0.8, Preload = 1.2 (preload cells are pinned, but within the preload set, closer cells still load first) |
| `explicitOverrideMultiplier` | Gameplay scripts can set a multiplier (0.5–2.0) on specific cells via `StreamingPriorityOverride` |
| `antiStarvationBoost` | Increases linearly with seconds spent queued as a pending load request. Prevents distant cells from never starting. |

When multiple volumes cover the same cell, the **highest** computed priority
across all covering volumes is used (not an average). This ensures a cell
covered by both a low-priority network volume and a high-priority camera
volume gets the camera priority.

The `StreamingCell.priority` field stores this cached computed value per
frame. The `StreamingVolume.priority` field defines that volume's **base**
contribution weight; it feeds into the formula above, scaled by distance
and the volume type multiplier.

### Streaming Budget

The canonical `StreamingBudget` is defined above in the streaming-volume section.
`terrainMemoryReservationMB` and `foliageMemoryReservationMB` are **sub-
allocations** carved out of `maxResidentMemoryMB`. The terrain and foliage
subsystems each have their own internal caches (`TerrainStreamingBudget`)
that are capped by these reservations. The remaining budget
(`maxResidentMemoryMB - terrainReservation - foliageReservation`) is
available for scene objects and other cell payloads.

This avoids double-counting: the world-streaming system owns the total cap,
and terrain/foliage operate within their allocated slices. If a terrain tile
cache exceeds its reservation, terrain evicts internally without asking the
world-streaming system.

## World Composition

Large worlds are composed from multiple layers:

- **Persistent layer**: Always-loaded core objects (sky, global audio, game
  mode)
- **Streaming layers**: Cell-based world content
- **Dynamic layers**: Runtime-spawned content (projectiles, VFX, temporary
  objects)

Layer ownership determines streaming responsibility. The canonical
`WorldLayerOwnership` enumeration is defined in the cooked World Index
schema above:

- `WorldStreaming` — managed by the streaming system
- `GameplayScript` — managed by gameplay code
- `NetworkReplication` — managed by the networking layer

## Networking Integration

In multiplayer, the server owns streaming authority:

- Server determines which cells are relevant to each client
- Server sends cell load/unload notifications to clients
- Clients stream cells as directed by the server
- Network-relevant objects within cells are replicated normally

**Budget scope in multiplayer**: The `StreamingBudget` is applied **per
peer**:
- The server manages its own world view with a server-side budget (usually
  higher, since servers must keep relevant cells for all connected clients
  resident).
- Each client has its own budget, enforced locally. The server does not
  track per-client memory usage; it only sends load/unload directives.
- Bandwidth for cell state notifications is managed through the networking
  layer's bandwidth budget.

The server may keep all cells loaded (no server-side streaming) in
smaller-scale games, or apply server-side streaming for large open worlds.
This is a deployment configuration, not a code change.

## Editor Integration

Editor streaming tools register through the `EditorPanelHost` system:

- **World Composition View**: Registered as a standalone `EditorTab`
  (`WorldCompositionTab`). Displays layer list, cell grid with
  load-state color coding, and streaming statistics. Subscribes to
  `StreamingCellStateChangedEvent` on the `EditorDataBus`.
- **Streaming Volume Placement**: `StreamingVolume` objects are scene
  entities with a gizmo component. Volume editing uses the standard
  viewport gizmo system — no custom editor panel needed.
- **Cell Baking**: Exposed as an editor command in the Build menu
  (`BakeStreamingCells`). The command iterates cells, exports their
  content to streaming assets, and reports diagnostics.
- **Streaming Preview**: A viewport overlay toggle that color-codes cells by
  load state (green=loaded, yellow=loading, red=unloaded, gray=failed).
  Registered as a `ViewportPanel` overlay.
- **Streaming Budget HUD**: A bottom-dock panel showing real-time budget
  usage, load queue depth, and frame-time impact.

## Related Documents

- [Scene Runtime](./scene-runtime.md): cell loading integrates with the scene
  model
- [Asset Pipeline](./asset-pipeline.md): streaming cell assets, `CancellationToken`,
  and async I/O contract
- [Terrain And Foliage Architecture](./terrain-and-foliage-architecture.md):
  terrain tile cache and foliage cluster streaming; budget sub-allocation
- [Networking Architecture](./networking-architecture.md): server-authoritative
  streaming and per-peer budget scoping
- [Concurrency And Jobs](../foundation/concurrency-and-jobs.md): streaming job
  system and `CancellationToken`
- [Editor Panel Host](../editor/editor-panel-host.md): world composition tab
  registration, `StreamingCellStateChangedEvent` on `EditorDataBus`
- [Editor Document Model](../editor/editor-document-model.md): cell baking as
  editor commands
