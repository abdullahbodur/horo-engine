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

- The renderer supports deferred, forward+ and baseline forward paths. The
  frontend selects a recipe from product preferences, scene requirements and
  effective feature/format/limit predicates under
  [ADR-028](../../adr/028-renderer-capability-limits-and-product-profiles.md).
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
- Ray tracing, mesh shaders and bindless resources require implemented backend
  paths, effective device support and product permission independently.
- Optional high-end features use declared, implemented and cooked raster or
  CPU-driven fallbacks. Missing required features fail admission; a profile
  cannot silently disable content requirements.
- The render graph owns resource allocation and barrier scheduling; individual
  features declare inputs and outputs.

## Lighting

### Deferred Lighting

Preferred path for High and Ultra when the complete GBuffer attachment, format,
sample and resource-limit requirements pass. Otherwise the frontend evaluates
the explicitly allowed fallback recipes before admitting resource work.

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

Preferred path for Standard when compute preparation and its resource requirements
are effective. Baseline uses forward raster with bounded CPU light preparation;
this is also the explicit fallback when compute is unavailable.

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

Rect and Sky light recipes declare their shader/format/limit requirements.
Baseline may use basic approximations; richer area-light and dynamic-sky paths
are selected only when their requirements and product budgets pass admission.

### Clustered / Tiled Culling

Lights are assigned to screen-space tiles or world-space clusters. Each tile
stores a compact index list. Shaders iterate only over relevant lights.

Culling is performed on CPU or GPU according to effective support, the selected
recipe and the product's bounded light/work budget.

## Shadows

### Cascaded Directional Shadows

Directional lights use cascaded shadow maps (CSM).

- cascades are split by logarithmic or mixed partitioning
- cascade count is a validated product budget bounded by effective resource limits
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
a separate optional pass. High may prefer contact shadows; Ultra may prefer a
virtual-shadow-map recipe. Both require independent resource/format predicates
and an explicit fallback to the admitted shadow-atlas recipe.

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
- effective-capability and product-policy gated, with a baked-GI/probe fallback

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

Available only when effective support includes the required ray operations and
resources, the effect implementation is linked, and product policy enables it.

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

Scene-linear HDR and HDR/EDR display output are separate contracts.
[ADR-033](../../adr/033-presentation-and-display-ownership.md) assigns OS/display
facts to Platform and resolved surface output to the render frontend. The graph
owns final conversion/composition against that contract; native attachment
encoding and metadata must agree so conversion occurs exactly once. A float
scene target alone does not prove HDR display support. Scene color policy stays
with RND-013; output capability modeling belongs to RND-008.2.

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

The frontend selects an upscaler provider from the explicit product/user policy
and effective support for that provider's requirements. Vendor identity alone
does not authorize selection; provider selection never switches the renderer API.
Temporal providers consume jittered low-resolution color, motion vectors, depth
and exposure. Each provider declares its exact inputs and cooked/runtime support;
native resolution is the explicit optional-upscaler fallback.

## Ray Tracing

Ray tracing is gated by effective ray-operation support, implemented/cooked effect
paths and product policy; an Ultra preference alone does not enable it.

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

Built-in profile recipes do not require ray tracing. Their optional ray-traced
effects declare raster fallbacks. Content that explicitly requires a ray path
fails admission on unsupported hardware rather than silently changing its effect.

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

Fallback to a declared bound-resource variant when effective bindless support is
absent or product policy disables it; missing required variants fail admission.

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

Virtual texturing is optional and admitted through its effective feature/format
requirements and product residency budgets, not through a profile rank.

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

## Product Profile Policy

[ADR-028](../../adr/028-renderer-capability-limits-and-product-profiles.md) owns
the Baseline through Ultra recipe preferences and the legacy API-tier migration.
This document owns effect algorithms and their concrete requirements. A profile
is not a bundle of guaranteed hardware features: every selected pass declares
required operations, formats, limits, inputs, variants and finite budgets.
The frontend records optional fallback decisions and rejects missing required
paths before resource creation. No feature or upscaler selects another renderer
backend, and no frame-hot path queries the native driver for capability policy.

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
- Profile/feature fallback tests verifying declared degradation, rejection of
  missing required paths and preservation of the active plan after failed updates.
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
