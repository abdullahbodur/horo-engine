# Rendering Architecture

## Purpose

This document defines the backend-neutral rendering model, render extraction,
frame and pass execution, GPU resource ownership, synchronization, resize,
device failure, and headless rendering behavior.

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
- Backend loss or unavailability returns typed errors rather than leaking API
  failures through engine interfaces.

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
OpenGL / Vulkan / Null Backend
```

The frontend owns engine rendering policy. Backends own API translation,
device/context state, synchronization primitives, and concrete GPU objects.

## Backend Interface

```cpp
class IRenderBackend {
public:
    virtual Result<void> Initialize(const RenderBackendConfig&) = 0;
    virtual Result<FrameToken> BeginFrame(const FrameDescriptor&) = 0;
    virtual Result<void> Execute(const RenderExecutionPlan&) = 0;
    virtual Result<void> Present(FrameToken) = 0;
    virtual Result<void> Resize(FramebufferExtent) = 0;
    virtual void Shutdown() = 0;
};
```

Backend interfaces use Horo value types. OpenGL names, Vulkan handles, GLAD,
Volk, GLFW, and native surface types do not appear in public render API headers.

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

## Frame Contract

One frame:

1. acquires a backend frame token
2. processes completed resource work
3. uploads bounded pending data
4. builds or validates the execution plan
5. executes ordered passes
6. resolves viewport and GUI targets
7. presents or returns an offscreen result
8. retires deferred resources

Failure before presentation has a defined recovery result. A failed frame does
not leave the frontend believing resources were successfully committed.

## Render Graph

The frontend represents passes and resources as a directed acyclic graph:

```cpp
struct RenderPassDescriptor {
    RenderPassId id;
    std::vector<ResourceUse> reads;
    std::vector<ResourceUse> writes;
    RenderPassKind kind;
};
```

The graph:

- validates read-before-write and cycles
- determines pass order
- identifies transient resource lifetimes
- provides synchronization requirements to explicit APIs
- remains backend-neutral

Simple backends may execute the compiled plan serially. Scene systems do not
manually order backend commands around hidden global state.

## Resource Model

Supported resource classes include:

- buffers
- textures
- samplers
- shaders and pipelines
- framebuffers and render targets
- meshes and material bindings

Creation is described by immutable descriptors. Public handles are typed and
generation checked.

```cpp
Result<TextureHandle> CreateTexture(const TextureDescriptor&, InitialData);
Result<void> DestroyTexture(TextureHandle);
```

Asset IDs and render handles remain distinct. The asset system owns logical
asset identity; the renderer owns resident GPU representation.

## Upload And Streaming

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

The host declares the render-capable thread. Backend calls occur only there
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

Logical window size, framebuffer extent, DPI scale, and render target extent are
distinct values.

Resize is coalesced and committed at a frame boundary. Zero-sized minimized
surfaces suspend presentation without creating invalid resources.

Viewport render targets are recreated transactionally. Consumers observe either
the previous valid target or the new valid target.

## Device Or Context Failure

Backends classify failure as:

- recoverable frame failure
- surface recreation required
- device/context recreation required
- fatal unsupported or corrupted state

Recovery tears down and recreates backend-owned resources from frontend
descriptors and asset identities. Backend handles are never assumed stable
across recreation.

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
the render plan. Modal dimming and UI composition do not mutate world rendering
state.

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
- render graph cycle and invalid-resource rejection
- deterministic pass ordering
- stale resource handle rejection
- upload cancellation and generation replacement
- resize, minimize, and target recreation
- deferred destruction after frame completion
- shader reflection/material validation
- null backend contract equivalence
- backend initialization and device-loss error mapping

## Related Documents

- [Render Settings UI Reference](./render-settings.html): quality presets, render feature toggles, resolution, and GPU profiler panel.

- [Runtime Lifecycle](./runtime-lifecycle.md)
- [Scene Runtime](./scene-runtime.md)
- [Asset Pipeline](./asset-pipeline.md)
- [Built-In Scene Primitives](./built-in-scene-primitives.md)
- [Ownership And Resource Lifetime](../foundation/ownership-and-resource-lifetime.md)
- [Platform Abstraction](../foundation/platform-abstraction.md)

