# Renderer Foundation

## Intent

The renderer foundation exists so higher-level engine code can describe frame work without inheriting the shape of the OpenGL backend. The goal is a stable submission contract that later rendering features can build on without bypassing ownership or pass boundaries.

## Engine-Facing Seam

The renderer-facing contract is:

- `Renderer` as the orchestration facade used by scene and editor code
- `RenderFrameConfig` as the per-frame envelope for light and frame metadata
- `RenderPassConfig` as the named per-pass contract
- `IRenderBackend` as the backend seam that receives translated draw commands

Callers should prefer:

1. `Renderer::BeginFrame(...)`
2. `Renderer::BeginPass(...)`
3. `Renderer::Submit(...)` / `Renderer::SubmitSkinned(...)` / `Renderer::SubmitWireframe(...)`
4. `Renderer::EndPass()`
5. `Renderer::EndFrame()`

`Renderer::BeginScene/EndScene` remain compatibility helpers only while older callers are migrated.

## Backend Isolation

- `OpenGLRenderBackend` owns OpenGL-specific draw submission and program binding behavior.
- Higher-level code such as `scene/systems/RenderSystem.cpp` and `renderer/Renderer.cpp` should not reach for OpenGL program ids or backend-specific upload behavior directly.
- Backend-specific code should stay behind the `IRenderBackend` seam unless a renderer-internal helper is clearly documented as backend-owned.

## Frame and Pass Model

- A frame is the outer renderer-owned unit for lights, stats, and transient backend state.
- A pass is the named submission scope inside a frame, such as `OpaqueScene` or `WireframeOverlay`.
- Scene systems submit into the active pass; they do not create backend resources or decide backend implementation details.
- Editor overlays may open a temporary frame only when no outer frame is active, so the explicit pass contract works in both embedded and self-contained render paths.

## Resource Ownership

- `Material` stores shared handles to `Shader` and `Texture` resources.
- Renderer backends borrow those handles during submission; they do not take ownership.
- `DebugDraw` and `DebugHUD` own their private shader resources through `std::unique_ptr`.
- Raw pointers inside draw commands are borrowed references for the duration of a single submission call, not long-lived ownership.

## Review Checklist

- Does higher-level code submit through frame/pass descriptors rather than backend-specific APIs?
- Are OpenGL-only details isolated to the backend implementation or renderer-internal helpers?
- Is each shader/material/texture relationship clearly owning, shared, or borrowed?
- Can fake backend tests validate pass order and submission translation without a GL context?
