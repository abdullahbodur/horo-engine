# Asset Pipeline

## Purpose

This document defines the asset pipeline for Horo Engine: how source assets
flow from raw files on disk into runtime-ready artifacts inside a packaged
release.

The pipeline is host-agnostic. The same importers, cookers, and packagers are
used by the GUI, CLI, and MCP.

## Asset Lifecycle

```text
Source Asset
    |
    v
Importer + Metadata
    |
    v
Editor Asset (authoring representation)
    |
    v
Cooker (per platform target)
    |
    v
Cooked Asset (runtime-ready)
    |
    v
Packager (chunks/bundles)
    |
    v
assets.horo (release archive)
```

## Asset Identification

Every imported asset has a **stable logical asset ID**. The ID must be:

- unique within the project
- independent of the source file path
- independent of the source file name
- stable across renames and moves
- encoded as one canonical lowercase UUID in the sidecar `assetId` field

The importer assigns the random 128-bit ID once and commits it to the sidecar:

```cpp
AssetId GenerateAssetId(const std::filesystem::path& projectRelativePath,
                        const std::array<uint8_t, 16>& randomGuid) {
    // randomGuid is generated once and stored in the sidecar metadata.
    // It survives renames and moves as long as the sidecar travels with the asset.
    return AssetId::FromBytes(randomGuid);
}
```

There is no parallel `assetGuid` identity. `assetId` is the single authority.
The derived `.horo/asset_index.json` stores both ID and normalized project path
lookups, but it never invents an ID and may always be rebuilt from valid
sidecars.

## Implemented AST-001A Baseline

`HoroEngine::Assets` is a backend-neutral target depending only on Foundation.
The implemented baseline contains canonical `AssetId`/`AssetTypeId` parsing,
immutable registry snapshots, sidecar rebuild, deterministic derived-index
storage, synchronous providers, and a bounded asynchronous load service over
the process `JobSystem`.

Registry candidates are mutable only on their owner thread while being built.
Publish first validates the complete candidate and then atomically replaces one
immutable shared state. Snapshots pin that state and are safe for concurrent,
allocation-free ID/path lookup while later revisions publish. A global duplicate
ID, duplicate canonical path, or portable case collision rejects the candidate
and preserves the last valid snapshot.

Project open uses the explicit two-stage form: the operation worker calls
`PrepareAssetRegistryCandidate()` to scan committed sidecars into an unpublished
candidate; the application owner thread first writes the deterministic derived
index and only then publishes the validated immutable registry revision. The
worker never replaces the registry consumed by runtime/editor readers.

An asynchronous request captures an `AssetRecord` and registry revision at
submission; workers never access mutable registry state. Registry replacement
therefore cannot invalidate an in-flight read. Completion carries its source
revision so a later authoritative installation boundary can reject stale work.
Service shutdown stops admission, cancels and joins owned requests, and prevents
callbacks after service destruction.

AST-001A does not implement importers, ID generation, cooking, archives, cache,
hot reload, or Content Browser integration. Runtime-scene consumption is the
separate AST-001B slice described below.

## Implemented AST-001B Runtime Scene Consumption

`HoroEngine::RuntimeScene` depends one-way on `HoroEngine::Assets` and resolves
versioned scene dependencies through the AST-001A snapshot/provider contracts.
The scene definition stores only stable AssetId and expected AssetTypeId values;
it never stores a source path, process-local provider handle, or mutable registry
record.

`QueuePreparation` captures an immutable registry snapshot and validates the
complete dependency set before starting I/O. Each accepted async request carries
the captured registry revision. Workers read provider-owned cooked bytes without
re-entering the registry. Registry replacement therefore cannot invalidate
in-flight memory, while the owner-thread activation boundary can still reject a
logically stale candidate by comparing revisions.

Loads are admitted in configurable bounded waves. The default candidate limits
are 1,024 dependencies, eight concurrent loads, and one GiB of logical resident
payload. Budget checks are incremental and fail fast. Reused payload leases and
new payloads both count once toward the candidate budget; physical storage
shared with the active scene is not copied. Matching AssetId/type payloads may
be reused independently when the registry revision is unchanged.

Every declared dependency is required in AST-001B. Missing records, type
mismatch, empty payload, provider failure, cancellation, queue pressure, budget
overflow, or authoritative revision change prevents activation and preserves
the previous active scene. No fallback asset or partial scene activation is
invented. `RuntimeSceneView` exposes allocation-free lookup of the opaque cooked
bytes; decoding and typed resource installation belong to later asset/material/
renderer slices.

### Collision Rules

- Two source files with the same name in different directories are independent
  assets and receive independent IDs.
- A duplicate ID or canonical path is a global ambiguity: no candidate is
  published and the previous registry remains active.
- Paths are compared both canonically and with a portable case fold so a project
  cannot become ambiguous on a case-insensitive filesystem.
- A source without a sidecar is untracked and excluded with
  `asset.registry.sidecar_missing`; AST-001A never generates an ID.
- Missing/empty identity, invalid UUID, malformed JSON, unsupported schema, and
  an orphan sidecar are distinct typed diagnostics. These record-local failures
  may publish one degraded snapshot containing the remaining valid records.

## Source Assets

Source assets are files created by external tools:

| Type          | Extensions                             |
|---------------|----------------------------------------|
| meshes        | `.fbx`, `.obj`, `.gltf`, `.glb`        |
| textures      | `.png`, `.jpg`, `.tga`, `.exr`, `.hdr` |
| materials     | `.horomat`                             |
| shaders       | `.vert`, `.frag`, `.comp`, `.hlsl`     |
| shader graphs | `.horoshadergraph`                     |
| scenes        | `.horo`                                |
| prefabs       | `.prefab`                              |
| audio         | `.wav`, `.ogg`                         |

Built-in scene primitives (cube, sphere, capsule, etc.) are not source assets.
They are described by `PrimitiveMeshDescriptor` and resolved by the engine
without import. See [Built-In Scene Primitives](./built-in-scene-primitives.md).

Source assets live in the project directory under `assets/`. They are not
shipped in a release unless explicitly requested by a developer package
profile.

## Import

Importing reads a source asset and produces an editor-side representation.
Import is triggered by:

- GUI drag-and-drop into the asset browser
- CLI: `horo-engine asset import <path>`
- MCP: `import_asset` tool
- project open / asset discovery scan

### Importer Interface

```cpp
class IAssetImporter {
public:
    virtual ~IAssetImporter() = default;

    virtual std::vector<std::string> SupportedExtensions() const = 0;
    virtual ImportResult Import(const std::filesystem::path& sourcePath,
                                const ImportOptions& options) = 0;
};
```

### Import Result

```cpp
struct ImportResult {
    std::string assetId;          // stable logical identifier
    std::string assetType;        // mesh, texture, material, ...
    std::filesystem::path importedPath;
    AssetMetadata metadata;
    std::vector<std::string> generatedDependencies;
    std::vector<ImportDiagnostic> diagnostics;
};
```

Each importer registers itself in `AssetImportService`:

```cpp
AssetImportService::RegisterImporter(std::make_unique<FbxImporter>());
AssetImportService::RegisterImporter(std::make_unique<ObjImporter>());
AssetImportService::RegisterImporter(std::make_unique<PngImporter>());
```

### Import Concurrency

Importers are stateless and thread-safe. The pipeline executes independent
imports concurrently using the engine job system. The default concurrency is
capped by the number of available hardware threads, with a lower priority than
runtime frame work.

```cpp
class AssetImportService {
public:
    std::vector<ImportResult> ImportMany(const std::vector<std::filesystem::path>& paths,
                                         const ImportOptions& options);
};
```

`ImportMany` schedules one job per asset, gathers results, and reports a single
aggregate diagnostic list. Ordering is preserved only when the caller requests
it.

## Parallel Import And Cook

Large projects may contain thousands of assets. Sequential import and cook is
unacceptable. The pipeline schedules independent import and cook jobs on the
engine job system.

Rules:

- Import jobs for different source files run concurrently.
- Cook jobs run concurrently once their dependencies are cooked.
- Dependency edges enforce ordering: a material cannot cook before its textures.
- The job graph is rebuilt from the asset dependency graph on every cook
  command.

```cpp
class AssetCookService {
public:
    // Cook all assets for a platform target using the job system.
    CookReport CookAll(const CookProfile& profile,
                       const std::vector<CookTarget>& targets);
};
```

The cook report contains per-asset success/failure, timing, cache hits, and
invalidated dependencies.

## Metadata

Every imported asset has a sidecar metadata file:

```text
assets/
    models/
        cube.fbx
        cube.fbx.horo          // metadata
```

Metadata includes:

- logical asset ID
- source file hash
- importer version
- metadata schema version
- import timestamp
- cooker settings
- user overrides (compression, scale, coordinate system)
- dependencies

```json
{
  "schemaVersion": 1,
  "assetId": "a1b2c3d4-e5f6-4890-abcd-ef1234567890",
  "assetType": "core.mesh",
  "sourceHash": "sha256:abc123...",
  "importerVersion": "1.2.0",
  "importSettings": {
    "coordinateSystem": "YUp",
    "unitScale": 1.0
  },
  "cookSettings": {
    "meshCompression": "draco",
    "generateTangents": true
  },
  "dependencies": [
    "texture_checker_001"
  ]
}
```

The canonical `assetId` is the stable identity. The sidecar file name follows
the source asset name; moving source and sidecar together preserves identity.

Rebuild is all-or-nothing at the snapshot boundary. A malformed or
version-skewed derived index is never partially consumed; it is discarded and a
sidecar rebuild is attempted. Read-only rebuild publishes only memory state.
Edit-mode rebuild may atomically replace `.horo/asset_index.json`. Filesystem
scans reject root escape and symlink ambiguity before opening source metadata.

## Schema Migration

Metadata sidecar files carry a `schemaVersion` field. When the importer or the
metadata schema changes, the pipeline compares the stored version with the
current version:

```cpp
enum class MigrationAction {
    None,       // schema is current
    Reimport,   // importer changed, re-import from source
    Upgrade,    // metadata format changed, rewrite sidecar in-place
    Error       // unsupported old version, manual intervention required
};
```

Rules:

- If `importerVersion` in metadata is older than the registered importer, the
  asset is automatically re-imported.
- If only the sidecar `schemaVersion` changed and the importer is the same, the
  metadata is upgraded in-place without touching the source asset.
- If the old schema is no longer supported, a diagnostic error is emitted and
  the asset is marked for manual re-import.

```cpp
MigrationAction AssetMigrationService::Evaluate(const AssetMetadata& metadata);
```

Migration is triggered on project open and on explicit "Validate Assets"
commands. It is never silent: the GUI import log, CLI output, and MCP result
report every migrated asset.

## Asset Configuration

Every imported asset carries two distinct configuration blocks in its sidecar
metadata:

- **Import settings**: how the source file is interpreted.
- **Cook settings**: how the editor asset is transformed into runtime artifacts.

This separation keeps interpretation concerns separate from runtime optimization
concerns. It also matches how artists and technical artists think about assets:
"What is this file?" is different from "How do we ship it?"

```json
{
  "schemaVersion": 1,
  "assetId": "a1b2c3d4-e5f6-4890-abcd-ef1234567890",
  "assetType": "core.texture",
  "importSettings": {
    "colorSpace": "sRGB",
    "wrapMode": "Repeat"
  },
  "cookSettings": {
    "generateMipmaps": true,
    "mipmapFilter": "Box"
  }
}
```

### Texture Configuration

| Setting           | Block  | Description                                                                     |
|-------------------|--------|---------------------------------------------------------------------------------|
| `colorSpace`      | import | `sRGB`, `Linear`, `HDR`. Defines how shaders sample the texture.                |
| `wrapMode`        | import | `Repeat`, `Clamp`, `MirrorRepeat`. Written into the sampler descriptor.         |
| `generateMipmaps` | cook   | Whether to build a mipmap chain.                                                |
| `mipmapFilter`    | cook   | `Box`, `Lanczos`, `Kaiser`.                                                     |
| `compression`     | cook   | `BC7`, `ASTC4x4`, `ETC2`, `None`. Defaults come from the active `CookProfile`.  |
| `maxResolution`   | cook   | Per-asset resolution cap. Final size is `min(source, profile.max, target.max)`. |

### Mesh Configuration

| Setting            | Block  | Description                                                  |
|--------------------|--------|--------------------------------------------------------------|
| `coordinateSystem` | import | `YUp`, `ZUp`. Normalized during import.                      |
| `unitScale`        | import | Scale factor from source units to engine units.              |
| `importNormals`    | import | `Imported`, `Calculate`, `CalculateSmoothed`.                |
| `meshCompression`  | cook   | `None`, `Draco`, `MeshOpt`. Platform profile may override.   |
| `generateTangents` | cook   | Required for normal-mapped meshes.                           |
| `lodSettings`      | cook   | Optional automatic LOD generation. Stored as derived assets. |

### Material Configuration

| Setting             | Block | Description                                                    |
|---------------------|-------|----------------------------------------------------------------|
| `blendMode`         | cook  | `Opaque`, `Masked`, `Translucent`, `Additive`.                 |
| `parameterDefaults` | cook  | Default color, float, vector, and texture slot values.         |
| `shadingModel`      | cook  | `Lit`, `Unlit`. Expanded later as more models are implemented. |

### Shader Configuration

| Setting        | Block | Description                                                                    |
|----------------|-------|--------------------------------------------------------------------------------|
| `entryPoints`  | cook  | Map of stage to function name: `{ "vertex": "vsMain", "fragment": "fsMain" }`. |
| `targetStages` | cook  | Which SPIR-V stages to produce: `Vertex`, `Fragment`, `Compute`.               |
| `defines`      | cook  | Preprocessor macros included in the variant key.                               |

### Audio Configuration

| Setting       | Block  | Description                                                     |
|---------------|--------|-----------------------------------------------------------------|
| `channels`    | import | `Mono`, `Stereo`. Spatialized sounds must be mono.              |
| `compression` | cook   | `PCM`, `Vorbis`, `Opus`. Profile and target aware.              |
| `sampleRate`  | cook   | `44100`, `22050`. Final rate is `min(source, profile, target)`. |

### Scene Configuration

| Setting           | Block  | Description                                                            |
|-------------------|--------|------------------------------------------------------------------------|
| `referenceMode`   | import | `Embed` or `Reference`. Large scenes should reference external assets. |
| `chunkAssignment` | cook   | Which release chunk this scene belongs to.                             |

### Cook Profiles

A `CookProfile` defines platform-specific defaults. It lives in the project
settings and is selected by `--profile` on the command line.

```json
{
  "id": "pc-low",
  "textureDefaults": {
    "maxResolution": 1024,
    "compression": "BC7",
    "generateMipmaps": true
  },
  "meshDefaults": {
    "maxLOD": 2,
    "meshCompression": "MeshOpt"
  },
  "audioDefaults": {
    "compression": "Opus",
    "sampleRate": 22050
  }
}
```

```json
{
  "id": "pc-high",
  "textureDefaults": {
    "maxResolution": 4096,
    "compression": "BC7",
    "generateMipmaps": true
  },
  "meshDefaults": {
    "maxLOD": 4,
    "meshCompression": "None"
  },
  "audioDefaults": {
    "compression": "Vorbis",
    "sampleRate": 44100
  }
}
```

### Configuration Precedence

Settings are resolved in the following order, from lowest to highest priority:

1. **Cook profile** — platform-specific defaults.
2. **Folder rules** — per-folder overrides defined in project settings.
3. **Asset sidecar** — per-asset overrides in the `.horo` metadata file.

```cpp
nlohmann::json ResolveConfig(AssetType type,
                              const CookProfile& profile,
                              const std::vector<FolderRule>& folderRules,
                              const AssetMetadata& sidecar);
```

A higher-priority override replaces a lower-priority value. Missing values fall
back to the next level.

### Folder Rules

Folder rules apply overrides to every asset of a given type under a project
path.

```json
{
  "folderRules": [
    {
      "path": "assets/ui",
      "types": [
        "texture"
      ],
      "overrides": {
        "cookSettings": {
          "maxResolution": 512,
          "compression": "BC7"
        }
      }
    },
    {
      "path": "assets/audio/sfx",
      "types": [
        "audio"
      ],
      "overrides": {
        "cookSettings": {
          "compression": "Opus",
          "sampleRate": 22050
        }
      }
    }
  ]
}
```

Folder rules are evaluated after the cook profile and before the asset sidecar.
They are useful for bulk policy changes without editing hundreds of sidecar
files.

## Editor Asset

Editor assets are the authoring representation. They are stored in a canonical
intermediate format that preserves editable properties. For example:

- a mesh editor asset stores raw vertex data and material slots
- a texture editor asset stores the uncompressed image and import settings
- a material editor asset stores parameter values and texture references

Editor assets are not loaded by the runtime directly. They must be cooked.

## Cooking

Cooking transforms editor assets into runtime-optimized formats. Cooking is
performed for one or more **platform targets**:

- `desktop-vulkan` — desktop GPU assets for the Vulkan backend
- `desktop-opengl` — desktop GPU assets for the OpenGL backend
- `headless-null` — validation-only artifacts for null-renderer tests and CI

Operating-system release targets select one or more supported cook targets. A
new graphics backend, such as a native mobile or platform-specific backend,
must define its render backend contract before the asset pipeline treats its
cooked output as a supported target.

Each target may produce different binary output for the same source asset. A
texture cooked for `desktop-vulkan` may keep a SPIR-V-compatible shader package
and GPU-compressed texture payload, while `headless-null` keeps only validation
metadata needed by tests.

Cooking is triggered by:

- explicit "Cook Assets" command
- play-in-editor mode (uses the current editor target)
- release build
- CLI: `horo-engine asset cook --target desktop-vulkan`
- MCP: `cook_assets` tool

### Cooker Interface

```cpp
class IAssetCooker {
public:
    virtual ~IAssetCooker() = default;

    virtual std::string AssetType() const = 0;
    virtual CookResult Cook(const AssetMetadata& metadata,
                            const CookTarget& target,
                            const std::filesystem::path& source,
                            const std::filesystem::path& output) = 0;
};
```

Cookers are idempotent and deterministic. Given the same source asset,
metadata, target, and cooker version, they produce byte-identical output.

### Cooked Asset Format

Cooked assets use a binary format with:

- versioned header
- type tag
- target platform tag
- compressed payload
- cryptographic hash

### Prefab Cooking

Prefabs are authored assets. During cooking, the pipeline resolves every prefab
reference placed in scenes and inlines the expanded objects into the cooked scene
artifact. Release builds do not ship raw `.prefab` files as runtime data. See
[Prefab Architecture](./prefab-architecture.md).

Cooked assets are stored under `build/<preset>/cooked_assets/<target>/` during
development and under the release staging directory during packaging.

### Cooked Format Compatibility

The cooked asset binary header includes a format version. The runtime supports a
sliding window of format versions:

- The runtime must read the current format version and at least the two previous
  versions.
- If a cooked asset is older than the supported window, it is automatically
  re-cooked on editor startup.
- If a cooked asset is newer than the runtime, the runtime reports an error and
  requests a compatible engine version.

This policy allows engine updates without immediately invalidating the entire
asset cache.

### Target-Specific Cooking

The `CookTarget` carries the supported backend profile, feature level, and
compression profile. Cookers read the target and choose appropriate formats:

```cpp
struct CookTarget {
    std::string backendProfile;  // desktop-vulkan, desktop-opengl, headless-null
    std::string featureLevel;    // vulkan11, opengl46, null-validation
    std::string compression;     // bc7, none, etc. selected by supported profile
};
```

A single source texture produces one cooked file per target:

```text
build/debug/cooked_assets/desktop-vulkan/texture_checker_001.cooked
build/debug/cooked_assets/desktop-opengl/texture_checker_001.cooked
build/debug/cooked_assets/headless-null/texture_checker_001.cooked
```

## Shader Pipeline

Shaders require a dedicated pipeline because they are source code that must be
transpiled and optimized for each target graphics API.

Pipeline stages:

1. **Preprocess**: resolve `#include`, macros, and shader graph nodes.
2. **Compile to SPIR-V**: Horo source dialect is compiled to SPIR-V as the
   canonical intermediate representation.
3. **Transpile to target**: SPIR-V is translated or packaged for the selected
   backend profile:
    - `desktop-vulkan` → SPIR-V with validated reflection metadata
    - `desktop-opengl` → GLSL or backend-approved translated shader payload
    - `headless-null` → reflection-only payload for validation and tests
4. **Variant generation**: produce permutations for static shader keywords
   (e.g., `USE_NORMAL_MAP`, `SHADOWS_ENABLED`). Variants are cached by keyword
   hash.
5. **Pack**: cooked shader binaries and variant tables are stored alongside
   material cooked assets.

```cpp
struct ShaderCookResult {
    std::filesystem::path backendPayloadPath;
    std::vector<ShaderVariant> variants; // per keyword permutation
    std::vector<std::string> dependencies; // included headers, textures
};
```

Shader variants are tracked in the dependency graph so that changing a keyword
or an included file invalidates the affected variants.

See the rendering subsystem documentation for shader binding, keyword systems,
and runtime variant selection.

## Dependency Tracking

The pipeline maintains an explicit **asset dependency graph**. Nodes are asset
IDs plus version metadata; edges represent references such as texture usage,
shader inclusion, material parameter, or scene object.

```cpp
struct AssetDependencyNode {
    AssetId id;
    std::string sourceHash;      // invalidates node if source changes
    std::string importerVersion; // invalidates node if importer changes
    std::string cookerVersion;   // invalidates node if cooker changes
    std::string targetProfile;   // desktop-vulkan, desktop-opengl, headless-null
};

struct AssetDependencyEdge {
    AssetId from;
    AssetId to;
    std::string kind; // texture_ref, shader_include, material_param, scene_child
};
```

The cooker writes edges into `CookResult`:

```cpp
struct CookResult {
    std::filesystem::path outputPath;
    CookTarget target;
    std::vector<std::string> dependencies; // asset IDs
    std::vector<std::string> outputs;      // generated asset IDs (LOD, mipmaps)
    bool success;
};
```

The build system uses this graph for incremental cooking:

- If a node's source hash, importer version, cooker version, or target changes,
  the node is re-cooked.
- If any dependency node is re-cooked, all dependent nodes are re-cooked.
- Cycles are rejected at graph build time with a diagnostic error.

### Derived Assets

Some cookers generate additional assets:

- mesh cooker generates LOD meshes
- texture cooker generates mipmaps
- shader cooker generates variant binaries

Derived assets receive their own asset IDs and are inserted into the dependency
graph as outputs of the source node. They are cooked together with their parent
and invalidated together.

## Incremental Cook

`Cook All` does not reprocess every asset. The pipeline compares the cache key
of each node against the stored cache entry. Only changed nodes and their
transitive dependents are cooked.

```cpp
struct CookReport {
    std::size_t totalAssets;
    std::size_t cacheHits;
    std::size_t cacheMisses;
    std::size_t failed;
    std::chrono::milliseconds elapsed;
};
```

Cache hit means the exact cooked output already exists in the asset cache and
can be reused.

## Packaging

Release packaging groups cooked assets into **chunks** (also called bundles).
Chunks are logical collections of assets that can be loaded and unloaded
together. Examples:

- `core` — engine shaders, default materials, fonts
- `level_01` — assets for the first level
- `shared_characters` — character models and animations used by multiple levels
- `dlc_weapons` — optional downloadable content

```cpp
struct PackageChunk {
    std::string id;
    std::vector<AssetId> assets;
    std::vector<std::string> dependencies; // other chunk IDs
    CompressionMode compression;
    bool encrypted;
};
```

Chunks are assembled into `assets.horo`, a deterministic archive:

```text
assets.horo
    header
    chunk table of contents
    per-chunk compressed entries
    integrity block
```

The archive:

- is addressed by logical asset ID
- supports compression per chunk
- supports encryption per chunk for protected content
- supports delta patches between versions
- includes deterministic ordering for reproducible builds
- includes manifest compatibility metadata

At runtime, the engine mounts required chunks and unloads chunks that are no
longer needed. This enables level streaming and memory budgeting.

See [Release Architecture](../release/release.md) for packaging, signing, and verification
details.

## Runtime Loading

Runtime code requests assets through the `IAssetProvider` interface. Two loading
modes are supported:

- **Synchronous load**: suitable for small assets that must be available
  immediately. Blocks the caller thread.
- **Asynchronous load**: suitable for large assets (meshes, audio, textures).
  Returns a future-like handle and avoids frame hitches.

```cpp
class IAssetProvider {
public:
    virtual ~IAssetProvider() = default;
    virtual Result<bool> Exists(AssetId id,
                                const CancellationToken& cancellation) const = 0;
    virtual Result<std::vector<uint8_t>> Load(
        AssetId id, const CancellationToken& cancellation) const = 0;
};
```

Synchronous access is the provider contract. `AssetLoadService` adds bounded
`JobSystem` scheduling and returns a move-only `AssetLoadHandle` with poll,
wait, cancel, and single-consumption result operations. Payloads are owned bytes;
providers enforce an allocation bound before reading.

AST-001A provides:

- `FilesystemAssetProvider`: resolves only canonical `<AssetId>.cooked` files
  beneath an injected cooked-artifact root and rejects symlink artifacts.
- `MemoryAssetProvider`: deterministic concurrent headless/test provider.

`ArchiveAssetProvider` remains part of the later packaging/runtime-loading
slice and is not implemented by AST-001A.

Runtime loaders (mesh, texture, shader, material) consume the bytes returned by
the provider and do not know whether the asset came from disk or archive.

### Loading Strategy

- Small assets (< 64 KiB) may be loaded synchronously on the main thread.
- Large assets are requested asynchronously before they are needed. The runtime
  polls `IsReady()` each frame and installs the asset once available.
- Streaming assets (audio, video, large textures) use a dedicated streaming
  reader that reads chunks over multiple frames.

## Asset Deletion

When a source asset is deleted, the pipeline must clean up dependent artifacts:

1. Remove the sidecar metadata.
2. Remove the imported editor asset.
3. Remove cooked outputs for all targets.
4. Remove cache entries.
5. Mark dependent assets (materials, scenes) as missing-reference and emit
   `AssetReferenceBrokenEvent`.

The deletion is reflected in the asset dependency graph before the next cook.
Zombie cooked assets must not remain in the build output directory.

## Runtime Asset Unloading

Release builds do not support hot reload, but they do support chunk-based
unloading. When a chunk is unmounted, its assets are released. The runtime
loader is responsible for invalidating handles that point to unloaded assets.

## Hot Reload

During editor development, changing a source asset triggers hot reload. The
orchestration is handled by `AssetHotReloadService`:

1. A file system watcher (or explicit import trigger) detects a source change.
2. The change is debounced (typically 250 ms) to avoid partial writes.
3. The source asset is re-imported.
4. Dependent assets are re-cooked using the dependency graph.
5. `AssetReloadedEvent` is published on `EngineDataBus`.
6. Runtime systems subscribe to the event and reload the asset.

```cpp
class AssetHotReloadService {
public:
    void OnSourceChanged(const std::filesystem::path& sourcePath);
    void Tick(); // called every editor frame
};
```

### Per-Asset-Type Reload Strategy

| Asset type | Reload strategy                                                                                |
|------------|------------------------------------------------------------------------------------------------|
| Texture    | Replace GPU texture in-place if format matches; otherwise defer reload to next frame boundary. |
| Mesh       | Recreate vertex/index buffers; update bounding box and LODs.                                   |
| Material   | Update uniform values and texture bindings without recreating mesh draws.                      |
| Shader     | Recreate pipeline state objects and rebind materials that use the shader.                      |
| Scene      | Full scene reload or incremental merge depending on the change scope.                          |

Hot reload is editor-only. Release builds do not support asset modification at
runtime; they rely on chunk loading and unloading.

## Asset Cache

Imported and cooked assets are cached by content hash. The cache key is:

```text
<assetId>:<sourceHash>:<importerVersion>:<cookerVersion>:<cookerSettingsHash>:<targetPlatform>:<metadataSchemaVersion>
```

Cache hits skip import/cook and reuse the previous output. Cache misses
reprocess the asset.

The cache lives in:

```text
build/<preset>/asset_cache/<target>/
```

### Cache Sharing

The cache directory can be shared across branches and machines:

- Local shared cache: `build/shared_asset_cache/`
- CI cache artifact: uploaded and downloaded between builds
- Future: authenticated network cache server with signed immutable entries

Cache entries are immutable and keyed by deterministic hashes, so sharing is
safe. Stale entries are evicted by LRU policy when a size limit is configured.

## Errors And Diagnostics

Import and cook errors are surfaced as structured diagnostics:

```cpp
struct ImportDiagnostic {
    enum class Severity { Info, Warning, Error } severity;
    std::string code;        // stable error code
    std::string message;     // human-readable
    std::optional<int> line; // source line when available
};
```

Diagnostics propagate to:

- GUI import log
- CLI stderr / JSON output
- MCP tool result
- `EngineDataBus::AssetImportedEvent`

## Adding A New Importer

1. Create `asset/importers/<Name>Importer.h` and `.cpp`.
2. Implement `IAssetImporter`.
3. Register in `AssetImportService`.
4. Add import tests in `tests/test_asset/`.
5. Add fixture files in `tests/fixtures/`.

## Adding A New Cooker

1. Create `asset/cookers/<Name>Cooker.h` and `.cpp`.
2. Implement `IAssetCooker`.
3. Register in `AssetCookService`.
4. Add cook tests.
5. Update metadata schema if new settings are required.

## CLI Commands

```bash
# Import an asset
horo-engine asset import assets/models/cube.fbx

# Import many assets in parallel
horo-engine asset import assets/models/*.fbx

# Cook all assets for the current editor target
horo-engine asset cook

# Cook all assets for a specific platform target and profile
horo-engine asset cook --target desktop-vulkan --profile pc-high

# Cook multiple targets
horo-engine asset cook --target desktop-vulkan --target desktop-opengl

# Cook a specific asset
horo-engine asset cook --asset a1b2c3d4-e5f6-4890-abcd-ef1234567890 --target desktop-vulkan

# Validate and migrate stale metadata
horo-engine asset validate

# Package assets for release with chunks defined in package profile
horo-engine release --output dist/ --target desktop-vulkan --profile pc-high

# Package all targets
horo-engine release --output dist/
```

## Related Documents

- [System Design](../foundation/system-design.md): module boundaries.
- [Engine Data Bus](../foundation/engine-data-bus.md): asset lifecycle events.
- [Concurrency And Job System](../foundation/concurrency-and-jobs.md): parallel import,
  cooking, cancellation, resource budgets, and progress.
- [Error And Diagnostics](../foundation/error-and-diagnostics.md): stable import, cook, and
  packaging diagnostics.
- [Rendering Architecture](./rendering-architecture.md): runtime GPU resource
  creation, upload, and shader/material validation.
- [Audio Architecture](./audio-architecture.md): cooked clip and stream formats,
  runtime loading, and audio memory budgets.
- [Prefab Architecture](./prefab-architecture.md): prefab asset format, expansion,
  and cook-time inlining.
- [Release Architecture](../release/release.md): packaging and verification.
- [Horo Package System](../packages/package-system.md): bulk asset import from packages.
- [Asset Import Modal](./asset-import-modal.html): HTML reference design for the
  import queue, diagnostics, and per-importer settings.
- [Asset Browser](../editor/asset-browser.html): HTML reference design for the main asset
  browser with folder tree, grid/list views, and preview pane.
- [Testing Architecture](../delivery/testing-architecture.md): import and cook tests.
