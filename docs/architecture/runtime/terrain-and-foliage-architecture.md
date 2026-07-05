# Terrain And Foliage Architecture

## Purpose

This document defines the terrain, landscape, foliage, and vegetation
subsystems for Horo Engine. It covers heightfield-based terrain, layer
painting, instanced foliage rendering, wind simulation, LOD, collision,
render extraction, streaming budget integration, and editor authoring tools.

## Terrain System

### Heightfield Model

Terrain is a heightfield defined on a regular grid:

```cpp
struct TerrainHeightfield {
    uint32_t  tilesPerSide;
    float     metersPerTile;
    float     heightScale;
    std::vector<float> heights;   // row-major
};
```

### Terrain Component

A `TerrainComponent` attaches to an entity:

```cpp
struct TerrainComponent {
    AssetId             heightfieldAsset;
    TerrainMaterialLayer layers[16];
    uint8_t             activeLayerCount;
    TerrainLODSettings   lod;
};
```

**Layer count across tiers**: The `layers[16]` array size is the struct
maximum across all tiers. At runtime, `activeLayerCount` is clamped to the
tier's limit: the engine never writes to `layers[i]` for `i >= tierCap`.
The terrain shader permutation for active layer count is declared as a
`TerrainLayerCount` feature flag in the shader manifest, following the
`ShaderPermutationKey` model in material-and-shader-model.md. Lower-tier
permutations are compiled with the smaller layer counts and those shader
variants are selected when the tier clamps `activeLayerCount`.

Heightfield assets are authored in the editor or imported from external
formats (heightmap PNG/EXR, RAW16, GeoTIFF, Houdini HeightField).

### Terrain Collision

Terrain collision geometry is derived from the heightfield at asset cook
time:

- A simplified heightfield mesh is generated per tile at a configurable
  collision resolution (typically coarser than the visual resolution)
- Collision tiles are fed to the physics system as static concave mesh
  colliders
- When a terrain tile streams in, its collision tile streams in alongside
  the visual data
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
- Tiles are rasterized into GPU-friendly vertex/index buffers per LOD
- LOD selection uses screen-space error relative to camera distance
- Tile skirts prevent cracks between adjacent LOD levels
- `TerrainLODSettings` controls LOD count, transition distance, and morph

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

GPU blending uses the vertex weight attributes and samples layer textures
proportionally. The terrain shader permutation is selected based on active
layer count, declared as a `TerrainLayerCount` feature flag in the shader
manifest. Height-based blending, slope-based blending, and noise-based
transitions are supported through the material-function graph.

## Foliage System

### Instanced Rendering

Foliage uses GPU instancing with indirect draws dispatched through
backend-neutral indirect draw batches. Backend-specific command names never
appear in the terrain subsystem contract:

- Each foliage type maps to one or more static meshes
- Instance transforms are stored in structured GPU buffers
- Culling (frustum, distance, occlusion) is performed via compute shader on
  the GPU
- Draw calls use the backend's indirect draw submission

```cpp
struct FoliageType {
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

**Instance limits**: `instanceLimit` is a tier-enforced hard cap per
foliage type. The engine does not silently truncate instances. When the
limit is exceeded, a typed diagnostic is emitted and instances beyond the
limit are culled from the draw list (furthest-first). The foliage baker
warns before baking if the placement density would exceed the target tier's
cap. Tier caps: `es3` 16K, `dx11` 256K, `dx12_vulkan` 2M, `high_end` 10M+.

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

### Render Extraction

Terrain and foliage submit instances to the frame via `TerrainRenderExtractor`:

```
TerrainRenderExtractor::Extract(RenderWorldSnapshot& snapshot)
  1. Iterate CPU/streaming/frustum-visible terrain tile candidates
  2. For each tile, push terrain draw commands to snapshot.opaque
  3. Iterate CPU/streaming/frustum-visible foliage cluster candidates
  4. For each cluster, push foliage draw commands:
     - Opaque foliage (trunks, bark) → snapshot.opaque
     - Masked/translucent foliage (leaves) → snapshot.transparent with SortMode::DistanceToCamera
```

This follows the same `RenderInstance` snapshot model defined in
rendering-architecture.md. The extractor emits candidates; a later render-graph
GPU culling pass consumes those candidates and produces indirect draw lists for
the backend. The extractor runs alongside `VfxRenderExtractor` and the main
scene extractor.

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

Placement is computed offline at authoring time and baked into instance
buffers. Runtime procedural spawning (e.g., grass growing during gameplay)
is supported through the `FoliageSpawner` gameplay service:

```cpp
struct FoliageSpawnRequest {
    FoliageTypeId  foliageType;
    WorldCoordinate position;
    float          scale;
    uint32_t       seed;
    float          lifetime;       // 0 = permanent
};
```

Runtime-spawned instances are stored in a **separate dynamic buffer** per
foliage type. `TerrainRenderExtractor` merges the baked and dynamic buffers
during extraction: dynamic instances are appended to the visible instance
list after baked instances. Dynamic instances respect the same tier cap
as baked instances; if the combined total exceeds `instanceLimit`, dynamic
instances are evicted first (oldest-first by spawn time).

### LOD And Billboard Impostors

Foliage LOD uses:

- Mesh LOD chains per foliage type
- Billboard impostors for the farthest LOD
- Cross-fade between LOD levels to avoid popping
- Aggregate distance culling per foliage cluster

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

## Memory And Streaming

Terrain tiles and foliage instance data are streamed based on camera
proximity:

- Terrain height and weight data uses a tile cache with LRU eviction
- Foliage instance buffers are loaded per-cluster with distance-based
  priority
- Streaming uses the asset pipeline's async I/O with cancellation tokens

### Streaming Budget

Terrain and foliage streaming budgets are **sub-allocations** carved from
the world-streaming system's `StreamingBudget`:

```cpp
struct TerrainStreamingBudget {
    size_t   maxResidentTerrainMemoryMB;   // ≤ StreamingBudget.terrainMemoryReservationMB
    size_t   maxResidentFoliageMemoryMB;   // ≤ StreamingBudget.foliageMemoryReservationMB
    uint32_t maxConcurrentTileLoads;
};
```

The world-streaming `StreamingBudget` owns the total memory cap and reserves
slices for terrain and foliage via `terrainMemoryReservationMB` and
`foliageMemoryReservationMB`. Terrain operates within its allocated slice;
if the terrain tile cache exceeds its reservation, terrain evicts internally
without coordinating with the world-streaming system. Foliage instance
buffers are loaded per-cluster with distance-based priority. Terrain
tiles and foliage clusters are payloads within `StreamingCell` objects.
Load priority is coordinated through the world-streaming priority queue;
terrain does not maintain an independent priority system.

## Feature Tiers

| Feature              | `es3`      | `dx11`      | `dx12_vulkan` | `high_end`   |
| -------------------- | ----------- | ----------- | -------------- | ------------ |
| Terrain sculpting    | Read only   | Full editor | Full editor    | Full editor  |
| Terrain layers       | 4           | 8           | 16             | 16           |
| Foliage instances    | 16K         | 256K        | 2M             | 10M+         |
| GPU culling          | CPU only    | GPU (CS)    | GPU (CS)       | GPU (CS)     |
| Wind animation       | Vertex only | Vertex      | Vertex + CS    | Vertex + CS  |
| Billboard impostors  | No          | Manual      | Baked          | Baked + GPU  |
| Terrain LOD          | 2 levels    | 4 levels    | 6 levels       | 8 levels     |
| Foliage collision    | No          | Optional    | Optional       | Optional     |
| Runtime spawning     | No          | Yes         | Yes            | Yes          |
| Hole carving         | No          | Yes         | Yes            | Yes          |

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
