# ADR-027: Renderer Resource Identity and Descriptors

- **Status**: Proposed
- **Date**: 2026-08-31
- **Supersedes**: None
- **Scope**: Backend-neutral resident renderer resources
- **Issue**: [#290](https://github.com/abdullahbodur/horo-engine/issues/290) ([RND-001.1])
- **Normative document**: [Rendering Architecture](../architecture/runtime/rendering-architecture.md)

## Context

Horo's renderer frontend already owns logical offscreen-target identities, while
the OpenGL and Metal editor viewport implementations still own native textures,
framebuffers, and mesh buffers directly. The next renderer migration must move
those objects behind one backend-neutral contract without making an asset ID, a
native API value, or a C++ pointer stand in for resident GPU identity.

The contract must reject stale and foreign handles, permit slot reuse without
aliasing an old resource, support asset reload and backend recreation, and defer
native destruction until the GPU has stopped using an object. It must also keep
resource description independent of backend selection and leave render-graph
access declarations to the render graph.

## Decision

### Resource taxonomy

The renderer defines distinct public types for these resident resource classes:

- buffers;
- textures and texture views;
- samplers;
- shader modules and pipelines;
- render targets;
- meshes.

Those classes share one identity and lifetime model, grouped only as
architectural taxonomy (not a public kind tag):

- leaf resources, realized directly by the backend: buffer, texture, sampler,
  shader module;
- derived resources: texture view;
- composite resources: pipeline, render target, mesh.

A texture view is a typed subresource reference owned by the renderer, never a
native view or GUI texture value. A render target is a logical attachment set
over texture views. A mesh is a renderer-owned composite over vertex and index
buffers plus validated layout and draw metadata. A pipeline is a composite over
shader-module handles plus immutable raster state.

Material bindings remain typed render data that reference compatible resident
resources; they are not a new source of resource identity. Scene
[`MaterialId`](../architecture/runtime/material-and-shader-model.md) is the
material-table key owned by scene conversion. It is not a resident GPU handle,
does not use owner/slot/generation, and is not `AssetId`. Bindings may name a
`MaterialId` together with resolved texture, sampler, or buffer handles.
Promoting `MaterialId` onto this registry requires a later ADR. Effect batches
in [ADR-011](011-vfx-effect-ownership-simulation-domain-and-renderer-boundary.md)
that carry `MaterialId` follow that material-table rule.

Framebuffer objects, descriptor sets, argument buffers, heaps, command
allocators, and other API-specific helper objects remain backend-private
implementation details unless a later accepted decision promotes one to a
backend-neutral contract.

Each public resource class has its own named handle and descriptor. Public code
does not use one untyped `ResourceHandle`, and feature code does not branch on a
runtime resource-kind tag. Shared implementation behind the named types is
permitted when it does not widen the public contract.

### Resident handles

Every resident resource handle has the following semantic fields:

```cpp
struct RenderResourceOwnerId {
    std::uint64_t value;
};

struct TextureHandle {
    RenderResourceOwnerId owner;
    std::uint32_t slot;
    std::uint32_t generation;
};
```

The exact private storage helper may differ, but all three values are part of
handle identity:

- `owner` identifies the creating frontend resource registry;
- `slot` identifies one registry entry;
- `generation` identifies one occupation of that slot.

Zero is invalid for every field. A valid handle is typed by its C++ handle type,
is process-local, and is meaningful only to its creating frontend. It is a
reference, not ownership: copying a handle does not extend resource lifetime.
Handles are never serialized, used as asset identity, converted to native API
values, or assumed stable across frontend shutdown or backend recreation.

`RenderFrontend::Create` assigns a non-zero, monotonically increasing 64-bit
owner ID for each registry lifetime from the renderer's process-scoped identity
authority. The authority is thread-safe and never reuses an ID in the process.
A lookup validates handle type, owner, slot bounds, live state, and generation
before reaching a backend. Owner-ID exhaustion fails registry creation rather
than reusing an ID. Releasing a slot advances its 32-bit generation before reuse.
A generation value is never allowed to wrap to an earlier value; an exhausted
slot is permanently retired and allocation continues from another slot or fails
with a typed capacity error. There is no generation compaction. A 32-bit
occupation counter per slot is accepted; the capacity path is extra slots, not
reuse of a wrapped generation.

These rules make stale, foreign, wrong-type, malformed, and retired handles
deterministically rejectable. C++ typing prevents ordinary wrong-type calls;
untrusted or decoded input must still be validated before constructing a typed
handle.

### Immutable descriptors

Creation accepts an immutable backend-neutral descriptor value. A descriptor
contains only structural creation policy. At minimum, the resource contracts
cover:

| Descriptor | Required policy |
|---|---|
| Buffer | non-zero byte size, typed usage flags, access/memory class |
| Texture | dimension, non-zero extent, format, mip count, layer count, sample count, typed usage flags |
| Texture view | source texture handle, view format, mip range, layer range, aspect |
| Sampler | min/mag/mip filters, address modes, LOD range, anisotropy and comparison policy |
| Shader module | stage, portable code-format identifier, entry point and validated interface metadata |
| Pipeline | shader-module handles, binding/layout contract, vertex layout, topology, raster, depth/stencil, blend, sample and attachment-format policy |
| Render target | attachment view handles and extent/sample compatibility |
| Mesh | vertex/index buffer handles, vertex layout, index format/count, topology and local bounds |

Enums, flags, formats, extents, ranges, and layouts are Horo-owned types. A
descriptor cannot contain an asset path, `AssetId`, native handle, native enum,
raw pointer, callback, mutable runtime state, synchronization object, recovery
policy, reconstruction blob, or backend-selected default. Debug labels may
accompany creation for diagnostics, but do not participate in resource identity
or descriptor compatibility.

Initial bytes are supplied through an explicit upload or initial-data argument;
they are not stored in the descriptor. Shader-module code is a typed
`ShaderCodeView` argument to `CreateShaderModule`, not a buffer initial-data
blob and not a field of the shader-module descriptor. Per-pass load/store
operations, clear values, access declarations, barriers, and transient lifetime
intervals belong to render-pass and render-graph contracts, not persistent
resource descriptors.

Descriptor values remain immutable after successful creation. Resize,
replacement, shader reload, or another structural change creates a new resident
generation and retires the old one; it does not mutate the descriptor associated
with an existing handle. A convenience operation such as `ResizeRenderTarget`
may coordinate this replacement, but must return or publish the new identity and
must not make a stale handle alias the replacement.

### Validation and errors

The frontend performs common structural validation before allocating a registry
entry or invoking a backend. It validates non-zero sizes, ranges, enum/flag
values, referenced handles, cross-resource compatibility, arithmetic overflow,
and limits exposed by the immutable capability snapshot. The selected backend
then validates native feasibility without weakening frontend validation.

Failures cross both boundaries as typed, actionable results. The error identifies
the operation and resource class and distinguishes at least:

- malformed or incompatible descriptor;
- unsupported format, usage, feature, or limit;
- wrong owner, stale generation, retired resource, or out-of-range slot;
- resource not ready or already retiring;
- capacity or budget exhaustion;
- thread-affinity or lifecycle-state violation;
- backend creation, upload, or device-loss failure;
- missing reconstruction source on a recoverable resource.

No unsupported combination silently falls back to another format, usage,
resource class, or backend.

### Creation result and pending publication

Frontend validation that fails before a slot is reserved returns a typed error
and no handle. After a slot is reserved, creation enqueues a bounded frontend
request and returns immediately:

```cpp
template<typename Handle>
struct ResourceCreation {
    Handle handle;
    ResourceOperationId operation;
};
```

`handle` is well-formed and typed. Until publication it is `Pending`: not
invalid, and not usable. Frame submissions, derived-resource creation, and
bind/draw that name a `Pending` handle are rejected as not ready.

`ResourceOperationId` identifies the registry completion result for that
generation. It is not a resident handle (`TextureHandle` and peers) and not
`Horo::OperationId`. The frontend may project a user-visible create into the
application `OperationStore` through the existing coordinator
([ADR-010](010-job-waiting-and-operation-store-ownership.md)); `Horo::OperationId`
is not the resource handle and is not required for unpublished GPU work.

Successful native realization and required initial upload publish the generation
as `Ready`. Failure stores the typed error on the operation, marks the generation
`Failed`, and never publishes `Ready`. `Failed` is terminal for that generation:
the handle stays permanently unusable. After the completion result is stored, the
entry proceeds `Failed` → `Retired`, the slot generation advances, and the slot
may be reused. Diagnostic tooling reads the operation result, not a long-lived
`Failed` registry occupancy.

### Dependency retention and replacement

A resident generation may retain the exact generations named by its immutable
descriptor. Public handles remain non-owning. The registry entry owns the native
realization and the internal pins:

```text
Mesh generation 41
    pins Buffer generation 12
    pins Buffer generation 7

RenderTarget generation 9
    pins TextureView generation 4
    pins TextureView generation 6

TextureView generation 4
    pins Texture generation 18

Pipeline generation 3
    pins ShaderModule generation 21
```

Releasing a referenced resource prevents new direct use of that public handle
and transitions its logical state to `Retiring`. It does not invalidate already
created dependent generations. The referenced native realization remains resident
until both of the following are true:

1. every dependent generation has dropped its internal pin;
2. every previously accepted GPU submission or upload that can reference it has
   completed.

Only then does the backend destroy the native object on the render-capable
thread and the frontend advance the entry to `Retired`.

A composite `Ready` handle therefore cannot name a retired dependency. `Release`
of a buffer used by a live mesh succeeds as logical retirement of the buffer
handle; `Draw(mesh)` remains valid until the mesh itself is released. New
`CreateMesh` / `CreateTextureView` calls that pass the retiring buffer or texture
handle are rejected.

Replacing a referenced resource never retargets existing dependent generations.
Dependents continue to reference and retain the exact generation captured by
their immutable descriptor until they are themselves replaced or retired.
Texture resize that publishes `T2` leaves `V1` and `R1` pinned to `T1`. Callers
that need the new extent create `V2` and `R2`. The same rule applies to shader
module → pipeline and buffer → mesh replacement.

### Registry state and lifetime

The renderer frontend is the sole owner of logical resident identity. A registry
entry has an explicit `Pending`, `Ready`, `Retiring`, `Retired`, or `Failed`
state.

Only `Ready` resources may enter new frame submissions. Release or replacement
of that generation atomically transitions the entry to `Retiring`, so the handle
is rejected for new direct work immediately. Native destruction still waits for
pins and GPU completion as defined above.

Deferred retirement is bounded and drained during normal frames. The capacity
and back-pressure policy is this ADR, also summarized in
[Rendering Architecture](../architecture/runtime/rendering-architecture.md):

- owner-ID or slot-pool exhaustion returns a typed capacity error and does not
  allocate;
- create or upload that exceeds the declared per-frame budget returns typed
  back-pressure or remains in the bounded producer queue; a full queue rejects
  the request rather than mutating the registry concurrently;
- retirement drain has a per-frame native-destroy budget; leftover `Retiring`
  entries wait;
- capacity pressure must not skip pin or GPU-completion checks, and must not
  destroy a native object on the wrong thread.

Shutdown stops new creation and submissions, resolves or cancels pending
uploads, drains eligible retirements while the device/context is usable,
invalidates all generations, and destroys the backend only after its required
release calls. Device loss follows the reconstruction rules below and never
calls an unavailable graphics API.

### Request ordering and thread affinity

Descriptor construction and CPU preparation may occur on workers. Registry
mutation, backend realization, publication, replacement, pin updates, and
destruction occur serially on the host-declared render-capable thread defined
by [Rendering Architecture](../architecture/runtime/rendering-architecture.md).
That thread is the graphics-affine thread. There is not a second unnamed render
thread.

[ADR-018](018-command-registration-permissions-threading-and-packaged-build-policy.md)
`CommandThreadPolicy::RenderSafePoint` dispatches debug commands onto that same
thread at its frame-synchronization boundary. `OwnerThreadNextFrame` is the
main/editor simulation safe point and does not mutate the resource registry.

Queued work owns or copies every value and byte range needed until completion;
it does not capture shorter-lived references. Serialization covers the short
registry state transitions and graphics-affine native calls, not decoding, mesh
preparation, or other CPU-heavy work. Producers submit bounded value requests,
and the frontend batches or drains them within its declared upload budget
instead of permitting concurrent registry mutation.

Resource validity is evaluated when the frontend dequeues and accepts a request,
not when a producer constructs it. If `SubmitFrame(mesh)` is accepted before
`Release(mesh)`, the frame stands. If `Release(mesh)` is accepted first, the
later submit is rejected as retiring or stale. Producer wall-clock order is not
the contract.

### Asset identity and reconstruction

`AssetId` identifies authored or cooked logical content across sessions. A render
resource handle identifies exactly one resident realization in one frontend
registry lifetime. Neither can substitute for the other.

Asset-to-render resolution is explicit and may use a renderer-owned residency
key containing the asset ID, source/cooked revision, and declared rendering
variant. That key is lookup policy, not the public handle and not backend-native
identity. Procedural and transient resources may have no asset ID. Reloading or
recooking an asset creates a new resident generation; the old generation retires
normally and remains distinct until GPU completion.

Every resident resource that participates in automatic device or backend
recovery must have a reconstruction source owned outside the backend-native
object. The source lives on a renderer-owned residency/recreation record, not
on the immutable descriptor:

```cpp
enum class ResourceRecoveryPolicy {
    RecreateFromAsset,
    RecreateFromRetainedCpuData,
    RebuildByOwner,
    NonRecoverable
};
```

- `RecreateFromAsset` re-resolves the residency key (`AssetId`, revision,
  variant) into a new registry generation;
- `RecreateFromRetainedCpuData` reapplies retained CPU bytes (texture pixels,
  buffer contents, `ShaderCodeView`) that the residency layer kept;
- `RebuildByOwner` notifies the owning subsystem, which submits a new create
  with a fresh descriptor and payload;
- `NonRecoverable` resources have no reconstruction source. After backend
  recreation their owners are notified; the renderer does not invent a
  placeholder.

Initial descriptor bytes are not an implicit reconstruction source. A procedural
texture whose pixels were discarded after upload is `NonRecoverable` unless the
residency record retained those bytes or an owner rebuild callback exists
outside the descriptor. Shader modules that must survive device loss retain
cooked bytecode or portable source on the residency record, not only the
shader-module descriptor.

Backend recreation invalidates every handle owned by the old registry. Recovery
allocates a new registry and new owner ID, then rebuilds according to each
record's policy. It never preserves process-local handles by matching slot
numbers.

### Migration

The renderer migration proceeds without parallel identity policies:

1. The frontend registry implements named owner/slot/generation handles, entry
   state, validation, rollback, pins, and retirement.
2. Mesh upload moves CPU views into generic buffer and mesh creation and publishes
   only ready mesh handles.
3. OpenGL and Metal viewport resources move behind the same texture, view,
   render-target, buffer, and mesh contracts; native objects remain private.
4. Editor GUI integration receives an opaque editor image identity derived from
   a ready renderer texture view through the matching integration layer. It does
   not expose a renderer handle as `GLuint`, `MTLTexture*`, or `uintptr_t` to
   editor feature code.
5. Parity validation covers malformed, stale, foreign, pending, retiring,
   pinned-dependency release, non-retargeting replacement, reconstruction-source
   recovery, rollback, device-loss, and repeated-shutdown behavior.

The current `RenderTargetHandle {index, generation}` and
`RenderMeshHandle {MeshResourceId, generation}` are transitional. They migrate
to the owner/slot/generation rule rather than becoming a second accepted model.

## Consequences

Feature code can describe and reference GPU resources without knowing the
selected API, while the frontend has enough identity to reject foreign and stale
references before native execution. Asset reload, viewport resize, backend
recreation, and frames-in-flight destruction all have one generation rule.
Composite resources keep their captured dependency generations until they retire.

The cost is a larger handle than a two-word slot/generation pair, explicit
registry state, pin counts, deferred-retirement bookkeeping, residency records
for recovery, and capability-aware validation. Backends must translate more
Horo-owned descriptor values and report failures precisely. These costs are
accepted because they protect ownership, portability, and deterministic failure
behavior.

This decision does not define render-graph scheduling, asset cooking formats,
shader language selection, material authoring, bindless indexing, streaming
budgets, or GUI backend internals. Later decisions may extend descriptors, but
cannot expose native types or change the identity and lifetime invariants here
without superseding this ADR.

## Rejected Alternatives

### Native API handles in public contracts

Rejected because OpenGL names, Metal objects, and future API handles have
different validity and ownership rules, leak backend dependencies, and cannot
provide cross-backend stale/foreign validation.

### One untyped resource handle with a runtime kind

Rejected because it moves wrong-resource-class errors from compilation to
runtime and encourages generic APIs whose valid operations depend on hidden
tags. Named handles preserve narrow contracts.

### Slot and generation without an owner domain

Rejected because independently created frontends can issue the same pair. A
foreign handle could then alias a valid local resource instead of failing.

### Asset ID as resident GPU identity

Rejected because one asset may have multiple revisions, variants, subresources,
or simultaneous old/new GPU realizations, while procedural and transient
resources have no asset identity.

### Pointer, `shared_ptr`, or native object ownership in handles

Rejected because pointer identity does not reject reuse, shared ownership hides
the renderer's retirement point, and ordinary CPU destruction cannot prove GPU
completion or graphics affinity.

### Descriptor or content hash as the handle

Rejected because equal descriptions or bytes may require distinct lifetimes and
because collision, mutable upload state, and backend recreation still require a
registry generation. Hashes may be cache keys, not resident identity.

### In-place descriptor mutation

Rejected because concurrent frames could observe different structure through
one identity and because stale submissions would be indistinguishable from work
targeting the replacement.

### Immediate native destruction on release

Rejected because submitted GPU work can outlive the CPU call. Logical retirement
must be immediate, but native destruction waits for pins and backend completion.

### Cascading invalidation of dependents on dependency release

Rejected because a still-`Ready` composite handle would then name a retired
dependency, which is the stale-use class this identity model exists to reject.
Dependents retain the captured generations until they themselves retire.

### Automatic retarget of dependents onto replacement generations

Rejected because descriptors are immutable snapshots of exact generations.
Retargeting in place would make one handle observe two structures.

### Reconstruction source stored on the descriptor

Rejected because descriptors forbid callbacks, asset IDs, and retained blobs.
Recovery policy belongs on the residency/recreation record.
