# Ownership And Resource Lifetime Architecture

## Purpose

This document defines ownership, borrowing, handles, allocation domains,
resource destruction, caches, and cross-thread lifetime rules for Horo Engine.

The goal is to make every important lifetime answerable from the public
contract: who creates the resource, who may use it, which thread owns mutation,
and when destruction occurs.

## Core Decisions

- RAII and value semantics are the default.
- Public APIs distinguish owning, shared, borrowed, and identity handles.
- Raw pointers are non-owning unless an API explicitly documents otherwise.
- Engine resources do not depend on destruction through process-global static
  order.
- Cross-thread work captures stable values or lifetime-safe handles, not
  untracked references.
- GPU resources are destroyed through their owning backend and affinity.
- Caches own entries and expose leases or handles; callers do not own cached
  objects.
- Allocation optimization follows measurement and declared lifetime domains.

## Ownership Vocabulary

| Form | Meaning |
|---|---|
| Value | Independent owned state |
| `std::unique_ptr<T>` | One transferable owner |
| `std::shared_ptr<T>` | Explicit shared lifetime with justified cost |
| `T&`, `T*`, `std::span<T>` | Borrowed access bounded by the caller's operation |
| Stable ID | Identity, not memory ownership |
| Resource handle | Generation-checked reference into an owning registry |
| Lease | Temporary access that keeps a managed resource alive |

`shared_ptr` is not the default repair for unclear lifetime. Shared ownership
must identify the participants and the condition that releases the final owner.

## Composition Roots

Hosts construct services in dependency order and destroy them in reverse:

```text
Platform
  -> Observability
  -> Configuration
  -> Job System
  -> Assets / Scene / Physics / Renderer / Pipeline
  -> Application Services
  -> Editor / GUI / MCP Adapters
```

The exact graph may vary by host, but ownership remains explicit. Service
locators and hidden process-global owners are not used.

`Observability` in this order begins as a minimal bootstrap and emergency sink
that does not depend on resolved configuration. After configuration is loaded,
the composition root installs or reconfigures the normal sinks, levels, and
retention policy, then replays bounded bootstrap records. The canonical
sequence is defined by
[Observability Logging](../observability/observability-logging.md#runtime-lifecycle).

## Borrowing Rules

- A borrow never outlives its documented owner.
- A callback that may outlive the call captures an owning handle or stable ID.
- Containers do not return references that become invalid without documenting
  the invalidation operation.
- `string_view`, `span`, and iterator lifetimes are limited to the owning
  snapshot or call.
- GUI tabs and modals release subscriptions before their referenced bus.
- Runtime components that reference physics or render resources use
  generation-checked handles or a documented non-owning lifetime relationship.

## Resource Handles

Long-lived and reloadable resources use typed handles:

```cpp
template<typename Tag>
struct Handle {
    uint32_t index;
    uint32_t generation;
};

using TextureHandle = Handle<TextureTag>;
using MeshHandle = Handle<MeshTag>;
```

Registries validate type, index, generation, and owner domain. Stale handles
fail safely and never alias a newly allocated object at the same slot.

Serialized project and asset data stores stable logical IDs, not process-local
handles. The authority that owns the runtime registry resolves those IDs at an
explicit load or activation boundary: the asset registry resolves asset IDs,
and `SceneRuntime` resolves scene-object and entity references. Successful
resolution produces a handle or lease scoped to that authority; failed or stale
resolution returns a typed failure and never leaves a process-local handle in
persistent state.

## Allocation Domains

The supported lifetime domains are:

| Domain | Lifetime |
|---|---|
| Persistent | Service, project, scene, or loaded-resource lifetime |
| Frame | Cleared after the owning frame synchronization point |
| Scratch | One operation or job callback |
| Streaming | Bounded buffers recycled by producer/consumer pipelines |
| Emergency | Preallocated crash and out-of-memory reporting capacity |

General-purpose allocation remains valid unless profiling proves a dedicated
allocator is needed. Custom pools or arenas require:

- an explicit owner and reset point
- alignment and overflow guarantees
- debug validation
- metrics for reserved, committed, peak, and failed allocation
- sanitizer-compatible fallback or test mode

Emergency capacity uses preallocated storage and allocation-free counters where
accounting is cheap. Normal execution may publish those counters through the
metrics system. A crash or out-of-memory path may only read an already available
snapshot; it does not allocate, take ordinary locks, or publish through the
normal telemetry path while reporting the failure.

## Frame And Scratch Memory

Frame memory cannot escape the frame. Types allocated there are not captured by
jobs, stored in persistent containers, or referenced by deferred renderer work
beyond the corresponding fence.

Scratch allocators belong to an operation, worker, or task context. Reset occurs
only after all child work and callbacks using that memory complete.

## Scene And ECS Lifetime

The scene runtime owns entities, component storage, systems, and scene-scoped
resources. Entity references use generation-checked entity IDs.

Scene unload:

1. stops new scene work
2. cancels or joins scene-scoped jobs
3. disables system updates
4. releases external resource leases and physics bindings
5. destroys components and entities
6. releases scene-owned assets and scratch storage

Editor scene documents do not own runtime entities.

## GPU Resource Lifetime

Renderer frontend handles are backend-neutral. Concrete GPU objects are owned
by the selected backend and destroyed on a thread with the correct graphics
affinity.

Destruction may be deferred until the GPU has completed use. Deferred deletion
queues are bounded, generation-aware, and drained during normal frames,
renderer shutdown, and device-loss recovery.

Draining depends on backend availability. While the device or context remains
valid, the owning backend performs the required destruction calls on the
correct affinity. After loss or teardown makes those calls unavailable,
recovery invalidates the affected generations and retires queue bookkeeping
without calling the unavailable graphics API. Backend recovery must complete
any API-required release before destroying a still-usable device or context.

No destructor calls an unavailable graphics API after its context has been
destroyed.

## Cache Ownership

Caches define:

- key identity and invalidation
- whether entries are strong, weak, or leased
- memory and item budgets
- eviction policy
- thread-safety
- behavior while an entry is loading
- shutdown and hot-reload semantics

Acquiring a lease pins the managed entry with an internal retain count or an
equivalent owner token. This is an implementation detail of the owning registry
or cache and does not require exposing `shared_ptr` as the public API.

Eviction removes the entry from cache lookup and releases the cache's retain.
It does not invalidate an active lease or transfer primary ownership to the
caller. Final destruction is deferred until the last lease is released, and an
evicted key cannot issue a new lease unless it is loaded again. Duplicate
concurrent loads of the same key are coalesced where the asset contract permits
it.

## Cross-Thread Lifetime

Jobs receive immutable values, shared immutable snapshots, stable IDs, or
explicit leases. They do not retain references to stack state or GUI objects.

Main-thread continuations verify that their target session, document, project,
or resource generation is still current before commit. A completed worker job
does not resurrect a closed project or unloaded scene.

## Shutdown

Every owner has an idempotent shutdown path when resource release may fail,
block, or require affinity. Destructors remain final safety nets, not the only
place complex shutdown ordering is expressed.

Shutdown diagnostics identify outstanding jobs, leases, subscriptions, GPU
objects, and cache entries before their owners disappear.

## Testing

Required tests cover:

- stale generation rejection
- move-only owner transfer
- borrow invalidation contracts
- scene unload with active jobs
- cache eviction with active leases
- stable-ID resolution failure without persisted runtime handles
- renderer shutdown and device loss with deferred deletion
- no graphics API calls after backend invalidation
- configuration and project replacement without stale callbacks
- allocator reset boundaries and overflow
- emergency reporting without allocation or ordinary locks
- sanitizer runs for ownership-sensitive modules
- leak and outstanding-resource summaries at test shutdown

## Related Documents

- [System Design](./system-design.md)
- [Concurrency And Job System](./concurrency-and-jobs.md)
- [Scene Runtime](../runtime/scene-runtime.md)
- [Rendering Architecture](../runtime/rendering-architecture.md)
- [Observability Metrics And Profiling](../observability/observability-performance.md)
