# Material And Shader Model

## Purpose

This document defines Horo Engine's material and shading architecture. It
specifies the standard PBR material model, shader variant system, material asset
format, material instances, render feature tiers, and the boundary between
authoring content and runtime shader compilation.

The goal is to give artists and technical artists a stable material contract
while giving the renderer enough information to compile efficient shader
variants, batch render instances, and fall back gracefully on lower-end
hardware.

## Scope

Covered:

- standard PBR material model and parameter set
- material asset format and serialization
- material instances and overrides
- shader variants, permutation keys, and compilation policy
- feature tiers and capability-driven fallback
- pipeline cache and offline shader compilation boundary
- integration with the asset pipeline, render graph, and scene runtime

Not covered:

- specific renderer backend implementations (see
  [Rendering Architecture](./rendering-architecture.md))
- texture import settings (see [Asset Pipeline](./asset-pipeline.md))
- lighting and shadow algorithms (see
  [Advanced Rendering Architecture](./advanced-rendering-architecture.md))

## Core Decisions

- Materials are authored assets. They reference a shader graph or shader source
  and declare parameters and feature flags.
- Material instances override parameters without duplicating shader bindings or
  variant selection logic.
- The standard PBR material model is the default and must be supported by every
  renderer backend.
- Shader variants are produced from explicit permutation keys, not from runtime
  string substitution.
- Feature tiers declare hardware capability sets. Materials and shaders declare
  which tiers they require; the runtime selects the best available tier and
  records fallback decisions.
- Pipeline state objects are cached and reused. Shader compilation may happen
  offline during cook or lazily at runtime, but the cache key format is the same.
- No gameplay code queries raw shader handles. Gameplay sees only material names,
  parameter overrides, and feature flags.

## Shader Graph Editor Surface

Shader and material graph authoring uses the shared editor graph surface defined
in [Editor Panel and Tab Architecture](../editor/editor-panel-host.md). The first
production graph surface is built on `imgui-node-editor` behind a private Horo
adapter; `imnodes` is reserved for prototype or simple internal graph tools.

The node editor widget is not the shader compiler and not the material schema.
It renders graph snapshots and emits user-intent commands. The material/shader
subsystem owns:

- graph asset schema and stable node, pin, and link identity;
- type checking between pins;
- cycle and dependency validation;
- texture, sampler, material parameter, and feature-tier validation;
- shader-code generation or graph IR emission;
- diagnostics and source/graph location mapping;
- cook-time variant generation and runtime fallback policy.

Node positions, collapsed state, selection, and zoom are editor presentation
state. Shader graph semantics are serialized through stable graph asset data,
not through third-party widget IDs or layout state.

AI-assisted shader editing may propose nodes, links, generated shader source, or
parameter changes, but it must go through graph edit commands and material
validation before any asset is modified. The assistant cannot inject raw shader
source into a material asset without diagnostics, preview, and undo integration.

## Material Model

### Standard PBR Parameters

Every standard PBR material exposes these parameters:

| Parameter | Type | Default | Description |
|---|---|---|---|
| `albedo` | `Vec3` or texture | `(0.5, 0.5, 0.5)` | Base color. |
| `metallic` | `float` or texture | `0.0` | 0 = dielectric, 1 = metal. |
| `roughness` | `float` or texture | `0.5` | Perceptual roughness. |
| `normal` | texture | none | Tangent-space normal map. |
| `occlusion` | `float` or texture | `1.0` | Ambient occlusion multiplier. |
| `emissive` | `Vec3` or texture | `(0, 0, 0)` | Emissive color. |
| `emissiveIntensity` | `float` | `1.0` | Scalar emissive multiplier. |
| `opacity` | `float` or texture | `1.0` | Alpha value for translucent/masked. |
| `opacityMaskThreshold` | `float` | required for `Masked` | Alpha-test threshold for masked. |

The standard shader uses a metallic-roughness workflow. Specular-glossness is not
a core workflow but may be added through custom shaders.

### Blend And Shading Modes

| Mode | Behavior | Use case |
|---|---|---|
| `Opaque` | No transparency, writes depth. | Most surfaces. |
| `Masked` | Alpha test, writes depth, no sorting. | Foliage, grates, decals. |
| `Translucent` | Blended transparency, sorted back-to-front. | Glass, water, fog. |
| `Additive` | Additive blend, no depth write. | VFX, glows. |

| Shading Model | Notes |
|---|---|
| `Lit` | Default PBR. |
| `Unlit` | Emissive/opacity only. |
| `Hair` | Optional; requires anisotropic scattering. |
| `Cloth` | Optional; sheen and fuzz layers. |
| `Subsurface` | Optional; translucency and scattering. |
| `ClearCoat` | Optional; dual normal/roughness layer. |

New shading models must register their required vertex attributes, permutation
keys, and render passes. The renderer may skip unsupported shading models on
lower feature tiers. For example, `Hair`, `Cloth`, `Subsurface`, and `ClearCoat`
are typically disabled on `es3`; `dx11` and above may support them depending on
the renderer backend.

### Material Input Types

Material parameters may be:

- scalar `float`
- `Vec2`, `Vec3`, `Vec4`
- `Color` (sRGB or linear depending on semantic)
- texture `AssetId` with optional sampler state override
- boolean feature toggles

Boolean feature toggles participate in shader variant generation. Scalar and
color parameters do not. Texture presence usually participates in variant
generation unless the shader uses bindless default descriptors.

## Material Asset Format

A material asset is a JSON document stored as `.horomat` or produced from a
shader graph `.horoshadergraph`.

```json
{
  "schemaVersion": 1,
  "assetGuid": "a1b2c3d4-e5f6-7890-abcd-ef1234567890",
  "assetType": "material",
  "sourceShader": {
    "kind": "standard",
    "variantSet": "core.shaders.standard"
  },
  "parameters": {
    "albedo": {
      "type": "texture",
      "value": "texture_brick_001"
    },
    "roughness": {
      "type": "float",
      "value": 0.8
    },
    "metallic": {
      "type": "float",
      "value": 0.0
    }
  },
  "features": {
    "normalMap": true,
    "emissive": false,
    "clearCoat": false
  },
  "renderState": {
    "blendMode": "Opaque",
    "shadingModel": "Lit",
    "cullMode": "Back",
    "depthTest": "LessEqual",
    "depthWrite": true
  },
  "minTier": "es3",
  "preferredTier": "dx12_vulkan",
  "tierOverrides": {
    "es3": {
      "features": { "normalMap": false },
      "parameters": { "roughness": 1.0 }
    }
  }
}
```

The asset pipeline cooks this into a runtime material descriptor plus a set of
shader variant requests.

## Material Instances

A material instance inherits shader, feature flags, and render state from a
parent material and overrides a subset of parameters.

```json
{
  "schemaVersion": 1,
  "assetGuid": "b2c3d4e5-f6a7-8901-bcde-f23456789012",
  "assetType": "material_instance",
  "parentMaterial": "a1b2c3d4...",
  "parameterOverrides": {
    "albedo": { "type": "texture", "value": "texture_concrete_002" },
    "roughness": { "type": "float", "value": 0.9 }
  }
}
```

Rules:

- Instances cannot change feature flags or render state that affect the shader
  variant key. Those changes require a new material.
- Scalar, color, and texture **value** overrides do not change the variant key.
  Changing a texture override from one existing texture to another does not alter
  the "texture present" permutation; only the bound descriptor changes.
- Instances share the same pipeline state objects and shader variants as the
  parent whenever possible.
- Overrides are stored in the scene object component, not in the parent material
  asset.
- Runtime material instances live in the scene runtime material table and are
  keyed by parent material plus override hash.

## Shader Variant System

A shader variant is a fully compiled shader program produced from a base shader
plus a permutation key.

### Permutation Key

```cpp
struct ShaderPermutationKey {
    ShaderId shaderId;
    FeatureMask featureMask;
    RenderPassId passId;
    VertexLayoutId vertexLayoutId;
    TargetPlatformId platform;
    QualityTier tier;
};
```

Feature flags that participate in the key must be declared explicitly in the
shader manifest. Implicit feature detection from material parameters is not
allowed.

### Variant Compilation Policy

- **Cook-time**: all declared permutations are compiled during asset cooking.
- **Runtime lazy**: missing permutations may be compiled on first use if the
  platform allows runtime shader compilation.
- **Headless/server**: only variants requested by the release profile are
  compiled; no runtime compilation.

The pipeline emits diagnostics when a requested variant is missing and runtime
compilation is disabled.

### Variant Explosion Guard

- Feature flags must be opt-in in the shader manifest.
- The cook profile may cap the maximum number of variants per shader.
- Materials that request unsupported feature combinations produce errors during
  import, not at runtime.

## Feature Tiers

Feature tiers group renderer capabilities.

| Tier | Representative capabilities |
|---|---|
| `es3` | Forward rendering, limited lights, no compute, no bindless. |
| `dx11` | Forward+/limited deferred, compute, texture arrays. |
| `dx12_vulkan` | Full deferred, bindless, compute, ray-tracing optional. |
| `high_end` | Mesh shaders, hardware ray tracing, virtual texturing, Nanite-style virtual geometry if implemented. |

Tier selection order:

1. Target platform declares maximum tier.
2. Cook profile may lower the tier for release builds.
3. Runtime GPU caps clamp the tier.
4. User settings may lower the tier further.

A material declares `minTier` and `preferredTier`. The runtime records the
selected tier and any disabled features.

`minTier` is the lowest tier the material can run on. `preferredTier` is the
tier the author optimized for. `tierOverrides` patch features and parameters
when the selected tier is below `preferredTier`; each key in `tierOverrides`
must match a declared tier name (`es3`, `dx11`, `dx12_vulkan`, `high_end`). If a
tier lacks an override, the material runs with the base parameters and feature
set, possibly with degraded quality but without failing conversion.

## Pipeline Cache

The renderer owns a `ShaderPipelineCache` keyed by `ShaderPermutationKey`. It
maps to:

- compiled shader binaries
- pipeline state objects
- descriptor set layouts (or equivalent backend abstractions)
- root signature / pipeline layout handles

Cache lifetime:

- process-scoped in development
- shipped as a cold-start cache in release builds
- invalidated when shader source, target platform, or renderer backend version
  changes

The cache must be serializable and reloadable without recompiling if the binary
is compatible.

## Renderer Integration

### Material Table

The scene runtime owns a `MaterialTable` that maps `MaterialId` to:

- parent material asset
- resolved parameter block
- selected shader variant
- pipeline state object reference

The table is populated during scene conversion. Changing the parent material or
feature flags at runtime requires an explicit material swap. Editing per-instance
parameter overrides does not require a swap; the scene runtime updates the
instance's entry in the material table under a new override hash while the parent
material remains shared.

### Render Extraction

Mesh instances extract:

- `MaterialId`
- per-instance parameter overrides (when supported)
- transform, bounds, and visibility flags

The render frontend groups instances by material and shader variant for batching.

### Pass Binding

Materials declare which render passes they participate in:

- `OpaqueForward` / `DeferredGBuffer`
- `ShadowCaster`
- `DepthPrepass`
- `Translucent`
- `MotionVectors`
- `CustomN`

The render graph queries the material table for the correct variant per pass.

### GBuffer Contract

When a material participates in the deferred path, the active shader declares
its GBuffer output layout. The standard PBR material model defines a canonical
layout so that lighting, decal, SSR/SSGI, and post-processing passes can consume
GBuffer data without per-material negotiation.

The standard GBuffer includes, at minimum:

| Slot | Content |
|---|---|
| `GBUF_ALBEDO` | Albedo (RGB) and opacity (A) if needed. |
| `GBUF_NORMAL` | Tangent-space normal (RG) and roughness/metallic packed bits. |
| `GBUF_MATERIAL` | Metallic, roughness, occlusion, and material flags. |
| `GBUF_EMISSIVE` | Emissive color (RGB) and emissive intensity scalar. |
| `GBUF_MOTION` | Per-pixel motion vectors for TAA and motion blur. |

Custom shaders may append additional slots but must not reorder or remove the
standard slots. The renderer backend compiles the declared layout into a stable
`GBufferLayoutId` used by downstream passes.

## Shader Graph

A shader graph is an authoring-time visual or textual representation that
generates shader source. It is not interpreted at runtime.

- Graphs compile to shader source during import.
- Generated source must conform to the shader manifest contract.
- Custom nodes must declare required features, inputs, outputs, and target
  passes.
- Graph assets ship only in development builds for hot-reload; they are stripped
  from release builds.

## Diagnostics And Validation

Material validation rules:

- all referenced textures must exist and match expected usage
- parameter types must match the shader manifest
- feature flag combinations must be legal
- `minTier` must not exceed any target platform tier
- translucent materials must not request opaque-only passes
- masked materials must explicitly provide `opacityMaskThreshold`

Cook-time diagnostics:

- list of emitted variants per shader
- variants skipped due to caps or profile limits
- missing variant warnings when runtime compilation is disabled

## Testing Requirements

- Unit tests for permutation key equality and hash stability.
- Tests for material instance override inheritance.
- Cook tests that verify variant emission for each standard feature flag.
- Runtime tests that verify fallback tier disables features without crashing.
- Visual regression tests for standard material spheres under representative
  lighting.

## Related Documents

- [Shader Graph UI Reference](./shader-graph-editor.html)

- [Material Editor UI Reference](./material-editor.html): preview, shader domain, texture slots, parameters, and tier compatibility panel.

- [Rendering Architecture](./rendering-architecture.md): render graph, backend
  abstraction, pass extraction.
- [Asset Pipeline](./asset-pipeline.md): import, cook, cache, and platform
  profiles.
- [Advanced Rendering Architecture](./advanced-rendering-architecture.md):
  lighting, shadows, global illumination, post-processing.
- [Built-In Scene Primitives](./built-in-scene-primitives.md): default material
  assignment and vertex layout.
