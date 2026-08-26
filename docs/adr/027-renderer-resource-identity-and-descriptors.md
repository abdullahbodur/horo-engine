# ADR-027: Renderer Resource Identity and Descriptors

- **Status**: Accepted
- **Date**: 2026-08-26
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

A buffer, texture, sampler, shader module, or pipeline is a directly realized
backend object. A texture view is a typed subresource reference owned by the
renderer, never a native view or GUI texture value. A render target is a logical
attachment set over texture views. A mesh is a renderer-owned composite over
vertex and index buffers plus validated layout and draw metadata.

Material bindings remain typed render data that reference compatible resources;
they are not a new source of resource identity. Framebuffer objects, descriptor
sets, argument buffers, heaps, command allocators, and other API-specific helper
objects remain backend-private implementation details unless a later accepted
decision promotes one to a backend-neutral contract.

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
with a typed capacity error.

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
raw pointer, callback, mutable runtime state, synchronization object, or
backend-selected default. Debug labels may accompany creation for diagnostics,
but do not participate in resource identity or descriptor compatibility.

Initial bytes are supplied through an explicit upload or initial-data argument;
they are not stored in the descriptor. Per-pass load/store operations, clear
values, access declarations, barriers, and transient lifetime intervals belong
to render-pass and render-graph contracts, not persistent resource descriptors.

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
- backend creation, upload, or device-loss failure.

No unsupported combination silently falls back to another format, usage,
resource class, or backend.

### Registry state and lifetime

The renderer frontend is the sole owner of logical resident identity. A registry
entry has an explicit `Pending`, `Ready`, `Retiring`, `Retired`, or `Failed`
state. Creation reserves one new generation in `Pending`; only successful native
realization and required initial upload can publish it as `Ready`. A failed entry
preserves its typed failure for the completion result, invalidates the candidate
handle, and proceeds through rollback without becoming usable.

Only `Ready` resources may enter new frame submissions. Release or replacement
atomically transitions the entry to `Retiring`, so the handle is rejected for new
work immediately. The backend retains the native object until every previously
accepted frame or upload that can reference it has completed. It then destroys
the native object on the required graphics-affine thread and lets the frontend
advance the entry to `Retired`.

Deferred retirement is bounded and drained during normal frames. Capacity
pressure reports a typed error or applies an explicit documented back-pressure
policy; it cannot trigger unsafe immediate destruction. Shutdown stops new
creation and submissions, resolves or cancels pending uploads, drains eligible
retirements while the device/context is usable, invalidates all generations,
and destroys the backend only after its required release calls. Device loss
follows the recovery rules in the ownership contract and never calls an
unavailable graphics API.

Descriptor construction and CPU preparation may occur on workers. Registry
mutation, backend realization, publication, replacement, and destruction occur
serially on the frontend's host-declared render-capable thread. Queued work owns
or copies every value and byte range needed until completion; it does not capture
shorter-lived references.

### Asset and residency identity

`AssetId` identifies authored or cooked logical content across sessions. A render
resource handle identifies exactly one resident realization in one frontend
registry lifetime. Neither can substitute for the other.

Asset-to-render resolution is explicit and may use a renderer-owned residency
key containing the asset ID, source/cooked revision, and declared rendering
variant. That key is lookup policy, not the public handle and not backend-native
identity. Procedural and transient resources may have no asset ID. Reloading or
recooking an asset creates a new resident generation; the old generation retires
normally and remains distinct until GPU completion.

Backend recreation invalidates every handle owned by the old registry. Recovery
re-resolves stable asset identities or retained CPU descriptors/data into a new
registry; it never preserves process-local handles by matching slot numbers.

### Migration

The renderer migration proceeds without parallel identity policies:

1. The frontend registry implements named owner/slot/generation handles, entry
   state, validation, rollback, and retirement.
2. Mesh upload moves CPU views into generic buffer and mesh creation and publishes
   only ready mesh handles.
3. OpenGL and Metal viewport resources move behind the same texture, view,
   render-target, buffer, and mesh contracts; native objects remain private.
4. Editor GUI integration receives an opaque editor image identity derived from
   a ready renderer texture view through the matching integration layer. It does
   not expose a renderer handle as `GLuint`, `MTLTexture*`, or `uintptr_t` to
   editor feature code.
5. Parity validation covers malformed, stale, foreign, pending, retiring,
   replacement, rollback, device-loss, and repeated-shutdown behavior.

The current `RenderTargetHandle {index, generation}` and
`RenderMeshHandle {MeshResourceId, generation}` are transitional. They migrate
to the owner/slot/generation rule rather than becoming a second accepted model.

## Consequences

Feature code can describe and reference GPU resources without knowing the
selected API, while the frontend has enough identity to reject foreign and stale
references before native execution. Asset reload, viewport resize, backend
recreation, and frames-in-flight destruction all have one generation rule.

The cost is a larger handle than a two-word slot/generation pair, explicit
registry state, deferred-retirement bookkeeping, and capability-aware validation.
Backends must translate more Horo-owned descriptor values and report failures
precisely. These costs are accepted because they protect ownership, portability,
and deterministic failure behavior.

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
must be immediate, but native destruction waits for backend completion.
