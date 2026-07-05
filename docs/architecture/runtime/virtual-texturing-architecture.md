# Virtual Texturing Architecture

## Purpose

This document defines the virtual-texturing subsystem for Horo Engine. It
covers sparse virtual texture pages, page table indirection, page residency
management, feedback-based streaming, page cache compression, and integration
with the material system and asset pipeline.

## Virtual Texture Model

Virtual texturing decouples logical texture resolution from physical GPU
memory:

- A virtual texture appears as a single large texture to shaders (up to
  128K×128K)
- Physical GPU memory holds only the currently visible pages
- An indirection table maps virtual page coordinates to physical page locations
- Missing pages are streamed from disk asynchronously

```cpp
struct VirtualTexture {
    VirtualTextureId   id;
    uint32_t           virtualWidth;        // up to 131072
    uint32_t           virtualHeight;
    uint32_t           pageSize;            // typically 128×128 texels
    PixelFormat        format;
    uint32_t           mipLevels;
    uint32_t           physicalPageCount;   // GPU memory budget in pages
    VirtualTexturePageTable pageTable;
};
```

## Page Table

The page table is a GPU-resident texture that maps virtual to physical:

```cpp
struct VirtualTexturePageTable {
    // GPU resource
    TextureHandle      indirectionTexture;   // backend-neutral page-table texture
    uint32_t           tableWidth;           // virtualWidth / pageSize / tileSize
    uint32_t           tableHeight;

    // CPU mirror for residency tracking
    std::vector<PageTableEntry> cpuMirror;
};

struct PageTableEntry {
    uint32_t   physicalPageIndex;   // or INVALID_PAGE if not resident
    uint32_t   lastAccessFrame;
    bool       isResident;
    bool       isRequested;         // loading in progress
};
```

The indirection texture is sampled in the material shader before the actual
texture sample. The GPU computes the virtual page coordinate and looks up the
physical page UV offset.

## Feedback System

Page residency is driven by GPU feedback:

- A feedback pass writes which virtual pages were accessed during rendering
- Feedback is read back to the CPU (typically 1-2 frames behind)
- Pages with high access frequency are prioritized for loading
- Unused pages are evicted under memory pressure

```cpp
struct VirtualTextureFeedback {
    BufferHandle       feedbackBuffer;
    uint32_t           feedbackWidth;       // viewport-aligned feedback resolution
    uint32_t           feedbackHeight;
    std::vector<VirtualPageCoord> requestedPages;  // CPU-side aggregated requests
};
```

Feedback uses a compute shader that writes page IDs to an append buffer.
The buffer is read back using async GPU-CPU transfer.

## Page Streaming

Page loading is asynchronous and priority-driven:

```cpp
struct VirtualPageStreamRequest {
    VirtualTextureId   textureId;
    VirtualPageCoord   pageCoord;        // virtual page (x, y, mip)
    float              priority;         // derived from feedback frequency
    CancellationToken  cancelToken;
};
```

Streaming uses the asset pipeline's async I/O:

1. Request is queued with priority
2. Page data is read from the virtual texture page cache on disk
3. Page is decompressed (BCn on GPU, or CPU decompress for lower tiers)
4. Page is uploaded to the physical texture atlas
5. Page table entry is updated on GPU
6. CPU mirror is marked resident

## Page Cache

Virtual texture pages are cached on disk:

```text
<project>/build/<preset>/asset_cache/<target>/virtual_textures/
├── <textureId>/
│   ├── page_0_0_0.bin     # mip 0, page (0,0)
│   ├── page_0_1_0.bin     # mip 0, page (1,0)
│   ├── ...
│   └── page_3_5_2.bin     # mip 2, page (5,3)
└── index.json
```

The page cache is content-addressed by texture source hash, virtual
coordinates, and import settings.

## Material Integration

Materials declare virtual texture usage:

```cpp
struct MaterialVirtualTextureSlot {
    std::string    slotName;         // e.g., "BaseColor", "Normal"
    AssetId        virtualTextureId;
    VirtualTextureSampler sampler;   // trilinear, anisotropic level
};
```

The material shader receives the virtual texture indirection table binding and
performs the two-level sample:

```hlsl
float4 SampleVirtualTexture(VirtualTexture vt, float2 uv, float mip) {
    float2 pageUV = vt.PageTable.Sample(uv, mip);   // indirection lookup
    return vt.PhysicalAtlas.Sample(pageUV, mip);     // physical sample
}
```

## Performance And Feature Tiers

| Feature              | `es3`      | `dx11`      | `dx12_vulkan` | `high_end`   |
| -------------------- | ----------- | ----------- | -------------- | ------------ |
| Virtual texturing    | No          | Yes         | Yes            | Yes          |
| Max virtual size     | —           | 32K×32K     | 64K×64K        | 128K×128K    |
| Page size            | —           | 128×128     | 128×128        | 128×128      |
| GPU feedback         | —           | CPU readback| Append buffer  | Append buffer|
| Async page upload    | —           | Staged      | Direct         | Direct       |
| Physical page budget | —           | 4K pages    | 8K pages       | 16K pages    |

## Related Documents

- [Virtual Texturing Debug UI Reference](./virtual-texturing-debug.html)

- [Rendering Architecture](./rendering-architecture.md): virtual texture bindings and passes
- [Material And Shader Model](./material-and-shader-model.md): virtual texture material slots
- [Asset Pipeline](./asset-pipeline.md): page cache and streaming I/O
- [Terrain And Foliage Architecture](./terrain-and-foliage-architecture.md): terrain virtual texturing
- [LOD And Culling Architecture](./lod-and-culling-architecture.md): mip-based LOD integration
