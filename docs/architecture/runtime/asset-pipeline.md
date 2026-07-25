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

## Future Parallel Import And Cook

Large projects may contain thousands of assets. Sequential import and cook is
unacceptable. The pipeline schedules independent import and cook jobs on the
engine job system.

This job-graph orchestration and the aggregate `CookReport` are future
architecture outside AST-001C. AST-001C retains bounded structured concurrency
for its supported operation but does not implement dependency-graph scheduling.

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

Target-profile selection is future architecture outside AST-001C. The examples
below preserve that contract without claiming that these profiles or their
desktop targets are currently accepted.

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

Cooking transforms tracked editor assets into deterministic runtime artifacts.
AST-001C is deliberately scoped as a backend-free proof of this boundary. Once
implemented, this slice will admit only the `headless-null` **cook-target ID**.
That ID is not the Null renderer and must not make the generic cooker or cook
operation renderer-dependent. Its artifacts will contain validation-only data
usable by headless applications and by runtime compositions that use the Null
renderer. This slice must not parse importer formats or create GPU resources.

OpenGL, Metal, Vulkan, and future interactive backends are equal future cook-
target peers. A target becomes available only when its owning module
provides a typed renderer capability contract that the host validates and
activates. No renderer is the base, default, or fallback for another, and an
unavailable target is a typed error rather than an implicit substitution.

### Modular Cooker Catalog

The canonical host-owned authority invariant is: the host owns bounded immutable
source storage, the typed cooker registry, cache keys, output placement, operation
state, and publication. Cooker contributions claim a stable contribution ID plus
an `AssetTypeId`/target pair. Built-in cookers and trusted extension cookers enter
the same descriptor validation, immutable snapshot, and lookup path; trust and
binary discovery remain Extension Manager concerns as defined by the
[Extension System](../extensions/plugin-system.md).

Registration is transactional. The host validates a complete candidate set and
publishes one immutable catalog snapshot only if every contribution is valid.
Conflicting claims are resolved only by project policy or explicit user choice
naming one exact contribution ID; registration or module load order is never a
tie breaker. Each cook operation pins one accepted snapshot for all of its jobs.
Disable and update remain restart-required in AST-001C, and live module unload is
not supported by this slice.

The internal C++ strategy interface is not a stable third-party binary ABI. Both
internal strategies and C-ABI cookers receive immutable borrowed views into the
host's bounded source storage; those views are valid only for the invocation and
must not be retained. External cooker binaries must use a versioned C ABI with
fixed-width values, opaque contexts, size/version-checked structures, borrowed
bounded byte spans, and host-owned output callbacks. No source-byte ownership is
transferred to a plugin. STL types, C++ exceptions, RTTI assumptions, allocator
ownership, and C++ object ownership do not cross that ABI. The adapter must
validate all output before it can enter the cache or a candidate cooked
generation, so external and built-in contributions cannot bypass host invariants.

### Determinism And Failure Invariants

A cooker receives an invocation-bounded immutable borrowed source view, immutable
host-canonical cooker-input metadata, a typed target, and cancellation. Before
invocation, the host serializes exactly the fields that the current metadata
schema declares cooker-visible into `canonicalCookerMetadataBytes`: fields use
schema-defined order, scalar encodings are fixed, strings are UTF-8, and repeated
or map values are sorted by their canonical encoded key. The serialization is
versioned and length-delimited. It is produced only after supported schema
migration and default resolution, then frozen for the invocation. Unknown sidecar
fields and nondeterministic bookkeeping such as import timestamps, source paths,
diagnostics, and editor/UI state are not cooker input. Its cryptographic digest
and the metadata `schemaVersion` are immutable canonical inputs; arbitrary mutable
metadata is not.

The host retains ownership of the bounded source and metadata storage for the
entire invocation. Given the same complete `CacheKeyV1` inputs defined in
[Incremental Cook And Cache Reuse](#incremental-cook-and-cache-reuse), the cooker
must deterministically emit byte-identical payload, dependency, and diagnostic
outputs through the host-owned writers. The host operation validates those
outputs, computes the complete requested `CacheKeyV1` digest, and only then
deterministically constructs the complete artifact-envelope bytes. The versioned
envelope records its format version, the full `CacheKeyV1` digest, asset ID/type,
target, source digest, bounded payload, and payload digest; malformed, oversized,
or integrity-mismatched artifacts are rejected as typed errors. The host applies
the same envelope and requested-key validation to freshly cooked output and cache
entries before either can enter a candidate generation.

The operation uses bounded structured concurrency. Missing cooker, malformed or
oversized input/output, cache corruption, cancellation, and cooker failure abort
the candidate generation. Accepted jobs are cancelled and joined before the
operation returns. No failed or cancelled operation changes the active
generation.

### Generation Publication

Cook output is immutable and content-addressed by the exact manifest bytes:

```text
<cooked-root>/<target>/
    generations/<manifest-sha256>/
        <asset-id>.cooked
        manifest.json
    current.json
```

`manifest.json` deterministically orders artifacts by canonical `AssetId` and
binds each artifact path, artifact digest, and full `CacheKeyV1` digest. While
validating fresh or cache-reused output, the host decodes each envelope and
requires its cache-key digest to byte-match the manifest entry's expected key in
addition to verifying its normal identity, target, size, and digest fields. The
host verifies all staged artifacts, publishes the complete generation directory,
and atomically replaces `current.json` last. `current.json` contains the active
manifest digest and its relative generation path; it is the sole authority
selecting a generation. Orphaned staging or inactive generations are never
inferred as current.

Runtime composition resolves `current.json` once, verifies the manifest and
relative path, and constructs the existing `FilesystemAssetProvider` with the
immutable generation directory. The provider continues to load canonical
`<AssetId>.cooked` files and needs no cooker/cache knowledge.

### Future Platform-Target Cooking Contracts

The contracts in this subsection are retained future architecture and are
outside AST-001C. They do not make desktop cooking, prefab expansion, profile
selection, or the listed triggers currently available.

Operating-system release targets will select one or more supported cook targets.
OpenGL, Metal, and Vulkan remain equal peers (`desktop-opengl`, `desktop-metal`,
and `desktop-vulkan`); none is a base, default, or fallback. A future native
mobile or platform-specific target must likewise publish and validate its typed
capability before selection. The same source asset may produce distinct bytes
for every selected target—for example, renderer-specific shader packages and
texture compression for desktop targets versus validation metadata for
`headless-null`.

Future cooking may be triggered by:

- an explicit **Cook Assets** command
- play-in-editor for the explicitly selected current editor target
- a release build or package operation
- CLI and MCP adapters described in [Future Host Commands](#future-host-commands)

The future `CookTarget` carries a stable target ID, feature level, and
compression profile. Profiles are explicit selection data, not inheritance from
another renderer target:

```cpp
struct CookTarget {
    std::string targetId;          // desktop-opengl, desktop-metal, desktop-vulkan, ...
    std::string featureLevel;      // target-owned validated capability level
    std::string compressionProfile;
};
```

Under the canonical host-owned authority invariant above, cookers never receive
source or destination filesystem authority. An internal strategy or C-ABI adapter
receives the invocation-bounded immutable borrowed input view plus the selected
target and writes payloads, dependencies, and diagnostics through bounded host-
owned callbacks. Callback-result validation may admit output only to a candidate
cooked generation; the host then stages and transactionally publishes that
generation. Catalog snapshot publication belongs exclusively to the separate
registration transaction described in [Modular Cooker Catalog](#modular-cooker-catalog).
A cooker cannot choose an output path or publish a generation.

Future cooked artifacts retain a versioned header, asset-type and target tags,
a bounded payload, and a cryptographic digest. The compatibility policy will be:

- the runtime reads the current cooked format and at least the two previous
  versions
- an artifact older than that window is re-cooked on editor startup
- an artifact newer than the runtime is rejected with a request for a compatible
  engine version

Prefab cooking is also future work. It will resolve prefab references in scenes
and inline expanded objects into the cooked scene artifact; release packages
will not ship raw `.prefab` files as runtime data. See
[Prefab Architecture](./prefab-architecture.md).

The future development output contract uses
`build/<preset>/cooked_assets/<target>/` as each target's `<cooked-root>/<target>`
location; packaging uses the release staging directory. Every target keeps a
distinct immutable generation and its own `current.json`, rather than writing
path-authoritative cooker output directly:

```text
build/debug/cooked_assets/desktop-opengl/generations/<manifest-sha256>/...
build/debug/cooked_assets/desktop-metal/generations/<manifest-sha256>/...
build/debug/cooked_assets/desktop-vulkan/generations/<manifest-sha256>/...
build/debug/cooked_assets/headless-null/generations/<manifest-sha256>/...
```

### Phase A Boundary

AST-001C must prove a real tracked source/sidecar to `headless-null` artifact flow,
verified cache reuse, atomic generation activation, and existing filesystem
provider loading. `headless-null` will be the only target admitted by this slice
once implemented. Importer-sidecar mutation, archives, hot reload, shader
compilation, GPU upload, and editor UI are separate later slices. Desktop target
formats must not be claimed until their typed renderer capabilities and consumers
exist.

## Shader Pipeline

The following shader pipeline is a future target and is not implemented by
AST-001C. Shader compilation/transpilation and every renderer-specific payload
remain outside Phase A; the list below does not activate a desktop cook target.

Shaders require a dedicated pipeline because they are source code that must be
transpiled and optimized for each target graphics API.

Pipeline stages:

1. **Preprocess**: resolve `#include`, macros, and shader graph nodes.
2. **Compile to SPIR-V**: Horo source dialect is compiled to SPIR-V as the
   canonical intermediate representation.
3. **Transpile to target**: SPIR-V is translated or packaged for a future
   typed backend capability; illustrative peers are:
    - `desktop-vulkan` → SPIR-V with validated reflection metadata
    - `desktop-opengl` → GLSL or backend-approved translated shader payload
    - `desktop-metal` → MSL or backend-approved translated shader payload
    - `headless-null` → reflection-only validation payload
4. **Variant generation**: produce permutations for static shader keywords
   (e.g., `USE_NORMAL_MAP`, `SHADOWS_ENABLED`). Each canonical keyword/permutation
   set is folded into the effective settings digest, whose schema is identified by
   the effective settings schema version. Variant cache identity derives from the
   complete `CacheKeyV1`; a keyword hash may be an index or display aid, but is
   never sufficient cache authority.
5. **Pack**: cooked shader binaries and variant tables are stored alongside
   material cooked assets.

```cpp
struct ShaderCookResult {
    LogicalOutputId backendPayload;
    std::vector<LogicalOutputId> variants; // host-approved keyword permutations
};
```

The shader cooker writes payload bytes, reflection, dependencies, and diagnostics
through bounded host-owned writers; the logical output IDs above refer only to
host-owned result slots and grant no filesystem or publication authority. Only
the host chooses staging paths, cache locations, manifest entries, generation
IDs, and publication paths.

Changing a variant's canonical keyword/permutation set changes its effective
settings digest and therefore its `CacheKeyV1`. In the future dependency-aware
pipeline, changed included-file dependency digests will change the versioned
dependency-aware key described below and invalidate affected variants and their
transitive dependents.

See the rendering subsystem documentation for shader binding, keyword systems,
and runtime variant selection.

## Dependency Tracking

The dependency graph below is a future incremental-cook contract. AST-001C does
not schedule dependency graphs and rejects unsupported non-empty dependency sets.
When added, graph target IDs remain typed capabilities rather than a fixed
renderer preference.

The pipeline maintains an explicit **asset dependency graph**. Nodes are asset
IDs plus version metadata; edges represent references such as texture usage,
shader inclusion, material parameter, or scene object.

```cpp
struct AssetDependencyNode {
    AssetId id;
    CacheKeyDigest artifactIdentity; // CacheKeyV1 now; dependency-aware key later
};

struct AssetDependencyEdge {
    AssetId from;
    AssetId to;
    std::string kind; // texture_ref, shader_include, material_param, scene_child
};
```

The cooker reports edges and generated outputs through host-owned bounded result
writers. The completed host-owned result contains logical identities only:

```cpp
struct CookResult {
    CookTarget target;
    HostOwnedCookOutputs outputs; // bounded, host-owned result
    bool success;
};
```

During execution, bounded host-owned payload, dependency, diagnostic, and derived-
asset writers build `HostOwnedCookOutputs`; cookers cannot return paths or publish
those results themselves. Derived IDs are logical asset identities approved by
the host, not filesystem names. Only the host chooses staging paths, cache
locations, manifest entries, generation IDs, and publication paths.

The build system uses this graph for incremental cooking:

- Any `CacheKeyV1` component change invalidates the node's cached artifact.
- Importer changes invalidate a node only when re-import produces a changed
  canonical source, cooker-input metadata, or effective settings digest; importer
  identity or version is not an unbound hidden cache-key input.
- Under the future dependency-aware key, a changed dependency artifact identity
  or digest invalidates the node and every transitive dependent.
- Cycles are rejected at graph build time with a diagnostic error.

### Derived Assets

Some cookers generate additional assets:

- mesh cooker generates LOD meshes
- texture cooker generates mipmaps
- shader cooker generates variant binaries

Derived assets receive their own asset IDs and are inserted into the dependency
graph as outputs of the source node. They are cooked together with their parent
and invalidated together.

## Incremental Cook And Cache Reuse

AST-001C must compute a canonical cache-key digest from this complete tuple:

```text
CacheKeyV1(
    asset identity/type,
    exact source digest,
    canonical cooker-input metadata digest,
    metadata schema version,
    cooker contribution identity/version,
    effective settings digest,
    effective settings schema version,
    typed target ID,
    profile-presence tag byte (`0x00`; zero profile bytes),
    artifact envelope format version
)
```

`CacheKeyV1` is the canonical complete cache key for AST-001C. It intentionally
contains no dependency-content field because this slice rejects every non-empty
dependency set rather than pretending that V1 covers dependency content. The
typed target-ID field is the canonical length-delimited `headless-null` ID in the
current slice. It is followed by exactly one profile-presence tag byte. AST-001C
requires tag `0x00`, followed by zero profile bytes. Tag `0x01` is reserved for a
future compatible profile-bearing extension and is unsupported and rejected by
V1 in this slice. An omitted tag, an empty profile string, and a null profile are
not alternate encodings of absent profile data.

The tuple encoding is explicitly versioned; every variable-width field is length-
delimited, while the profile-presence tag is exactly one byte. It is not ad-hoc
string concatenation. The metadata digest is over only canonical
cooker-visible semantic metadata, never nondeterministic timestamps, paths,
diagnostics, or editor/UI state. Any byte-affecting input, contract, schema, or
format-version change changes or invalidates the key. A hit may be used only after
the immutable entry's artifact envelope, size bounds, and cryptographic digests
are verified and the envelope's full cache-key digest is compared byte-for-byte,
in constant time, with the requested `CacheKeyV1` digest. A miss must invoke the
selected cooker. An artifact found under the wrong key path is corruption handled
by the configured typed miss-or-failure policy, never a verified hit. A corrupt,
truncated, oversized, or symlinked entry must produce a typed cache failure and
must never be treated as active output.

A successful cook report must distinguish cooked artifacts from verified cache
hits. For identical inputs, fresh cooking and cache reuse must produce byte-
identical artifact and manifest bytes. Dependency-graph scheduling is not part of
AST-001C; a non-empty unsupported dependency set must fail explicitly rather than
being cooked in an accidental order.

Future dependency-aware cooking, outside AST-001C, must introduce a separate
versioned extension rather than silently changing V1. For example:

```text
CacheKeyV2(
    every CacheKeyV1 component,
    canonical ordered digest of dependency artifact identities/digests
)
```

The host canonicalizes dependency order before hashing; graph iteration or plugin
reporting order cannot affect identity. Any `CacheKeyV1` component change
invalidates the node. Importer changes do so only through changed canonical
source, cooker-input metadata, or effective settings digests, never through an
unbound hidden importer input. A changed dependency artifact identity or digest
changes `CacheKeyV2` and invalidates the node and every transitive dependent.
That future operation compares these complete versioned keys with stored immutable
entries and reports aggregate results without weakening per-entry verification:

```cpp
struct CookReport {
    std::size_t totalAssets;
    std::size_t cacheHits;   // verified immutable entries only
    std::size_t cacheMisses;
    std::size_t failed;
    std::chrono::milliseconds elapsed;
};
```

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

AST-001C's cache is a derived, immutable content-addressed store. The canonical
key is the digest of the versioned canonical `CacheKeyV1` tuple defined in
[Incremental Cook And Cache Reuse](#incremental-cook-and-cache-reuse): asset
identity/type, exact source digest, canonical cooker-input metadata digest,
metadata schema version, cooker contribution identity/version, effective settings
digest and schema version, typed target ID, the required `0x00` absent-profile tag
with zero profile bytes, and artifact envelope format version. Tag `0x01` is
reserved but unsupported and rejected in AST-001C; omitted, empty, and null
profiles are not alternate V1 encodings. The variable-width fields remain length-
delimited to prevent concatenation ambiguity. The digest, rather than a source
path or registration order, selects the entry. This is the complete key for
AST-001C, which rejects non-empty dependency sets; future dependency content
requires the separate versioned extension described there.

```text
<cache-root>/<first-two-digest-hex>/<remaining-digest-hex>.cooked
```

Writers stage a unique sibling file and publish without overwriting an existing
key. If another writer wins, the host decodes and verifies the existing envelope,
including a constant-time byte comparison of its full cache-key digest with the
requested key, and verifies the existing bytes are identical before discarding
its staged file. Readers decode the envelope, enforce configured bounds, verify
its normal identity, target, size, and cryptographic digest fields, and compare
its full cache-key digest byte-for-byte, in constant time, with the requested
`CacheKeyV1` digest before reporting a hit. An otherwise valid artifact under a
wrong key path is corruption handled by the configured typed miss-or-failure
policy, never a verified hit. Cache corruption, truncation, oversize, symlinks,
and cancellation produce typed errors and cannot alter `current.json`.

Changing the artifact envelope format necessarily invalidates prior cache entries
because its format version is part of the key. A versioned compatibility reader
may reuse an older envelope only under a separate, explicit compatible-cache
policy; it must validate that old format and may not treat it as a hit under the
new format version's key.

Shared-machine, CI/network cache transport, eviction, and signing are future
policies. They may move immutable entries but cannot weaken key construction,
verification, or generation publication authority.

### Future Cache Locations And Sharing

The following deployment and retention policies are outside AST-001C:

- per-preset local cache root: `build/<preset>/asset_cache/<target>/`
- local cache shared across branches: `build/shared_asset_cache/`
- CI cache artifacts uploaded and downloaded between builds
- an authenticated network cache with signed immutable entries

These locations contain the same verified content-addressed entries; a location
or transport is never part of artifact authority. When a size limit is
configured, stale cache entries are evicted by LRU policy. Eviction may remove
only derived cache entries: it cannot mutate an immutable cooked generation,
replace `current.json`, or relax digest and envelope verification on a later hit.

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

1. Define a stable cooker contribution ID and its exact `AssetTypeId`/target
   claims.
2. Implement the internal strategy for a built-in contribution, or expose the
   versioned C ABI function table for a trusted external binary.
3. Submit the descriptor and adapter through one candidate registration
   transaction; do not mutate the live catalog directly.
4. Add deterministic artifact, bounds, cancellation, malformed output, and
   conflict-policy tests. External binaries also require a separately compiled C
   fixture proving ABI version/size and ownership rules.
5. Verify the selected contribution by exact ID; never depend on load order.

## Future Host Commands

CLI/MCP adapters are outside AST-001C. Future GUI, CLI, and MCP cook adapters
must invoke one typed cook application operation rather than duplicate cooker,
cache, or generation-publication logic. Equivalent typed operations own import,
validation, and release/package behavior. The generic cook operation must not be
called "headless": `headless-null` will be the only target ID admitted by this
slice once implemented. Until a later slice admits another target, the operation
must return a typed unavailable-target error for every other target; none may be
substituted.

The following block is a non-executable sketch of future command shapes. No CLI
or MCP cook adapter described here exists in AST-001C.

```text
# FUTURE COMMAND SHAPES — documentation only; not executable in AST-001C
# Import one asset
horo-engine asset import assets/models/cube.fbx

# Batch import
horo-engine asset import assets/models/*.fbx

# Cook all assets for the explicitly selected current editor target
horo-engine asset cook

# Cook an explicit target and profile (future target acceptance)
horo-engine asset cook --target desktop-vulkan --profile pc-high

# Cook multiple equal-peer targets (future target acceptance)
horo-engine asset cook --target desktop-vulkan --target desktop-metal --target desktop-opengl

# Cook a specific asset for a specific target
horo-engine asset cook --asset a1b2c3d4-e5f6-4890-abcd-ef1234567890 --target headless-null

# The sole target this slice will admit once implemented
horo-engine asset cook --target headless-null

# Validate and migrate stale metadata
horo-engine asset validate

# Package a release for an explicit target/profile
horo-engine release --output dist/ --target desktop-vulkan --profile pc-high

# Package all configured release targets
horo-engine release --output dist/
```

Future MCP adapters expose the same catalog—single and batch import, current-
target cook, explicit/multiple-target cook, specific-asset cook, validation, and
release/package—and map their typed requests and results to the same application
operations as CLI. Existing names such as `import_asset` and `cook_assets` are
adapters only; they do not own pipeline policy.

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
