# ADR-138: Terrain Source, Cooked Tile, Cache and Streaming Ownership

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: Terrain/foliage imported and canonical source ownership, deterministic cook inputs/outputs, dataset/tile manifests, cache authorities, world-streaming integration, decoded residency, consumer-native artifacts, replacement generations, cancellation and shutdown
- **Issue**: [TRF-002.1](https://github.com/abdullahbodur/horo-engine/issues/1937)
- **Jira**: [HORO-1893](https://horo-engine.atlassian.net/browse/HORO-1893)
- **Related**: [ADR-008](008-error-model-exception-boundary-and-registry.md), [ADR-010](010-job-waiting-and-operation-store-ownership.md), [ADR-012](012-world-streaming-partition-authority-and-subsystem-boundaries.md), [ADR-023](023-world-index-and-cell-format-architecture-decision.md), [ADR-027](027-renderer-resource-identity-and-descriptors.md), [ADR-034](034-gpu-memory-and-residency-ownership.md), [ADR-085](085-physics-shape-authoring-cook-and-runtime-boundary.md), [ADR-105](105-navigation-asset-and-scene-ownership-boundary.md), [ADR-137](137-terrain-foliage-ownership-data-tier-and-lifecycle.md)
- **Normative documents**: [Asset Pipeline](../architecture/runtime/asset-pipeline.md), [Terrain and Foliage Architecture](../architecture/runtime/terrain-and-foliage-architecture.md), [World Streaming Architecture](../architecture/runtime/world-streaming-architecture.md)

## Context

ADR-137 establishes TerrainApi/Runtime ownership and separates authored, cooked,
Scene, runtime and integration data. The current Terrain document still says that
heightmaps can be imported from several formats, tiles are rasterized, an LRU cache is
used and Terrain/Foliage payloads live in streaming cells, without defining the
canonical source, exact cooked generation, cache authorities or load/replace protocol.

The generic Asset pipeline already owns tracked source bytes, cooker registration,
dependency-aware keys, immutable cache entries, staging and atomic `current.json`
publication. World Streaming owns cell demand, priority, aggregate reservation and
cell commit/eviction. Render, Physics and Navigation own their native resources. If
Terrain duplicates any of these, two caches may claim the same bytes, a tile may load
outside cell budget, or a source/cook watcher may replace an active terrain behind
readers.

Large terrain also cannot be one eagerly loaded artifact. Dataset and tile manifests
must preserve seam/dependency integrity while allowing bounded independent reads.
Hot reload cannot mix incompatible old/new neighbor tiles, and cancellation or cache
corruption must preserve the last valid published and resident generations.

This ADR assigns source, derived output, cache, streaming and replacement ownership.
TRF-002.2 owns exact external heightmap parsing/canonical normalization. Later TRF-002
tickets may freeze binary layouts and algorithms without changing these authorities.

## Decision

### 1. Assets owns generic storage/publication; Terrain owns domain semantics

| Responsibility | Authority |
|---|---|
| Source path trust, tracked `AssetId`, sidecar/source byte snapshot and immutable borrow | Assets/Application import operation |
| External heightmap/container decode and canonical Terrain source schema/validation | Terrain Import/Model contribution |
| Generic cooker catalog, dependency scheduling, cache keys/store, staging, generation manifest and atomic publication | Assets Cook |
| Terrain tiling, seam/mip/LOD derivation, layer/hole/spline semantics, foliage clustering and Terrain artifact schemas | Terrain Cook contribution |
| Selected immutable cooked generation and runtime byte leases | Assets provider |
| Dataset/tile/cluster validation, decoded cache, logical residency and feature-local eviction | TerrainRuntime |
| Cell demand/priority, aggregate reservations, provider sequencing and cell commit/evict barrier | World Streaming authority |
| GPU/native render resource cache/residency | Render |
| Solver-native collision shape/body cache and lifetime | Physics |
| Provider-native navigation tile/cache and query publication | Navigation |

Terrain contributions use the same host-owned importer/cooker catalogs and bounded
borrowed source/output writers as other domains. They create no second asset ID,
dependency graph, scheduler, cache root, generation directory, current pointer or
package authority. Assets treats Terrain payloads as bounded typed contribution data
and does not interpret height, layer, seam, foliage placement or LOD semantics.

Provider descriptors are inert. Registration validates stable contribution/type/schema
IDs and dependencies but performs no source scan, decode, cache access or global
mutation. The application host explicitly composes the contributions.

### 2. External source and canonical authoring source are separate

PNG/EXR/RAW/GeoTIFF/Houdini files are untrusted import inputs, not runtime assets or
canonical height truth. Import captures exact bytes plus explicit options and produces
a detached canonical Terrain source candidate containing finite grid/coordinate/units,
sample encoding, height/layer/hole data, provenance and diagnostics.

TRF-002.2 freezes format-specific parsing, orientation, endianness, nodata, color/
numeric conversion, units, geospatial policy and canonical encoding. Until then, no
importer may infer these from filename/platform defaults and publish a permanent schema.

The editor-owned Terrain source asset is the only mutable authoring truth after import.
It records stable Terrain identity and semantic revision, not the external absolute
path as identity. Reimport is an explicit transaction comparing expected source/
document revision; it cannot overwrite sculpt/paint changes silently. Imported bytes,
editor selection/brush state, timestamps and filesystem enumeration order are not
runtime data.

Source admission checks dimensions, sample/layer counts, integer overflow, decoded
bytes/work, finite numeric values, coordinates/bounds and project trust before
allocation/publication. Failure leaves the existing source revision unchanged.

### 3. One immutable dataset manifest indexes independently verifiable tiles

Terrain Cook emits a bounded `CookedTerrainDataset` envelope containing:

```cpp
struct CookedTerrainDatasetManifest {
    TerrainDatasetId dataset;
    TerrainContentRevision content;
    TerrainCookSchemaVersion schema;
    TerrainFeatureTier tier;
    TerrainCoordinateProfile coordinates;
    TerrainDatasetBounds bounds;
    TerrainDependencyFingerprint dependencies;
    BoundedArray<TerrainTileManifestEntry> terrainTiles;
    BoundedArray<FoliageClusterManifestEntry> foliageClusters;
    TerrainDatasetDigest digest;
};
```

Each `TerrainTileManifestEntry` describes exactly one tile and one LOD. The bounded
manifest therefore scales with tile-times-LOD count and is paged/indexed under declared
hard limits; streaming may batch adjacent entries but never treats one artifact as an
ambiguous multi-LOD payload. Each sorted tile/cluster entry carries its stable ID,
integer grid/LOD address, exact
bounds, semantic/seam/dependency signatures, artifact identity, byte size, content
digest, peak decode/prepare estimates and declared consumer payloads. Entries are
ordered by canonical typed address, never filesystem or job completion order.

Tile payloads are immutable independently readable artifacts. They contain bounded
Terrain-owned neutral height/layer/hole/foliage/render-source data and references to
separate dependencies. They do not contain source paths, editor state, runtime handles,
world-streaming reservations, native GPU buffers, solver shapes/bodies, navigation
provider objects or backend commands.

Collision/navigation neutral source descriptors may be declared Terrain outputs, but
ADR-085 Physics and ADR-105 Navigation own their separately keyed consumer artifacts
and native conversion. A Terrain cache hit never proves a native consumer hit or
readiness.

The dataset envelope binds the complete canonical manifest bytes. Missing, duplicate,
overlapping, out-of-bounds, digest-mismatched or seam-incompatible entries fail the
candidate. Runtime never scans a directory and infers membership.

### 4. Cook identity closes over every semantic input

Terrain extends the Assets dependency-aware cook key with a canonical fingerprint of:

- exact canonical Terrain source identity/revision/digest;
- every material/layer/texture/mesh/foliage-rule/spline/mask dependency artifact;
- coordinate/unit, tiling, border/seam, LOD/mip, compression/quantization and hole
  policies;
- selected Terrain tier and exact finite effective limit/profile revision;
- Terrain source/cooked/manifest/payload/cooker algorithm versions;
- generic Assets cooker/target/envelope/toolchain identity; and
- each neutral consumer-source schema whose emitted bytes are included.

No timestamp, path, editor layout, machine locale, thread count, job order, cache
location or active runtime/provider handle enters semantic identity. A provider-native
Physics/Navigation/Render artifact fingerprint belongs to that consumer's own derived
key, not the Terrain tile key.

Same complete inputs produce byte-identical canonical manifest/tile payloads and
diagnostics order. Any byte-affecting input/version changes the key. Fresh output and
cache reuse pass the same envelope, requested-key, size, digest, bounds, dependency and
Terrain semantic validation before generation assembly.

### 5. Cache names identify different owners and lifetimes

“Terrain cache” is never used without one of these explicit classes:

| Cache/store | Owner | Contents/lifetime | Eviction authority |
|---|---|---|---|
| Terrain source/document storage | Editor/Assets | Mutable canonical authored revisions | Document/asset transaction only; not a cache |
| Cook cache | Assets | Immutable content-addressed contribution artifacts | Assets cache policy; never changes active generation |
| Cooked generation | Assets | Verified manifest and immutable packaged/runtime artifacts selected by `current.json` | New atomic generation publication/package lifecycle only |
| Provider byte/chunk cache | Assets provider/package system | Immutable artifact bytes under provider leases | Provider policy after leases release |
| Decoded Terrain tile/cluster cache | TerrainRuntime | Horo-neutral decoded/prepared payloads for one dataset generation | Terrain policy inside granted World Streaming slice |
| GPU residency/cache | Render | Native resources and frames-in-flight retirement | Render under ADR-027/034 |
| Collision artifact/body cache | Physics | Solver artifacts and installed bodies | Physics |
| Navigation artifact/tile cache | Navigation | Neutral/provider artifacts and installed query topology | Navigation |

Each physical allocation has one charge identity and one release acknowledgement.
Shared leases may project the same charge across ledgers but cannot count it twice or
make it unaccounted. Evicting a derived cook-cache entry does not unload a resident
tile; evicting decoded Terrain data cannot release a GPU/Physics/Navigation lease;
publishing a new generation does not delete the old while readers remain.

TerrainRuntime may evict only disposable decoded/prepared data inside its admitted
slice. A tile required by an Active/pinned cell, mutation candidate, frame snapshot or
consumer install is leased and not evictable. Pressure outside the slice or requiring
cell removal is reported to World Streaming rather than solved by a private global LRU.

### 6. World Streaming commands; Terrain executes feature-local residency

World Streaming resolves demands into a generation-scoped command equivalent to:

```cpp
struct TerrainResidencyRequest {
    StreamingCellHandle cell;
    TerrainRuntimeHandle terrain;
    TerrainContentRevision content;
    BoundedArray<TerrainTileId> tiles;
    BoundedArray<FoliageClusterId> clusters;
    StreamingReservationToken reservation;
    TerrainRequiredReadiness required;
    StreamingRequestGeneration request;
};
```

The request contains approved work and reservation, not a native target or mutable
cache pointer. Terrain validates exact dataset/manifest membership, revisions, bounds,
cost and reservation before starting one operation-owned task group. Unknown cost is
not zero. Growth must be reserved before allocation.

Terrain owns the per-request feature pipeline:

```text
Absent -> Requested -> Loading -> Decoded -> PreparingConsumers -> Ready
   \          \           \              \                    -> Failed
    +---------------------------------------------------------> Retiring -> Absent
```

Assets provider reads immutable exact artifacts. Workers validate envelope/digest,
decode into operation-owned candidates and produce Horo-only consumer descriptors.
Render/Physics/Navigation prepare their own exact-generation resources through owner
queues. Terrain reports readiness evidence; World Streaming alone commits the cell
after every required provider is prepared.

Terrain has no independent camera/distance cell scheduler. It may order bounded units
inside an already admitted request, but cannot change global priority, invent demand,
borrow reservation, commit/evict a cell or silently lower required readiness. Optional
detail fallback must be declared in the captured Terrain plan and reported.

### 7. Loading, cancellation and failure publish no partial tile

All source/provider reads, allocation, decode and consumer preparation occur in owned
candidates. No snapshot exposes partially decoded height/layers, half a foliage
cluster, or a tile whose required consumers are not prepared.

Cancellation before cell/tile commit stops new work, joins/yields workers and retires
candidate consumer resources in reverse ownership order. Cancellation is not evidence
that provider/native work stopped; payload/reservation/module leases remain until each
owner acknowledges retirement. A cancelled candidate never becomes a cache hit or
active generation by side effect.

Malformed/corrupt/oversized bytes, missing dependencies, stale dataset/cell/request/
capability revision, reservation denial and consumer failure return typed results to
World Streaming. Required failure aborts the aggregate cell candidate. Optional
failure is visible only when the plan declared absence/fallback. The prior resident
tile/cell generation remains unchanged.

### 8. Replacement is manifest-driven and seam-safe

Assets atomically publishes a complete new cooked generation; that alone does not
mutate a running TerrainRuntime. Editor hot reload/application code pins the new Assets
generation, validates the dataset manifest and requests a Terrain replacement candidate
against the old content/runtime generation.

Seam signatures hash canonical geometric and material boundary samples plus seam-schema
and cook-profile identity. They deliberately exclude tile coordinates and neighbour
IDs so identical boundary content remains cache-reusable; the placement manifest
separately validates the exact neighbour identities, revisions and adjacency. The
manifest declares a dependency/seam closure for changed tiles. Terrain may retain
unchanged old payloads only when their full artifact identity and neighbor seam/
dependency signatures match the new manifest. Otherwise it prepares the affected
closure together. A visible/physical/nav-required cohort cannot mix incompatible
old/new seams or consumer-source schemas.

The coordinator publishes one new immutable Terrain root/content revision at a safe
point after required resident cohorts and consumers prepare. Unrequested tiles refer
only to the new manifest and load from it later. Old snapshots/cells/GPU/Physics/
Navigation state remain pinned to the old generation until retirement. Equality of
tile coordinate, `AssetId` or cache path cannot revalidate them.

Failure, cancellation, stale source, newer replacement supersession or cache corruption
destroys only the candidate and preserves the old Active root. Runtime never follows
`current.json` repeatedly, combines files from two generation directories or repairs a
manifest from directory contents.

### 9. Runtime mutation does not rewrite cook/cache truth

ADR-137 runtime deformation and dynamic foliage live in separate mutation-overlay
state. They may reference stable base tile/type identities and exact content/mutation
revisions, but never alter source assets, cooked payloads, cache entries, generation
manifests or Assets provider bytes.

Persistence/replication records product-owned semantic changes or typed overlay state,
not decoded-cache/native buffers. A recook/replacement explicitly rebases or rejects
overlays by later TRF policy; it cannot apply them to a coordinate-matched new tile
without generation/semantic validation.

### 10. Packaging and runtime validation retain complete provenance

Packaging includes only the selected verified generation closure and required tile/
cluster/dependency artifacts for the product/world chunks. Source files, import
candidates, editor transactions and cook-cache entries are excluded from normal runtime
packages. Streaming cell Terrain/Foliage blocks reference exact manifest/artifact
identities; they are not a second copy of identity or publication authority.

Runtime validates dataset/tile schema compatibility, expected target/tier/profile,
manifest membership, size/digest/bounds and dependencies before allocation/publication.
A newer unsupported schema, missing required tile, stale cell reference or wrong target
fails rather than invoking a cooker, scanning source or selecting another artifact.

### 11. Shutdown retires in owner order and preserves durable truth

TerrainRuntime shutdown closes new residency/replacement admission, cancels/yields
owned jobs, invalidates candidates and asks consumer owners to retire exact generations.
It retains decoded bytes, Assets leases, reservations and module/provider dependencies
until worker, snapshot, Render, Physics and Navigation acknowledgements complete.

World Streaming retains cells/reservations in Retiring while Terrain retirement is
pending. Assets generation/cache storage remains independently valid; runtime shutdown
does not delete source, cooked generations or cook cache. A bounded retirement deadline
transitions the generation to `RetirementBlocked`: it remains quarantined and fully
charged, rejects replacement/new tile admission for the affected budget slice and emits
a fatal typed diagnostic for host policy. The deadline never force-frees resources or
marks a cell Absent early; only the missing owner acknowledgements can release it.

### 12. Errors, diagnostics and qualification are typed and bounded

Stable outcomes include:

```text
terrain.source.invalid
terrain.source.stale
terrain.cook.dependency_missing
terrain.cook.nondeterministic
terrain.manifest.invalid
terrain.tile.not_found
terrain.tile.corrupt
terrain.tile.stale
terrain.cache.invalid
terrain.streaming.reservation_denied
terrain.streaming.capacity_exceeded
terrain.streaming.consumer_failed
terrain.streaming.cancelled
terrain.streaming.retirement_stalled
terrain.replacement.incompatible
```

Diagnostics expose safe dataset/tile IDs, generations/revisions, stage, bounded sizes/
counts, cache class/hit outcome, reservation state and normalized nested cause. They do
not dump source/payload bytes, paths, native handles, pointers or unbounded provider
messages. Metrics remain aggregate by stage/cache class/result, not per tile/asset.

Required evidence includes:

- source import/reimport ownership and stale document/source transaction rollback;
- canonical source candidates with malformed/hostile dimensions, numeric values,
  overflow, work and byte limits;
- deterministic dataset/manifest/tile output across thread counts/job order/hosts for
  qualified inputs, plus every dependency/key/schema invalidation;
- fresh-versus-cache-hit parity and corrupt/wrong-key/truncated/oversized/symlinked
  cache entries never entering a candidate generation;
- manifest duplicate/missing/overlap/order/bounds/seam/dependency/digest rejection;
- explicit proof that Terrain payloads contain no native Render/Physics/Navigation
  handles and each consumer owns its derived/native cache;
- World Streaming request/reservation/growth/priority/cell-generation races with no
  private Terrain global scheduler or budget escape;
- cancellation/failure at provider read, decode, each consumer prepare, aggregate
  commit and retirement with old resident state preserved;
- changed-tile seam closure, compatible unchanged reuse, incompatible cohort rejection,
  superseding hot reload and old/new lease-safe retirement;
- cook cache, active generation, provider bytes, decoded Terrain, GPU, Physics and
  Navigation cache eviction independently affecting only their owned lifetime;
- packaging closure and runtime wrong-target/tier/schema/membership/digest/dependency
  validation without runtime cook/source fallback; and
- partial initialization, cell eviction, device/provider loss, world unload and repeated
  shutdown with reservations/dependencies retained until safe acknowledgement.

## Consequences

- Assets remains the sole generic source/cook/cache/publication authority while Terrain
  owns heightfield/foliage semantics and independently verifiable feature payloads.
- World Streaming controls global demand and budget; Terrain can optimize/evict only
  decoded feature-local state inside the granted slice.
- Cooked generations, decoded tiles and consumer-native resources have distinct owners,
  identities and retirement, eliminating ambiguous “terrain cache” behavior.
- Manifest-driven seam/dependency closure permits bounded tile streaming and safe hot
  replacement without mixing incompatible generations.
- Native Physics/Navigation/Render artifacts remain consumer-owned and cannot leak into
  Terrain public/cooked contracts.
- Implementation requires versioned Terrain schemas, deterministic cook tests, bounded
  manifest validation and coordinated multi-owner preparation/retirement.

## Rejected Alternatives

### Let Terrain own a separate importer/cooker scheduler and cache root

Rejected because Assets already owns source snapshots, catalogs, dependencies, atomic
publication and cache integrity. A second path would create incompatible truth.

### Store one monolithic eagerly loaded terrain artifact

Rejected because large worlds need bounded independent tile reads, cost estimates,
cell packaging and eviction. A verified manifest owns membership and seams.

### Put native Physics or Navigation objects in Terrain tiles

Rejected because provider build identity, thread affinity, readiness and retirement
belong to the consumer. Terrain may emit only neutral source descriptors.

### Let Terrain prioritize cells from camera distance

Rejected because World Streaming combines camera, gameplay, network and preload demand
under one global budget/fairness authority. Terrain executes admitted feature work.

### Treat any file under a tile directory as manifest membership

Rejected because stale/orphan/partial files could become active. The canonical verified
manifest is the only dataset membership authority.

### Follow Assets `current.json` on every tile request

Rejected because one running Terrain generation could mix artifacts across publications.
Runtime pins one verified generation until explicit replacement.

### Replace changed tiles independently without seam/dependency closure

Rejected because neighbors and consumer artifacts may be incompatible, causing cracks
or visual/physical/navigation disagreement. Compatible reuse requires exact signatures.

### Use one LRU to evict cook, decoded, GPU, Physics and Navigation data

Rejected because these stores have different owners, leases, budgets and safety gates.
Each owner releases only its own resources; World Streaming coordinates global pressure.

### Publish decoded terrain before required consumers prepare

Rejected because a cell could appear Active without required collision/navigation or
with stale GPU state. Required readiness participates in the aggregate commit.

### Rewrite cooked/cache bytes for runtime deformation

Rejected because it destroys deterministic artifact identity and reader safety. Runtime
mutation is a separate revisioned overlay.

### Force-remove retiring tiles when a shutdown deadline expires

Rejected because workers, snapshots, frames or provider objects may still reference
them. Typed incomplete retirement preserves ownership truth.
