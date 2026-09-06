# ADR-175: Replication Dirty-State Capture and Copy Strategy

- **Status**: Accepted
- **Date**: 2026-09-07
- **Supersedes**: None
- **Scope**: Authoritative change detection, capture safe points, copied snapshot ownership, dirty hints, allocation, cancellation, shutdown and qualification ownership
- **Issue**: [NET-004.2](https://github.com/abdullahbodur/horo-engine/issues/1131)
- **Jira**: [HORO-1131](https://horo-engine.atlassian.net/browse/HORO-1131)
- **Related**: [ADR-099](099-replication-ownership-authority-and-compatibility.md)
- **Normative documents**: [Multiplayer Replication Architecture](../architecture/runtime/multiplayer-replication-architecture.md), [Networking Architecture](../architecture/runtime/networking-architecture.md), [Scene Runtime](../architecture/runtime/scene-runtime.md)

## Context

Replication needs stable state after simulation commits without extracting the same
gameplay fields once per client. Three candidate models were considered:

1. polling arbitrary Scene/ECS memory from NetworkRuntime;
2. treating push notifications or dirty bits as authoritative values; and
3. capturing declared canonical values once at an owner safe point into bounded,
   immutable NetworkRuntime-owned storage.

Polling creates a second reflection/layout authority and can observe a partial
transaction. Value-bearing notifications duplicate gameplay state and lose their
meaning when coalesced, duplicated or reordered. A canonical copy costs bounded capture
storage, but gives every interested connection the same complete committed source and
keeps gameplay ownership separate from baselines and transport scheduling.

## Decision

### 1. One post-commit canonical copy is authoritative for replication

Horo accepts canonical copied state as the only replication capture strategy. At the
fixed network capture safe point after a complete simulation/structural commit,
NetworkRuntime invokes the declaring owner's adapter once for each admitted target. The
adapter reads a validated owner-thread view and writes canonical values through the
pinned descriptor's bounded codecs into prepared candidate storage.

Successful publication yields an immutable snapshot identified by the exact world,
authority, session, object, object generation, descriptor generation, simulation tick
and source revision. Interest selection, per-client baselines, delta encoding and wire
routing consume that snapshot; they do not call gameplay capture again. Quantized bytes
are a declared network representation and never replace canonical gameplay or save/hash
authority.

The candidate is private until all required fields and identities validate. Capture
failure, capacity denial or stale identity publishes nothing and preserves the previous
immutable snapshot and baselines.

### 2. Dirty notifications are lossy, idempotent scheduling hints

An owner may advance a monotonic source revision or mark an object/field dirty. A hint
contains no value, grants no authority and is safe to coalesce, duplicate or lose. It
only asks NetworkRuntime to consider the target at a later legal safe point.

Before suppressing publication, the capture path compares the newly captured canonical
state with the compatible prior snapshot under the pinned descriptor. A missing hint
therefore cannot make a changed authoritative value permanently invisible: bounded
periodic reconciliation or an owner revision mismatch schedules a fresh capture. A
duplicate hint cannot create a second semantic revision or unbounded history.

Polling ECS/component memory, generic event-bus mirroring, pointer/layout comparison,
and a separate mutable replication cache owned by gameplay are prohibited. There is one
canonical gameplay source and one immutable NetworkRuntime projection.

### 3. Capture observes only committed owner state

Scene/Gameplay owns the transaction barrier. Structural changes and field mutations
become visible together, then the capture phase opens a read-only view for one exact
committed tick/revision. NetworkRuntime cannot capture while a transaction is preparing
or committing, retain the view after adapter return, or race the next owner mutation.

Scene replacement, authority change, session replacement, object reuse and descriptor
replacement advance their generations. A candidate whose complete identity no longer
matches at publication is stale and discarded without changing the active snapshot.

### 4. Allocation and work are bounded before publication

Product and descriptor limits bound admitted objects, fields, canonical bytes,
candidates and retained snapshot generations. The capture coordinator reserves or
reuses storage before opening the owner view. Adapter work is bounded by the declared
field set and performs no blocking I/O, transport calls, service discovery or ambient
registration.

One source snapshot is shared by all eligible client projections. Per-client work is
limited to interest, compatible baseline/delta and routing decisions owned by later
stages; it never repeats source extraction. Failure to reserve required storage is a
typed capture failure, not permission to allocate without bound or publish a partial
snapshot.

### 5. Cancellation, loss and shutdown preserve a complete boundary

Cancellation before publication retires only the candidate. Cancellation after
publication cannot mutate the immutable snapshot; downstream work may finish under its
pin or be discarded by its operation identity. Late completions from an old world,
session, authority, object or descriptor generation are stale.

Shutdown first closes new capture admission, cancels candidates, drains or discards
downstream pins under bounded policy, then releases snapshots and owner views before
their modules/worlds. Dirty hints arriving after admission closes are ignored as
idempotent scheduling input; they cannot reopen capture.

### 6. Executable qualification belongs to the implementation tickets

This decision fixes the strategy and its observable gates; it does not introduce a
premature NetworkRuntime implementation. [NET-004.3](https://github.com/abdullahbodur/horo-engine/issues/1132)
owns the capture coordinator, revision comparison and focused tests for lost and
duplicated hints, stale identities, capacity failure, cancellation and shutdown.
[NET-004.4](https://github.com/abdullahbodur/horo-engine/issues/1133) owns compatible
baseline/delta encoding and tests that duplicate or missing delivery never changes the
captured source authority.

Those tests must prove:

- a lost hint is recovered by revision reconciliation and a duplicated hint publishes
  at most one semantic snapshot;
- capture never observes a partially committed field or structural change;
- stale world/session/authority/object/descriptor candidates preserve the prior pin;
- cancellation and capacity failure publish no partial candidate; and
- shutdown rejects new work and drains every candidate/view/snapshot pin in owner order.

This ADR claims no executable coverage by itself. The linked implementation tickets
cannot close without those gates.

## Consequences

### Positive

- All clients derive from one stable committed source without repeated extraction.
- Dirty hints can be cheap and lossy without becoming a correctness authority.
- Immutable snapshots isolate owner mutation from encoding, interest and transport work.
- Generation fencing and private candidates make failure transactional.

### Negative

- Capture requires bounded copied storage and may overlap old/new snapshot generations.
- Owners must provide canonical adapters and revisions rather than relying on reflection.
- Periodic reconciliation is required because hints are intentionally not reliable.

## Rejected Alternatives

### Poll arbitrary ECS/component state

Rejected because it depends on layout/reflection, repeats work, bypasses owner safe
points and may observe a partial transaction.

### Put values in dirty notifications

Rejected because notification loss/coalescing becomes data loss and creates a second
mutable source beside gameplay state.

### Let each connection capture the owner independently

Rejected because clients could observe different ticks/revisions and extraction cost
would scale with client count.

### Keep a gameplay-owned mutable replication shadow

Rejected because two mutable authorities can diverge. Gameplay owns canonical values;
NetworkRuntime owns immutable derived snapshots and baselines.
