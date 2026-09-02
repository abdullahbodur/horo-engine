# Terrain And Foliage Architecture

## Purpose

This document defines the terrain, landscape, foliage, and vegetation
subsystems for Horo Engine. It covers heightfield-based terrain, layer
painting, instanced foliage rendering, wind simulation, LOD, collision,
render extraction, streaming budget integration, and editor authoring tools.

[ADR-137](../../adr/137-terrain-foliage-ownership-data-tier-and-lifecycle.md)
is the foundation contract for this subsystem. Terrain is a backend-neutral runtime
vertical slice with distinct `TerrainApi` and `TerrainRuntime` ownership; it is not
part of generic Foundation, RuntimeScene internals or a renderer backend.
[ADR-138](../../adr/138-terrain-source-cooked-tile-cache-and-streaming-ownership.md)
defines the complementary source, cook, tile-manifest, cache, residency and generation-
replacement contract.
[ADR-139](../../adr/139-terrain-render-extraction-material-lod-and-tier-boundary.md)
defines the render-candidate, material, per-view LOD/visibility, product-profile and
core-1.0 versus post-1.0 GPU-driven boundary.
[ADR-140](../../adr/140-foliage-placement-baked-dynamic-state-and-eviction-ownership.md)
defines deterministic placement, baked/ephemeral/durable state classification, mutation,
capacity, persistence handoff and eviction ownership.

## Ownership And Data Boundaries

`HoroEngine::TerrainApi` owns public typed identities, revisions, handles,
descriptors and narrow command/query/snapshot interfaces. `HoroEngine::TerrainRuntime`
owns live dataset/tile/cluster generations, mutable overlays, preparation, snapshot
publication and retirement. Hosts compose it with Assets, Scene, World Streaming,
Render, Physics and Navigation through backend-neutral ports.

The data path is explicit:

```text
authored terrain/foliage assets and document commands
  -> deterministic target/tier-keyed cook
  -> immutable bounded dataset/tile/cluster payloads
  -> typed RuntimeScene binding
  -> TerrainRuntime generation and immutable snapshots
  -> leased render/physics/navigation/streaming projections
```

Editor documents own source mutation and undo. Assets/Pipeline own import, cook and
atomic artifact publication. RuntimeScene owns entity/binding lifetime. World
Streaming owns cell demand and aggregate reservations. Render, Physics and Navigation
own their realized resources and native-thread retirement. UI, CLI, MCP and gameplay
use typed application/Terrain capabilities and never mutate buffers directly.

Stable `TerrainDatasetId`, `TerrainTileId`, `FoliageTypeId`, `FoliageClusterId` and
`FoliageInstanceId` values are distinct from runtime handles, revisions, `AssetId`,
entity/cell IDs and native handles. TRF-001.2 freezes their exact encoding. Content,
residency, mutation and capability revisions advance independently so consumers check
the state they actually use.

## Terrain System

### Heightfield Model

Terrain heightfield source/cooked metadata describes a bounded regular grid. Bulk
samples remain inside an authored document transaction, immutable asset lease or
operation-owned decode candidate; the public runtime contract does not expose a
mutable owning vector for the whole terrain:

```cpp
struct TerrainHeightfieldDescriptor {
    TerrainDatasetId dataset;
    AssetId sourceAsset;
    TerrainSourceRevision sourceRevision;
    TerrainTileGrid grid;
    TerrainSampleEncoding encoding;
    SceneBounds bounds;
    TerrainDescriptorLimits limits;
};
```

### Terrain Component

A Scene entity carries only a typed binding to a cooked dataset and product feature
policy. Decoded tiles, layer arrays and dependent-system resources remain outside the
component:

```cpp
struct TerrainSceneBinding {
    TerrainDatasetId dataset;
    AssetId cookedDataset;
    TerrainSceneTransform transform;
    TerrainFeatureRequirements requiredFeatures;
    TerrainTierPolicy tierPolicy;
};
```

Layer definitions live in a bounded authored/cooked layer-set asset. The selected
Terrain plan validates its exact finite layer limit and a compatible cooked shader
variant before activation. Required content exceeding the resolved limit fails;
optional reduction is allowed only through an explicitly authored/cooked product
fallback and is never a runtime clamp that drops layers silently.

Heightfield assets are authored in the editor or imported from external
formats (heightmap PNG/EXR, RAW16, GeoTIFF, Houdini HeightField).

### Source, Cook And Dataset Ownership

External files are untrusted import inputs, not canonical terrain identity or runtime
data. Assets/Application owns tracked source bytes, `AssetId`, generic importer/cooker
orchestration, dependency scheduling, immutable cache storage, generation staging and
atomic publication. Terrain Import/Model owns format interpretation and the canonical
authored height/layer/hole/coordinate semantics; Terrain Cook owns deterministic tiling,
seams, LOD/mips, foliage clustering and Terrain artifact schemas. Neither contribution
creates a parallel asset graph, cache root, scheduler or publication pointer.

One immutable `CookedTerrainDatasetManifest` binds the selected content revision, tier,
coordinate profile, bounds, dependency fingerprint and canonically sorted tile/cluster
entries. Every entry declares stable typed address, exact bounds, seam/dependency
signatures, artifact identity, digest, byte/peak-work bounds and required neutral
consumer payloads. Tile artifacts are independently readable and verifiable, but the
verified manifest is the only membership authority; runtime never infers a dataset by
enumerating files.

The Terrain cook fingerprint includes the exact canonical source revision/digest, every
accepted dependency artifact, tiling/seam/LOD/compression policy, effective tier/limits,
Terrain schemas and algorithm versions, and generic target/toolchain envelope identity.
Paths, timestamps, locale, job order and runtime/provider handles are excluded. Native
GPU resources, solver objects and navigation-provider tiles remain separately keyed and
owned by Render, Physics and Navigation.

### Terrain Collision

Terrain collision geometry is derived from the heightfield at asset cook
time:

- A simplified heightfield mesh is generated per tile at a configurable
  collision resolution (typically coarser than the visual resolution)
- Collision tiles are fed to the physics system as static concave mesh
  colliders
- A cell prepares required collision and optional/required visual payloads under the
  same generation-tagged aggregate activation; headless visual absence is explicit
- Hole-carved vertices are excluded from the collision mesh; the physics
  system treats those areas as passthrough
- NavMesh generation respects the hole mask: holed areas are excluded from
  the walkable surface

```cpp
struct TerrainCollisionSettings {
    uint32_t  collisionResolution;   // vertices per tile side (default 33)
    bool      generatePerTile;       // true = one collider per tile
    float     collisionMargin;       // outward expansion for stability
};
```

### LOD And Tiling

Terrain is split into tiles at authoring time:

- Each tile is `N x N` vertices (default 129×129, corresponding to
  128×128 quads)
- Terrain Cook emits neutral bounded representation payloads and geometric-error,
  seam, skirt/morph and neighbor-compatibility metadata per LOD
- TerrainRuntime exposes only resident legal representations in its immutable snapshot
- RenderFrontend selects a compatible LOD per view from screen-space error, projection,
  output, hysteresis, profile and finite budget; distance alone is not the policy
- Native vertex/index resources and draw execution remain renderer-owned
- `TerrainLODSettings` contributes authored/cooked intent; it is not a backend command
  or permission to invent an unavailable representation

```cpp
struct TerrainLODSettings {
    uint8_t  lodCount;
    float    lod0Distance;
    float    lodDistanceMultiplier;
    bool     morphEnabled;
};
```

### Layer Painting

The editor terrain tools support:

- Height sculpting (raise, lower, smooth, flatten, noise)
- Layer weight painting (up to 4 layers simultaneously)
- Hole carving (per-vertex visibility mask)
- Spline-based path/road deformation

Paint operations use the terrain data model with undo support. Undo captures
the terrain data model state change (height/weight snapshot), not brush
settings. Brush parameters (radius, falloff, strength) are transient UI
state preserved per-session in the editor workspace state; the undo stack
does not scroll brush settings. Changing the brush mid-session does not
invalidate prior undo entries.

Hole carving sets a per-vertex visibility flag in the terrain data. Holes
propagate to collision (excluded from collision mesh) and NavMesh (excluded
from walkable area). Characters and physics objects pass through holed
regions.

[ADR-108](../../adr/108-dynamic-overlay-carving-and-tile-rebuild-policy.md)
requires persistent/editor height, hole and spline changes to recook affected
collision/navigation tiles through their owning application operations. Runtime
terrain deformation cannot mutate cooked NavMesh or use simple obstacle carving as
general polygonization. It first makes the affected region conservatively
unavailable, then requires an explicitly composed transactional runtime grounded-
tile rebuild capability; the 1.0 runtime baseline returns
`UnsupportedDynamicChange` and keeps the safe exclusion until recook/replacement.

### Material Blending

Terrain materials are blended per-vertex using weight layers:

```cpp
struct TerrainMaterialLayer {
    AssetId  diffuseTexture;
    AssetId  normalTexture;
    AssetId  maskTexture;     // R=metallic, G=roughness, B=ambient occlusion
    float    uvScale;
    AssetId  materialFunction; // optional material-function graph reference
};
```

Terrain authoring/cook owns layer order, weight/hole encodings, material-function
references and required blend semantics. Material/Shader cook validates those semantics
and emits an explicit finite target permutation family. RenderFrontend selects only a
cooked variant compatible with the active raster recipe, neutral Terrain payload layout,
required features and effective limits; the backend realizes it without changing layer,
hole or blend meaning. Height-, slope- and noise-based transitions are permitted only
when declared by the material graph and cooked permutation contract. TRF-003.2 freezes
the exact layer limit, key and blend rules.

## Foliage System

### Instanced Rendering

Foliage exposes bounded neutral instance candidates. The core 1.0 renderer may group
them into backend-neutral direct or instanced batches after CPU per-view visibility and
LOD planning; it does not require compute, GPU Scene or indirect execution. A post-1.0
GPU-driven recipe may use renderer-owned compute culling and generated indirect batches
only after explicit capability, artifact, budget, fallback and backend qualification.
Backend-specific command names never appear in the Terrain contract:

- Each foliage type maps to one or more static meshes
- TerrainRuntime owns baked/dynamic logical instance membership and immutable transforms
- RenderFrontend owns native instance storage, per-view culling/LOD results and batching
- the private backend owns native resources, commands and synchronization

```cpp
struct FoliageType {
    FoliageTypeId         id;
    FoliageDefinitionRevision revision;
    AssetId               meshId;
    FoliagePlacementRules placement;
    FoliageCullingRules   culling;
    FoliageWindSettings   wind;
    float                 minScale;
    float                 maxScale;
    bool                  alignToTerrain;
    uint32_t              instanceLimit;  // tier-enforced maximum
};
```

**Instance limits**: `instanceLimit` is validated against the selected finite
provider-neutral Terrain tier during cook and runtime admission. Required content above
the limit fails with a typed result. Horo does not silently truncate, hide furthest
instances or infer limits from a graphics API name. An optional lower-density cooked
variant must be explicitly authored and selected by declared fallback policy.

### Foliage Collision

Foliage is **visual-only by default** — no collision response with
characters, projectiles, or physics objects. Per-foliage-type collision can
be opted in for gameplay-relevant foliage (e.g., large trees, destructible
bushes):

```cpp
struct FoliageCollisionSettings {
    bool      enableCollision;        // default false
    float     trunkRadius;            // simplified cylinder collision
    float     trunkHeight;
    bool      blockProjectiles;
    bool      blockNavigation;        // affects NavMesh carving
};
```

When collision is enabled, simplified collision primitives (cylinder or
capsule) are generated per instance at bake time and registered with the
physics system as static bodies. NavMesh carving at runtime (dynamic
obstacle removal) is not supported for foliage; foliage collision is
baked into the initial NavMesh generation.

Removing/destructing baked navigation-blocking foliage does not automatically open
walkability. It requires authored affected-tile recook or the explicit ADR-108
runtime rebuild capability over authoritative post-change geometry. Adding a
runtime blocker may use the logical overlay and supported optional carving, but
failure/absence remains conservative and observable.

### Render Extraction

`TerrainRenderExtractor` leases one immutable Terrain generation and publishes a bounded,
view-independent `TerrainRenderExtractionSnapshot`. Tile and foliage candidates carry
typed Terrain identities/revisions, neutral geometry/artifact references, bounds/
transform inputs, material requirements, legal LOD/seam data, hole/visibility semantics,
stable logical instance identity and finite cost estimates.

The snapshot contains no selected per-view LOD, visible list, draw command, indirect
argument, native handle, pipeline object or mutable Terrain pointer. Multiple views may
consume it without advancing Terrain state. Capacity exhaustion fails extraction rather
than truncating candidates.

RenderFrontend derives generation-checked `RenderObjectId` values, performs per-view
visibility/LOD/seam planning, resolves cooked material variants/resources and maps the
candidates into ADR-036 pass classifications. Opaque, Masked, TransparentSorted and
TransparentAdditive come from authored/cooked material semantics; “trunk” or “leaf” does
not imply a pass. The private backend only translates the validated plan.

### Decal Interaction

Decals project onto terrain surfaces by sampling the GBuffer within the
decal projection box. Foliage does not receive decals — the decal pass
renders before the foliage pass in the render graph, so foliage instances
are drawn on top of decaled terrain. This avoids decal-projection artifacts
on thin geometry (leaves, grass blades).

### Procedural Placement

Foliage instances are placed procedurally based on configurable rules:

- Density map (per-terrain-tile texture or global density curve)
- Slope/altitude constraints
- Exclusion zones (layer masks, water bodies, building footprints)
- Clustering for variety (clump size, radius)
- Random seed per foliage type for determinism

Placement is computed deterministically during Terrain cook from canonically sorted,
versioned rule/source inputs. A specified PRNG seed tuple, quantization and tie policy
produce stable ordered baked `FoliageInstanceId` records independent of locale, worker
count and job order. Oversized placement fails or uses an explicitly authored/cooked
lower-density variant; cook never truncates from host memory or camera distance. Exact
rule, PRNG and quantization schemas are TRF-004.2 work.

Runtime procedural spawning (for example, grass growing during gameplay) is supported
through the Terrain mutation command boundary:

```cpp
struct FoliageSpawnRequest {
    TerrainRuntimeHandle terrain;
    TerrainMutationRevision expectedRevision;
    FoliageTypeId foliageType;
    WorldCoordinate position;
    FoliageScale scale;
    FoliagePlacementSeed seed;
    FoliageLifetime lifetime;
    FoliageOverflowPolicy overflow;
    GameplayAuthorityContext authority;
};
```

Runtime-spawned instances live in a separate revisioned Terrain overlay; cooked cluster
artifacts remain immutable. The lifetime is explicit: cell-, session- or owner-bound
ephemeral state is excluded from slot saves, while a `DurableWorldDelta` creates an
authorized base-relative canonical spawn/update/tombstone through the Runtime Save/
Persistent World boundary. A baked removal references the exact base revision and stable
`FoliageInstanceId`; durable spawns use a distinct persistent mutation identity. Runtime,
render, Physics and Navigation handles never persist.

`TerrainRenderExtractor` merges the baked base and active overlay through one immutable
revisioned snapshot. Capacity exhaustion rejects atomically in the 1.0 baseline. Any
optional replacement policy must be registered/versioned with an eligible state class,
stable priority/tie order and persistence effect; it cannot evict baked/durable or another
owner's state. There is no implicit oldest-, furthest- or visibility-based deletion.

### LOD And Billboard Impostors

Foliage LOD uses:

- Mesh LOD chains per foliage type
- Billboard impostors for the farthest LOD
- Cross-fade between LOD levels to avoid popping
- Aggregate distance culling per foliage cluster

These are legal cooked representations and transition rules owned by Terrain content.
RenderFrontend selects among resident compatible representations per view. The 1.0
baseline uses bounded CPU selection; post-1.0 GPU selection remains renderer-owned and
cannot feed gameplay or World Streaming truth back synchronously.

Impostors are pre-baked at authoring time. The baker renders the foliage
mesh from multiple view angles and stores the result in a texture atlas.
Impostor rendering uses a single quad with atlas UV lookup and depth for
parallax correction.

### Wind Simulation

Wind is applied as vertex displacement in the foliage vertex shader:

```cpp
struct FoliageWindSettings {
    float  primaryStrength;
    float  secondaryStrength;
    float  primaryFrequency;
    float  secondaryFrequency;
    float  gustProbability;
    float  gustStrength;
    float  branchFlexibility;
    float  leafFlutter;
};
```

Wind parameters are configured per foliage type. Global wind direction and
base speed come from the scene's `WindComponent`. Wind is computed on the
GPU using procedural noise; no CPU simulation state per instance is
required.

### Editor Authoring

Terrain and foliage authoring tools are registered through the
`EditorPanelHost` extension system:

- **Terrain sculpt and paint tools** register as `ViewportPanel` overlay
  tools. The toolbar dispatches typed operations (`TerrainSculptOp`,
  `TerrainPaintOp`) through the `EditorToolbar` result channel. The
  viewport panel renders the tool-specific gizmo (brush radius indicator)
  and terrain overlay (height/weight visualization).
- **Foliage paint/erase/select tools** register similarly as
  `ViewportPanel` overlay tools. The foliage type palette is a separate
  `EditorTab` (`FoliagePaletteTab`) that publishes the active foliage type
  to the `EditorDataBus`.
- **Density visualization overlay** is rendered by the viewport panel as a
  post-process overlay, reading terrain density data from the terrain
  service. It does not require a separate tab.
- **Wind preview** is a viewport toggle that enables the `WindComponent`
  simulation in the editor viewport, animating foliage in real time for
  preview.

The `EditorToolbar` only produces typed results; terrain/foliage domain
operations are performed by the registered tools consuming those results.

## Runtime Lifecycle And Readiness

TerrainRuntime uses the exhaustive aggregate states `Absent`, `Preparing`, `Prepared`,
`Active`, `Replacing`, `Suspended`, `Retiring` and `Failed`. Preparation validates the
Scene binding/effective plan, reserves all peak work and builds a detached candidate.
Workers return generation-tagged evidence only; the Terrain owner lane publishes the
candidate at the RuntimeScene safe point after every required participant is Prepared.

Readiness is an immutable generation-scoped snapshot with separate logical, streaming,
visual, collision, navigation and mutation dimensions. Each dimension is
`NotRequested`, `Preparing`, `Ready`, `Unavailable`, `Failed`, `Suspended` or
`Retiring`. Optional visual absence is valid for a headless server, while required
collision/navigation cannot be inferred from visual tile residency.

Failure or cancellation before publication destroys only candidate-owned state and
leaves the old Active generation unchanged. Cancellation after publication is an
explicit unload/replacement. Replacement retains the old dataset, snapshots, tiles,
GPU work, collision/navigation installs and module/provider dependencies until every
lease/fence retires; stale worker or participant evidence cannot publish by matching a
slot, coordinate or asset.

Authored mutation goes through document/application commands and recook. Optional
runtime deformation/dynamic foliage uses a typed command with terrain generation,
expected mutation revision, bounded scope, gameplay authority, lifetime and overflow
policy. The owner builds a separate overlay candidate and atomically advances mutation
revision. Cooked data is never edited in place.

Foliage state is classified before commit. Baked instances are immutable content;
ephemeral overlays follow explicit cell/session/owner lifetime; durable mutations are
base-relative canonical deltas captured exactly once by Runtime Save/Persistent World.
Before cell eviction removes the last active dirty copy, Terrain publishes an immutable
delta root into the Persistent World ledger at the owner safe point. Failure/capacity
denial blocks retirement and keeps the source charged; it cannot drop state or force an
implicit user-slot save.

Shutdown closes admission, cancels/yields owned task groups, invalidates candidates,
requests exact-generation renderer/physics/navigation retirement and retains assets,
reservations and dependencies until all consumers acknowledge release. A deadline may
report `terrain.shutdown.incomplete`; it cannot force-free possibly referenced state.

## Memory And Streaming

World Streaming converts camera, gameplay, network and preload demand into typed,
generation-scoped Terrain residency requests. Terrain executes admitted work as a
bounded `Queued -> Reading -> Validating -> Decoding -> PreparingConsumers -> Prepared`
pipeline using immutable provider byte leases and cancellation tokens. It cannot create
a private camera-distance queue or begin allocation before the shared reservation is
accepted. Foliage clusters use the same authority and lifecycle.

“Terrain cache” must identify its owner. Assets owns immutable cook-cache entries and
published cooked generations; the Assets provider owns leased artifact/chunk bytes;
TerrainRuntime owns Horo-neutral decoded tile/cluster data; Render owns GPU residency;
Physics owns solver artifacts/bodies; Navigation owns provider artifacts and installed
query tiles. Each allocation has one charge and one release acknowledgement. Evicting
one class never implies release of another.

World Streaming owns the global cell priority queue and aggregate CPU/GPU/staging/
retirement ledger. Terrain owns only its provider-local cache policy within an admitted
slice. It cannot maintain a competing cell-demand scheduler, evict an activation-
critical pinned cell, borrow another feature's reservation or allocate before budget
growth is accepted. Every old/new replacement generation, decode/upload copy and
retiring resource remains accounted until its owner releases it.

### Streaming Budget

Terrain and foliage streaming budgets are **sub-allocations** carved from
the world-streaming system's `StreamingBudget`:

```cpp
struct TerrainStreamingBudget {
    ByteCount maxResidentTerrainBytes;
    ByteCount maxResidentFoliageBytes;
    uint32_t maxConcurrentTileLoads;
    uint32_t maxConcurrentRetirements;
};
```

The world-streaming `StreamingBudget` owns the total memory cap and reserves
slices for terrain and foliage. The resolved byte/count budget is a capability token,
not an additional allowance. Terrain may evict only disposable provider-local detail
inside its allocated slice; pressure involving a pinned/activation-critical cell is a
typed request to World Streaming. Foliage instance buffers are loaded per-cluster with
the priority assigned by World Streaming. Terrain tiles and foliage clusters are
payloads within `StreamingCell` objects. Active cells, replacement candidates, immutable
snapshots and consumer installs pin their exact dataset generation and leases.

Hot replacement validates a complete candidate generation before publication. A changed
tile expands to its manifest-declared seam/dependency closure; exact-signature matches
may reuse old immutable payloads, while incompatible neighbors prepare together. World
Streaming commits the aggregate cell only after every required Terrain, Render, Physics
and Navigation participant is prepared. Failure or cancellation discards candidate-
owned state and preserves the prior Active generation. Old generations retire only
after all byte, snapshot, native-resource and provider leases acknowledge release.

## Feature Tiers

Terrain tiers are provider-neutral product preference profiles: `Baseline`,
`Standard`, `High` and `Ultra`. They do not name graphics APIs, platforms, shader
models or devices and do not grant capability.

Resolution intersects the exact cooked variants with TerrainRuntime, World Streaming,
Render, Physics, Navigation, host-mode and product-policy capabilities. The immutable
result records the selected tier, exact finite layer/LOD/instance/byte/work limits,
enabled algorithms, all provider/cooked revisions and every explicit fallback reason.
TRF-001.3 owns the versioned numeric table and shared validator.

| Capability family | Baseline | Standard | High | Ultra |
|---|---|---|---|---|
| Heightfield/layer/LOD scale | Minimal product-required cooked path | Increased finite authored/cooked limits | Higher finite quality limits | Highest qualified finite product limits |
| Foliage scale/culling | Bounded required representation with declared CPU/GPU path | Larger qualified plan and optional GPU culling | High-density qualified GPU path where supported | Maximum explicitly qualified plan, never “unlimited” |
| Wind/impostor quality | Minimal cooked/vertex path when required | Optional cooked impostor and richer wind variants | Higher qualified visual recipe | Highest qualified optional recipe |
| Collision/navigation | Independently required or unavailable by scene/host policy | Same ownership; quality may use a higher cooked variant | Same | Same |
| Runtime deformation/spawn | Explicit optional capability, not implied | Explicit optional capability | Explicit optional capability | Explicit optional capability |
| Editor authoring | Application/editor permission and host capability, not runtime tier | Same | Same | Same |

Required content that cannot fit the selected plan fails cook/activation with a typed
result. Optional reduction occurs only when the product declares an ordered fallback
and the compatible cooked variant exists. Runtime never silently clamps layers, drops
instances, removes collision, changes renderer/provider or switches CPU/GPU algorithms
to make a tier appear successful.

## Verification

Required coverage includes:

- target/dependency checks for backend-neutral TerrainApi/Runtime and no native/editor/
  service-locator leakage;
- strong-type separation for dataset/tile/type/cluster/instance identity, runtime
  handles, content/residency/mutation/capability revisions and leases;
- malformed, oversized and incompatible source/cooked/scene descriptors with bounded
  decode and no mutable whole-dataset buffer crossing the public boundary;
- canonical dataset membership/order, tile digest/bounds/seam/dependency validation,
  complete cook-key invalidation and byte-identical deterministic recook/cache reuse;
- independent corruption/missing-artifact rejection and package-time verification with
  no directory enumeration or mixed-generation tile admission;
- every tier/cooked/provider capability combination, required failure and each declared
  fallback reason without silent clamp/drop/provider selection;
- view-independent render-candidate extraction, deterministic capacity failure, multi-
  view reuse and strict separation of residency, presentation visibility and gameplay;
- Terrain tier/render profile independence, material/pass classification, CPU 1.0 LOD/
  seam planning and optional post-1.0 GPU recipe admission/fallback;
- candidate failure/cancellation and replacement at every Scene/streaming/render/
  physics/navigation prepare/commit/retirement boundary;
- stale worker, snapshot, tile, GPU, collision and navigation evidence rejected across
  generation/revision changes;
- runtime mutation authority/revision/capacity/overflow cases with no partial update,
  cooked-source mutation or implicit foliage eviction;
- deterministic placement across worker/order/locale variations, baked/ephemeral/durable
  identity separation and base-relative save delta golden fixtures;
- active-to-dormant dirty foliage handoff, SaveCaptureEpoch one-copy capture, base-remap
  incompatibility and eviction pressure with no persistent state loss;
- world-streaming reservation/growth/pressure accounting for staging, old/new and
  retiring generations without double charge or oversubscription;
- source/cook/provider-byte/decoded/GPU/Physics/Navigation cache ownership, pinning and
  release tests, including pressure and replacement seam-closure cases;
- headless, dedicated server, editor preview, Null Render and every interactive backend
  consuming the same Horo contract; and
- bounded owner/worker/native-thread handoff, frame-hot allocation/I/O checks, device
  loss, world unload, repeated shutdown and retirement timeout.

## Related Documents

- [Rendering Architecture](./rendering-architecture.md): terrain shader integration,
  GPU-driven rendering, `RenderWorldSnapshot` and `RenderInstance` model
- [Material And Shader Model](./material-and-shader-model.md): terrain material
  functions, `ShaderPermutationKey`, feature flags for layer count
- [World Streaming Architecture](./world-streaming-architecture.md): terrain tile and
  foliage cluster streaming, `StreamingBudget` sub-allocation
- [Physics Architecture](./physics-architecture.md): terrain collision geometry,
  foliage collision opt-in, hole mask propagation
- [Navigation And AI Architecture](./navigation-and-ai-architecture.md): NavMesh
  generation from terrain heightfield, hole mask exclusion
- [LOD And Culling Architecture](./lod-and-culling-architecture.md): foliage LOD chains,
  impostor baking, GPU-driven culling pipeline
- [Decal System Architecture](./decal-system-architecture.md): terrain decal projection,
  foliage decal passthrough
- [VFX And Particles Architecture](./vfx-and-particles-architecture.md):
  foliage-interacting VFX, debris on foliage collision
- [Editor Panel Host](../editor/editor-panel-host.md): terrain/foliage tool
  registration as `ViewportPanel` overlays and `EditorTab` panels
- [Editor Document Model](../editor/editor-document-model.md): undo system and
  terrain data model snapshots
- [ADR-137](../../adr/137-terrain-foliage-ownership-data-tier-and-lifecycle.md):
  module/data authorities, typed identity and revisions, provider-neutral tier plan,
  aggregate lifecycle, readiness and shutdown baseline
- [ADR-138](../../adr/138-terrain-source-cooked-tile-cache-and-streaming-ownership.md):
  canonical source and deterministic tile cooking, cache authorities, World Streaming
  residency, seam-safe replacement and generation retirement
- [ADR-139](../../adr/139-terrain-render-extraction-material-lod-and-tier-boundary.md):
  immutable render candidates, material/permutation boundary, renderer-owned per-view
  visibility/LOD, independent profile axes and CPU/GPU recipe split
- [ADR-140](../../adr/140-foliage-placement-baked-dynamic-state-and-eviction-ownership.md):
  deterministic placement, immutable baked base, ephemeral overlays, durable canonical
  deltas, capacity policy and no-loss cell eviction
