# ADR-023: World Index and Cell Format Architecture Decision

- **Status**: Proposed
- **Date**: 2026-08-28
- **Supersedes**: None
- **Scope**: Cooked world index manifest (`world.index`), streaming cell archive format (`.wcell` / `horopak` chunk), binary encoding, versioning, endianness, integrity hashing, feature-provider payloads, typed error model, and cancellation lifecycles
- **Issue**: [WST-004.1](https://github.com/abdullahbodur/horo-engine/issues/1564)
- **Jira**: [HORO-1564](https://horo-engine.atlassian.net/browse/HORO-1564)
- **Parent**: [WST-004](https://github.com/abdullahbodur/horo-engine/issues/1521)
- **Normative document**: [World Streaming Architecture](../architecture/runtime/world-streaming-architecture.md)

## Context

Horo Engine's open-world runtime streams spatial partitions (cells) asynchronously into active runtime scenes. Authoring data (scenes, terrain heightfields, foliage clusters, audio zones, physics colliders, navigation meshes) is transformed by Asset Pipeline cook stages into immutable, runtime-ready streaming partitions.

Prior to this decision:

- `docs/architecture/runtime/world-streaming-architecture.md` established high-level cell grid concepts, streaming volume priority scoring, memory budgets, and the cell lifecycle state machine (`Unloaded`, `Loading`, `Loaded`, `Unloading`, `Failed`).
- `docs/adr/003-artifact-identity.md` and `docs/architecture/runtime/asset-pipeline.md` defined general `.horo` package chunk containers and asset IDs, but lacked a normative specification for cooked world index manifests (`world.index`) and spatial cell chunks (`.wcell`).
- The runtime lacked an explicit binary layout contract, little-endian data guarantees, integrity hash algorithms, version negotiation semantics, feature-provider payload offset tables, and a Foundation-compliant typed error model for corrupted or version-skewed cell artifacts.

Ticket #1564 ([WST-004.1]) requires ratifying the normative specification for:

1. **Cooked World Index format (`world.index`)**: Manifest containing world bounding volume, partition grid dimensions, spatial cell hierarchy, layer definitions, streaming volumes, and checksum validation table.
2. **Cooked Cell Archive format (`.wcell` / `horopak` chunk)**: Header with magic number (`HOROCELL`), schema version, little-endian encoding, CRC32/SHA-256 integrity hash, and independently compressed TOC payload blocks for Core ECS, Terrain, Foliage, Physics Mesh, Audio, Navigation Mesh, Destruction, and custom providers.
3. **Versioning & Extension**: Forward/backward compatibility rules, version negotiation, and typed rejection of corrupted or version-skewed cell artifacts via `Result<LoadedCellPayload, StreamingCellError>`.
4. **Lifecycle & Cancellation**: Normal, failure, rollback, and cancellation lifecycles during cell decode without leaking partial state.

## Decision

**The engine establishes a deterministic, little-endian, multi-layer cooked world index (`world.index`) and chunked cell container format (`.wcell` / `HOROCELL`) with independent feature-provider payload slices, mandatory CRC32/SHA-256 verification, strict version negotiation, cooperative cancellation at every decode phase, and typed error reporting via Foundation `Result<T, Error>`.**

### 1. Ratify-or-Revise Outcomes

| Area | Current Baseline | Ratified Outcome |
|---|---|---|
| **World Index Manifest (`world.index`)** | High-level conceptual descriptions only | **Ratified as a binary and JSON-mirrored manifest** specifying global bounding box, partition grid dimensions, spatial cell hierarchy (multi-LOD grid/quadtree), layer definitions with explicit ownership, pre-authored streaming volumes, and a cryptographic cell checksum table. |
| **Cell Container Magic & Header** | Undefined format | **Ratified as `HOROCELL` (8-byte ASCII: `0x48, 0x4F, 0x52, 0x4F, 0x43, 0x45, 0x4C, 0x4C`)** with a 96-byte fixed-size little-endian header containing schema version, flags, compression codec, uncompressed/compressed sizes, CRC32, SHA-256, and feature-provider TOC offsets. |
| **Endianness** | Native/implicit | **Ratified as strict Little-Endian (LE)** across all platforms and architectures (ARM64, x86_64). Big-endian host platforms (if ever supported) must swap on read/write. |
| **Integrity & Hashing** | General `.horo` archive TOC hash | **Ratified dual-tier integrity verification**: SHA-256 of the fixed header (with its hash field zeroed) plus the exact on-disk cell body first, per-entry CRC32 after each block is decompressed, then aggregate CRC32 over the canonical concatenation of all decoded blocks. |
| **Payload Decomposition** | Monolithic baked scene asset | **Ratified modular TOC layout**: Core ECS is exactly one required TOC entry; Terrain, Foliage, Physics Mesh, Audio, Navigation Mesh, Destruction, and custom providers are independent entries compressed with the cell codec. |
| **Versioning & Compatibility** | Monotonic version bump | **Ratified major/minor schema versioning with capability flags**: Minor revisions allow optional feature payload skipping (`FeaturePayloadFlags::Optional`); major revisions or missing required providers trigger typed rejection. |
| **Error Handling** | Generic exception or boolean failure | **Ratified Foundation-compliant typed errors**: `Result<LoadedCellPayload, StreamingCellError>` utilizing the `StreamingCellErrorDomain` with stable error codes. |
| **Decode Lifecycle & Cancellation** | Single-step load | **Ratified 5-phase staged pipeline**: (1) Async I/O, (2) cell-artifact SHA-256 and TOC validation, (3) independent block decompression and CRC32 checks, (4) provider staging including Core ECS, and (5) owner-thread atomic commit. Cooperative cancellation is evaluated between and within phases with zero residual state. |

---

### 2. Cooked World Index Specification (`world.index`)

The World Index manifest is the root entry point for world streaming. It is cooked alongside the game assets into package chunks or standalone directory structures.

#### Data Structures

```cpp
namespace Horo::WorldStreaming {

enum class CoordinateSystem : uint8_t {
    RightHanded_YUp = 0,
    RightHanded_ZUp = 1,
    LeftHanded_YUp  = 2,
    LeftHanded_ZUp  = 3,
};

struct WorldBoundingVolume {
    Vec3 minBounds; // meters (X, Y, Z)
    Vec3 maxBounds; // meters (X, Y, Z)
};

struct WorldGridDimensions {
    float cellSize;             // meters per side (e.g. 64.0f, 128.0f)
    Vec3  gridOrigin;           // world-space anchor for grid coordinate (0, 0, 0)
    int32_t minGridX, maxGridX; // grid bounds
    int32_t minGridY, maxGridY;
    int32_t minGridZ, maxGridZ;
    CoordinateSystem coordinateSystem;
    uint8_t lodLevels;          // number of spatial partition hierarchy levels (default 1)
};

enum class WorldLayerFlags : uint32_t {
    None        = 0,
    Persistent  = 1 << 0, // Always resident, never unstreamed
    Optional    = 1 << 1, // Can be disabled on lower-end memory tiers
    ServerOnly  = 1 << 2, // Stripped or unstreamed on client
    ClientOnly  = 1 << 3, // Stripped or unstreamed on dedicated server
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
    Sphere = 0,
    Box    = 1,
    ConvexHull = 2,
};

struct WorldStreamingVolumeDefinition {
    uint32_t volumeId;
    StreamingVolumeType type;
    StreamingVolumeShape shape;
    Vec3 origin;                   // world meters
    Vec3 extents;                  // radius (for Sphere) or half-extents (for Box)
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

The C++ structures above are logical models only; native object layout, pointer
size, `std::string`, and `std::vector` representation are never serialized.

#### Canonical Binary Layout (Version 1)

`world.index` begins with a 192-byte (`0xC0`) little-endian header:

```text
Offset | Type      | Field                | Description
-------+-----------+----------------------+---------------------------------------------
 0x00  | uint32    | magic                | 0x58444E49 ('I','N','D','X' on disk)
 0x04  | uint16    | versionMajor         | Breaking schema version (1)
 0x06  | uint16    | versionMinor         | Backwards-compatible version (0)
 0x08  | uint32    | headerFlags          | Reserved in v1; must be zero
 0x0C  | uint32    | headerSize           | 0x000000C0
 0x10  | uint8[16] | worldGuid            | Canonical 16-byte Guid representation
 0x20  | uint32    | worldNameOffset      | Offset relative to stringTableOffset
 0x24  | uint32    | worldNameSize        | UTF-8 byte count; no NUL terminator
 0x28  | float32[3]| minBounds            | IEEE-754 finite world-space meters
 0x34  | float32[3]| maxBounds            | IEEE-754 finite world-space meters
 0x40  | float32   | cellSize             | IEEE-754 finite meters per cell side
 0x44  | float32[3]| gridOrigin           | IEEE-754 finite world-space meters
 0x50  | int32     | minGridX             | Inclusive grid bound
 0x54  | int32     | maxGridX             | Inclusive grid bound
 0x58  | int32     | minGridY             | Inclusive grid bound
 0x5C  | int32     | maxGridY             | Inclusive grid bound
 0x60  | int32     | minGridZ             | Inclusive grid bound
 0x64  | int32     | maxGridZ             | Inclusive grid bound
 0x68  | uint8     | coordinateSystem     | CoordinateSystem enum
 0x69  | uint8     | lodLevels            | Number of hierarchy levels
 0x6A  | uint16    | reserved0            | Must be zero
 0x6C  | uint32    | layerTableOffset     | Absolute file offset; 8-byte aligned
 0x70  | uint32    | layerCount           | Number of 32-byte layer entries
 0x74  | uint32    | volumeTableOffset    | Absolute file offset; 8-byte aligned
 0x78  | uint32    | volumeCount           | Number of 64-byte volume entries
 0x7C  | uint32    | cellTableOffset      | Absolute file offset; 8-byte aligned
 0x80  | uint32    | cellEntryCount       | Number of 96-byte cell entries
 0x84  | uint32    | stringTableOffset    | Absolute file offset; 8-byte aligned
 0x88  | uint32    | stringTableSize      | Exact UTF-8 byte count
 0x8C  | uint32    | reserved1            | Must be zero
 0x90  | uint64    | fileSize             | Exact manifest byte count
 0x98  | uint8[32] | manifestHash         | Canonical SHA-256 described below
 0xB8  | uint8[8]  | reserved2            | Must be zero
```

Section entry layouts are fixed:

- A **32-byte layer entry** stores `uint16 layerId` at `0x00`, `uint8 ownership`
  at `0x02`, zero `uint8 reserved` at `0x03`, `uint32 flags` at `0x04`,
  `float32 priorityMultiplier` at `0x08`, string-table-relative
  `uint32 layerNameOffset`/`layerNameSize` at `0x0C`/`0x10`, and twelve zero
  reserved bytes at `0x14`.
- A **64-byte volume entry** stores `uint32 volumeId` at `0x00`, `uint8 type`,
  `uint8 shape`, `uint8 unloadOutside`, and zero reserved byte at `0x04`–`0x07`,
  `float32 origin[3]` at `0x08`, `float32 extents[3]` at `0x14`, `float32 priority`
  at `0x20`, `uint32 layerMask` at `0x24`, and 24 zero reserved bytes at `0x28`.
  `layerMask` bits address layer-table ordinals 0–31, not arbitrary `layerId`
  values; worlds requiring more layers use explicit volume-layer lists in a
  future major format.
- A **96-byte cell entry** stores `int32 gridX/Y/Z` at `0x00`/`0x04`/`0x08`,
  `uint8 lodLevel` at `0x0C`, zero reserved byte at `0x0D`, `uint16 layerId` at
  `0x0E`, 16-byte `chunkAssetId` at `0x10`, aggregate `uint64
  uncompressedSize`/`compressedSize` at `0x20`/`0x28`, aggregate `uint32
  payloadCrc32` at `0x30`, zero `uint32 reserved` at `0x34`, 32-byte body
  artifact `sha256Hash` at `0x38`, and eight zero reserved bytes at `0x58`.

Tables are ordered by ascending `layerId`, ascending `volumeId`, and cell key
`(layerId, lodLevel, gridZ, gridY, gridX)` respectively; duplicate keys are
invalid. The string table contains the NFC-normalized UTF-8 `worldName` first,
then layer names in layer-table order, without terminators. All section padding
is zero and all offsets/ranges must be contained within `fileSize`.

Version 1 packs sections without discretionary gaps. `layerTableOffset = 0xC0`;
`volumeTableOffset = AlignUp(layerTableOffset + layerCount * 32, 8)`;
`cellTableOffset = AlignUp(volumeTableOffset + volumeCount * 64, 8)`; and
`stringTableOffset = AlignUp(cellTableOffset + cellEntryCount * 96, 8)`.
`fileSize = stringTableOffset + stringTableSize`; trailing bytes are forbidden.
All floating-point fields must be finite and encode zero as positive zero.
Guid/AssetId fields use the persistent 16-byte sequence returned by the Horo ID
value type (`FromBytes` inverse), with no platform-native GUID field swapping.

`manifestHash` is SHA-256 over exactly `fileSize` bytes with the hash field
(`0x98`–`0xB7`) replaced by 32 zero bytes during hashing. The optional JSON mirror
is a tooling projection of this logical model; it is not runtime-authoritative
and is not an input to `manifestHash`.

---

### 3. Cooked Cell Archive Specification (`.wcell` / `HOROCELL`)

A cooked streaming cell is packaged either as an individual `.wcell` file or as a dedicated chunk within a `.horo` package archive (`horopak`).

#### Fixed Header Layout (96 Bytes, Little-Endian)

```text
Offset | Type     | Field                     | Description
-------+----------+---------------------------+---------------------------------------------------
 0x00  | uint8[8] | magic                     | ASCII 'HOROCELL' (0x48, 0x4F, 0x52, 0x4F, 0x43, 0x45, 0x4C, 0x4C)
 0x08  | uint16   | versionMajor              | Breaking schema version (kCellVersionMajor = 1)
 0x0A  | uint16   | versionMinor              | Backwards-compatible minor version (kCellVersionMinor = 0)
 0x0C  | uint32   | headerFlags               | Cell flags (Encrypted, Compressed, HasDebugInfo)
 0x10  | int32    | gridX                     | Spatial grid X coordinate
 0x14  | int32    | gridY                     | Spatial grid Y coordinate
 0x18  | int32    | gridZ                     | Spatial grid Z coordinate
 0x1C  | uint8    | lodLevel                  | Hierarchy LOD level (0 = full detail)
 0x1D  | uint8    | coordinateSystem          | CoordinateSystem enum
 0x1E  | uint16   | layerId                   | World layer identifier
 0x20  | uint32   | compressionCodec          | 0=None, 1=LZ4, 2=Zstandard
 0x24  | uint32   | payloadCrc32              | Aggregate CRC32 of decoded blocks in TOC order
 0x28  | uint64   | uncompressedSize          | Sum of all entry uncompressedSize values
 0x30  | uint64   | compressedSize            | Cell-body bytes from featureTableOffset through final block, including TOC and zero padding
 0x38  | uint32   | featureTableOffset        | Byte offset to Feature Payload TOC from file start (must be 0x60 in v1)
 0x3C  | uint32   | featureTableCount         | Number of entries in Feature Payload TOC
 0x40  | uint8[32]| sha256Hash                | Canonical SHA-256 of fixed header and cell body
```

The header occupies bytes `0x00`–`0x5F` inclusive (96 bytes). Fields after `0x3C` continue through the 32-byte `sha256Hash` at `0x40`, so the first byte after the header is `0x60`.

#### Feature Provider Table of Contents (TOC)

Version 1 requires the TOC to begin immediately after the header at `0x60`.
An array of `featureTableCount` fixed 40-byte entries defines independently
compressed payload blocks:

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
    Required  = 1 << 0, // Must be decoded; failure to decode fails cell load
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

#### Canonical Cell Body And Integrity Ranges

`CoreEcs` is not a separate pre-TOC payload. Every cell contains exactly one
`CoreEcs` entry, it is the first TOC entry, and it carries `Required`; an empty
cell still contains a valid empty-core schema block. The built-in Core ECS
provider stages that entry into detached `RuntimeSceneStorage`. Terrain, Foliage,
Physics Mesh, Audio, Navigation Mesh, Destruction, and custom providers use the
same TOC/decode contract.

TOC entries are sorted by ascending raw `providerType`; duplicate provider types
are invalid. Custom extensions allocate unique values in the `0x8000`–`0xFFFF`
range rather than repeating `CustomExt`. Each `payloadOffset` is absolute,
8-byte aligned, and points to a non-overlapping block after the TOC. Padding
between the TOC and blocks or between blocks consists only of zero bytes.
Exactly one of `Required` or `Optional` must be set on every entry; `CoreEcs` is
always `Required`.

Block placement is canonical: the first offset is
`AlignUp(0x60 + featureTableCount * 40, 8)` and each following offset is
`AlignUp(previous.payloadOffset + previous.compressedSize, 8)`. The final block
ends exactly at `0x60 + header.compressedSize`; trailing body bytes are invalid.

Compression is per entry, not whole-cell:

- If `headerFlags.Compressed` is set, every entry is independently compressed
  with `compressionCodec`; one decompressor invocation produces one entry's
  decoded bytes.
- If it is clear, `compressionCodec` must be `None` and every entry must satisfy
  `compressedSize == uncompressedSize`.
- `FeaturePayloadFlags::Encrypted` applies to that entry only; the header
  `Encrypted` bit is the OR-summary of all entry encryption flags. Decryption
  precedes decompression. Entry `compressedSize` is the complete on-disk encrypted
  envelope size when encryption is enabled; the authenticated envelope format is
  versioned by the application-security contract and yields the codec input bytes.

The **cell body** is the contiguous byte range
`[featureTableOffset, featureTableOffset + compressedSize)`. It includes the TOC,
all zero padding, and every compressed entry block. `sha256Hash` is SHA-256 over
the complete cell artifact range `[0x00, 0x60 + compressedSize)`, with header
bytes `0x40`–`0x5F` replaced by 32 zero bytes during hashing. This protects all
fixed-header control fields, the TOC, padding, and compressed blocks. A standalone
file or package chunk has exactly that length; trailing bytes are invalid.

CRC fields use CRC-32/ISO-HDLC (IEEE polynomial `0x04C11DB7`, reflected
`0xEDB88320`, initial/final XOR `0xFFFFFFFF`). After an entry is decoded, its
`payloadCrc32` is checked over exactly its `uncompressedSize` decoded bytes. The
header `payloadCrc32` is the CRC of all decoded entry bytes concatenated in
canonical TOC order with no padding or separators. It may be calculated without
decoding skipped optional entries by the standard GF(2) `crc32_combine` operation
over each entry's `(payloadCrc32, uncompressedSize)` pair in TOC order.

Header `uncompressedSize` is the sum of entry uncompressed sizes. The matching
`StreamingCellChecksumEntry` in `world.index` must equal all four header aggregate
fields (`compressedSize`, `uncompressedSize`, `payloadCrc32`, and `sha256Hash`)
before any entry is decoded.

`WorldLayerDefinition::layerId`, `StreamingCellId::layerId`, the binary index,
and the cell header all use `uint16`. The cooker rejects worlds that cannot
assign unique layer IDs in `0`–`65535`; truncation is forbidden.

---

### 4. Versioning, Extension, and Error Contract

#### Version Negotiation Rules

1. **Major Version Incompatibility**: If `header.versionMajor != kCellVersionMajor`, the cell artifact is unconditionally rejected. The engine does not perform runtime schema transmutation across major versions; assets must be re-cooked.
2. **Minor Version Forward Compatibility**: If `header.versionMinor > kCellVersionMinor`, the runtime continues decoding if and only if all unknown feature payloads have the `FeaturePayloadFlags::Optional` flag set. If any unknown or unhandled feature payload has `FeaturePayloadFlags::Required`, the cell is rejected.
3. **Provider Versioning**: Each feature provider inspects its own `FeaturePayloadEntry::version`. If a provider receives a payload version it cannot support:
   - If `Optional`, the provider skips its payload slice and logs a diagnostic warning.
   - If `Required`, the provider returns an error, triggering cell load rejection.

#### Error Domain and Typed Error Codes

World streaming errors are integrated with Foundation's `ErrorCode` and `Result<T, Error>` model:

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
    RequiredPayloadMissing    = 12,// CoreEcs or another format-required payload is absent
    InvalidManifest           = 13,// world.index layout, ordering, hash, or mirror data is invalid
};

struct StreamingCellError {
    StreamingCellErrorCode code;
    StreamingCellId cellId;
    std::string message;
    std::vector<Diagnostic> diagnostics;
};

// Result alias for streaming cell decoders
template <typename T>
using StreamingResult = Result<T, StreamingCellError>;

} // namespace Horo::WorldStreaming
```

---

### 5. Decode, Cancellation, and Failure Lifecycles

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

#### Lifecycle Phase Guarantees

1. **Async I/O Phase**: Reads raw bytes from the archive into a linear buffer allocated from the streaming scratch pool. If `cancelToken.IsCancelled()` is signaled, the read is aborted and the buffer returned to the pool immediately.
2. **Integrity & Validation Phase**: Verifies artifact length, computes SHA-256 over the fixed header with its hash field zeroed plus the exact cell body, and compares it with both the header and `world.index` before trusting header control fields or TOC contents. It then validates magic bytes `HOROCELL`, version compatibility, exact v1 TOC position/size, canonical provider ordering, required single `CoreEcs`, zero padding, non-overlapping aligned entry ranges, aggregate sizes, and containment within the file/chunk. Finally, it recomputes aggregate uncompressed size and aggregate CRC via `crc32_combine` over TOC metadata and compares both with the header/index before decompression.
3. **Independent Block Decode Phase**: Iterates canonical TOC order. Each required or supported optional entry is decrypted if required, independently decompressed with the header codec into bounded staging memory, checked for exact decoded size, and verified against its entry CRC32. Unsupported optional entries may remain compressed and are not staged. Budget failure returns `AllocationLimitExceeded`; any size/hash/CRC mismatch returns `CorruptedIntegrity`.
4. **Provider Staging Phase**: Decoded entries are dispatched to registered `IFeatureStreamingProvider` instances. The required built-in Core ECS provider constructs entities in detached `RuntimeSceneStorage`; Terrain, Foliage, Physics Mesh, Audio, Navigation Mesh, Destruction, and custom providers construct resources in private candidate containers.
5. **Atomic Synchronization & Commit**: On the owner thread during `CommitDeferredLifecycleChanges`, the detached candidate is merged into the active `RuntimeScene` in a single structural transaction. If an error or cancellation occurred in phases 1–4, the candidate is discarded, feature providers drop their staged allocations, and the active scene remains completely untouched.

---

## Consequences

### Positive

- **Deterministic & Portable**: Fixed little-endian layout ensures bit-identical behavior across ARM64 macOS, x86_64 Linux, and x64 Windows.
- **Fail-Fast Integrity**: SHA-256 of the canonical fixed header and complete on-disk cell body catches control-field, TOC, block, and truncation corruption before decompression. Per-entry and aggregate CRC32 then validate each decompressor output and canonical decoded cell content.
- **Subsystem Decoupling**: Modular feature-provider TOC allows terrain, foliage, physics, navigation, audio, and destruction to evolve payload schemas independently without breaking core ECS layout.
- **Leak-Free Cancellation**: Cooperative cancellation and transactional candidate swapping guarantee zero partial entities or orphaned GPU/physics handles when volumes move rapidly.
- **Foundation Alignment**: Typed `StreamingResult<T>` and `StreamingCellErrorCode` integrate cleanly with Horo's error registry and diagnostic observation.

### Negative / Trade-offs

- **Cook Overhead**: Generating dual checksums (CRC32 and SHA-256) and layout manifests adds computational work during the asset cooking phase.
- **TOC Alignment Padding**: Enforcing 8-byte TOC, table, and payload-block alignment adds minor zero-padding overhead, which is negligible compared to typical multi-megabyte cell payloads. Version 1 defines no 64-byte alignment requirement.

## Verification Requirements

- Golden-byte fixtures validate the 192-byte `world.index` header, fixed table
  entry sizes, canonical sort order, NFC string table, zero padding, and manifest
  hash with the hash field zeroed.
- Cell fixtures validate the 96-byte header, 40-byte TOC entries, exactly one
  required first `CoreEcs` entry, Destruction/custom entries, and uint16 layer IDs
  without truncation.
- Integrity tests mutate fixed-header control fields, the header hash field, TOC,
  padding, compressed blocks, decoded blocks, and aggregate fields independently
  and assert failure at the specified artifact-SHA/per-entry-CRC/aggregate-CRC
  stage.
- Codec tests prove one decompressor invocation per selected entry and identical
  aggregate CRC results for compressed and uncompressed cells.
- Bounds tests cover overflow, overlap, misalignment, duplicate provider types,
  missing Core ECS, non-zero reserved/padding bytes, malformed string ranges, and
  non-canonical manifest ordering.

---

## Rejected Alternatives

1. **Monolithic Scene Archive (Unpartitioned ECS + Assets)**:
   - *Rejected*: Storing all feature data in a single monolithic ECS serialization stream prevents parallel provider decoding, prevents streaming budget sub-allocation (e.g. reserving terrain cache independently), and forces the engine to parse unused feature data on headless servers.
2. **Dynamic JSON/CBOR Format for Cooked Cells**:
   - *Rejected*: Text or dynamically-typed schemaless formats incur significant parse latency, high allocation rates, and lack explicit zero-copy memory alignment guarantees required for fast frame-rate streaming.
3. **Big-Endian / Native-Endian Binary Formats**:
   - *Rejected*: Variable endianness creates non-deterministic cross-platform cooked archives and complicates CI artifact verification across distinct target architectures. Standardizing on Little-Endian matches all current target hardware (x86_64, ARM64).
4. **Silent Degradation on Version Mismatch**:
   - *Rejected*: Silently ignoring missing or corrupted cell data causes invisible gameplay bugs (e.g. falling through missing physics colliders or invisible terrain). Incompatible versions are rejected with typed diagnostics.
