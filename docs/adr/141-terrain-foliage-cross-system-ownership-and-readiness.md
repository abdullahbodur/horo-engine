# ADR-141: Terrain/Foliage Cross-System Ownership and Readiness

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: Terrain/Foliage immutable producer snapshots, Render/Physics/Navigation consumer preparation, World Streaming/Scene coordination, typed receipts, criticality, generation-safe aggregate activation, rollback, replacement, runtime failure, retirement, cancellation and shutdown
- **Issue**: [TRF-005.1](https://github.com/abdullahbodur/horo-engine/issues/1963)
- **Jira**: [HORO-1919](https://horo-engine.atlassian.net/browse/HORO-1919)
- **Related**: [ADR-008](008-error-model-exception-boundary-and-registry.md), [ADR-010](010-job-waiting-and-operation-store-ownership.md), [ADR-012](012-world-streaming-partition-authority-and-subsystem-boundaries.md), [ADR-016](016-navigation-target-ownership-and-dependency-boundary.md), [ADR-027](027-renderer-resource-identity-and-descriptors.md), [ADR-085](085-physics-shape-authoring-cook-and-runtime-boundary.md), [ADR-105](105-navigation-asset-and-scene-ownership-boundary.md), [ADR-107](107-navigation-query-consistency-and-snapshot-ownership.md), [ADR-137](137-terrain-foliage-ownership-data-tier-and-lifecycle.md), [ADR-138](138-terrain-source-cooked-tile-cache-and-streaming-ownership.md), [ADR-139](139-terrain-render-extraction-material-lod-and-tier-boundary.md), [ADR-140](140-foliage-placement-baked-dynamic-state-and-eviction-ownership.md)
- **Normative documents**: [Terrain and Foliage Architecture](../architecture/runtime/terrain-and-foliage-architecture.md), [World Streaming Architecture](../architecture/runtime/world-streaming-architecture.md), [Physics Architecture](../architecture/runtime/physics-architecture.md), [Navigation and AI Architecture](../architecture/runtime/navigation-and-ai-architecture.md), [Rendering Architecture](../architecture/runtime/rendering-architecture.md)

## Context

Terrain/Foliage content participates in World Streaming, Scene activation, rendering,
collision, navigation and gameplay. ADR-137 assigns each subsystem its mutable state and
introduces multi-dimensional readiness. ADR-138 defines immutable tile artifacts and
World Streaming admission. ADR-139 defines render-candidate ownership. ADR-140 defines
foliage mutations and dirty-state handoff. The remaining gap is one precise producer/
consumer and readiness protocol across the owners.

Current documents say that Terrain supplies descriptors, Physics/Navigation/Renderer
prepare their own resources and a cell waits for required readiness. They do not define
the complete generation tuple, receipt state, no-fail publication gate, how owner-specific
safe points compose, or what happens when one consumer fails after others prepare. A
boolean “collision ready” or callback can be stale, can refer to the wrong tile replacement
and can cause a cell to become Active while gameplay sees mismatched visual/collision/nav
generations.

No cross-system coordinator may become a hidden owner. Terrain cannot mutate a Physics
body, NavMesh or GPU resource. World Streaming cannot interpret height/hole/foliage
semantics. Physics, Navigation and Render cannot load cells or advance Terrain mutations.
Gameplay cannot infer readiness by probing native state. The integration must coordinate
immutable inputs and typed receipts while leaving every mutable store and safe point with
its existing owner.

This ADR defines that coordination boundary. TRF-005.2 owns concrete collision/navigation
producer snapshot adapters. TRF-005.5 owns concrete world-streaming manifest/budget/
readiness integration. Those tickets may refine data layouts without moving authority.

## Decision

### 1. Integration coordinates owners; it does not absorb them

| Responsibility | Authority |
|---|---|
| Terrain dataset/tile/foliage logical state, mutations, neutral semantic inputs and snapshot leases | TerrainRuntime owner lane |
| Cell demand, criticality plan, dependency DAG, aggregate reservations, activation ticket and cell Active/Evicting state | World Streaming partition authority |
| Scene entity/binding candidate and final live Scene visibility | RuntimeScene lifecycle authority |
| Visual resources, per-view plan, RenderObject mapping, GPU submission and fence retirement | RenderFrontend/private backend |
| Shape/body/material/filter realization, collision world and pre-step publication | Physics |
| Navigation artifact/provider realization, topology/query root and tile-reader retirement | Navigation |
| Durable foliage delta capture/dormancy | Runtime Save/Persistent World under ADR-140 |
| Gameplay meaning, authority and consumption of an aggregate committed capability | Product gameplay owner |

The application host composes narrow adapters and one integration coordinator around the
existing RuntimeScene/World Streaming lifecycle. The coordinator owns activation records,
dependency order, reservation leases and receipt routing only. It owns no Terrain tile,
body, NavMesh, GPU object, gameplay component or persistent delta.

Adapter descriptors are inert metadata. Registration validates stable adapter/consumer/
schema IDs, supported versions, criticality support, dependencies, owner execution role,
finite costs/deadlines and no-fail publication contract. It performs no source decode,
native creation, service discovery, registration side effect or live-state mutation.

### 2. Terrain publishes consumer-specific immutable semantic snapshots

Terrain captures one revision-consistent source root and projects only the minimum neutral
input required by each consumer:

```cpp
struct TerrainConsumerSnapshotHeader {
    TerrainRuntimeHandle terrain;
    TerrainSnapshotRevision terrainRevision;
    TerrainContentRevision content;
    TerrainConsumerInputSchemaVersion schema;
    WorldDatasetId world;
    StreamingCellHandle cell;
    StreamingFence fence;
    TerrainConsumerRequestGeneration request;
    OriginEpoch origin;
    TerrainRequiredReadiness required;
};
```

Render receives ADR-139 bounded candidates. Physics receives bounded neutral collision
source descriptors/material/subshape mappings plus holes and transforms. Navigation
receives bounded grounded-surface/hole/obstacle/link source descriptors and provenance.
World Streaming receives cost/dependency/readiness metadata, not native payload internals.
TRF-005.2 freezes exact Physics/Navigation snapshot schemas.

Every snapshot owns immutable values or leases whose lifetime extends through the
consumer's last read. It names the exact dataset/tile/foliage identities, content/
residency/mutation/capability revisions and coordinate/origin evidence used. It never
contains a mutable Terrain pointer, editor/source object, backend/vendor handle, Physics
body, Navigation polygon, GPU resource, consumer callback or unbounded container.

Snapshots are produced on the Terrain owner lane from a committed Terrain generation.
Workers may encode bounded neutral candidates from leased roots, but cannot mutate active
Terrain or publish readiness. Same input generation yields canonical candidate order and
diagnostics independent of worker completion.

### 3. Each consumer owns preparation and returns typed receipts

The shared receipt shape is equivalent to:

```cpp
enum class TerrainConsumerStage : uint8_t {
    Pending,
    Staged,
    StagedFallback,
    Prepared,
    Published,
    Retiring,
    Retired,
    Unavailable,
    Failed
};

struct TerrainConsumerReceipt {
    TerrainConsumerKind consumer;
    TerrainConsumerIncarnation incarnation;
    TerrainConsumerRequestGeneration request;
    TerrainSnapshotRevision terrainRevision;
    StreamingFence fence;
    ActivationTicket activation;
    TerrainConsumerStage stage;
    TerrainConsumerResourceRevision resources;
    EffectiveCapabilitiesRevision capabilities;
    TerrainConsumerCost actualCost;
    OptionalFallbackDecision fallback;
    OptionalError error;
};
```

`Staged`/`StagedFallback` mean the consumer owns a complete private candidate that is
not gameplay-visible. Only the aggregate state named `Ready` in Decision 7 means the
RuntimeScene/World Streaming root has committed and is visible to gameplay.

A receipt is immutable evidence from the owning adapter, not a public constructor or
boolean that Terrain/gameplay can forge. `Staged` means all fallible I/O/decode/native
resource preparation is privately staged and validated but not live. `Prepared` means
the owner has scheduled/validated a no-fail publication action for the exact activation
ticket at its declared safe point. `Published` means the private generation is installed
behind activation-scoped routing; it is not gameplay-visible until aggregate commit.
`Retired` means readers/jobs/fences/native releases and charged cell ownership ended.

Receipt absence is not success. A current resource with a similar tile/cell ID is not
evidence. The coordinator validates every identity/revision/fence/capability/cost and
legal state transition before accepting it. Duplicate terminal receipts are idempotent;
out-of-order, foreign-incarnation and stale-generation receipts are rejected and counted.

### 4. Criticality and fallback are frozen before work starts

Each requested dimension/consumer is `Required`, `Optional` or `Degradable` in the
validated Terrain/Scene/cell product plan:

- `Required` must reach Prepared/Published before aggregate activation;
- `Optional` may be explicitly NotRequested/Unavailable without blocking activation,
  but cannot later be inferred Ready; and
- `Degradable` may return `StagedFallback` only for a predeclared substitute whose typed
  contract satisfies all dependents.

Headless/server visual absence may be optional. Collision or navigation may be required
or optional by scene/product semantics; their names do not hardcode criticality. A
required gameplay consumer cannot depend on an unavailable optional dimension. Fallback
never changes backend/provider, removes collision/hole semantics, substitutes flat
terrain, drops foliage, cooks at runtime or alters gameplay authority.

The plan records exact required dimensions, accepted fallback IDs, dependency DAG,
effective capabilities, content/provider schemas, cost limits and revisions. Runtime
failure or budget pressure cannot silently rewrite it. A policy change prepares a new
generation.

### 5. Preparation is detached and budgeted as one transaction

The coordinator executes this bounded sequence:

1. Validate the Terrain/Scene/cell plan and exact consumer descriptors.
2. Ask each owner for deterministic upper-bound costs and reserve aggregate staging,
   resident, old/new overlap, retirement and queue capacity before work.
3. Capture one Terrain snapshot root and create consumer-specific leased projections.
4. Start consumers in validated dependency order; their workers/native lanes create
   detached candidates only.
5. Poll non-blocking typed receipts. Growth requests occur before allocation and can be
   denied without oversubscription.
6. After every required/degradable candidate is Staged/StagedFallback, request
   `PreparePublish` on each owning safe-point lane.
7. Revalidate all generations, cancellation and reservation ownership at the commit gate.

Render, Physics and Navigation may use different workers/native roles, but none waits on
another native thread while holding a live-world lock. The coordinator never calls an
owner's private API from an arbitrary callback. Completion records use pre-reserved
bounded queues and retain adapter/module/snapshot/reservation leases until consumed.

### 6. Owner safe points compose through activation-scoped routing

One physical instant cannot atomically mutate GPU, Physics, Navigation and Scene stores
on unrelated owner threads. Atomicity is achieved by separating private owner publication
from aggregate visibility:

1. Each Prepared owner performs its prevalidated no-fail publication at its declared
   render-safe, Physics pre-step, Navigation-owner or Terrain-owner safe point.
2. The published candidate remains addressed only by `ActivationTicket`; ordinary
   gameplay/query/render routing cannot discover it.
3. After all required Published receipts are accepted, RuntimeScene publishes one
   immutable aggregate activation root at `CommitDeferredLifecycleChanges` and World
   Streaming marks the exact cell attempt Active.
4. Consumers resolve live bindings only through that committed root/generation.

For replacement, the old aggregate root remains visible until the new root commits. For
initial activation, no entity/gameplay observer sees the candidate before final commit.
No expected fallible work remains after the final gate. A no-fail publication contract
violation is a contained session/partition fault, not permission to report partial
success or fake rollback after visibility.

Post-commit observer events are notifications only. `OnCellActive`, renderer callbacks,
Physics contacts, navigation updates or gameplay events cannot act as readiness receipts
or create the missing state.

### 7. Aggregate readiness is generation-scoped and multi-dimensional

Terrain exposes an immutable `TerrainReadinessSnapshot` keyed by the committed aggregate
root and exact Terrain/consumer revisions. Logical, streaming, visual, collision,
navigation and mutation dimensions remain separate. Each is `NotRequested`, `Preparing`,
`Ready`, `Unavailable`, `Failed`, `Suspended` or `Retiring`, with required/fallback
evidence and consumer resource revision where applicable.

`Ready` means the exact dimension is reachable through the committed aggregate root. It
cannot be inferred from:

- decoded height/foliage data;
- cell Resident state;
- a Render frame/draw, Physics body count or Navigation query success;
- retained old-generation native resources;
- a worker completion callback; or
- another dimension's state.

Gameplay requests one typed capability/snapshot with required dimensions and maximum
accepted revision age. It receives Ready, Preparing, Unavailable, Failed or Stale; it
never downcasts consumer objects or probes native state. Physics collision remains
authoritative for physical queries; Navigation remains authoritative for paths; Render
visibility remains presentation-only.

### 8. Failure and cancellation preserve the old aggregate generation

Any expected failure before the final commit gate cancels dependent work, requests
retirement for every started candidate and leaves the old Active aggregate root unchanged.
Initial activation remains non-Active. The coordinator retains all handles and leases
until every owner returns Retired; rollback means no live visibility, not immediate free.

Cancellation is cooperative. A cancelled OS read, shader upload, Physics shape creation or
Navigation build may still finish; its stale receipt routes only to cleanup. Cancellation
wins before the final gate. After that gate, the outcome is Committed/TooLate and reversal
requires a new replacement/eviction transaction.

Errors preserve the failing consumer and nested cause. Optional failure retires its
partial state and publishes explicit Unavailable only if dependency/criticality permits.
Degradable failure may use only the frozen substitute. Required failure rolls back the
whole candidate. The coordinator never relabels a native allocation failure as a missing
Terrain tile or drops a consumer silently.

### 9. Runtime failure is a new state transition, not retroactive rollback

After aggregate activation, device loss, Physics corruption/provider loss, Navigation
provider loss or required Terrain invalidation changes the affected dimension to
Suspended/Failed for the exact generation. The product's predeclared runtime failure
policy chooses a new replacement, safe gameplay pause, cell eviction or session failure.
It cannot claim that the original commit never occurred.

Optional visual loss on a headless/declared configuration may keep logical/collision/
navigation Active. Loss of required collision/navigation prevents dependent gameplay at
the next owning safe boundary. Existing queries/frames/ticks retain their leased old
snapshot rules; new access routes through the updated aggregate readiness.

Recovery prepares a complete new consumer/aggregate generation using the same protocol.
An owner cannot patch a native object and mark the old receipt current without advancing
its resource/capability revisions.

### 10. Replacement and retirement use exact reverse dependencies

Terrain content/mutation, cell, renderer/device, Physics world, Navigation topology,
capability or product-plan changes create a new request/activation generation. Candidate
reuse requires exact semantic/artifact/capability compatibility and transfers explicit
leases; matching coordinates or type names are insufficient.

Eviction/replacement first revokes the aggregate live route so no new gameplay/render/
query access begins. It then executes the registered reverse dependency DAG while needed
Terrain/Scene bindings remain valid:

- consumer owners quiesce new native access and remove logical bindings;
- RuntimeScene removes/replaces ECS bindings at its safe point;
- Render waits for frames/GPU completion, Physics for step/query/body readers, Navigation
  for topology/query/tile readers, and Terrain for snapshots/workers;
- dirty durable foliage transfers under ADR-140 before its last active copy retires; and
- World Streaming releases/transfer charges only after every Retired receipt.

No universal “entities before providers” order is assumed; descriptor dependencies define
the actual graph and composition rejects cycles. A retirement timeout reports stalled
owners and keeps the attempt Evicting/resources charged. It never fabricates Retired,
reuses a generation/slot or force-frees referenced state.

### 11. Standalone and multiplayer hosts use the same semantics

A standalone non-streamed Terrain scene uses a host asset/Scene lifecycle coordinator
with the same snapshots, receipts, aggregate root and retirement rules. It does not add a
concrete WorldStreaming dependency to Terrain, Physics or Navigation.

In multiplayer, the server owns gameplay activation/relevance and durable mutations;
each peer independently prepares local Render/Physics/Navigation resources under its
capabilities and budgets. A server readiness/relevance command does not forge a client's
local receipt, and local visual readiness does not grant server gameplay authority.
Network identities map to fresh local incarnations; remote generation counters are not
compared as local handles.

### 12. Shutdown is the same bounded retirement protocol

Shutdown closes coordinator and adapter admission, invalidates pending tickets, requests
cooperative cancellation, revokes live aggregate roots in dependency order and waits only
at ADR-010-permitted bounded owner drains. Terrain snapshots, reservations, adapters,
modules and native resources remain owned until their workers/readers/fences return
Retired.

Late receipts retain their old incarnation and route to cleanup only. A deadline returns
typed incomplete shutdown with outstanding owners/costs. Destructors do not invoke foreign
safe points, block for unbounded native completion or force-free another subsystem's state.

### 13. Errors and observability retain the full correlation tuple

Stable error categories include:

```text
terrain.integration.plan_invalid
terrain.integration.snapshot_incompatible
terrain.integration.consumer_unavailable
terrain.integration.consumer_prepare_failed
terrain.integration.fallback_invalid
terrain.integration.receipt_stale
terrain.integration.receipt_transition_invalid
terrain.integration.aggregate_commit_stale
terrain.integration.required_runtime_failed
terrain.integration.retirement_stalled
terrain.integration.shutdown_incomplete
```

Safe context includes Terrain/runtime/content/residency/mutation/capability revisions,
world/cell/fence/request/activation identities, consumer/incarnation/resource revision,
criticality/fallback, owner phase, costs and nested cause. Native handles/pointers, source
paths and unbounded provider text never cross the public error boundary.

Metrics expose bounded counts/durations for prepare, owner-safe-point wait, aggregate
commit, rollback, runtime failure, fallback, retirement, stale receipts, reserved/actual/
retiring bytes and incomplete shutdown by registered consumer kind. Per-tile/instance IDs
are diagnostic snapshot fields, not metric dimensions. Observability cannot drive
readiness or fallback.

### 14. Verification proves owner isolation and aggregate behavior

Required coverage includes:

- target/header/dependency tests proving Terrain/Physics/Navigation/Render public
  boundaries contain only Horo types and no reverse concrete subsystem/native dependency;
- inert adapter descriptor composition, duplicate/schema/dependency-cycle/criticality/
  deadline/cost rejection and no ambient registration side effects;
- canonical bounded producer snapshots with exact revision/origin/cell/request evidence,
  lease lifetime and no mutable/native/unbounded data;
- each consumer's Pending/Staged/StagedFallback/Prepared/Published/Retiring/Retired and
  Unavailable/Failed transitions, including duplicate/out-of-order/stale receipts;
- Required/Optional/Degradable matrices and dependency validation with no implicit
  fallback, hardcoded consumer-name criticality or missing-receipt success;
- failure/cancellation at every estimate/reserve/snapshot/stage/prepare/owner-publication/
  final-gate step, proving old aggregate visibility and complete cleanup;
- owner-specific Render, Physics pre-step, Navigation and Terrain safe points publishing
  private activation-scoped roots before one RuntimeScene/World Streaming live root;
- initial activation and replacement under adversarial worker/safe-point order with no
  gameplay observer seeing partial cross-system state;
- independent logical/streaming/visual/collision/navigation/mutation readiness and typed
  gameplay queries without native probes or cross-dimension inference;
- runtime device/provider/required-dimension loss and complete recovery/replacement without
  retroactive rollback claims or in-place receipt mutation;
- reverse-DAG eviction/replacement with Render frame/GPU, Physics step/query, Navigation
  topology/query, Terrain snapshot and durable-foliage handoff leases;
- shared budget/growth/old-new-retirement accounting without double charge or release on
  queued cleanup;
- streamed/standalone, Null/headless, server/client and optional-omitted compositions; and
- repeated shutdown, late receipts, stalled owners and deadline expiry with no slot reuse,
  force-free or false Retired acknowledgement.

## Consequences

- Terrain publishes immutable typed semantic inputs and consumes typed receipts without
  owning consumer-native state.
- Render, Physics and Navigation keep their mutable stores, worker/native affinity,
  safe-point publication, error cause and retirement authority.
- World Streaming/RuntimeScene can expose one coherent Active generation even though
  private owner publication occurs at different safe points.
- Readiness becomes exact, multi-dimensional and generation-scoped rather than a set of
  booleans or callbacks.
- Rollback preserves the previous live aggregate but may retain candidate resources until
  asynchronous consumer retirement completes.
- Runtime required-consumer failure is explicit Suspended/Failed/recovery policy, not a
  fictional rollback of already observed gameplay.
- Integration needs bounded activation records, receipt queues, reservation overlap and
  adapter qualification; this is coordination cost without ownership transfer.
- TRF-005.2 and TRF-005.5 must define concrete snapshots and streaming integration within
  this protocol.

## Rejected Alternatives

### Let Terrain create or mutate Physics, Navigation or Render objects

Rejected because it crosses native ownership, affinity and lifetime boundaries. Terrain
publishes neutral immutable snapshots and observes receipts.

### Let consumers pull mutable Terrain state when convenient

Rejected because independent reads can mix content/mutation/residency generations and
outlive storage. Consumers receive leased revision-consistent snapshots.

### Use one `bool ready` per consumer

Rejected because it omits request/incarnation/revision/fence, fallback, cost and retirement
evidence and can accept stale work.

### Treat worker completion as Prepared or Active

Rejected because private resources may still need owner-safe-point publication and the
aggregate root is not yet visible. Worker completion is only staged evidence.

### Publish each consumer live as soon as it finishes

Rejected because gameplay could observe new collision with old navigation/render/Terrain
or vice versa. Private publication stays activation-scoped until aggregate commit.

### Make one coordinator thread own every subsystem safe point

Rejected because GPU, Physics and Navigation retain distinct affinity and invariants. The
coordinator routes tickets/receipts; owners execute their own no-fail publications.

### Hardcode Render optional and Physics/Navigation required

Rejected because host/product/scene semantics vary. Criticality is explicit and validated
per plan, while dependency requirements prevent invalid combinations.

### Infer collision/navigation readiness from decoded Terrain data

Rejected because native consumer resources, topology and publication may fail or lag.
Only the matching committed consumer receipt proves readiness.

### Let a retained old resource satisfy a new generation

Rejected because content, capability, origin, provider or consumer schemas may differ.
Reuse needs exact compatibility and an explicit lease transfer.

### Roll back an already observed aggregate activation after runtime failure

Rejected because gameplay may have consumed it. Runtime failure creates a new visible
state and recovery/eviction transaction.

### Release reservations when cleanup is merely queued

Rejected because workers/readers/GPU/native objects may remain. Release follows Retired
acknowledgement from the owning consumer.

### Force-retire stalled consumers at shutdown

Rejected because the coordinator cannot prove foreign readers/fences are finished.
Incomplete shutdown retains ownership and reports the outstanding owner.
