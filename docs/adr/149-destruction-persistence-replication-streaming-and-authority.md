# ADR-149: Destruction Persistence, Replication, Streaming and Authority

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: Canonical destruction save/network state, server authority, replication capture/apply, late join, active/dormant streaming handoff, restore, compatibility, cancellation and shutdown
- **Issue**: [DFR-006.1](https://github.com/abdullahbodur/horo-engine/issues/2035)
- **Jira**: [HORO-1989](https://horo-engine.atlassian.net/browse/HORO-1989)
- **Related**: [ADR-008](008-error-model-exception-boundary-and-registry.md), [ADR-010](010-job-waiting-and-operation-store-ownership.md), [ADR-012](012-world-streaming-partition-authority-and-subsystem-boundaries.md), [ADR-023](023-world-index-and-cell-format-architecture-decision.md), [ADR-085](085-physics-shape-authoring-cook-and-runtime-boundary.md), [ADR-092](092-character-controller-determinism-and-state-composition.md), [ADR-097](097-default-real-time-transport-backend.md), [ADR-098](098-protocol-session-and-trust-policy.md), [ADR-099](099-replication-ownership-authority-and-compatibility.md), [ADR-112](112-save-archive-container-and-compatibility-policy.md), [ADR-114](114-canonical-runtime-world-persistence-boundary.md), [ADR-140](140-foliage-placement-baked-dynamic-state-and-eviction-ownership.md), [ADR-144](144-destruction-ownership-authority-state-and-runtime-geometry-boundary.md), [ADR-145](145-destruction-source-chunk-geometry-collision-and-cook-ownership.md), [ADR-146](146-destruction-runtime-activation-physics-cleanup-and-rollback.md), [ADR-147](147-destruction-event-and-cosmetic-consumer-ownership.md)
- **Normative documents**: [Destruction and Fracture Architecture](../architecture/runtime/destruction-and-fracture-architecture.md), [Save Game and Persistence](../architecture/runtime/save-game-and-persistence.md), [Multiplayer Replication Architecture](../architecture/runtime/multiplayer-replication-architecture.md), [World Streaming Architecture](../architecture/runtime/world-streaming-architecture.md), [Scene Runtime](../architecture/runtime/scene-runtime.md)

## Context

ADR-144 assigns live semantic state to `DestructionWorld`; ADR-146 defines atomic
intact/chunk activation and durable dormancy handoff; ADR-147 prevents presentation
events from becoming state authority. The persistent and replicated representation is
still unspecified. Saving component memory or solver bodies would duplicate ownership,
expose native state and fail across content/provider versions.

Destruction also crosses cell residency. World Streaming may unload a cell while its
destructibles are damaged, fractured or have authoritative moving chunks. Dropping the
last live copy before Persistent World accepts it loses gameplay state; letting DFR
decide residency would create a second streaming authority.

Multiplayer adds role and compatibility requirements. A server must remain authoritative
even when a client predicts cosmetics. Late join needs a coherent semantic snapshot plus
subsequent deltas and any required Physics motion, without replaying historical VFX/
Audio events or assuming the same chunk index means the same chunk under another asset
generation.

This ADR defines the state/authority composition and compatibility gates. DFR-006.2
owns the exact save snapshot schema and adapter; DFR-006.4 owns the exact replication
descriptor/wire encoding.

## Decision

### 1. Destruction semantics have one authority in every host role

| Concern | Authority |
|---|---|
| Live health, phase, deterministic seed state, broken/active/support/dormant membership and semantic revision | Scene-scoped `DestructionWorld` |
| Local/server command permission and authority epoch | Product gameplay authority composed by the host |
| Stable asset/content identity and published artifact bytes | Assets/Destruction Cook |
| Body motion, sleep, contacts and native solver state | Physics |
| Save capture/restore transaction and archive | Runtime Save |
| Dormant cell deltas/tombstones and durable eviction handoff | Persistent World |
| Residency, cell demand, activation/eviction barrier and resource budgets | World Streaming |
| Replication schemas, baselines, interest, encoding and delivery | NetworkRuntime plus DFR adapter |
| Transport session and protected message delivery | `INetworkTransport` |
| Presentation prediction/effects | Application consumer adapters under ADR-147 |

Standalone grants the local gameplay authority. A dedicated/listen server grants the
server world authority. Autonomous and simulated clients hold replicas and may submit
authorized requests, but they cannot commit canonical DFR commands. Process locality,
possession, visibility, loaded cell, Physics contact or UI focus never grants authority.

Every command, snapshot and delta names the exact runtime world/scene generation,
destructible identity/content generation, authority epoch and semantic revision. Network
I/O, save workers and streaming workers never mutate `DestructionWorld`; they deliver
owned typed candidates to its owner safe point.

### 2. One canonical snapshot contains semantic reconstruction state

The minimum owner-produced snapshot is equivalent to:

```cpp
struct CanonicalDestructionSnapshot {
    DestructionSnapshotSchemaVersion schema;
    DestructionWorldId world;
    DestructibleId destructible;
    FractureAssetId fractureAsset;
    FractureArtifactContentIdentity content;
    DestructionGeneration generation;
    DestructionStateRevision stateRevision;
    DestructionAuthorityEpoch authorityEpoch;
    DestructionSemanticPhase phase;
    QuantizedDestructionHealth health;
    DestructionDeterministicSeedState seed;
    ArtifactScopedChunkMask brokenChunks;
    ArtifactScopedChunkMask activeChunks;
    ArtifactScopedChunkMask supportedChunks;
    ArtifactScopedChunkMask dormantChunks;
    BoundedSupportState support;
};
```

`content` includes exact DFR schema/algorithm/source/recipe/dependency digests and the
stable chunk-table compatibility fingerprint. Compact masks use only that artifact's
canonical stable chunk table; the content identity is mandatory and masks cannot be
interpreted against another ordinal table. Migrations operate on stable
`DestructionChunkId`, never position or display name.

`seed` is the versioned semantic seed/cursor required to reproduce future deterministic
DFR decisions. It is not a native RNG object, heap image or worker state. `support`
contains only the additional bounded canonical anchor/connectivity-progress facts not
derivable from the exact artifact plus masks. DFR-006.2/006.4 freeze encoding and prove
that redundant representations cannot disagree.

The snapshot excludes native Physics bodies/shapes/manifolds/islands, ECS/runtime
handles, Render/GPU resources, event-journal cursors, VFX/Decal/Audio objects, jobs,
queues, callbacks, cache paths and wall-clock time. Historical ADR-147 presentation
facts are not save/network reconstruction state.

### 3. Active chunk motion remains a paired Physics-owned state

Gameplay-authoritative active chunks may require transform, linear/angular motion and
sleep/wake semantics. Physics owns their Horo canonical checkpoint/replication fields
and maps them by stable world/destructible/chunk identity. DFR records that the chunk is
active; it does not duplicate body motion or serialize Jolt state.

Save capture and network snapshot epochs pair the DFR semantic snapshot with the exact
Physics checkpoint/baseline for every required active chunk. The manifest proves equal
world generation, fixed tick and chunk set. Missing, extra or stale required motion
rejects the aggregate candidate; it cannot be replaced with zero velocity, an intact
body or a client-local body observation.

Cosmetic debris/motion remains VFX-owned and excluded. A dormant chunk has no live
Physics body and therefore no body checkpoint; its canonical DFR dormancy/support state
is sufficient for later ADR-146 reactivation. Product policy must explicitly classify
any chunk whose exact motion can be discarded during durable dormancy.

### 4. Save capture is one coherent owner-safe epoch

The DFR canonical adapter registers one inert ADR-114 participant descriptor with
bounded schemas, dependencies and limits. Runtime Save coordinates capture but does not
interpret masks, support or authority. At the owner safe point it pins exact scene/world,
fixed tick, DFR revision/content, Physics checkpoint, Persistent World ledger and
participant-registry revisions.

`DestructionWorld` returns an owned immutable bounded snapshot. Resident and dormant
destructibles are captured once by their current canonical owner; Scene and Persistent
World cannot both emit the same state. Large sets use immutable/COW roots with reserved
bytes. Workers serialize detached values only and cannot hold the live owner locked
across I/O.

Failure to capture any required DFR/Physics/Persistent World participant fails the whole
save cut. Optional omission must be schema/product-approved and represented explicitly.
Save success does not change live DFR revision, streaming residency or authority and
does not replay/clear the event journal.

### 5. Restore prepares semantic and derived candidates, then publishes once

Restore validates the archive, base scene/world, DFR snapshot schema, exact or migratable
artifact content, limits and paired Physics state before allocating a live candidate.
The DFR adapter migrates semantic values in detached storage and builds a candidate
`DestructionWorld` root. Physics prepares exact ADR-145 shapes/bodies from the validated
artifact and restores its canonical Horo motion checkpoint without deserializing native
solver memory.

Required initial cells apply saved DFR/Persistent World deltas before their normal
activation barrier. Other cell state remains dormant in the ledger. RuntimeScene joins
Scene, DFR, Physics, World Streaming and other required participants into the ADR-114
aggregate restore commit. Before commit the old world remains complete; after commit
the restored root and its required bodies/residency agree.

Failure/cancellation/stale generation before commit retires only candidate resources.
After commit, recovery is a new restore/replacement/command, not partial rewind. Restore
does not emit historical impacts, damage facts, sounds, decals or VFX; an explicitly
authored load presentation is a new occurrence.

### 6. NetworkRuntime replicates semantic snapshots/deltas, not object memory

The DFR module declares versioned inert replication descriptors and capture/apply
adapters under ADR-099. `NetworkRuntime` owns interest, baselines, acknowledgements,
serialization and transport. At the post-commit capture safe point, the DFR adapter
produces an immutable full snapshot or delta between exact semantic revisions. Dirty
hints schedule capture but never replace comparison against owner state.

The authority server alone produces authoritative DFR deltas. Clients validate session,
world/content/schema, authority epoch, base/target revision, bounds and canonical order
before decode allocation. Network threads produce an owned apply candidate; the client
DFR owner accepts it only at its legal safe point through the ADR-146 aggregate
transaction with required Scene/Physics/Render changes.

Deltas contain typed semantic operations or exact bounded set changes with one base and
target revision. They never carry native body IDs, generic property paths, event-bus
payloads or consumer effects. Duplicate target revision is idempotent. Old authority,
stale base, gap, conflict or out-of-order delta cannot patch current state; it triggers
bounded baseline recovery.

Client prediction may stage separately identified commands and cosmetic cues. It cannot
advance authoritative DFR revision. Reconciliation discards/rebases predicted candidates
by explicit policy after the server snapshot commits; it never treats a predicted
Physics contact or visible fracture as server truth.

### 7. Late join is snapshot-first with bounded revision fencing

Before DFR state admission, the joining client proves the required DFR schema,
artifact-content/chunk-table identity, tier/capability limits and dependent Physics/
Render products. Required mismatch rejects participation or selects only an explicitly
declared product mode such as spectator/no-world; it never downloads an untrusted
runtime mesh, maps by chunk index/name or silently uses an intact/fallback object.

The server captures one interest-scoped full DFR snapshot at revision `R` paired with
required Physics motion at the same network tick. Deltas after `R` enter a bounded
ordered buffer. The client prepares exact assets/bodies/render objects privately,
publishes the snapshot through one aggregate root, then applies contiguous deltas. A
buffer overflow/gap requests a newer full baseline; it does not skip state.

Interest changes use the same protocol. Historical ADR-147 facts are not replayed.
Optional bounded join presentation must use a new explicit catch-up occurrence and
cannot affect canonical state. Chunk motion interpolation begins from NetworkRuntime/
Physics baselines only after semantic activation.

### 8. World Streaming owns residency; Persistent World prevents loss

Every destructible has one stable streaming ownership scope/cell established by cooked
world data. Crossing chunks do not change canonical owner because a body moved across a
cell boundary. World Streaming alone admits activation, pins, replacement and eviction;
DFR reports typed state/cost/readiness and cannot load/unload cells itself.

Before eviction removes the last live canonical copy, DFR freezes the exact
destructible/content/revision snapshot and required Physics-to-dormant policy result.
Persistent World validates and accepts it into the cell ledger, returning a generation-
scoped durable handoff receipt. Only then may ADR-146 aggregate commit remove live Scene,
Physics and Render representations. Handoff failure, capacity denial, stale revision or
required active motion that policy cannot dormancy-convert blocks eviction.

Ledger records remain DFR semantic values under the DFR schema even though Persistent
World owns their durable storage/lifetime. Loading a cell resolves compatible assets,
prepares the dormant delta and required consumers, then publishes through the normal
cell aggregate barrier. A newer server-authoritative revision wins only through explicit
authority/reconciliation rules; streaming order never decides truth.

Forced eviction is not permission to lose canonical state. It may cancel cosmetic work
or apply a separately authorized product policy, but required dirty DFR handoff remains
a blocker. Budgets account resident roots, dormant records, in-flight handoff, candidate
activation and old/new overlap together.

### 9. Compatibility is schema- and stable-identity-based

Exact content identity is the fast path. A changed fracture artifact requires a
registered versioned migration that maps every referenced stable chunk, anchor/support
fact, seed schema and policy meaning. The migration declares source/target content
fingerprints, bounds, deterministic output and loss policy. Unmapped broken/active/
dormant chunks or incompatible support semantics fail restore/join/replacement.

Save archive migration runs in detached storage under ADR-112. Network schema
negotiation occurs before world admission under ADR-098/099. A server and client cannot
continue authoritative play with different semantic chunk tables merely because render
meshes look similar. Assets may fetch an approved exact published artifact through
normal package policy; DFR does not accept source paths or generate missing geometry at
restore/join time.

Unknown optional fields follow their versioned schema. Unknown required fields,
oversized masks/counts, invalid overlap between chunk sets, non-finite health/motion,
bad seed/support state or integrity failure rejects before owner mutation. There is no
best-effort partial mask application.

### 10. Limits, errors, observability and shutdown remain bounded

Profiles bound destructibles/chunks per snapshot, mask/support bytes, active Physics
checkpoints, deltas per tick, baseline/delta buffers, migrations, dormant ledger bytes,
simultaneous handoffs/restores and old/new overlap. Counts and byte products are checked
before allocation. Backpressure never grows an unbounded save/network/streaming queue.

Typed results distinguish `AuthorityDenied`, `StaleAuthorityEpoch`, `StaleRevision`,
`ContentIncompatible`, `MigrationUnavailable`, `InvalidChunkMask`,
`InvalidSupportState`, `PhysicsStateMismatch`, `ReplicationGap`,
`LateJoinBufferExceeded`, `DormantHandoffDenied`, `PersistentWorldCapacityExceeded`,
`StaleGeneration`, `IntegrityFailure`, `CancelledBeforeCommit` and
`ShutdownInProgress`. Results retain bounded identity,
revision/epoch and owner-stage provenance without native handles.

Metrics cover captured/restored/replicated destructibles/chunks/bytes, delta/baseline
size, gaps, compatibility outcomes, handoff latency/failure, resident/dormant counts and
candidate overlap. Dimensions use finite results/profile/schema versions, never asset/
chunk IDs, peer addresses, paths or native handles. Diagnostics cannot grant authority,
apply a delta or force eviction.

Shutdown closes commands/capture, freezes final owner roots, cancels candidates,
completes or explicitly fails required durable handoffs, revokes network/session
authority, retires consumer and Physics/Render resources, and finally releases DFR/
ledger/artifact roots after readers drain. Network/I/O worker completion is fenced by
world/session generation. A deadline reports incomplete shutdown and never force-frees
or silently discards unsaved canonical state.

## Compatibility And Follow-Ups

Existing schemes that save a mutable component, serialize solver state, replicate
generic property paths/events or evict damaged objects without durable handoff are not
compatible. Migration must produce the canonical DFR snapshot and paired owner state;
there is no dual-write legacy path because two authorities can diverge.

[DFR-006.2](https://horo-engine.atlassian.net/browse/HORO-1990) defines the exact
canonical snapshot/save adapter. [DFR-006.4](https://horo-engine.atlassian.net/browse/HORO-1992)
defines the replication descriptor and deterministic wire encoding. Later focused
decisions own streaming-cell record layout, production limits/diagnostics and any
prediction mode beyond cosmetic isolation.

## Consequences

### Positive

- Save, network and streaming reconstruct the same single-owner semantic state.
- Minimum state explicitly carries content identity, revision, seed and required chunk/
  support sets.
- Physics motion remains paired and coherent without serializing native solver state.
- Late join and cell activation publish complete semantic/consumer roots atomically.
- Durable handoff prevents damaged/fractured state loss under eviction pressure.

### Costs

- Capture/restore and network baselines must coordinate DFR, Physics and Persistent
  World epochs.
- Content changes need explicit stable-ID migrations or reject old saves/clients.
- Active/dormant overlap and late-join buffering require finite peak reservations.

### Rejected Alternatives

- **Serialize `DestructibleComponent` or Jolt bodies**: copies implementation/native
  state, duplicates ownership and is not compatible across builds/providers.
- **Let clients commit contact-driven fracture**: makes local Physics evidence server
  authority and diverges canonical revisions.
- **Replicate the generic event bus**: events are notifications/presentation facts, not
  current state or a stable wire schema.
- **Map saved/network masks by current chunk index or name**: silently applies state to
  different semantic chunks after content change.
- **Replay every historical event for late join/restore**: causes unbounded effects and
  confuses reconstruction with new gameplay occurrences.
- **Evict first and save later**: can lose the last canonical copy on failure/crash.

## Verification

Required contract and integration coverage includes:

- golden minimum snapshots containing exact artifact content, state revision, seed,
  broken/active/supported/dormant masks and support data;
- mask/set invariants, bounds, malformed/oversized input and no native/transient state;
- coherent DFR plus Physics active-chunk capture at one tick and rejection of mismatched
  body sets/motion baselines;
- aggregate save/restore failure at every participant with the old world unchanged;
- server-only canonical commit, request authorization, stale authority epochs, duplicate/
  out-of-order/gapped deltas and bounded baseline recovery;
- late join with exact/missing/mismatched content, buffered deltas, buffer overflow and
  no historical presentation replay;
- resident-to-dormant handoff, crash/failure before acknowledgement, blocked forced
  eviction, later activation and no duplicate Scene/Persistent World ownership;
- compatible registered stable-ID migration and rejection of index/name/partial mapping;
- standalone/listen/dedicated/autonomous/simulated/headless role matrices with identical
  canonical semantics; and
- cancellation/replacement/shutdown with save/network workers, handoffs, Physics steps,
  queries and frames in flight without forced release or state loss.
