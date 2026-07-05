# LOD And Culling Architecture

## Purpose

This document defines the level-of-detail (LOD) and visibility culling
subsystems for Horo Engine. It covers mesh LOD, HLOD, impostors, frustum
culling, occlusion culling, distance culling, and GPU-driven culling
pipelines.

## Mesh LOD

### LOD Chain

Each renderable mesh defines a chain of LOD levels:

```cpp
struct MeshLODChain {
    uint32_t              lodCount;
    MeshLODLevel          levels[MAX_LOD_COUNT];
    LODSelectionMode      selectionMode;
    float                 lodBias;          // global quality bias
};

struct MeshLODLevel {
    AssetId               meshId;           // simplified mesh asset
    float                 screenSize;       // fraction of viewport height
    float                 transitionDuration; // cross-fade time
};
```

LOD selection uses screen-space size relative to viewport height:

- `screenSize = (boundingSphere.radius / distance) * (viewportHeight / tan(fov/2))`
- Select the first LOD where `screenSize >= level.screenSize`
- Apply `lodBias` to shift selection (negative = more detail, positive = less)

### Smooth LOD Transition

Transitions between LOD levels use dithered cross-fade:

- During the transition window, both LOD levels are rendered
- A screen-door dither pattern determines which level contributes per pixel
- The dither pattern is temporal (varies each frame) to avoid static noise
- Cross-fade is GPU-driven via a global dither pattern texture

### LOD Generation

LOD levels are generated at asset import time:

- Mesh simplification uses quadric error metrics (QEM)
- Target triangle count is configurable per LOD level
- Generated LOD meshes are stored as derived assets alongside the source mesh
- Skeleton and skinning data are preserved for skinned mesh LOD

```cpp
struct MeshLODGenerationSettings {
    float  targetTriangleRatio[MAX_LOD_COUNT];  // e.g., 0.5, 0.25, 0.1
    bool   preserveUVBoundaries;
    bool   preserveHardEdges;
    bool   generateImpostors;
};
```

## Hierarchical LOD (HLOD)

HLOD groups nearby static meshes into cluster proxies:

- Static meshes within the same spatial cluster are merged
- Cluster proxy is a simplified combined mesh with merged materials
- HLOD clusters are generated offline (baked into level data)
- HLOD transitions are distance-based, not screen-size

```cpp
struct HLODCluster {
    BoundingBox          bounds;
    AssetId              proxyMeshId;
    float                transitionDistance;
    std::vector<EntityId> sourceEntities;  // entities replaced by this proxy
};
```

HLOD clusters are organized in a spatial hierarchy (octree) for efficient
activation. Editor tools allow manual adjustment of cluster boundaries and
proxy quality.

## Impostors

Billboard impostors capture a mesh from multiple viewing angles:

- Impostors are pre-rendered at baking time from 8-32 viewpoints
- Stored as a texture atlas with depth for parallax correction
- Used as the farthest LOD for foliage and distant static meshes
- Impostor rendering is a single quad draw with atlas lookup

```cpp
struct ImpostorAsset {
    AssetId    atlasTexture;
    AssetId    depthTexture;
    uint32_t   viewCount;         // horizontal × vertical viewpoints
    float      objectRadius;      // for screen-size calculation
};
```

## Frustum Culling

Frustum culling eliminates objects outside the camera frustum:

- CPU culling: bounding sphere/frustum test on the job system
- GPU culling: compute shader tests bounding spheres in parallel
- Results feed into indirect draw buffers

```cpp
struct CullingFrustum {
    Plane    planes[6];
    float    nearZ;
    float    farZ;
};
```

GPU frustum culling is the default for DX12/Vulkan tiers. CPU culling is the
fallback for OpenGL ES3.

## Occlusion Culling

### Software Occlusion

Software occlusion uses a hierarchical Z-buffer (Hi-Z):

- Previous frame's depth buffer is downsampled to a mip chain
- Object bounding boxes are tested against the Hi-Z pyramid
- Objects fully occluded are skipped in the draw list
- Hardware occlusion queries are used as a fallback on older APIs

### Portal / Precomputed Visibility

For indoor scenes, portal-based occlusion is available:

- Precomputed visibility sets per spatial cell
- Portals (doorways, windows) connect cells
- Only cells visible through portals from the camera cell are rendered
- Visibility is computed offline and stored as a bit array per cell

```cpp
struct VisibilityCell {
    BoundingBox      bounds;
    std::vector<VisibilityPortal> portals;
    std::bitset<MAX_CELLS> visibleCells;   // precomputed at bake time
};
```

## Distance Culling

Objects beyond a maximum draw distance are culled regardless of visibility:

```cpp
struct DistanceCullingSettings {
    float  maxDrawDistance;        // absolute max distance
    float  perObjectMultiplier;    // scale per object (e.g., large objects visible farther)
    bool   cullShadowsBeyond;      // also cull shadow casters beyond distance
};
```

Per-object culling distances are configured on the renderable component.
Distance culling is integrated with the GPU culling compute pass.

## GPU-Driven Culling Pipeline

On DX12/Vulkan and High-End tiers, culling is fully GPU-driven:

```
1. Compute shader: frustum + occlusion + distance culling
2. Compact visible instance list
3. Write indirect draw commands to GPU buffer
4. Execute indirect draws (no CPU readback)
```

The render graph submits a backend-neutral `IndirectDrawBatch` per material bucket. Backend implementations translate that batch to their native indirect execution API privately.
Culling parameters (frustum, Hi-Z, camera position) are uploaded as uniform
buffers each frame.

## Debug And Visualization

- LOD visualization overlay (color by LOD level)
- Frustum culling visualization (show culling frustum)
- Occlusion culling visualization (show Hi-Z pyramid, occluded objects in
  wireframe)
- Draw call statistics per culling category
- GPU timing for culling compute passes

## Feature Tiers

| Feature              | `es3`        | `dx11`       | `dx12_vulkan`  | `high_end`    |
| -------------------- | ------------- | ------------- | --------------- | -------------- |
| Mesh LOD             | 2 levels      | 4 levels      | 6 levels        | 8 levels       |
| HLOD                 | No            | Yes           | Yes             | Yes            |
| Impostors            | No            | Static        | Baked           | Baked + depth  |
| Frustum culling      | CPU           | CPU           | GPU compute     | GPU compute    |
| Occlusion culling    | HW queries    | HW queries    | Hi-Z + portal   | Hi-Z + portal  |
| GPU-driven pipeline  | No            | No            | Yes             | Yes            |
| Smooth LOD fade      | No            | Dithered      | Temporal dither | Temporal dither|

## Related Documents

- [LOD Debugger UI Reference](./lod-debugger.html)

- [Rendering Architecture](./rendering-architecture.md): draw call submission and indirect draws
- [Terrain And Foliage Architecture](./terrain-and-foliage-architecture.md): foliage LOD and impostors
- [World Streaming Architecture](./world-streaming-architecture.md): visibility cell streaming
- [Advanced Rendering Architecture](./advanced-rendering-architecture.md): ray-traced occlusion
- [Scene Runtime](./scene-runtime.md): renderable component and bounding data
