# ADR-012: World Streaming Partition Authority and Subsystem Boundaries

- **Status**: Proposed
- **Date**: 2026-08-28
- **Supersedes**: None
- **Scope**: Partition/state ownership, async provider readiness and retirement, reservations, epoch/generation fencing, Scene integration, networking and shutdown
- **Issue**: [WST-001.1](https://github.com/abdullahbodur/horo-engine/issues/1528)
- **Jira**: [HORO-1528](https://horo-engine.atlassian.net/browse/HORO-1528)
- **Normative document**: [World Streaming Architecture](../architecture/runtime/world-streaming-architecture.md)

## Context

World streaming stages cell payloads, feature resources and Scene entities across
I/O, simulation and native device owners. The previous proposal used void provider
callbacks while requiring readiness/deallocation acknowledgements, conflated cell
and partition generations, and gave normal eviction and shutdown opposite teardown
orders. Budget authority also lacked a reservation protocol and optional failures
could unnecessarily fail the entire cell.

The existing ADR-023 index/cell formats, ADR-026 canonical coordinates, ADR-016
navigation residency adapter and ADR-017 prefab/Scene contracts must remain coherent
with the lifecycle decision. This ADR changes runtime ownership and protocols,
not the `world.index` or `.wcell` wire layout.

## Decision

**StreamingPartitionAuthority owns partition topology, canonical cell state,
fencing and the budget ledger. WorldStreamingManager orchestrates queues and host
integration through that authority. Feature providers expose bounded asynchronous
staging, prepared-publication and retirement acknowledgements. A cell becomes
Active only after required readiness and a Scene transaction; it becomes Unloaded
only after cell-owned resources retire. Cancellation fences stale publication but
does not assume all running I/O can stop.**

### Ownership And Execution Roles

| Owner | Responsibility |
|---|---|
| StreamingPartitionAuthority | Grid/layers, volumes, state transitions, partition epoch, cell generation and global reservations |
| WorldStreamingManager | Bounded frame integration, requests, completion drains and barrier orchestration |
| Scene Runtime | Detached ECS candidates and CommitDeferredLifecycleChanges transactions |
| Asset Pipeline | Registry/catalog, bounded async reads, validation, cooked payload leases |
| Feature providers | Domain data/native resources, affinity, readiness, publication and retirement |
| Editor | Independent authoring document/paging/pins and explicit runtime preview |

Authority and manager are distinct logical roles, not two state machines or aliases
for competing public types. An implementation may colocate them without duplicating
canonical state. Providers never decide world-cell residency or run independent
camera-driven cell loaders; Scene Runtime never owns the partition grid.

StreamingAuthorityRole is a logical owner mapped by host composition to the scene
mutation executor: editor preview main, runtime simulation owner or server simulation
owner. It does not require a MainEditor thread in headless builds. Bounded admission
and completion drains run at declared PreUpdate service points; Scene structural
publication/removal occurs at CommitDeferredLifecycleChanges. Native provider work
stays on the provider's render/physics/audio role. Worker completions cannot mutate
live ECS or invoke arbitrary lifecycle observers.

[ADR-010](010-job-waiting-and-operation-store-ownership.md) and
[ADR-018](018-command-registration-permissions-threading-and-packaged-build-policy.md)
govern JobSystem/JobId, non-blocking owner dispatch and user-visible OperationStore
tracking. No independent provider executor/store or ordinary-frame join is added.

### Identity And Fence Scope

WorldPartitionId identifies the manifest's worldGuid. StreamingCellId retains the
ADR-023 `(x,y,z,lod,layerId)` tuple, including its uint16 layer identity and uint8 LOD;
runtime requests add partition scope rather than changing the serialized key.
Streaming volumes/cells use canonical WorldCoordinate64 under ADR-026, not rebased
float positions as durable identity. Layer validity comes from manifest membership.

PartitionEpoch identifies the mounted partition incarnation and changes on world
replacement/reload, even for the same worldGuid. StreamingGeneration identifies a
per-cell residency attempt; it changes on new admission or invalidation, not at every
Loading/Resident/Active phase. Both reserve zero and use checked monotonic increments.
Overflow closes admission and retires the affected incarnation/slot with
GenerationExhausted; counters never wrap or silently reuse an outstanding identity.

Every asset/provider/Scene request captures partition, epoch, cell, generation and
request identity. Publication also checks expected phase and cancellation. Old Ready
results cannot reactivate a new cell. Retired attempts retain their own fence, so
late acknowledgements still free the correct old resources rather than being blindly
dropped. Completion routing and provider/module lifetime outlast outstanding work.

### Asynchronous Provider Contract

Use the existing **IFeatureStreamingProvider** name from ADR-023/016. The earlier
IStreamingFeatureProvider void-notification proposal is replaced, not retained as a
second interface. Host-composed inert descriptors declare stable IDs, payload/schema
support, native roles, cost bounds, criticality, finite positive stage/prepare/retirement
deadlines and a validated dependency DAG.
Cycles, missing required providers and incompatible phase contracts fail composition.

The normative protocol defines these bounded admission/poll operations:

| Operation | Acknowledgement |
|---|---|
| EstimateCellCost | Validated peak resource bound without I/O/allocation |
| BeginStage / PollStage | Generation-safe handle, then Pending, Ready or ReadyFallback; typed error on failure |
| PreparePublish / PollBarrier | Prepared only after provider-affine attachment preparation completes |
| PublishPrepared | Bounded no-fail publication at the validated common commit boundary |
| BeginRetire / PollBarrier | Retired only after readers, jobs, native fences and cell-owned resources are released |

Requests retain payload/fence/reservation/cancellation state. Poll never blocks.
Failed admission creates no accepted work; failure after acceptance retains a handle
until cleanup. Retirement/completion capacity is reserved before work starts. Prepared
publication cannot initiate allocation, I/O, waits or new recoverable work. Providers
that cannot meet that barrier contract cannot be activation-critical in the host.

| Policy | Required behavior |
|---|---|
| Required | Must be Ready and Prepared before Active; failure rolls back the cell |
| Optional | Allowed only for optional payloads; absence/failure records unavailability and retires partial resources |
| Degradable | A declared validated fallback satisfies its dependency; without one, failure is required failure |

CoreEcs and format-required payloads cannot be downgraded. A required consumer cannot
depend on an optional provider without a valid fallback. Optional late attachment is
a separate admitted owner transaction, never an unannounced mutation of Active state.
Optional staging also has a deadline and must retire failed partial work.

### Residency States And Transaction Barriers

There are five normal states plus Failed, **six** states in total:

| State | Contract |
|---|---|
| Unloaded | No cell-owned resident payload, active entity or cell-scoped provider resource; descriptors/shared caches may remain under separate ownership |
| Loading | Admitted asynchronous I/O/decode with current fence and reservations |
| Resident | Validated payload and detached candidates; provider stages may still be pending; no live ECS activation |
| Active | All required/fallback providers prepared and the current Scene activation transaction committed |
| Evicting | Access revoked, dependency-ordered cleanup pending; retained resources stay charged |
| Failed | Terminal diagnostic after cleanup; retry requires fresh admission/generation |

Resident -> Active requires validated bytes, admitted resource costs, all required
Ready/Prepared acknowledgements (including Prepared for any admitted fallback),
a prepared SceneActivationBatch and a fresh fence
check. Scene Runtime commits the candidate at CommitDeferredLifecycleChanges with
the providers' prepared publication actions. OnCellActive, if exposed as an observer
notification, is post-commit only and does not create entities. Required failure
before publication leaves live Scene state unchanged and rolls candidates back.
Post-publication native failure uses fallback/eviction policy, not a fictional
retroactive transaction rollback.

Cancellation/failure from Loading or Resident enters Evicting, not immediate Unloaded.
I/O interruption is requested where supported; buffers remain leased until the I/O
owner releases them. Active eviction revokes new access, quiesces bindings and removes
Scene/provider resources using the registered dependency DAG. Providers needing Scene
handles detach while those handles remain valid; backing data retires after consumers.

Evicting -> Unloaded/Failed requires Scene removal acknowledgement, Retired for every
started provider, retired I/O/read leases and released/transferred reservations.
Optional cleanup cannot be skipped. RetirementStalled leaves the cell Evicting and
charged; it never claims that queued frees or outstanding GPU work have completed.

### Budget Reservation And Failure Policy

The authority ledger counts CPU/GPU/resident/staging/retired resources and concurrent
copies once. Provider allowances are slices of the global budget, not extra memory.
Shared allocations have one charged owner plus leases. Estimate -> Reserve -> Stage
-> CommitReservation -> ReleaseReservation is explicit; required state transitions
cannot occur without admitted resources. Where metadata lacks an estimate, use a
validated provider upper bound or reject admission, never assume zero.

Unexpected staging growth requires additional admission **before** allocation.
Denial yields or fails under policy; providers cannot overrun then notify the manager.
Cancellation or a GPU-free request releases no credits until actual retirement.
Old partition work remains charged during replacement. Disposable provider cache/LOD
entries can evict locally; activation-critical resources require an authority-owned
fallback/eviction transition.

The normative budget defines configurable aggregate/sub-caps and bounded work units.
The frame-time target prevents starting more work; it is not native-call preemption
or an unconditional smooth-frame guarantee. Pinned demands and age priority never
bypass admission. Oversized mandatory content returns a visible failure requiring a
host safe fallback, loading barrier or pause.

### Retry, Networking And Editor Boundaries

Queue-age boost is a bounded priority term, not guaranteed admission. The normative
formula defines its slope/cap and stable tie ordering. Transient errors use at most
three default automatic retries after cleanup, with 2/4/8-second unscaled cooldowns
and fresh reservations. Permanent integrity/schema/provider errors wait for a relevant
revision change or explicit authorized retry. Volume reentry alone does not reset
exhausted retries; "volume cooldown trigger" is no longer an undefined mechanism.

The server owns gameplay relevance and replicated entity lifecycle. Peers own their
local staging, incarnation and budgets, so server/client Active may occur in different
frames. Ordered world/relevance commands map to local epochs and stale commands are
rejected. Clients report Ready/Unavailable/Failed; server policy must not activate
dependent gameplay without required content or an explicit safe fallback. Local
camera prefetch cannot grant gameplay authority, bypass local limits or duplicate
network-owned actors. ADR-018 NET/wst mutations retain permission and server-authority
checks and cannot skip provider retirement or fencing.

Editor authoring residency is independent: authoring data may page and own editing
pins. Runtime eviction cannot delete authoring data, and the whole world's geometry
need not remain in memory. Bake and isolated Play/Preview consume those explicit
boundaries.

### Navigation And Prefab Reconciliation

[ADR-016](016-navigation-target-ownership-and-dependency-boundary.md) NavigationRuntime
tile streaming is resource installation/removal under the World Streaming adapter,
not a second cell loader. The adapter reports costs/readiness and participates in
publication/rollback; tile retirement acknowledges all reader leases. Standalone
navigation retains its host asset-lifetime adapter without a concrete WorldStreaming
dependency in NavigationRuntime.

[ADR-017](017-prefab-role-ownership-and-capability-tiers.md) Tier 0-style offline
expansion flattens placed prefab content into cell CoreEcs definitions, preserving
identity/component/dependency rules. Activation does not synchronously spawn nested
CookedPrefab assets. Dynamic Tier 1 requests use SceneCommandBuffer::RequestSpawnPrefab
and its existing OperationStore/commit contract separately; cell-bound spawns capture
the fence and participate in eviction, while persistent/gameplay/network entities
are not deleted merely because their positions fall in the cell.

### Error Handling And Shutdown

Follow [ADR-008](008-error-model-exception-boundary-and-registry.md). Preserve
CellNotFound, CellAlreadyActive, **CellAlreadyLoading**, CellBudgetExceeded,
StaleGeneration, ProviderFailed, VolumeNotFound, InvalidCoordinate and Cancelled.
The normative error table adds generation/composition, stage/retirement and shutdown
timeout outcomes. Existing binary StreamingCellErrorCode values remain unchanged and
are retained as underlying causes rather than collapsed into success or empty data.
Required ProviderFailed rolls back then fails; optional/degradable failures follow
their explicit policy. Diagnostics include the complete fence, provider and cause.

Normal eviction, replacement and shutdown use the **same dependency DAG**, not
opposite hardcoded provider/entity orders. Shutdown closes admission, cancels work
and drains only under ADR-010's allowed bounded teardown rules. Asset Pipeline,
provider modules and completion routing remain alive until dependencies retire.
On timeout, return ShutdownIncomplete with outstanding owners/bytes, retain an
isolated retirement owner and its dependencies, and report failure. Never detach
work referencing freed state. Process-exit hosts may choose controlled termination;
in-process reload cannot destroy outstanding dependencies or call it a clean unload.

## Consequences

- Authority, orchestration, native preparation and Scene publication are explicit.
- Readiness and retirement barriers support multi-frame GPU/physics/nav work safely.
- Cancellation, budget growth, optional failures and shutdown timeouts have observable
  contracts instead of assuming immediate completion.
- Wire format/versioning remains ADR-023's responsibility. Qualification tests listed
  in the normative document are required implementation work, not tests added here.

## Rejected Alternatives

- **Void lifecycle callbacks as completion evidence**: Cannot represent pending
  staging or prove retirement of readers/device resources.
- **Independent provider cell loading or budget growth**: Creates competing residency
  policy and unaccounted allocations.
- **One ambiguous cell/partition generation**: Cannot distinguish world replacement
  from rapid reload or safely route old retirement acknowledgements.
- **Immediate activation on worker completion**: Violates native affinity and Scene
  transaction boundaries.
- **Immediate free on cancellation or timeout**: Risks use-after-free when I/O,
  workers or device queues cannot be physically interrupted.
- **Runtime eviction of authoring data**: Confuses independent residency policies and
  makes large-world editing unsafe.
