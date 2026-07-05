# Advanced Rendering Architecture

## Purpose

This document defines Horo Engine's high-end rendering feature stack: lighting,
shadows, global illumination, reflections, post-processing, temporal
anti-aliasing, upscaling, and capability tiers. It sits on top of the
[Rendering Architecture](./rendering-architecture.md) and the
[Material And Shader Model](./material-and-shader-model.md), filling in the
features required for modern visual fidelity without prescribing every backend
implementation detail.

The goal is to give the renderer a stable contract for producing AAA-quality
images while keeping fallback paths explicit and testable.

## Scope

Covered:

- deferred and forward+ lighting architecture
- shadow systems
- global illumination strategies and probes
- reflections and planar reflections
- HDR post-processing pipeline
- temporal anti-aliasing (TAA) and TAA upscaling (TAAU)
- vendor-neutral upscaling interface
- ray tracing boundary
- GPU-driven rendering and meshlets
- bindless resources
- virtual texturing
- occlusion culling
- terrain and foliage rendering boundary
- feature tiers and fallback policy

Not covered:

- RHI backend details (see [Rendering Architecture](./rendering-architecture.md))
- material parameter model (see [Material And Shader Model](./material-and-shader-model.md))
- asset import/cook (see [Asset Pipeline](./asset-pipeline.md))

## Core Decisions

- The renderer supports both deferred and forward+ paths. The active path is
  selected by feature tier and scene requirements.
- Lighting is clustered or tiled by default to scale with many punctual lights.
- Shadows use a configurable atlas with cascaded directional shadows and
  punctual shadow maps.
- Global illumination is provided by a combination of baked lightmaps,
  reflection probes, irradiance volumes, and optional real-time techniques.
- Reflections combine SSR, reflection probes, and optional ray-traced
  reflections behind a capability check.
- Post-processing is a explicit render graph pass chain, not hidden in
  individual materials.
- TAA and upscaling are optional but supported through a vendor-neutral
  interface.
- Ray tracing, mesh shaders, and bindless resources are gated by feature tier
  and compile-time capability.
- Every high-end feature has a deterministic raster or CPU-driven fallback;
  tiers disable features rather than fail.
- The render graph owns resource allocation and barrier scheduling; individual
  features declare inputs and outputs.

## Lighting

### Deferred Lighting

Default path for `dx12_vulkan` and `high_end` tiers.

Pass flow:

```text
GBuffer Pass
  -> albedo, normal, material properties, emissive, motion vectors
Lighting Pass
  -> tiled/clustered light evaluation using GBuffer
Transparency Pass
  -> forward-lit translucent objects
```

GBuffer layout is declared as part of the active material/shader model contract
(see [Material And Shader Model](./material-and-shader-model.md)). The layout
must be stable enough for post-processing, lighting, and decal passes to consume
it without per-material negotiation.

### Forward+ Lighting

Default path for `dx11` and fallback for `es3`.

Forward+ performs a light culling prepass to produce a per-tile light list,
then renders opaque and translucent meshes in a forward pass.

### Light Types

| Type        | Notes                                                    |
| ----------- | -------------------------------------------------------- |
| Directional | Sun/moon; cascaded shadow maps; no position attenuation. |
| Point       | Omni-directional; shadow cubemap or dual-paraboloid.     |
| Spot        | Cone-shaped; shadow map.                                 |
| Rect        | Optional; area light approximation.                      |
| Sky         | Ambient contribution and environment cubemap.            |

Rect and Sky light support is tier-dependent. `es3` may support only basic
approximations; full area-light and dynamic sky contributions are enabled on
`dx11` and above.

### Clustered / Tiled Culling

Lights are assigned to screen-space tiles or world-space clusters. Each tile
stores a compact index list. Shaders iterate only over relevant lights.

Culling is performed on CPU or GPU depending on tier and light count.

## Shadows

### Cascaded Directional Shadows

Directional lights use cascaded shadow maps (CSM).

- cascades are split by logarithmic or mixed partitioning
- cascade count is tier-dependent
- cascade blending reduces seams
- stable cascades reduce shimmer during camera rotation

### Punctual Shadows

Point and spot lights use shadow maps or cubemaps. Shadow allocation is managed
by a shadow atlas.

### Shadow Atlas

A global atlas packs dynamic shadow maps. Allocation is frame-local and
prioritized by:

- light importance
- screen size
- distance from camera

### Contact Shadows

Optional screen-space contact shadows add fine detail near the camera. They are
a separate pass that may be disabled on lower tiers. Contact shadows are used on
`dx12_vulkan` and may be combined with or replaced by virtual shadow maps on
`high_end`.

## Global Illumination

### Baked GI

Lightmaps store precomputed indirect lighting for static geometry.

- UV2-based lightmap atlases
- directional lightmaps for normal modulation
- stored as textures in the cook output

### Irradiance Volumes

Sparse volumes store irradiance probes for dynamic objects.

- placed by artists or generated procedurally
- trilinear interpolation for blending
- fallback to ambient term when outside volumes

### Reflection Probes

Cubemap probes capture local specular reflection.

- blended by influence volume
- parallax correction for box/sphere volumes
- updated dynamically or baked

### Screen-Space Global Illumination

Optional real-time technique using the depth buffer and GBuffer.

- lower accuracy than baked GI
- useful for dynamic objects and contact color bleeding
- tier-gated

## Reflections

### Screen-Space Reflections (SSR)

Trace rays in screen-space using the GBuffer and depth buffer.

- works for glossy and mirror-like surfaces
- fails at screen edges and occlusions
- combined with reflection probes to hide missing data

SSR and SSGI both consume the GBuffer and depth buffer. They are independent
passes and may run in either order; the render graph schedules them before any
pass that reads their output.

### Planar Reflections

Render the scene into a reflection texture for flat surfaces like water or
mirrors.

- expensive; used sparingly
- clipped to the reflection plane

### Ray-Traced Reflections

Available on tiers that support hardware ray tracing.

- more accurate than SSR
- denoising required
- fallback to SSR + probes when unavailable

## Post-Processing

Post-processing is a chain of render graph passes operating on the HDR image.

### Standard Passes

| Pass                  | Purpose                             |
| --------------------- | ----------------------------------- |
| `MotionBlur`          | Camera and object motion blur.      |
| `DepthOfField`        | Bokeh blur based on depth.          |
| `Bloom`               | Bright highlight glow.              |
| `LensFlare`           | Artifact glow around bright lights. |
| `ToneMapping`         | HDR to display mapping.             |
| `ColorGrading`        | LUT-based color correction.         |
| `Vignette`            | Edge darkening.                     |
| `FilmGrain`           | Optional grain.                     |
| `ChromaticAberration` | Optional lens artifact.             |

### HDR Pipeline

- scene is rendered to a high-precision floating-point target
- exposure is applied before tone mapping
- tone mapping uses a configurable curve (ACES, Reinhard, etc.)

### Exposure

Exposure may be:

- fixed manual value
- auto-exposure based on scene luminance histogram
- artistic key-value override

## Temporal Anti-Aliasing

TAA combines samples from multiple frames to reduce aliasing.

Requirements:

- per-pixel velocity vectors
- jittered projection matrix
- history buffer
- motion vector reprojection

TAAU (TAA Upscaling) renders at a lower resolution and reconstructs a higher
output resolution using temporal accumulation.

## Upscaling

Horo exposes a vendor-neutral upscaler interface.

Backends:

| Backend  | Notes                            |
| -------- | -------------------------------- |
| `Native` | No upscaling.                    |
| `TAAU`   | Temporal accumulation upscaling. |
| `DLSS`   | NVIDIA deep-learning upscaling.  |
| `FSR`    | AMD spatial/temporal upscaling.  |
| `XeSS`   | Intel XeSS upscaling.            |

The renderer selects the best available backend based on GPU vendor, tier, and
user preference. All backends share the same input: jittered low-res color,
motion vectors, depth, and exposure.

## Ray Tracing

Ray tracing is gated by feature tier and compile-time capability.

Use cases:

- reflections
- shadows
- ambient occlusion
- global illumination

Requirements:

- acceleration structure build/update
- ray tracing pipeline state objects
- shader table management
- denoising pass

Ray tracing is never required. Every ray-traced effect has a raster fallback.

## GPU-Driven Rendering

GPU-driven rendering moves culling and draw submission to the GPU.

Components:

- scene GPU buffer with instance data
- compute culling pass
- indirect draw/dispatch generation
- draw compaction

Benefits:

- fewer CPU draw calls
- better scaling with instance count
- enables GPU culling

Fallback to CPU-driven rendering when compute or indirect draw is unavailable.

## Bindless Resources

Bindless descriptor indexing reduces descriptor set pressure.

- textures referenced by index
- materials pass texture indices
- descriptor heap/array managed by the renderer

Fallback to bound descriptors on tiers without bindless support.

## Mesh Shaders And Meshlets

Optional high-end path for geometry submission.

- meshes are preprocessed into meshlets
- task shaders perform culling
- mesh shaders output triangles

Fallback to traditional vertex/index pipeline when mesh shaders are unavailable.

## Virtual Texturing

Virtual texturing allows scenes to use more texture data than GPU memory.

- texture data is split into tiles
- runtime requests visible tiles
- sparse texture arrays or page tables manage residency

Virtual texturing is optional and tier-gated.

## Occlusion Culling

Occlusion culling reduces draw calls for hidden objects.

Techniques:

- CPU frustum culling
- GPU Hi-Z occlusion culling
- portal culling for indoor scenes
- software occlusion rasterization (optional)

## Terrain And Foliage Rendering

Terrain and foliage are not core primitives; they require dedicated subsystems.
This document defines the boundary:

- terrain produces render instances through a virtualized geometry system
- foliage uses instanced rendering with wind and LOD
- both integrate with the material system and shadow passes
- both participate in culling and occlusion

Detailed terrain/foliage architecture is covered in a separate document when
implemented.

## Feature Tiers

| Tier          | Lighting                | Shadows                     | GI                        | Reflections          | Post  | TAA/Upscale   | Ray Tracing | Bindless | Mesh Shaders |
| ------------- | ----------------------- | --------------------------- | ------------------------- | -------------------- | ----- | ------------- | ----------- | -------- | ------------ |
| `es3`         | Forward, limited lights | Single cascade, no punctual | Ambient + probes          | Probes only          | Basic | Native        | No          | No       | No           |
| `dx11`        | Forward+                | Cascaded + atlas            | Lightmaps + probes        | SSR + probes         | Full  | TAA optional  | No          | Limited  | No           |
| `dx12_vulkan` | Deferred                | Cascaded + atlas + contact  | Lightmaps + probes + SSGI | SSR + RT reflections | Full  | TAA/TAAU/FSR  | Optional    | Yes      | Optional     |
| `high_end`    | Deferred/clustered      | Virtual shadow maps         | Real-time GI              | SSR + RT + probes    | Full  | All upscalers | Yes         | Yes      | Yes          |

## Render Graph Integration

Each feature declares its inputs, outputs, and resource dependencies to the
render graph. The render graph schedules passes, allocates transient resources,
and inserts barriers.

Features do not allocate render targets directly.

## Diagnostics And Profiling

Debug views:

- GBuffer channels
- shadow cascades
- light count heatmap
- cluster visualization
- motion vectors
- reflection probe influence
- overdraw
- GPU timing per pass

## Testing Requirements

- Visual regression tests for representative scenes.
- Tier fallback tests verifying degraded output without crashes.
- Shader permutation tests for each lighting path.
- Performance tests measuring draw call scaling.
- Determinism tests for TAA and motion vector generation.

## Related Documents

- [Rendering Architecture](./rendering-architecture.md): render graph, backend
  abstraction, pass extraction.
- [Material And Shader Model](./material-and-shader-model.md): PBR parameters,
  shader variants, feature tiers.
- [Asset Pipeline](./asset-pipeline.md): texture, mesh, and shader cook.
- [VFX And Particles Architecture](./vfx-and-particles-architecture.md):
  translucent effects, decals, volumetrics.
- Terrain And Foliage Architecture — not yet written; boundary is described in
  this document.
