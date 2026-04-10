# Backend-Agnostic Rendering Foundation And Runtime Selection

## Intent

The engine should choose a render backend without forcing scene, editor, or gameplay code to care which graphics API is active. This document defines the backend-agnostic renderer surface that must exist before Vulkan integration and backend parity work can proceed safely.

## Why This Comes First

Vulkan backend work is a follow-up, not the starting point.

Before a second backend ships, the engine needs a stable answer for:

- how a backend is selected
- who owns backend lifetime
- which renderer features are guaranteed across all backends
- how unsupported features fail or degrade
- which layers are allowed to know backend-specific details

Without those rules, Vulkan integration would hard-code a second path beside OpenGL instead of establishing a durable renderer architecture.

## Current Foundation

The repo already has the first seam in place:

- `Renderer` owns frame and pass orchestration
- `IRenderBackend` isolates backend draw submission
- `RenderFrameConfig`, `RenderPassConfig`, and draw command structs describe backend-neutral submission data
- tests can validate submission order through a fake backend

That is a good foundation, but runtime backend selection is still mostly implicit because the active backend is treated as a single process-wide renderer implementation detail.

## Required Architectural Rules

### 1. Engine-facing rendering API stays backend-neutral

Code in editor, scene, gameplay, and shared renderer-facing systems should only depend on:

- `Renderer`
- backend-neutral render descriptors
- backend-neutral resource handles and capabilities

They should not branch on `OpenGL` vs `Vulkan` behavior.

### 2. Backend-specific state stays backend-owned

Graphics API state such as pipeline state, descriptor binding strategy, framebuffer rules, shader binding layout, and synchronization must stay inside the active backend implementation.

The higher-level engine should request intent, for example:

- begin frame
- begin opaque pass
- draw mesh with this material
- draw wireframe overlay

It should not request API mechanics.

### 3. Runtime selection happens at bootstrap, not mid-frame

Backend selection should happen once during renderer startup or application bootstrap.

Preferred order:

1. choose backend from config / CLI / platform default
2. construct backend instance
3. initialize backend-owned resources
4. inject backend into `Renderer`
5. reject backend swaps while a frame or pass is active

Runtime hot-swapping can be deferred. It should not shape the first architecture unless there is a real product requirement.

### 4. Capability checks are typed and explicit

If one backend supports a feature and another does not, the renderer contract should expose that through typed capabilities rather than hidden failure.

Examples:

- wireframe overlay support
- debug label support
- compute pass support
- timestamp / GPU profiling support
- bindless resource support

Callers should be able to ask what is supported, or the renderer should provide a safe fallback path with documented behavior.

### 5. Resource lifetime boundaries must be stable across backends

Shared engine resources such as meshes, textures, materials, and shaders need a backend-neutral ownership model even if their backend payload differs.

Rules:

- engine-owned resources expose a stable handle or object identity
- backend implementations own API-native payloads derived from those resources
- creation, invalidation, and destruction rules are explicit
- no higher-level code keeps borrowed pointers to backend-native objects

## Suggested Runtime Selection Model

### Backend identity

Introduce a typed backend id, for example:

- `OpenGL`
- `Vulkan`

Do not use free-form strings as the primary internal selector.

### Backend creation boundary

Prefer a narrow creation path such as a renderer bootstrap helper or backend factory owned by the renderer module.

Responsibilities:

- validate requested backend against platform support
- create the backend implementation
- return a failure reason if unsupported
- expose the selected backend id for diagnostics

### Configuration sources

Selection priority should be deterministic:

1. explicit CLI override
2. project or engine config
3. platform default

That keeps tests and repro steps predictable.

## Minimum Contract Needed Before Vulkan Work

The following should exist before serious Vulkan integration starts:

- a typed backend selection model
- a documented renderer bootstrap path
- capability reporting or well-defined fallback behavior
- backend-neutral resource lifetime rules
- explicit ownership for backend construction and teardown
- tests that prove the active backend can be swapped at startup only, not during active rendering

## Review Checklist

- Does the new renderer-facing API stay backend-neutral?
- Is backend choice represented by a typed model instead of stringly-typed branching?
- Is backend initialization a bootstrap concern rather than scattered global behavior?
- Are unsupported features surfaced explicitly?
- Do resource ownership and destruction rules remain valid for more than one backend?
- Would this change make Vulkan integration cleaner, or does it couple engine code to a specific API?

## Follow-up Document

Once this document's rules are implemented or at least structurally represented in code, the next branch should tackle:

- `vulkan-backend-integration-and-backend-parity.md`

That document should focus on parity strategy, backend-specific gaps, validation layers, resource translation, and rollout sequencing rather than redefining renderer ownership boundaries.
