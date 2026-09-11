# ADR-175: Render Resource State and Barrier Model

- **Status**: Proposed
- **Date**: 2026-09-08
- **Supersedes**: None
- **Scope**: Backend-neutral resource access state, transition synthesis, subresource tracking and queue ownership transfer
- **Issue**: [RND-003.5](https://github.com/HoroCore/horo-engine/issues/300)
- **Jira**: [HORO-300](https://horo-engine.atlassian.net/browse/HORO-300)
- **Related**: [ADR-027](027-renderer-resource-identity-and-descriptors.md), [ADR-173](173-render-queue-submission-and-fence-contract.md)
- **Normative document**: [Rendering Architecture](../architecture/runtime/rendering-architecture.md)

## Context

Render-graph passes already declare semantic reads and writes, resource class,
and queue role. The renderer does not yet define the logical state carried by an
access, the exact hazards that require ordering, the granularity of transition
tracking, or how ownership moves between effective queues. Without one frontend
model, explicit APIs could expose native barriers while OpenGL inferred different
ordering, and each backend could silently invent policy.

This decision is separate from ADR-027's resident-resource lifecycle. A
`Pending`, `Ready`, `Retiring`, `Retired`, or `Failed` resource generation says
whether a resident handle may be used. It does not say how a ready buffer or
texture is accessed by graph work. This decision also does not define the graph
compiler implementation, native barrier enums, queue selection, or resource
allocation.

## Decision

### 1. State is a Horo-owned access contract

Every compiled resource use carries a logical state composed from:

- access intent: no access, read, write, or read/write;
- operation class: sampled, storage, color attachment, depth/stencil attachment,
  copy source, copy destination, vertex, index, indirect, uniform, or present;
- pipeline scope: the finite Horo stage set that may perform the access;
- texture layout intent where layout is semantically relevant;
- the effective `RenderQueueId` that owns the use.

The eventual C++ representation uses typed enums and validated flag sets. It
contains no OpenGL bit, Vulkan stage/access/layout value, Metal resource-usage
value, D3D12 state, native queue identifier, or backend pointer. Unsupported
combinations fail admission; a backend cannot reinterpret them.

The access-state model and ADR-027's `RenderResourceState` residency lifecycle
must use distinct types and names. Converting between them is meaningless and is
not provided.

### 2. Initial and final states are explicit at graph boundaries

Transient resources begin undefined with discarded contents. Their first use
must write every range later read, unless that use explicitly declares a legal
discard or clear operation. Reading undefined transient contents is rejected.

Imported persistent, history, presentation, or external resources declare an
exact initial state for the imported resident generation. “Unknown”, “general”,
or backend-default state is not accepted as evidence. An import also declares
the final state required when the graph releases observation or ownership. A
presentable output ends in the typed present state; an exported resource ends in
its declared consumer state.

Initial and final state evidence is scoped to the exact ADR-027 owner, slot, and
generation. Replacement, frontend recreation, device loss, or external lease
generation change invalidates prior evidence. The next graph imports the new
generation with a fresh declared state.

### 3. Hazards and ordering are synthesized deterministically

For overlapping ranges, the compiler identifies:

- read after write (RAW), making produced data visible before the read;
- write after read (WAR), preventing overwrite before prior readers finish;
- write after write (WAW), ordering competing producers.

Compatible read/read uses may share state without a barrier when their operation,
scope, layout, and effective queue contracts are mutually compatible. A read
before any dominating write or declared imported content is rejected. Declared
graph dependencies may add ordering, but do not excuse missing resource-state
validation.

Compilation walks deterministic pass and resource declaration order. Identical
graph, capability, queue-topology, and imported-state generations emit the same
normalized transition sequence. Pointer order, native identity, hash iteration,
and driver observations cannot change the result. Adjacent transitions may be
coalesced only when all access, visibility, queue, and diagnostic boundaries are
preserved.

### 4. Tracking is by checked subresource range

Textures use explicit mip, array-layer, and aspect ranges. Buffers use byte
offset and byte length. Every range is non-empty, within the immutable resource
descriptor, and validated with checked arithmetic before overlap tests. Texture
aspects must be supported by the format and use.

The compiler may promote tracking to the whole resource only when every use
covers it or when effective backend capability says finer tracking cannot be
represented without changing semantics. Promotion is recorded in the compiled
plan and diagnostics. It may add conservative ordering, but cannot discard
contents, create an apparent data dependency, or exceed a synchronization-work
budget. When conservative realization would violate an admitted concurrency
contract or limit, compilation returns typed unsupported.

### 5. Queue ownership follows effective queue identity

Queue roles resolve through ADR-173 before barrier synthesis. A role change does
not transfer ownership when both roles map to the same `RenderQueueId`. When an
overlapping use moves between different effective queue identities, the compiled
plan emits a matched release/acquire pair.

The release is associated with the producer submission's ADR-173 signal value;
the acquire waits on that exact queue timeline value. Both carry the same
resource generation and normalized range. Unmatched, cyclic, stale, or
non-monotonic transfer evidence is rejected. Normal-frame transfer is GPU-side
and never introduces a CPU, queue-idle, or device-idle wait. External resources
also obey their lease contract; a graph transition does not transfer that native
lease by itself.

### 6. The frontend owns policy; backends own faithful translation

The compiled plan contains normalized transitions before backend execution. An
explicit API backend translates every transition, access scope, layout intent,
range, and release/acquire edge into its private native model. It validates
representability before recording commands and returns typed unsupported or
invalid-plan failure without partial execution when translation is impossible.

An implicit API backend such as OpenGL realizes the same semantics with ordered
commands, memory visibility, texture feedback handling, and private state
restoration as required. “Implicit” describes the native API, not permission to
drop visibility, invent ordering, or accept a graph that an explicit backend
rejects.

Backends do not infer missing old state, widen access, choose a new layout,
change queue ownership, or silently insert global synchronization. Native details
remain private and may differ while observable Horo behavior stays equal.

### 7. Validation is bounded and failure is atomic

Graph construction and compilation use finite limits for resources, uses,
tracked ranges, transitions, ownership transfers, and diagnostics. Range
splitting or conservative promotion cannot create unbounded work.

Malformed ranges, invalid states, absent initial evidence, read-before-write,
unsupported translation, exhausted budgets, and stale resource, queue, device,
or plan generations produce typed errors before `Execute` accepts the plan. The
error identifies resource, range, producer and consumer passes, old and new
logical states, and an actionable failure class without native handles or
message parsing.

Compilation failure publishes no partial plan or state evidence. Execution
failure does not advance final-state evidence. Only accepted GPU work and its
ADR-173 completion timeline can establish state for a later external or
persistent import.

### 8. Migration has one policy source

Delivery proceeds in bounded stages:

1. add Horo state, scope, range, transition, and ownership-transfer values;
2. extend graph uses, imports, and exports with state and range declarations;
3. implement deterministic hazard validation and transition synthesis;
4. translate the normalized plan in Null and fake backends, then OpenGL and each
   explicit backend;
5. add cross-backend fixtures for hazards, overlap, boundary state, queue
   aliasing and transfer, malformed input, and invalidated generation evidence;
6. remove transitional backend-local inference after all callers publish the
   shared contract.

No stage maintains a second native-enum or backend-owned policy model. Until a
stage implements a state or translation, it reports typed unsupported rather
than approximating support.

## Consequences

One deterministic frontend model owns resource ordering across implicit and
explicit APIs. Subresource independence is preserved where supported, queue
ownership composes with ADR-173 timelines, and diagnostics explain transitions
without exposing native types. Device loss and replacement cannot reuse stale
synchronization evidence.

The cost is more graph metadata, checked range partitioning, bounded state
tracking, and stricter import/export declarations. Conservative backends may
serialize more work or reject plans they cannot faithfully represent. These
costs are accepted because hidden inference prevents parity and safe lifetime
reasoning.

## Rejected Alternatives

### Expose native state and barrier enums

Rejected because callers would encode one API, native types would leak through
RenderApi, and equivalent graphs would require backend-specific authoring.

### Keep every resource permanently in a general state

Rejected because a general state is not valid or efficient for every operation,
does not define visibility or ownership, and hides read-before-write errors.

### Let passes submit manual barriers

Rejected because feature code would need global knowledge, could contradict
graph ordering, and would make validation backend-specific.

### Track only whole resources

Rejected as the canonical model because independent mip, layer, aspect, and
buffer ranges are common. Bounded conservative promotion remains an explicit
realization rule, not the authored contract.

### Let each backend infer and retain state

Rejected because backend-local history cannot validate a graph before execution,
provide deterministic cross-backend diagnostics, or survive replacement, device
loss, and external ownership changes safely.

### Use CPU or device-idle waits for every transition

Rejected because normal-frame ordering belongs to GPU dependencies, global waits
destroy concurrency, and ADR-173 permits CPU waits only for bounded exceptional
purposes.
