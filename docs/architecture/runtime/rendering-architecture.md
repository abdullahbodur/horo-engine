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

[ADR-029](../../adr/029-opengl-compatibility-profile-and-platform-policy.md) owns
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

## Backend Module Registry

Renderer backends are engine-internal modules with separate CMake targets,
private implementation directories, and independently packageable artifacts.
Installed product composition participates only through an exact verified
component record and negotiated first-party module ABI. Development and test
hosts may explicitly link/register a backend target, but that convenience path
does not define product discovery. Static constructors, linker-section discovery,
process-global registries, arbitrary filesystem scanning, and backend `switch`
statements inside `RenderFrontend` are forbidden.

```cpp
class IRenderBackendProvider {
public:
    virtual ~IRenderBackendProvider() = default;

    virtual Result<std::unique_ptr<IRenderBackend>>
    Create() const = 0;
};

struct RenderBackendDescriptor {
    RenderBackendId id;
    std::string displayName;
    std::unique_ptr<IRenderBackendProvider> provider;
};

class RenderBackendRegistry {
public:
    Result<void> Register(RenderBackendDescriptor descriptor);

    Result<void> Seal() noexcept;

    Result<std::unique_ptr<IRenderBackend>>
    Create(const RenderBackendId& id) const;
};

Result<void>
RegisterOpenGLRenderBackend(RenderBackendRegistry& registry,
                            IOpenGLPresentationPort& presentationPort);
Result<void>
RegisterNullRenderBackend(RenderBackendRegistry& registry);
```

The registry owns move-only descriptors and their providers. A concrete provider
may capture backend-specific platform services without exposing native handles
through the common Render API. It rejects duplicate or invalid IDs before renderer
selection. Registration and provider invocation do not create devices,
windows, swapchains, contexts, or worker threads; those side effects occur only
in the selected backend's `IRenderBackend::Initialize` path.

Provider registration and creation are serialized on the composition thread;
`Create() const` does not imply concurrent access. The registry may invoke a
provider zero or more times, and every invocation returns an independent inert
backend. Borrowed provider dependencies outlive the provider; any dependency
borrowed by a returned backend outlives that backend. `Register` consumes its
move-only descriptor on entry, so a rejected provider is destroyed before the
failed registration returns rather than being retained by the registry.

Providers return typed results, but allocation or module defects may still throw.
The registry is the exception boundary: it preserves returned failures, translates
thrown exceptions into `render.registry.provider_exception`, and rejects successful
results that contain a null backend pointer.

The host's build profile controls which renderer artifacts are produced, not
which components every installed editor must contain. Product runtime selection
uses only the installed, verified, ABI-compatible, successfully probed set. This
keeps the editor core and headless/CLI installations free of unused GPU
dependencies.

```text
Resolve component record
    -> verify manifest/signature/ABI/probe state
    -> load exact verified module path
    -> negotiate private renderer module ABI
    -> adapt module function table into IRenderBackendProvider
    -> register provider in RenderBackendRegistry
    -> seal registry
    -> create RenderFrontend
```

The dynamic module loader and component registry are composition/application
services, not part of `RenderFrontend`. Development and unit-test builds may use
direct `RegisterOpenGLRenderBackend`/`RegisterNullRenderBackend` calls to avoid
packaging overhead while exercising the same in-process lifecycle contract.

First-party renderer modules cross a private, versioned C ABI with opaque
handles, host allocator/callback tables, explicit ownership, and strict unload
policy. The host adapter owns conversion into internal C++ renderer contracts.
This ABI is not the external extension/plugin ABI and does not establish an
unsupported third-party renderer marketplace. The package and ABI rules are
defined by
[Renderer Module Package Manifest](renderer-module-package-manifest.md).

Registry descriptors report identity and provider availability only. Product
component state is authoritative for installed/verified/probed status. Dynamic
GPU capabilities, driver versions, limits, and optional features are
authoritative only after the selected backend initializes. Selection UI may show
a known or installed module without claiming that device creation will succeed.

Interactive hosts additionally require side-effect-free module information and
window requirements before creating a presentation-capable window. That
pre-window contract and its required startup ordering are defined in
[Render Backend Parity Contract](render-backend-parity-contract.md); the current
OpenGL-first editor bootstrap is explicitly transitional.

## Backend Capabilities

Each backend exposes a value-type capability snapshot after initialization:

```cpp
struct RenderBackendCapabilities {
    RenderBackendId backend;
    bool presentsToWindow;
    bool supportsOffscreenTargets;
    bool supportsTimestampQueries;
    bool supportsCompute;
    bool supportsBindlessResources;
    bool supportsRayTracing;
};
```

Typed limits and an extensible feature set are future capability-contract
additions. They must not be consumed until their public render API types and
backend validation rules are implemented together.

The frontend and feature systems query capabilities through render API values,
not by downcasting or including backend headers. Unsupported optional features
produce typed validation errors or disable declared optional passes before frame
execution. Required project features fail during project/runtime validation, not
mid-draw.

Capability reporting is enforceable behavior, not advisory metadata. A backend
whose snapshot reports a feature as unsupported must reject an execution plan
that requires that feature, even if earlier frontend validation was bypassed.

Backend capability snapshots are immutable for one initialized backend
instance. Device recreation may produce a new snapshot; users of capabilities
observe that through the frontend's typed recreation result.

## Backend Implementation Boundary

Each concrete backend owns its API dependencies, context/device objects,
surface/swapchain objects, synchronization primitives, shader module objects,
pipeline caches, and backend-native diagnostics.

Rules:

- Backend source targets may include native API headers.
- Public render API headers may not include OpenGL, Vulkan, Metal, D3D12, GLAD,
  Volk, SDL3, Win32, Cocoa, X11, Wayland, or other native surface types.
- Backends implement `IRenderBackend` and backend-neutral resource contracts;
  they do not depend on `RenderFrontend`.
- Backend modules register providers explicitly with `RenderBackendRegistry`;
  registration performs no GPU or platform side effects.
- The frontend compiles render plans and owns render policy. Backends translate
  already-validated plans into API calls.
- A backend cannot mutate scene, editor, asset, gameplay, MCP, or application
  service state.
- External or plugin-provided renderer backends are a future extension point and
  are not part of the initial stable ABI.

## Backend Interface

```cpp
class IRenderBackend {
public:
    virtual Result<void> Initialize(const RenderBackendConfig&) = 0;
    virtual const RenderBackendCapabilities& Capabilities() const noexcept = 0;
    virtual Result<FrameToken> BeginFrame(const FrameDescriptor&) = 0;
    virtual Result<void> Execute(const RenderExecutionPlan&) = 0;
    virtual Result<void> Present(FrameToken) = 0;
    virtual void AbortFrame(FrameToken) noexcept = 0;
    virtual void AbortActiveFrame() noexcept = 0;
    virtual Result<void> Resize(FramebufferExtent) = 0;
    virtual void Shutdown() noexcept = 0;
};
```

`Shutdown` is explicit and idempotent for deterministic teardown. Backend
destructors must also release any remaining resources safely so an early-return
or failed composition path cannot leak native objects.

`RenderFrontend::BeginFrame` returns a move-only `RenderFrameScope` that owns the
matching token until successful presentation or abort. Its destructor aborts an
unpresented frame. Scope moves transfer abort ownership; moved-from scopes are
inert. Callers may explicitly `Cancel` a frame without relying on lexical scope
destruction. The frontend tracks its one outstanding scope, rejects a second begin,
and aborts plus invalidates that scope before backend shutdown if frontend
destruction occurs first. `RenderFrontend::SubmitFrame` is a convenience wrapper
over the same begin/execute/present path; it does not duplicate recovery logic.

Frontend methods, frame-scope methods, scope moves, and both destructors execute
serially on the same host-declared render-capable thread. Cross-thread scope
transfer and concurrent frontend/scope access are unsupported.

A begin failure or exception invokes token-independent
`AbortActiveFrame` because no token may have been returned. An expected
execute/present failure aborts the acquired token before the typed error is
returned. A successful begin result containing an invalid token is rejected as
`render.frontend.invalid_frame_token` and uses the same token-independent
cleanup. Backend exceptions are contained as `render.frontend.frame_exception`.
Initialization exceptions are contained as
`render.frontend.initialize_exception` and partial backend state is shut down.
Resize results also cross the frontend boundary unchanged; backend exceptions
are translated to `render.frontend.resize_exception`.

Backend interfaces use Horo value types. OpenGL names, Vulkan handles, GLAD,
Volk, SDL3, and native surface types do not appear in public render API headers.
Virtual dispatch occurs only at coarse frame, execution-plan, resource, and
lifecycle boundaries. Draw-item iteration and API command encoding remain inside
the selected backend, avoiding virtual dispatch per object or primitive.

All scene transforms, view/projection matrices, clip-depth conversion, bounds,
and viewport rays follow the shared [Scene Math](../foundation/scene-math.md)
contract. Backends may convert only the explicitly selected clip-depth range;
they must not introduce a second coordinate or matrix convention.

The typed pass contract binds a `PrimaryOutputAttachment` or a
`StaticMeshPassDescriptor` to a graphics pass. Static-mesh work contains only a
generation-safe target, extent, generic camera, immutable mesh views, transforms,
material bindings, and presentation tint. `RenderFrontend` validates target
generation and extent and dispatches the attached executor from
`RenderFrameScope::Execute`; editor code does not issue a separate viewport render
call. Primary output carries backend-neutral load/store operations and a finite
linear RGBA clear value. Copy and compute passes cannot bind the primary output. The implicit
primary output contains no native surface identity and is intentionally limited
to the first single-window slice; typed output handles replace it before
multi-window presentation.

`HoroEngine::RenderOpenGL` is an SDL- and ImGui-free module. Its provider borrows
an `IOpenGLPresentationPort`, remains inert until backend initialization, and the
backend controls context creation, make-current, present-mode configuration,
buffer swap, and context destruction through that port. The platform window
remains host-owned. The editor composition root registers the module, selects it
through `RenderFrontend`, and performs primary-output clear and presentation only
through the staged frame scope.

The current editor viewport uses frontend-owned logical target identities with
editor-private OpenGL and Metal texture bridges.
Render extraction resolves versioned primitive descriptors through
`PrimitiveMeshCache`, emits a deduplicated table of immutable generic mesh
resource views, and emits instances containing only mesh resource identity,
transform, bounds, material, and presentation state. Each executor owns its native
vertex/index buffer registry and an offscreen color/depth target, and exposes
only a GUI-bridge texture identity to `ViewportPanel`. The GUI identity remains
app-private and is not a public render texture handle. Render extraction and
static-mesh submission are backend-neutral. OpenGL and Metal editor integrations must
satisfy the same lifecycle and viewport parity suite defined by
[Render Backend Parity Contract](render-backend-parity-contract.md).
Selected editor instances carry backend-neutral tint and strength values; both
executors apply the same semantics without querying editor selection state directly.
The temporary typed `core.materials.default` resolution uses the shared neutral
viewport material and a bounded forward-preview lighting path. Render extraction
emits at most sixteen immutable world-space Directional, Point, or Spot light
values; both editor executors consume the same color, intensity, range, cone, and
normal-transform semantics. The first non-zero Directional light receives a
scene-bounds-fitted, texel-snapped 2048² orthographic shadow view with 3×3 PCF;
that selection and matrix are backend-neutral while native shadow resources remain
private to each executor. This is a viewport-quality baseline, not the general
material/PBR, clustered-lighting, cascaded-shadow, or punctual-shadow system.
The selected Light's influence geometry is also backend-neutral: Directional,
Point, and Spot line lists are built from the same immutable render snapshot,
then depth-tested without writing scene depth in each executor. Always-visible,
constant-pixel Light markers are composed and picked by the editor GUI layer.

Changing a project's renderer is a restart operation, not live migration of GPU
objects. The project setting records a canonical backend identifier; the host
closes project/runtime renderer state, destroys the frontend and matching GUI
adapter, recreates any backend-specific window attachment, and reopens the project
with a newly selected frontend. The current editor exposes the same selection
policy through `--renderer`; persistent project-setting and reopen orchestration
remain pending and must not be represented as implemented UI behavior.

## Render Snapshot

The scene runtime produces frame-owned render data:

```cpp
struct RenderWorldSnapshot {
    CameraData camera;
    std::span<const RenderInstance> opaque;
    std::span<const RenderInstance> transparent;
    std::span<const LightData> lights;
    DebugDrawSnapshot debug;
    SceneRevision sceneRevision;
};
```

Snapshots contain handles and immutable values. They do not contain pointers to
component pools, editor widgets, or backend objects.

Multiple views, including game, scene viewport, thumbnails, and previews, use
separate `RenderView` descriptors over compatible snapshots.

The [VFX contract](./vfx-and-particles-architecture.md) extends the snapshot with
bounded immutable GPU simulation work, CPU/GPU particle sources, decals and volume
batches. VfxRenderExtractor never submits graph passes or writes mapped GPU buffers.
RenderFrontend admits/uploads CPU frame slices, schedules VFX Compute once per
scene/emitter step, then per-view Sort/Cull and dependent rendering passes. Multiple
views or reusing a snapshot cannot advance the same simulation step twice. Native
encoding, graph resource barriers and fence-based deferred retirement remain renderer
responsibilities. Retained snapshot and GPU leases may outlive logical scene teardown
but cannot reference destroyed scene storage or publish into a new incarnation.

## XR Views And External Presentation Targets

XR supplies a bounded runtime-driven set of view descriptors and
generation-scoped runtime-owned image identities. Renderer does not assume that
an XR frame contains exactly two eyes, and public render contracts do not expose
fixed two-element matrix or target arrays.

The first production implementation may explicitly admit primary stereo only.
The contract still preserves view role, ordering, extent, projection, target,
and configuration identity so later quad-view or foveated-inset execution does
not require a public API migration. An unsupported view configuration is
rejected before resource acquisition and is never silently truncated.

Runtime-owned images enter the graph as typed external resources with declared:

- format, extent, array/view index, and permitted uses;
- color/depth role and composition requirements;
- acquire/wait/release ownership;
- synchronization and frames-in-flight lifetime;
- session, swapchain, image, and configuration generations.

Dynamic resolution, fixed or gaze-driven foveation, variable-rate shading,
density maps, depth submission, and motion inputs are renderer capabilities.
They are negotiated before frame execution. Foveation is not modeled as a
generic post-process, and Horo does not implement runtime-owned asynchronous
reprojection. Ordinary projection-layer rendering is the explicit fallback
when optional features are unavailable.

See [XR Architecture](./vr-ar-architecture.md) for session, view, capability,
privacy, and qualification ownership.

## Frame Contract

One frame:

1. acquires a backend frame token
2. processes completed resource work
3. uploads bounded pending data
4. builds or validates the execution plan
5. executes ordered passes
6. resolves viewport and GUI targets
7. permits explicitly declared composition work while the frame scope remains active
8. presents or returns an offscreen result
9. retires deferred resources

Failure before presentation has a defined recovery result. A failed frame does
not leave the frontend believing resources were successfully committed.

## Render Graph

The frontend represents passes and resources as a directed acyclic graph:

```cpp
struct RenderPassDescriptor {
    RenderPassId id;
    RenderPassKind kind;
};
```

The current foundation slice carries minimal compiled pass metadata. Resource
read/write declarations belong to the future render-graph authoring descriptor
and will be added only with typed `ResourceUse`, dependency validation, and
lifetime compilation.

The graph:

- validates read-before-write and cycles
- determines pass order
- identifies transient resource lifetimes
- provides synchronization requirements to explicit APIs
- remains backend-neutral

Simple backends may execute the compiled plan serially. Scene systems do not
manually order backend commands around hidden global state.

## Resource Model

The canonical identity, descriptor, validation, and lifetime policy is
[ADR-027: Renderer Resource Identity and Descriptors](../../adr/027-renderer-resource-identity-and-descriptors.md).
Supported resident resource classes are buffers, textures and texture views,
samplers, shader modules and pipelines, render targets, and meshes. Leaf,
derived, and composite classes share that model. Material bindings are typed
render data over those resources; scene `MaterialId` is a material-table key,
not a resident GPU handle. Backend framebuffer and binding-allocation objects
remain private.

Creation is described by immutable Horo-owned descriptors. Every public resource
class has a distinct typed handle whose identity contains the creating frontend
owner, registry slot, and non-wrapping generation. Handles are process-local
references rather than ownership, native values, or persistent identities.
Creation that reserves a slot returns a pending handle plus a
`ResourceOperationId`; only `Ready` generations may be submitted.

```cpp
ResourceCreation<TextureHandle> CreateTexture(const TextureDescriptor&, InitialData);
Result<void> DestroyTexture(TextureHandle);
```

The frontend validates structure, owner, generation, registry state, referenced
resources, and effective capabilities before native execution. A resident
generation retains the exact dependency generations named by its descriptor.
Release prevents new direct use of that handle immediately. Native destruction
waits until dependents drop their pins and prior GPU work completes on the
host-declared render-capable thread. Replacement never retargets existing
dependents. Handle state is validated when the frontend accepts a queued
request, not when a producer constructs it.

Asset IDs and render handles remain distinct. The asset system owns persistent
logical asset identity; the renderer owns one resident realization. Reload,
resize, replacement, and backend recreation publish new generations instead of
mutating an existing descriptor or preserving a process-local handle. Automatic
recovery requires a reconstruction source on the residency record
(`RecreateFromAsset`, `RecreateFromRetainedCpuData`, `RebuildByOwner`, or
`NonRecoverable`).

## Upload And Streaming

The canonical allocator, budget, residency and pressure policy is
[ADR-034: GPU Memory and Residency Ownership](../../adr/034-gpu-memory-and-residency-ownership.md).
The host supplies finite envelopes; the frontend owns reservation accounting and
readiness, while the backend owns native requirements, heaps and release.
Charge backing capacity, copies and replacement overlap once, including retired
resources until release acknowledgement. Heap slack is already charged capacity,
not memory returned merely by destroying a suballocated resource.

Streamed resources consume the World Streaming aggregate reservation through an
explicit adapter; its GPU claim and the renderer record project the same charge.
The renderer never selects cells to evict or discards activation-critical resources
under an Active cell. Non-streamed, GUI, viewport and presentation resources have
explicit owner scopes within the same host envelope. Native budget observations
remain estimates, not guaranteed allocation capacity or permission to exceed caps.

CPU asset preparation occurs on workers. GPU upload is queued to the
render-capable thread with:

- a memory budget
- per-frame upload budget
- cancellation before submission
- source asset and generation identity
- completion result

A late upload for an evicted, reloaded, or closed-project asset is discarded by
generation check.

## Shader And Pipeline Contract

Shaders are cooked by the asset pipeline. Runtime loading validates:

- format and version
- backend and feature compatibility
- reflected resource layout
- material binding compatibility
- required vertex attributes

Compilation errors preserve source mapping and structured diagnostics. Runtime
fallback shaders are explicit product policy, not a silent default.

## Material Binding

Material data is backend-neutral and refers to semantic parameters and asset
handles. Binding layouts are derived from validated shader reflection.

Missing required parameters fail validation. Optional parameters use declared
defaults; arbitrary string lookup in the draw loop is avoided.

## Threading And Synchronization

The host declares the render-capable thread. That thread is the graphics-affine
thread for registry mutation and native create/destroy. ADR-018
`CommandThreadPolicy::RenderSafePoint` runs on it at the frame-synchronization
boundary; it is not a second render thread. Backend calls occur only there
unless a backend method explicitly documents thread safety.

Worker threads may:

- decode images
- build mesh data
- compile backend-independent render plans
- prepare upload payloads

They do not create or delete graphics objects directly.

Deferred destruction waits for the relevant frame fence or backend-equivalent
completion. The renderer owns that queue.

## Resize And Surface Changes

[ADR-033](../../adr/033-presentation-and-display-ownership.md) owns presentation
and display boundaries. Platform owns native windows/display snapshots; the
matching private adapter owns the surface attachment; the backend owns graphics
presentation resources. Host policy supplies intent, and the frontend publishes
the resolved output contract after checking platform, surface, and effective
device support. Requested settings and active output are separate values.

Publish revisioned logical output candidates with owner-thread commands before
layout/extraction; commit active output only after realizing the same candidate
before native frame acquisition. Output-dependent extraction retains that
revision. A failed, pending, or mismatched realization skips its output instead
of executing a new-size plan on the previous surface.

Logical window size, framebuffer extent, DPI scale, and render target extent are
distinct values.

Resize is coalesced and committed at a frame boundary. The frontend rejects
resize while a frame scope is active without aborting that frame. Zero-sized
minimized surfaces suspend presentation without creating invalid resources.

Viewport render targets are recreated transactionally. Consumers observe either
the previous valid target or the new valid target.

Native surface reconfiguration has a stricter physical limitation: an OS-invalid
old surface cannot be promised as a rollback target. Publish a new configuration
only after success; retain the old one only if still usable, otherwise report
Suspended/Lost output. Coalesce revisioned platform events, commit native changes
before backend-frame acquisition, and retire prior surface generations safely.
Native window size, offscreen editor viewport resolution, and scene dynamic
resolution must not share a resize authority.

## Device Or Context Failure

Backends classify failure as:

- recoverable frame failure
- surface recreation required
- device/context recreation required
- fatal unsupported or corrupted state

Recovery tears down the old registry and recreates resources from each
residency record's reconstruction source (asset identity, retained CPU data, or
owner rebuild). Resources without a source are `NonRecoverable` and their
owners are notified. Backend handles are never assumed stable across
recreation.

## Null Renderer

The null backend:

- validates frame, pass, and resource contracts
- returns deterministic handles
- performs no GPU or window work
- supports headless application and scene tests
- records bounded command summaries when requested

It must not silently skip validation that a real backend relies on.

## Editor Integration

Editor viewports request render views and targets through renderer frontend
capabilities. Tabs and modals do not own backend resources.

The GUI backend consumes a declared final target or submits its own pass through
the render plan. A transitional editor-private graphics bridge may render after
scope execution and before presentation while a backend migration is in progress;
native GUI or graphics types do not enter RenderApi. Modal dimming and UI
composition do not mutate world rendering state.

## Metrics

The renderer exposes bounded metrics:

- CPU render extraction and submission time
- GPU frame and pass time when supported
- draw and dispatch counts
- resident and pending resource bytes
- upload bytes and queue depth
- shader/pipeline cache hit rate
- deferred deletion depth

Metrics follow [Observability Metrics And Profiling](../observability/observability-performance.md).

## Testing

Required tests cover:

- backend API header isolation
- duplicate/invalid backend registration rejection
- sealed-registry mutation rejection and deterministic enumeration
- unknown, not-installed, incompatible, and unavailable backend selection
  returning distinct typed errors before window creation
- verified-path module loading and private ABI negotiation
- probe failure/crash isolation and no-renderer repair routing
- provider invocation remaining inert until backend initialization
- provider-returned failures, thrown exceptions, and null successful values being
  contained at the registry boundary
- frontend initialization ownership, deterministic backend shutdown, and
  initialization exception containment
- frontend frame submission aborting owned frame state after expected failures
  or backend exceptions
- frontend rejection and cleanup of invalid successful frame tokens
- frontend resize propagation and exception containment
- capability-incompatible and malformed execution plans being rejected
- reported/implemented/driver-adjusted support separation and profile resolution
  following ADR-028, including stale snapshots and missing required fallback variants
- installed editor composition loading only the selected verified component while
  development/headless profiles may register explicit static/null modules
- render graph cycle and invalid-resource rejection
- deterministic pass ordering
- stale resource handle rejection
- upload cancellation and generation replacement
- resize, minimize, and target recreation
- deferred destruction after frame completion
- shader reflection/material validation
- null backend contract equivalence
- backend initialization and device-loss error mapping
- XR external-image generation, acquire/import/release ordering, and loss
- one-view, primary-stereo, and explicitly unsupported greater-than-two view
  configurations
- XR dynamic-resolution/foveation capability fallback and history invalidation

## Related Documents

- [Coordinate Precision And Origin Rebasing](./coordinate-precision-and-origin-rebasing.md)
- [ADR-026: Large-World Precision and Floating Origin Strategy](../../adr/026-large-world-precision-and-floating-origin-strategy.md)
- [Render Settings UI Reference](./render-settings.html): quality presets, render feature toggles, resolution, and GPU profiler panel.
- [Render Backend Parity Contract](./render-backend-parity-contract.md)
- [Renderer Distribution And Availability](./renderer-distribution-and-availability.md)
- [Renderer Module Package Manifest](./renderer-module-package-manifest.md)
- [Runtime Lifecycle](./runtime-lifecycle.md)
- [Scene Runtime](./scene-runtime.md)
- [Asset Pipeline](./asset-pipeline.md)
- [Built-In Scene Primitives](./built-in-scene-primitives.md)
- [Ownership And Resource Lifetime](../foundation/ownership-and-resource-lifetime.md)
- [Platform Abstraction](../foundation/platform-abstraction.md)
- [XR Architecture](./vr-ar-architecture.md)
