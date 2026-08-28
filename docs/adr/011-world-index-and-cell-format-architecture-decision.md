# ADR-011: World Index and Cell Format Architecture Decision

- **Status**: Proposed
- **Date**: 2026-08-28
- **Supersedes**: None
- **Scope**: Cooked world index manifest (`world.index`), streaming cell archive format (`.wcell` / `horopak` chunk), binary encoding, versioning, endianness, integrity hashing, feature-provider payloads, typed error model, and cancellation lifecycles
- **Issue**: [#1564](https://github.com/abdullahbodur/horo-engine/issues/1564) ([WST-004.1])
- **JIRA**: HORO-1564
- **Parent**: [#1521](https://github.com/abdullahbodur/horo-engine/issues/1521) ([WST-004])
- **Normative document**: [World Streaming Architecture](../architecture/runtime/world-streaming-architecture.md)

## Context

Horo Engine's open-world runtime streams spatial partitions (cells) asynchronously into active runtime scenes. Authoring data (scenes, terrain heightfields, foliage clusters, audio zones, physics colliders, navigation meshes) is transformed by Asset Pipeline cook stages into immutable, runtime-ready streaming partitions.

Prior to this decision:
- `docs/architecture/runtime/world-streaming-architecture.md` established high-level cell grid concepts, streaming volume priority scoring, memory budgets, and the cell lifecycle state machine (`Unloaded`, `Loading`, `Loaded`, `Unloading`, `Failed`).
- `docs/adr/003-artifact-identity.md` and `docs/architecture/runtime/asset-pipeline.md` defined general `.horo` package chunk containers and asset IDs, but lacked a normative specification for cooked world index manifests (`world.index`) and spatial cell chunks (`.wcell`).
- The runtime lacked an explicit binary layout contract, little-endian data guarantees, integrity hash algorithms, version negotiation semantics, feature-provider payload offset tables, and a Foundation-compliant typed error model for corrupted or version-skewed cell artifacts.

Ticket #1564 ([WST-004.1]) requires ratifying the normative specification for:
1. **Cooked World Index format (`world.index`)**: Manifest containing world bounding volume, partition grid dimensions, spatial cell hierarchy, layer definitions, streaming volumes, and checksum validation table.
2. **Cooked Cell Archive format (`.wcell` / `horopak` chunk)**: Header with magic number (`HOROCELL`), schema version, little-endian encoding, CRC32/SHA-256 integrity hash, compressed ECS payload, and feature-provider payload offsets (Terrain, Foliage, Physics mesh, Audio, Navigation mesh).
3. **Versioning & Extension**: Forward/backward compatibility rules, version negotiation, and typed rejection of corrupted or version-skewed cell artifacts via `Result<LoadedCellPayload, StreamingCellError>`.
4. **Lifecycle & Cancellation**: Normal, failure, rollback, and cancellation lifecycles during cell decode without leaking partial state.

## Decision

**The engine establishes a deterministic, little-endian, multi-layer cooked world index (`world.index`) and chunked cell container format (`.wcell` / `HOROCELL`) with independent feature-provider payload slices, mandatory CRC32/SHA-256 verification, strict version negotiation, cooperative cancellation at every decode phase, and typed error reporting via Foundation `Result<T, Error>`.**

### 1. Ratify-or-Revise Outcomes

| Area | Current Baseline | Ratified Outcome |
|---|---|---|
| **World Index Manifest (`world.index`)** | High-level conceptual descriptions only | **Ratified as a binary and JSON-mirrored manifest** specifying global bounding box, partition grid dimensions, spatial cell hierarchy (multi-LOD grid/quadtree), layer definitions with explicit ownership, pre-authored streaming volumes, and a cryptographic cell checksum table. |
| **Cell Container Magic & Header** | Undefined format | **Ratified as `HOROCELL` (8-byte ASCII: `0x48, 0x4F, 0x52, 0x4F, 0x43, 0x45, 0x4C, 0x4C`)** with a 64-byte fixed-size little-endian header containing schema version, flags, compression codec, uncompressed/compressed sizes, CRC32, SHA-256, and feature-provider TOC offsets. |
| **Endianness** | Native/implicit | **Ratified as strict Little-Endian (LE)** across all platforms and architectures (ARM64, x86_64). Big-endian host platforms (if ever supported) must swap on read/write. |
| **Integrity & Hashing** | General `.horo` archive TOC hash | **Ratified dual-tier integrity verification**: CRC32 for fast stream/decompression verification, and SHA-256 for cryptographic tamper/corruption detection against the `world.index` checksum table. |
| **Payload Decomposition** | Monolithic baked scene asset | **Ratified modular sliced layout**: Compressed core ECS payload (entities, components, archetypes) plus an extensible feature-provider payload table (Terrain, Foliage, Physics Mesh, Audio, Navigation Mesh). |
| **Versioning & Compatibility** | Monotonic version bump | **Ratified major/minor schema versioning with capability flags**: Minor revisions allow optional feature payload skipping (`FeaturePayloadFlags::Optional`); major revisions or missing required providers trigger typed rejection. |
| **Error Handling** | Generic exception or boolean failure | **Ratified Foundation-compliant typed errors**: `Result<LoadedCellPayload, StreamingCellError>` utilizing the `StreamingCellErrorDomain` with stable error codes. |
| **Decode Lifecycle & Cancellation** | Single-step load | **Ratified 4-phase staged pipeline**: (1) Async I/O, (2) Integrity Verification, (3) Decompression & Slice Extraction, (4) Feature Provider & ECS Instantiation. Cooperative cancellation token evaluated between and within each phase with zero residual state. |

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

struct WorldLayerDefinition {
    uint32_t layerId;
    std::string layerName;
    WorldLayerOwnership ownership; // WorldStreaming, GameplayScript, NetworkReplication
    WorldLayerFlags flags;
    float priorityMultiplier;     // multiplier applied to cells in this layer
};

enum class StreamingVolumeShape : uint8_t {
    Sphere = 0,
    Box    = 1,
    ConvexHull = 2,
};

struct WorldStreamingVolumeDefinition {
    uint32_t volumeId;
    StreamingVolumeType type;      // Camera, Gameplay, NetworkRelevance, Preload
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

### 3. Cooked Cell Archive Specification (`.wcell` / `HOROCELL`)

A cooked streaming cell is packaged either as an individual `.wcell` file or as a dedicated chunk within a `.horo` package archive (`horopak`).

#### Fixed Header Layout (64 Bytes, Little-Endian)

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
 0x24  | uint32   | payloadCrc32              | CRC32 of uncompressed payload
 0x28  | uint64   | uncompressedSize          | Total uncompressed payload byte count
 0x30  | uint64   | compressedSize            | Total compressed payload byte count on disk
 0x38  | uint32   | featureTableOffset        | Byte offset to Feature Payload TOC from file start
 0x3C  | uint32   | featureTableCount         | Number of entries in Feature Payload TOC
 0x40  | uint8[32]| sha256Hash                | SHA-256 cryptographic hash of compressed payload
```

#### Feature Provider Table of Contents (TOC)

Immediately following the fixed header (or located at `featureTableOffset`), an array of `featureTableCount` entries defines the sub-payloads:

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
2. **Integrity & Validation Phase**: Verifies magic bytes `HOROCELL`, endianness, schema version compatibility, and validates that all TOC offsets and sizes are strictly contained within `compressedSize`. Compares CRC32 and SHA-256 against `WorldIndexManifest`. Any mismatch immediately returns `StreamingCellErrorCode::CorruptedIntegrity` without executing decompression.
3. **Decompression & Slice Extraction Phase**: Decompresses the core ECS block and feature payload slices into bounded staging memory. Memory allocation is verified against the available `StreamingBudget`. If over-budget, returns `AllocationLimitExceeded`.
4. **Feature Provider & ECS Staging Phase**: Staged memory slices are dispatched to registered `IFeatureStreamingProvider` instances (Terrain, Foliage, Physics Mesh, Audio, NavMesh). Providers construct runtime-ready resources in private candidate containers. ECS entities are constructed in a detached `RuntimeSceneStorage` candidate.
5. **Atomic Synchronization & Commit**: On the owner thread during `CommitDeferredLifecycleChanges`, the detached candidate is merged into the active `RuntimeScene` in a single structural transaction. If an error or cancellation occurred in phases 1–4, the candidate is discarded, feature providers drop their staged allocations, and the active scene remains completely untouched.

---

## Consequences

### Positive
- **Deterministic & Portable**: Fixed little-endian layout ensures bit-identical behavior across ARM64 macOS, x86_64 Linux, and x64 Windows.
- **Fail-Fast Integrity**: Dual CRC32 and SHA-256 verification catches storage corruption and package truncation before expensive decompression or memory allocation.
- **Subsystem Decoupling**: Modular feature-provider TOC allows terrain, foliage, physics, navigation, and audio to evolve payload schemas independently without breaking core ECS layout.
- **Leak-Free Cancellation**: Cooperative cancellation and transactional candidate swapping guarantee zero partial entities or orphaned GPU/physics handles when volumes move rapidly.
- **Foundation Alignment**: Typed `StreamingResult<T>` and `StreamingCellErrorCode` integrate cleanly with Horo's error registry and diagnostic observation.

### Negative / Trade-offs
- **Cook Overhead**: Generating dual checksums (CRC32 and SHA-256) and layout manifests adds computational work during the asset cooking phase.
- **TOC Alignment Padding**: Enforcing 8-byte and 64-byte alignment boundaries adds minor padding overhead (under 128 bytes per cell), which is negligible compared to typical multi-megabyte cell payloads.

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
