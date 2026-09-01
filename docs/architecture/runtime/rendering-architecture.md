# Rendering Architecture

## Purpose

This document defines the backend-neutral rendering model, render extraction,
frame and pass execution, GPU resource ownership, synchronization, resize,
device failure, and headless rendering behavior.

The equal first-class obligations of interactive backend modules are defined by
[Render Backend Parity Contract](render-backend-parity-contract.md).

## Core Decisions

- Scene and editor code submit backend-neutral render data. This includes
  imported meshes, generated primitive meshes, debug geometry, and GUI overlays.
- Backend-specific API types remain private to backend targets.
- The renderer consumes immutable frame snapshots, not mutable scene storage.
  Primitive meshes are generated on demand and cached; their render instances
  are indistinguishable from imported mesh instances in the render snapshot.
- GPU resources use typed generation-checked handles.
- Resource creation and destruction obey graphics affinity and GPU completion.
- Frame and pass ordering is explicit and validated.
- The null renderer is a supported backend for tests and headless workflows.
- OpenGL, Metal, Vulkan, and future interactive backends are equal sibling
  implementations. Implementation order does not grant architectural priority.
- Backend loss or unavailability returns typed errors rather than leaking API
  failures through engine interfaces.
- The active renderer backend is selected by configuration or command-line
  override at host startup. Runtime scene, editor, asset, gameplay, and MCP code
  do not branch on concrete backend types.

## Layer Model

```text
Scene Runtime / Editor Viewport
          |
          v
Render Extraction
          |
          v
Render Frontend
    frame graph, sorting, resources, uploads
          |
          v
Render API
    backend-neutral command and resource contracts
          |
          v
Registered Backend Module
    OpenGL / Vulkan / Metal / D3D12 / Null
```

The frontend owns engine rendering policy. Backends own API translation,
device/context state, synchronization primitives, and concrete GPU objects.

## Scene And Clip-Space Conventions

Horo scene and authoring space is right-handed, uses positive Y as up, and uses
negative Z as the default forward/view direction. Angles crossing typed math and
render contracts are radians. Matrices are column-major and transform column
vectors; local transforms compose as translation, then rotation, then scale
(`T * R * S`). Imported coordinate systems are normalized at the asset boundary
before scene or render extraction.

Scene transforms and camera values remain backend-neutral. Clip-depth range is
an explicit render/API adaptation: OpenGL uses `[-1, 1]`, while Metal and Vulkan
use `[0, 1]`. Concrete backends may apply this projection adaptation, but must
not independently redefine scene handedness, camera policy, transform order, or
authoring units.

### Camera-Relative Rendering And Universal Shader Precision

To maintain sub-millimeter visual precision across worlds spanning thousands of
kilometers while preserving universal GPU performance across Mobile (OpenGL ES /
Metal iOS), WebGL, Desktop, and Consoles:

- **Universal 32-bit GPU Shaders**: All vertex, geometry, and fragment shaders
  execute standard 32-bit single-precision (`fp32`) / half-precision (`fp16`)
  floating-point arithmetic. No GPU backend requires or relies on hardware `fp64`
  vertex attributes or double-single emulation.
- **CPU Camera-Relative Matrices**: The high-precision subtraction
  $P_{\text{inst\_cam\_rel}} = P_{\text{inst\_world}} - C_{\text{camera}}$ is
  evaluated on the CPU during Render Extraction. The Model-View matrix
  $M_{\text{view\_rel}} = V_{\text{rel}} \cdot M_{\text{inst\_rel}}$ is passed
  in 32-bit float uniform/constant buffers.
- **Vertex Transformation**: Vertex shaders compute
  $v_{\text{clip}} = P_{\text{projection}} \cdot M_{\text{view\_rel}} \cdot v_{\text{mesh\_local}}$,
  eliminating floating-point vertex jitter and Z-fighting at extreme world distances.
- **Shadow and Culling Precision**: Frustum culling and shadow cascade splits
  operate in camera-relative space, maximizing depth precision near the camera.

See [Coordinate Precision And Origin Rebasing](./coordinate-precision-and-origin-rebasing.md)
and [ADR-026](../../adr/026-large-world-precision-and-floating-origin-strategy.md).

## Backend Selection

Renderer backend selection is host-owned startup policy. The application
composition root resolves the requested backend through the verified renderer
component registry before creating a graphics window. It loads and negotiates
the exact selected module, adapts its provider into `RenderBackendRegistry`,
seals the registry, and passes the selection to `RenderFrontend::Create`. The
frontend constructs and initializes that inert instance with a backend-neutral
`RenderBackendConfig`, owns it for the frontend lifetime, and shuts it down
before releasing it.

Install, verification, probe, repair, and no-renderer behavior are defined by
[Renderer Distribution And Availability](renderer-distribution-and-availability.md).

Selection inputs, in priority order:

1. explicit command-line override
2. project or user configuration
3. host default for the current platform and build profile

Canonical backend identifiers:

Identifiers are lowercase ASCII slugs of at most 64 characters. They begin with
a letter, contain only letters, digits, or `-`, and do not end with `-`. Registry
registration rejects every non-canonical spelling before configuration lookup.

| Identifier | Backend | Status |
|---|---|---|
| `null` | `HoroEngine::RenderNull` | Required for tests and headless tools |
| `opengl` | `HoroEngine::RenderOpenGL` | Implemented; current editor migration path |
| `vulkan` | `HoroEngine::RenderVulkan` | Planned explicit-API desktop backend |
| `metal` | `HoroEngine::RenderMetal` | Implemented Apple parity peer |
| `d3d12` | `HoroEngine::RenderD3D12` | Planned Windows backend |

Configuration and CLI spelling use the same identifiers:

```text
render.backend = "opengl"
horo-engine run --renderer opengl
HoroEditor --renderer opengl
```

The default backend is a host policy, not a property of scene data or gameplay
code and not an architectural ranking. A host may temporarily default to the
only implementation that satisfies its parity gate; headless tools and CI
default to `null`. A platform-specific default may change only through an
architecture update and release note because it affects startup behavior,
driver requirements, and artifact validation.

Fallback is explicit. If a requested backend is unavailable, unsupported, or
fails initialization, the host returns a typed startup error unless the user or
profile opted into a fallback list:

```text
render.backend = "vulkan"
render.fallbacks = ["opengl", "null"]
```

Automatic silent fallback is forbidden. Diagnostics must include the requested
backend, attempted fallback backend if any, failure category, and relevant
capability or platform reason. A fallback to `null` is allowed only for tools,
tests, and explicitly headless workflows; interactive editor/game hosts must not
silently switch to `null`.

## Capabilities, Limits And Product Profiles

[ADR-032](../../adr/032-d3d12-baseline-and-agility-sdk-policy.md) owns the planned
`d3d12` component's native baseline: Windows 11 x86_64, feature level 12_0,
Shader Model 6.0, and host-owned Agility activation. The SDK package pin, runtime
selection, feature queries, and effective engine paths remain distinct. Legacy
barriers and root signature 1.0 provide the baseline; advanced paths are optional.
This policy does not implement the backend or change the Windows host default.

[ADR-031](../../adr/031-vulkan-loader-platform-and-version-baseline.md) owns the
planned `vulkan` component's native baseline: Vulkan 1.3 instance/device support,
explicit dynamic-rendering/synchronization2/timeline feature enablement, a single
system loader, and actual-surface admission on Windows/Linux desktop. It defers
portability-subset product support. These are downstream requirements, not a
claim that a Vulkan backend target exists or that optional engine features are
already implemented.

[ADR-030](../../adr/030-metal-platform-and-feature-baseline.md) owns the `metal`
component's native baseline: macOS 14.0+, Apple7 on native arm64 or Mac2 on native
x86_64, explicit MSL 2.4 shaders, and conventional command-buffer/encoder APIs.
Each shipped variant needs separate qualification. OS, GPU family, shader
language, implemented operations, and product profiles remain distinct checks;
neither a non-null device nor the `metal` identifier grants effective support.

[ADR-029](../../adr/029-opengl-core-profile-and-platform-policy.md) owns
the `opengl` component's native version, platform, and deprecation policy:
desktop OpenGL 4.1 Core minimum, actual-context validation, and qualified desktop
Windows/Linux/macOS support. “Compatibility renderer” does not permit the OpenGL
Compatibility Profile. OpenGL ES/WebGL references elsewhere in this architecture
describe possible future targets, not support provided by this component.
The macOS warning never changes backend selection or project settings by itself.
Context requirements must reach the selected private platform adapter before
window creation; completing that seam and native admission is downstream
implementation work, not a claim that today's bootstrap is fully qualified.

[ADR-028](../../adr/028-renderer-capability-limits-and-product-profiles.md) owns
the renderer capability and profile policy. Backend-reported device facts,
implemented backend operations, driver-adjusted effective support, and requested
product quality are distinct contracts. The frontend owns the immutable effective
snapshot; resource and plan admission use it, never a raw report or profile rank.

`Baseline`, `Standard`, `High`, and `Ultra` select rendering recipe preferences.
They are not graphics APIs, build profiles, hardware guarantees, or gameplay
budgets. Optional features use declared, implemented and cooked fallback recipes;
missing required features return typed failures. Limits have explicit units and
format checks validate the complete usage/sample/view combination. No resource
API silently substitutes formats or lowers quality.

Final support is established after device initialization, before resource work
is admitted; module metadata and probe results do not replace that validation.
Snapshots carry frontend/device/revision identity. Worker plans are revalidated
on admission, quality changes publish a new policy at a frame boundary, and
device recreation invalidates old snapshots and resolves policy again before
resource reconstruction. Profile changes never silently switch a backend.

The existing `RenderBackendCapabilities` booleans are a transitional implementation.
The richer value contracts, driver-policy provider and profile resolver are
downstream implementation work; this M0 decision does not claim them as available.

## Raster Render Path And Quality Policy

[ADR-036](../../adr/036-raster-render-path-and-quality-architecture.md) owns
production raster recipe selection. `RenderFrontend` resolves one immutable,
versioned `RasterRecipe` from the requested product profile, project/content
requirements, the effective capability/limit snapshot, cooked variants and
finite budgets. Backends report facts and execute the compiled graph; they do not
select, promote, demote or silently substitute a path. Scene extraction and
materials likewise provide typed requirements and compatibility, not global path
policy.

Horo has three production opaque raster families:

- **Forward** uses bounded CPU-prepared per-view/per-draw light lists and requires
  no compute, storage-buffer, indirect-draw or multiple-render-target support.
- **Clustered Forward+** uses a depth prepass and bounded 3D view-space cluster
  light lists. “Forward+” is the product-facing name for this clustered family;
  there is no separate 2D tiled production path.
- **Deferred** writes a versioned GBuffer schema and performs screen-space
  lighting. It is a hybrid frame recipe: incompatible opaque materials and all
  baseline transparency still use declared forward passes. Clustering is an
  optional light-preparation contract on that family, not a fourth path: a
  Deferred recipe must declare clustered preparation (same overflow as Clustered
  Forward+) or clustering-less preparation (Forward-equivalent bounded
  screen-space overflow). Unspecified Deferred light counts fail admission.

Scene-material classification is keyed by scene-conversion `MaterialId`
([ADR-027](../../adr/027-renderer-resource-identity-and-descriptors.md)): a
material-table key, not a resident GPU handle. Opaque, masked, forward-only
opaque, stable sorted-alpha and additive work are mutually exclusive cooked
categories. Combined alpha-test plus blend (dithered/stochastic transition) is
unsupported unless authored as exactly one of those categories or an optional
named stochastic/OIT recipe. Masked coverage is consistent between depth, shadow
and color/GBuffer passes. Sorted alpha is depth-tested, non-depth-writing and
ordered back-to-front with stable instance-identity ties; additive work uses its
own commutative pass. Order-independent transparency and refraction are separate
optional recipes with declared capability, memory and fallback contracts.

[ADR-011](../../adr/011-vfx-effect-ownership-simulation-domain-and-renderer-boundary.md)
VFX batches keep their own pass kinds (`VfxParticlePass`, deferred/forward
`DecalRenderBatch`, `VolumetricVfxBatch`). The frontend maps those kinds onto the
resolved recipe; it does not re-encode them as the five scene-material
categories. A deferred-decal batch is remapped to the recipe's forward-decal
pass when Deferred is not selected; it is not dropped and is not left targeting
a missing GBuffer.

**MSAA can change the opaque family, not only the sample count.** Deferred MSAA
requires a declared multisample GBuffer, resolve and lighting contract;
otherwise a permitted Clustered Forward+ or Forward recipe that honors the
sample count may be selected. Diagnostics report that family change as a
first-class fallback reason. The resolver must not silently drop MSAA on
Deferred.

Cluster dimensions, light/reference counts, GBuffer formats, MSAA behavior,
semantic prepasses and overflow limits are typed recipe inputs bounded by the
effective snapshot. A profile name supplies no hidden numeric budget. Exceeding a
runtime bound follows the admitted deterministic policy and diagnostics; it never
changes the active path or accesses beyond allocated storage. Required complete
coverage fails admission when it cannot be represented.

Profile preferences resolve through complete-recipe checks:

| Profile | Preferred opaque family | Permitted ordered fallback |
|---|---|---|
| Baseline | Forward | None beyond failure of unsupported required baseline work. |
| Standard | Clustered Forward+ | Forward when project/content requirements and variants permit it. |
| High | Deferred | Clustered Forward+, then Forward. |
| Ultra | Deferred plus separately gated optional features | High, then its declared lower edges; Ultra is not a fourth opaque family. |

Each candidate must satisfy all required material, transparency, shadow,
scene-color, post-process-input, sample-count and budget predicates. The result
records requested and selected path/profile, failed predicates, fallback rule,
capability/content revisions and recipe generation. Explicit requirements may
remove fallback edges. Required content is never disabled merely to reach a lower
profile.

Every path publishes the same selected typed scene-color contract plus depth and
declared optional semantic resources.
[ADR-037](../../adr/037-scene-color-and-hdr-architecture.md) owns color encoding,
working space, exposure and output transformation. A versioned deferred GBuffer schema declares
its semantics, formats, encodings, sample behavior and producer/consumer stages;
material and lighting artifacts carry that identity. Forward paths may emit
normal, velocity or other semantic prepasses when a declared downstream input
requires them, so an effect does not select deferred rendering indirectly.

Recipe changes are explicit policy requests compiled and staged as new
generations. Publication occurs at
[ADR-018](../../adr/018-command-registration-permissions-threading-and-packaged-build-policy.md)
`CommandThreadPolicy::RenderSafePoint` after all required artifacts/resources
validate; that is the same graphics-affine frame-synchronization boundary as
[ADR-027](../../adr/027-renderer-resource-identity-and-descriptors.md). In-flight
frames retain their old generation.
Allocation failure, shader miss, slow frames, cancellation, stale inputs or device
failure never cause an unreported mid-frame downgrade. An adaptive-quality system
may request a new generation, but cannot mutate the active graph in place.

The editor's current bounded forward-preview lighting remains a migration
baseline. It is not production Forward until it consumes the common recipe,
material, graph, lifetime and qualification contracts. The Null backend can test
recipe resolution and graph structure without claiming native raster or image
parity.

## Scene Color, Exposure And Output

[ADR-037](../../adr/037-scene-color-and-hdr-architecture.md) owns scene color and
HDR policy. Every production raster recipe emits linear ACEScg/AP1 scene color in
canonical `RGBA16F`; wider-range/error-sensitive reductions use declared float32
intermediates. Packed unsigned, normalized LDR and native sRGB attachments are not
silent scene-color substitutes. Input color encodings are validated and converted
at asset/media boundaries, while non-color data textures bypass color transforms.

Exposure is a finite base-2 stop offset, `exposureEv`, with
`exposureScale = exp2(exposureEv)`: `+1 EV` doubles scene RGB and `-1 EV` halves
it. Automatic exposure is a GPU reduction whose `ExposureState` is consumed by
the next submitted view; same-frame histogram readback is forbidden. One
versioned `ExposureState` is published per view and shared by effects, histories,
tone mapping and diagnostics. Pre-exposure is a reversible internal
representation whose scale/generation follows every affected resource; it never
changes canonical scene values or becomes an effect-private estimate.

The frontend resolves a versioned `ColorPipelinePlan` using a pinned ACES 2 output
transform, cooked look policy, exposure and ADR-033's output snapshot. The plan
records working/output encodings, transform identities, gamut/white point,
transfer function, reference/paper white and peak/black luminance where known,
bit depth, dithering and all relevant generations. Scene color, float32
intermediates, LUTs, histories and output images are ADR-034 reservations
admitted before realize. Publication is
`CommandThreadPolicy::RenderSafePoint` on the host-declared render-capable
thread, the same class as `RasterRecipe` replacement. Backends translate this
plan; they do not choose tone curves, gamuts, brightness or fallback.

Baseline SDR output uses sRGB/Rec.709 primaries, D65 and the sRGB transfer
function. The first HDR contract uses a Rec.2020 container, D65, BT.2100 PQ, at
least 10-bit output and explicit finite paper-white/target-peak values in nits.
HDR display admission still requires the Platform/presentation facts and surface
contract in ADR-033. HLG, scRGB and platform EDR are separate future descriptors,
not aliases for HDR10.

Scene-referred work, including ADR-011 VFX additive/translucent batches, runs
before the output transform. Display-referred UI is composed afterward in
display-linear target space at the declared reference-white scale; the ADR-015
colorblind transform is ColorPipelinePlan step 6 and covers the complete
composed image, including HUD, before final gamut containment, dithering and
transfer encoding. Creative look/grading is step 3 and is not that pass.
Encoding occurs exactly once. Scene-linear captures and display screenshots are
separate typed products with their color-pipeline metadata.

Changing exposure/color/output conventions invalidates or explicitly rescales
dependent histories. SDR/HDR or display transitions stage a new plan and publish
at `CommandThreadPolicy::RenderSafePoint`; scene shading remains ACEScg. Missing transforms, invalid
metadata, hotplug, allocation failure or stale completion retain the last good
compatible plan or produce ADR-033's typed suspended/lost state, never a hidden
curve, precision or transfer-function substitution.

## Backend Module Registry

Renderer backends are engine-internal modules with separate CMake targets,
private implementation directories, and independently packageable artifacts.
Installed product composition participates only through an exact verified
component record and negotiated first-party module ABI. Development and test
hosts may explicitly link/register a backend target, but that convenience path
does not define product discovery. Static constructors, linker-section discovery,
process-global registries, arbitrary filesystem scanning, and backend `switch`
statements inside `RenderFrontend` are forbidden.
