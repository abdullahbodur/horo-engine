# ADR-174: Render Adapter and Device Discovery Contract

- **Status**: Proposed
- **Date**: 2026-09-06
- **Supersedes**: None
- **Scope**: Backend-neutral adapter identity, bounded discovery, deterministic selection, and device-creation diagnostics
- **Issue**: [RND-003.2](https://github.com/abdullahbodur/horo-engine/issues/297)
- **Jira**: [HORO-297](https://horo-engine.atlassian.net/browse/HORO-297)
- **Normative document**: [Rendering Architecture](../architecture/runtime/rendering-architecture.md)

## Context

Renderer modules are selected before device creation, while the current backend
configuration does not identify a discovered adapter or bind that selection to
the discovery revision that produced it. Backends could therefore enumerate in
driver order, silently select a different device, expose native identifiers, or
collapse unavailable, unsupported, and device-creation failures into text.

This decision defines the shared discovery and selection boundary. It does not
perform native enumeration, create a device, select a renderer module, or define
the complete capability snapshot owned by ADR-028.

## Decision

### 1. Discovery publishes bounded owned facts

The selected backend exposes `IRenderAdapterDiscovery` on the host-declared
render-capable owner thread. Discovery is synchronous, bounded to 1–64 records,
creates no device or surface, starts no background work, and returns an owned
`RenderAdapterSnapshot`. `Stop` closes admission idempotently before backend
shutdown; a later call returns the typed `DiscoveryStopped` error.

Each snapshot has a non-zero backend-lifetime revision. Records are strictly
ordered by unique `RenderAdapterId`, so native enumeration order, pointer values,
and completion timing cannot change selection. Empty snapshots are valid facts
and produce `NoCompatibleAdapter` when selection is attempted. Malformed,
duplicate, oversized, or unordered results fail as `InvalidSnapshot` before any
device work begins.

### 2. Adapter identity and properties remain native-free

`RenderAdapterId` is a bounded backend-scoped stable identifier using printable
machine-readable characters. It is not a native handle, array index, PCI address,
registry path, Objective-C object, or process pointer. Concrete backends may
derive it from stable platform facts but expose only the Horo value.

`RenderAdapterProperties` publishes a bounded display name, device kind, current
creation availability, advisory dedicated-memory fact, and presentation support.
Zero memory means unknown, not unlimited. Properties are discovery evidence,
not effective capability admission and not a promise that device creation will
succeed.

### 3. Selection applies exact constraints deterministically

The host supplies an optional exact adapter identity, optional exact device kind,
presentation requirement, and explicit permission for software adapters. An
explicit identity that is absent returns `RequiredAdapterNotFound`; an identified
but unavailable adapter returns `AdapterUnavailable`; unmet requirements return
`NoCompatibleAdapter`. Selection never changes backend, adapter, kind, presentation
policy, or software permission as an implicit fallback.

Without an exact identity, the first available canonical record satisfying every
constraint is selected. Software adapters are excluded unless explicitly allowed.
The result copies the selected facts and snapshot revision. Device creation must
revalidate that revision; replacement, hot-plug, driver reset, or rediscovery
invalidates stale selection with `StaleDiscovery` rather than retargeting it.

### 4. Device creation preserves typed diagnostics

`RenderBackendConfig` pairs an explicit adapter identity with its non-zero
discovery revision. Supplying only one is invalid. A backend revalidates both on
the owner thread before acquiring native device state.

Creation failures retain the adapter identity, a stable
`RenderDeviceCreationFailureKind`, a bounded actionable message, and explicit
retryability. Categories distinguish unavailable adapters, unsupported
requirements, driver rejection, memory exhaustion, device loss, cancellation,
shutdown, and unknown native failures. Backends translate native errors once at
this boundary and preserve the typed Horo error through the frontend. No failed
or half-created device becomes visible, and no failure selects another adapter.

### 5. Ownership and lifecycle

The host owns the discovery port and snapshots through selection. The backend
owns native enumeration and device state. Snapshot values are immutable and make
no driver calls when queried. Discovery and device creation run serially on the
render-capable owner thread, so there is no worker reference capture or implicit
cancellation lifetime. If a future backend needs asynchronous enumeration, it
must introduce an owned bounded operation with explicit cancellation and join;
it cannot weaken this contract locally.

Shutdown closes discovery, rejects new creation, drains or cancels only explicitly
owned bounded operations, destroys the device, and invalidates its discovery
revision. Re-enumeration after loss or adapter replacement produces a new
revision and requires explicit reselection.

## Consequences

- Host and backend tests share one deterministic, native-free adapter model.
- Explicit adapter choices cannot silently fall back after hot-plug or failure.
- Capability/profile resolution can bind to stable adapter and revision evidence.
- Backends must canonicalize native enumeration and preserve actionable failures.
- The transitional default with no explicit adapter remains allowed, but each
  backend must document and test its deterministic default until hosts perform
  discovery before initialization.

## Rejected Alternatives

### Use native handles or enumeration indices as adapter IDs

Rejected because their stability and representation vary by backend and process,
and because they would leak concrete API/platform types through RenderApi.

### Let each backend pick its preferred adapter silently

Rejected because driver order is not a product policy and an explicit user or
project selection must either be honored or fail actionably.

### Treat unavailable adapters as undiscovered

Rejected because a known-but-unavailable adapter needs different remediation from
an absent identity, especially after hot-plug, driver failure, or policy denial.

### Infer capability support from adapter kind or memory size

Rejected because device class and advisory memory do not prove any feature,
format, queue, or limit. ADR-028 owns reported and effective capability evidence.

### Run unbounded asynchronous discovery

Rejected because startup, cancellation, and shutdown would acquire hidden worker
lifetime and potentially block the render owner. The baseline contract is finite
and synchronous.
