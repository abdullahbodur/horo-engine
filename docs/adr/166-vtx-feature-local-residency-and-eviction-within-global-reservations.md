# ADR-166: VTX Feature-Local Residency and Eviction Within Global Reservations

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: World Streaming budget authority, VTX-local demand/priority/pins/residency/eviction, multidimensional reservation accounting, pressure, shared pages, cancellation and retirement
- **Issue**: [VTX-003.1](https://github.com/abdullahbodur/horo-engine/issues/2195)
- **Jira**: [HORO-2149](https://horo-engine.atlassian.net/browse/HORO-2149)
- **Parent**: [VTX-003](https://github.com/abdullahbodur/horo-engine/issues/2194)
- **Related**: [ADR-008](008-error-model-exception-boundary-and-registry.md), [ADR-010](010-job-waiting-and-operation-store-ownership.md), [ADR-012](012-world-streaming-partition-authority-and-subsystem-boundaries.md), [ADR-027](027-renderer-resource-identity-and-descriptors.md), [ADR-034](034-gpu-memory-and-residency-ownership.md), [ADR-164](164-virtual-texturing-ownership-product-scope-and-capability-tier.md), [ADR-165](165-virtual-texture-source-cooked-artifact-page-store-and-cache-ownership.md)
- **Normative documents**: [World Streaming Architecture](../architecture/runtime/world-streaming-architecture.md), [Virtual Texturing Architecture](../architecture/runtime/virtual-texturing-architecture.md)

## Context

ADR-164 gives VTX logical page choice and feature-local eviction while World Streaming
owns cell demand and aggregate admission and ADR-034 Renderer owns GPU capacity. The
boundary still needs an execution rule. Without one, VTX could add a global priority
queue, budget counters or implicit overcommit; World Streaming could micromanage pages;
or Renderer pressure could free logical pages before GPU retirement and refund capacity
that remains allocated.

Virtual pages may be demanded by several cells, views, materials and producers. They
move through I/O, decode, upload, mapping and retirement stages with overlapping CPU,
staging and GPU costs. A byte budget alone is insufficient, cancellation is not
physical completion, and an LRU timestamp is not authority to evict pinned or required
content.

This ADR fixes reservation, priority, pinning, residency and eviction semantics.
VTX-003.2 and later tickets freeze exact state types, queue algorithms, limits,
threading and tests without changing the authorities below.

## Decision

### 1. World Streaming owns global policy and VTX owns only an admitted slice

| Responsibility | Authority | Deliberate non-owner |
|---|---|---|
| Global CPU, I/O, decoded/staging memory, GPU, request/completion and frame-work budgets | World Streaming authority and host policy | VTX cannot create a competing global ledger |
| Cell/view demand class, product criticality, aggregate priority and admission order | World Streaming/application policy | VTX cannot promote its pages above other features |
| VTX page demand merge, duplicate suppression, page priority, pins and victim choice inside a granted slice | VTX runtime | World Streaming does not choose individual pages |
| GPU cost validation, allocation/mapping and physical retirement | Renderer under ADR-034 | A World/VTX reservation is not native allocation permission |
| Artifact reads and byte leases | Assets provider under ADR-165 | VTX does not account provider storage as its own cache |

The host composes one World Streaming authority for runtime streaming work. Non-cell
preview or explicitly standalone VTX operations still request a named host-owned
reservation scope; they do not fall back to an unaccounted VTX pool. Headless cook and
validation operations use their owning application-operation limits, not runtime
residency budgets.

### 2. One typed reservation covers peak multidimensional cost

World Streaming admits a generation-scoped reservation equivalent in semantics to:

```cpp
struct VirtualTextureReservation {
    StreamingReservationId id;
    StreamingFence fence;
    VirtualTextureGeneration generation;
    VtxReservationLimits limits;
    VtxCriticality criticality;
    VtxFallbackPolicy fallback;
};

struct VtxReservationLimits {
    ByteCount providerBytes;
    ByteCount decodedBytes;
    ByteCount uploadBytes;
    ByteCount gpuBackingBytes;
    WorkUnits ioWork;
    WorkUnits decodeWork;
    WorkUnits renderWork;
    CountLimit requests;
    CountLimit completions;
};
```

Exact fields and units are deferred, but every admitted path accounts for simultaneous
peak live copies, scratch, page tables, feedback/readback and old/new or upload/resident
overlap. Checked unknown cost is not zero. Reservation creation atomically charges all
required dimensions; partial success rolls back.

VTX subdivides the token internally but cannot enlarge, transfer or reinterpret it.
Discovered growth pauses the candidate and requests an additional global reservation
before allocating. Denial invokes the captured required/fallback policy or returns a
typed result; it never borrows from another cell, feature, preview or future frame.

Renderer independently validates the ADR-034 GPU claim. Both World Streaming and
Renderer accounting identities are correlated, not merged: one admits product/global
work, the other owns physical capacity. Completion updates actual use but unused credit
returns only through the owning reservation transaction.

### 3. VTX residency is generation-scoped and committed at owner safe points

The logical state model has these semantic stages:

```text
Absent -> Requested -> Reading -> Decoded -> Realizing -> Resident
   \          \           \          \           \          |
    +------------------------------------------------------> Evicting -> Absent
```

Every transition carries VTX generation, page identity, request operation and
reservation identity. Workers and providers publish bounded completions; only the VTX
owner safe point commits logical state. `Resident` means the active VTX generation has
a matching Renderer mapping completion and required leases, not merely that bytes were
read or an upload was submitted.

Repeated demand coalesces onto one compatible in-flight operation. Different content,
descriptor, renderer/device or reservation generations never coalesce. Late or stale
completions release their exact resources without publishing.

### 4. Priority is local ordering, not admission authority

World Streaming supplies bounded demand records with criticality, normalized aggregate
priority, deadline/age inputs and an admitted reservation. Materials, feedback,
producers and cameras supply observations through their typed owners; none directly
enqueues global work.

VTX deterministically combines compatible demands for the same page and orders only
work already eligible inside its slice. The ordering key may include required-before-
optional class, mip/coverage value, bounded recency/frequency and stable page identity
as a final tie break. Exact weights belong to VTX-003.3, are versioned policy inputs and
cannot depend on pointer value, thread completion order or backend name.

Priority changes order, not capacity, pin status, generation validity or required
content policy. Starvation/aging is bounded and cannot turn optional work into required
work or bypass global admission.

### 5. Pins are typed leases with an owner and expiry condition

A pin identifies the VTX/page generation, consumer scope, reason and release condition.
Supported semantic classes include activation-required cell pins, active material/frame
snapshot leases, in-flight upload/mapping leases, explicitly admitted editor-preview
pins and a required fallback/mip-tail set. Later contracts close the exact enum.

Pins are not booleans or reference-count guesses. Duplicate acquisition by one owner is
idempotent or separately identified; foreign release fails. Cell pins end only when
World Streaming revokes demand and dependent snapshots retire. GPU pins end on Renderer
acknowledgement. Cancellation invalidates future publication but does not release a pin
whose owner still uses the resource.

Pinned pages remain charged. If required pinned content cannot fit, admission or the
captured fallback fails visibly; VTX never evicts a pin, expands a pool or deletes a
different cell to manufacture space.

### 6. Eviction is deterministic, feature local and two phase

VTX may select only unpinned disposable pages of the matching active generation within
its granted slice. Candidate ordering is deterministic from captured policy inputs,
semantic importance/required tail, last qualified demand and stable page identity.
Wall-clock timing and unordered-container iteration do not decide victims.

Eviction has two phases:

1. VTX revokes logical availability at its safe point, stops new demand attachment and
   requests exact renderer/provider retirement.
2. After every referencing snapshot and byte lease, each worker, and each relevant GPU
   queue acknowledge
   retirement, VTX commits `Absent` and asks the global/Renderer owners to release or
   reclassify their charges.

`Evicting` is unavailable to new consumers but remains fully charged. A frame number,
cancel flag, page-table rewrite or CPU reference count alone does not prove retirement.
Failure to retire reports `RetirementStalled`; it cannot force reuse.

### 7. Shared pages use one charge identity and multiple consumer leases

When several cells/views demand identical page and content generations, VTX keeps one
logical residency record and attaches generation-checked consumer leases. The host
reservation policy assigns one primary/shared charge identity and explicit projections
to consumers; VTX cannot double charge, hide the charge or move it when a consumer
leaves.

Removing one cell releases only its demand/pin. The page becomes evictable after all
consumers and native work release. If global policy requires a charge transfer, World
Streaming performs it atomically before releasing the old reservation. Page sharing
never permits one partition, preview or product scope to spend another's budget.

### 8. Pressure and fallback remain owner directed

World Streaming may reduce a slice or request a bounded release target/deadline.
Renderer may report pressure or reservation failure through ADR-034. VTX responds by
cancelling optional candidates and selecting eligible local victims, then reports
released, pending-retirement and unavailable amounts. It cannot claim bytes released
until owners acknowledge them.

If local disposable pages are insufficient, VTX reports shortfall. World Streaming
alone chooses broader deferral, admitted quality fallback or cell eviction. Renderer
alone changes physical allocation policy. Required-to-optional, Sparse-to-Atlas or
quality fallback must already exist in the captured plan and be re-admitted; pressure
cannot invent it mid-generation.

### 9. Queues, cancellation and cleanup are bounded

Request, waiter, completion, cancellation and retirement queues have finite configured
capacity charged to the reservation/host. Coalescing has bounded waiter/fan-out counts.
Overflow returns typed `CapacityExceeded` or leaves demand globally pending; it never
drops required work silently or allocates an emergency untracked node.

Cleanup/completion capacity is reserved before producer admission. Cancellation closes
new work, invalidates operation publication, propagates owner requests and keeps leases
until terminal acknowledgements. Shutdown uses the same path after closing admission;
it does not clear maps, fake reservation release or block the frame thread on I/O/GPU.

### 10. Errors, diagnostics and migration fail closed

Typed outcomes distinguish reservation denied/stale/exhausted, cost unknown/overflow,
request/waiter/completion capacity, page pinned, no eligible victim, stale completion,
provider/renderer failure, cancellation and retirement stall. Diagnostics expose safe
IDs, generations, state, pin class/count, reserved/actual/pending-retirement cost and
bounded priority reason, never native handles, paths or unbounded per-page labels.

Existing VTX-local global budgets, singleton schedulers, unrestricted LRUs and early
refund counters are prototype policy, not compatibility contracts. Implementation must
route admission through World Streaming/host scopes, use ADR-034 for physical GPU
claims, convert booleans to typed leases and retain charges until acknowledgement.

Qualification covers exact-cap and one-over-cap admission, multidimensional peak
overlap, growth denial, duplicate/coalesced demand, pin ownership, stable victim order,
shared-page charge transfer, pressure shortfall, cancellation at every stage, stale
completions, full normal queues with progressing cleanup, device loss and stalled
retirement.

## Consequences

Positive consequences:

- VTX can optimize page locality without competing with global streaming policy.
- Pins, shared pages and retirement retain correct ownership under cancellation and
  pressure.
- CPU, I/O, staging, GPU and frame-work peaks are admitted together rather than hidden
  behind a byte-only cache size.
- World Streaming and Renderer receive honest pending-retirement accounting.

Costs and trade-offs:

- One page operation correlates VTX, global reservation, Assets and Renderer identities.
- Two-phase eviction delays reusable capacity, especially with frames in flight.
- Shared pages require explicit consumer leases and charge-transfer policy.
- Exact priority weights and bounds remain downstream measured decisions.

## Rejected Alternatives

### Give VTX an independent global scheduler and memory budget

Rejected because it would compete with terrain, scene, audio and other streaming work,
double-count shared costs and bypass cell/product admission.

### Let World Streaming select individual pages

Rejected because page/mip/feedback semantics are VTX policy. World Streaming owns
aggregate demand, priority and reservations, not feature-internal victim choice.

### Treat Renderer pressure as immediate logical eviction

Rejected because Renderer owns physical retirement while VTX owns logical state; queued
GPU readers and consumer snapshots may still pin the page and its charge.

### Use an unrestricted LRU and never pin pages

Rejected because recency alone cannot protect activation-required content, in-flight
operations, frame snapshots or shared consumers, and iteration/time order is not
deterministic.

### Refund reservation on cancel or page-table removal

Rejected because logical invalidation does not prove provider, worker or GPU retirement.
Credits return only after the owning acknowledgements.
