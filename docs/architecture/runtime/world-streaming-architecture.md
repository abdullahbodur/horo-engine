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
  zero `uint32` at `0x34`; 32-byte body SHA-256 at `0x38`; eight zero bytes at
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
| `0x40` | `uint8[32]` | `sha256Hash` | SHA-256 of exact on-disk cell body |

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
zero padding, and compressed blocks. Header SHA-256 covers exactly this range.
The matching `world.index` cell entry must match header compressed size,
uncompressed size, CRC32, and SHA-256 before block decode.

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
2. **Integrity & Validation Phase**: Validates magic/version, the exact v1 TOC location, canonical provider ordering, exactly one required first `CoreEcs`, zero padding, aligned non-overlapping block ranges, aggregate sizes, and file containment. SHA-256 over the exact cell body must match both header and `world.index` before the TOC is trusted. Aggregate uncompressed size and CRC are then recomputed from TOC metadata with `crc32_combine` and compared before decompression.
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
- Corruption tests independently mutate TOC bytes, padding, compressed bytes,
  decoded bytes, and aggregate fields and assert the specified failure phase.

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
