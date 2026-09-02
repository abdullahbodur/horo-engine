# ADR-140: Foliage Placement, Baked/Dynamic State and Eviction Ownership

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: Deterministic foliage placement, immutable baked base, ephemeral runtime overlays, durable spawn/remove/update deltas, identity, authority, capacity, World Streaming eviction, save capture/restore, replacement, cancellation, multiplayer and shutdown
- **Issue**: [TRF-004.1](https://github.com/abdullahbodur/horo-engine/issues/1954)
- **Jira**: [HORO-1910](https://horo-engine.atlassian.net/browse/HORO-1910)
- **Related**: [ADR-008](008-error-model-exception-boundary-and-registry.md), [ADR-010](010-job-waiting-and-operation-store-ownership.md), [ADR-012](012-world-streaming-partition-authority-and-subsystem-boundaries.md), [ADR-023](023-world-index-and-cell-format-architecture-decision.md), [ADR-108](108-dynamic-overlay-carving-and-tile-rebuild-policy.md), [ADR-114](114-canonical-runtime-world-persistence-boundary.md), [ADR-137](137-terrain-foliage-ownership-data-tier-and-lifecycle.md), [ADR-138](138-terrain-source-cooked-tile-cache-and-streaming-ownership.md), [ADR-139](139-terrain-render-extraction-material-lod-and-tier-boundary.md)
- **Normative documents**: [Terrain and Foliage Architecture](../architecture/runtime/terrain-and-foliage-architecture.md), [Save Game and Persistence](../architecture/runtime/save-game-and-persistence.md), [World Streaming Architecture](../architecture/runtime/world-streaming-architecture.md)

## Context

ADR-137 assigns live Terrain/Foliage logical state to TerrainRuntime and explicitly
forbids implicit oldest/furthest eviction. ADR-138 makes cooked clusters immutable
generation-scoped artifacts, while ADR-139 makes render visibility and GPU buffers
derived presentation state. The current Terrain document still describes offline
placement, a separate runtime dynamic buffer and a spawn request without defining which
runtime changes are ephemeral, which are durable, where tombstones live or what cell
eviction may discard.

Foliage has three materially different forms. Cooked placement is an immutable authored
base. Short-lived runtime foliage is a session overlay. Product-authorized changes such
as a harvested tree, planted durable object or permanent removal are canonical runtime
world deltas that must survive cell eviction and save/restore. Treating them as one buffer
would either serialize render/transient state, mutate cooked assets, or lose gameplay
facts under memory pressure.

ADR-114 already establishes RuntimeSaveService and subsystem canonical adapters as the
single durable save transaction, with Persistent World owning dormant cell deltas. World
Streaming already owns cell residency and requires dirty state handoff before retirement.
Terrain must integrate with those authorities without creating its own save file, dormant
world database or global eviction scheduler.

This ADR defines placement determinism, the three state categories, command/identity and
capacity rules, durable delta handoff and restore. TRF-004.2 owns exact foliage type,
placement, culling, wind and collision definitions. Later TRF-004 tickets may define
concrete runtime storage and simulation while preserving these authorities.

## Decision

### 1. Foliage state is classified before mutation or persistence

| State | Canonical authority | Lifetime and storage | Save behavior |
|---|---|---|---|
| Authored placement inputs and foliage definitions | Editor document/Assets plus Terrain Model | Mutable only through authoring transactions | Referenced through compatible cooked base; never copied back from runtime |
| Baked foliage base | Terrain Cook/Assets generation | Immutable deterministic cluster artifacts | Base identity/revision dependency only; instances are not repeated as save records |
| Active baked projection | TerrainRuntime | Decoded/resident logical view of exact cooked generation | Derived from base plus canonical deltas; not separately saved |
| Ephemeral runtime foliage | TerrainRuntime overlay | Explicit cell/session/owner lifetime; discarded by declared lifecycle | Excluded from slot save unless a separate command promotes semantic intent before capture |
| Durable foliage mutation | Runtime Save/Persistent World canonical state | Stable spawn/update/tombstone delta relative to compatible base | Captured once through the registered canonical participant/ledger |
| Render/Physics/Navigation realization | Owning consumer | Derived native resources, bodies and query topology | Excluded and rebuilt after load/activation |

Reachability from a Terrain container, a “dynamic” flag, existence across several frames
or possession of a render/physics handle does not make state durable. Durability is an
explicit product-authorized command classification validated before commit.

TerrainRuntime owns the active semantic overlay and knows how a foliage delta affects
its invariants. RuntimeSaveService coordinates capture/restore and archive publication;
Persistent World owns the dormant cell delta ledger. The Terrain canonical adapter
encodes/validates owned foliage semantics at that boundary but creates no second save
service, file, spill store, world ledger or autosave policy.

### 2. Baked placement is deterministic content, not runtime simulation

Terrain Cook evaluates foliage definitions and placement rules against exact canonical
Terrain source/dependency revisions. The cook fingerprint includes at least:

- dataset/tile/cluster/type/rule identities and schema versions;
- exact height/layer/hole/density/exclusion/spline dependency digests;
- placement algorithm and deterministic PRNG versions;
- authored seed, coordinate/quantization profile and finite placement limits;
- collision/render/nav neutral output schemas when emitted; and
- generic target, tier, toolchain and artifact-envelope identity.

Random samples use a specified versioned generator seeded by a domain-separated canonical
tuple, never process RNG, wall time, pointer values, thread IDs or job order. Inputs are
canonically sorted. Geometric predicates either use defined integer/fixed-point math or
explicit finite quantization and tie-breaking before they affect acceptance/order. Locale,
filesystem enumeration, worker count and floating-point contraction defaults cannot
change output bytes.

The result is a canonically ordered bounded set of stable baked `FoliageInstanceId`
records inside immutable cluster artifacts. Each records its foliage type, transform,
placement provenance/signature and required neutral consumer descriptors. Same complete
inputs emit byte-identical instance identity/order/data. Oversized inputs fail cook or use
an explicitly authored/cooked lower-density variant; cook never truncates by job finish,
camera distance or available host memory.

TRF-004.2 freezes exact rule fields, PRNG, coordinate quantization, overlap/tie policy and
definition schemas. Until then, implementations cannot publish an ad hoc permanent format
under this ADR.

### 3. Baked, runtime and persistent identities are not interchangeable

The identity families are distinct:

```cpp
struct FoliageInstanceId;             // stable baked identity in one compatible base
struct RuntimeFoliageInstanceHandle { // process-local active overlay reference
    FoliageRuntimeSlot slot;
    FoliageRuntimeGeneration generation;
};
struct PersistentFoliageMutationId;   // stable canonical delta identity
struct FoliageMutationRevision;       // optimistic concurrency for active state
```

A baked removal/update delta references the exact compatible world/dataset/base revision
and `FoliageInstanceId`. A durable spawn carries `PersistentFoliageMutationId`, stable
cell/world ownership, definition/archetype identity and canonical transform/properties.
Its active runtime handle, cluster slot, RenderObjectId, GPU offset, Physics body and
Navigation handle are derived and never persist.

Ephemeral spawns use runtime handles and an explicit owner/lifetime. They do not receive a
durable identity accidentally. Promotion to durable state is a separate authorized,
revision-checked command that creates a canonical record; it is not achieved by changing
a flag on a live buffer element.

Exact encodings remain TRF-001.2 work. IDs are nonzero, typed, generation-safe where
runtime-local, bounded on input and never derived from paths, array positions or native
handles.

### 4. Every runtime change is one transactional command

The command boundary is equivalent to:

```cpp
struct FoliageMutationCommand {
    TerrainRuntimeHandle terrain;
    TerrainContentRevision content;
    FoliageMutationRevision expectedMutation;
    FoliageMutationCommandId command;
    FoliageMutationKind kind;
    FoliageMutationTarget target;
    FoliageMutationPayload payload;
    FoliageLifetimePolicy lifetime;
    FoliageOverflowPolicy overflow;
    GameplayAuthorityContext authority;
};
```

Kinds include bounded Spawn, Remove and Update operations. Targets distinguish baked,
ephemeral and durable spawned identity. The command validates session/world/cell,
Terrain/content/mutation revisions, authority, definition, finite transform/scale, layer/
slope/exclusion policy, collision/navigation requirements, capacity, lifetime, persistent
scope and duplicate command identity before changing live state.

The owner builds a detached overlay/delta candidate, prepares every required dependent
consumer under the aggregate lifecycle, then atomically publishes one new mutation
revision at the owner safe point. Failure or cancellation before publication releases
candidate state and leaves the active overlay/canonical delta unchanged. A stale retry
cannot apply twice; terminal results remain queryable through bounded operation/command
state. Cancellation after commit is `TooLate`; reversal is a new authorized command.

No public command accepts a pointer to instance storage, native handle, mutable vector,
filesystem path or arbitrary unbounded callback. Gameplay and tools cannot edit baked
cluster bytes or the Persistent World ledger directly.

### 5. Runtime lifetimes are explicit and owner-scoped

`FoliageLifetimePolicy` distinguishes at least:

- `CellBoundEphemeral`: valid only while the exact cell generation is Active; cancelled/
  removed during its aggregate retirement;
- `SessionEphemeral`: owned by a named gameplay/session scope and reprojected across cell
  residency when policy admits, but excluded from durable saves;
- `OwnerBoundEphemeral`: removed when its generation-checked owner scope closes; and
- `DurableWorldDelta`: server/local-authority-approved canonical spawn/update/tombstone
  represented by Runtime Save/Persistent World.

Session/owner-bound overlays outside an Active cell require bounded dormant in-memory
ownership explicitly charged to their session owner; being off-cell does not authorize a
hidden Terrain global store. Products that cannot support that cost must reject the
lifetime at admission rather than silently convert it to cell-bound or durable state.

Wall-clock expiry is not deterministic simulation truth. Timed ephemeral lifetimes use a
declared simulation clock/tick and restore policy; durable timers are persisted only if a
canonical schema explicitly owns their semantic time basis.

### 6. Durable changes are base-relative canonical deltas

Durable foliage state stores only semantic differences from the compatible baked base:

- a baked removal is a tombstone for one stable `FoliageInstanceId`;
- a baked update records the explicitly mutable canonical fields and expected base;
- a durable spawn records stable mutation ID, foliage definition/archetype, ownership,
  transform and product-approved semantic fields; and
- removal of a durable spawn records/removes it according to the versioned delta schema,
  never by editing cooked artifacts.

The Terrain adapter defines deterministic canonical ordering/encoding, validation,
maximum records/bytes and migration hooks. Runtime Save owns the aggregate participant
registry, capture epoch, restore transaction, archive and compatibility result. For cell-
scoped state, Persistent World owns the active/dormant representation switch and ensures
one authoritative copy exists at the capture cut.

Before eviction removes the last active copy of dirty durable foliage, Terrain publishes
its immutable delta root into the Persistent World ledger at the registered owner safe
point. The handoff is an explicit retirement-DAG edge. Failure or capacity denial blocks
retirement and keeps source state/resources charged; it cannot convert the delta to
ephemeral state, drop it or trigger an implicit user-slot save.

Save capture includes active committed changes or the ledger record exactly once. It
excludes candidate commands, decoded baked instances, runtime handles, renderer buffers,
Physics bodies, Navigation tiles, jobs, queue state and diagnostics. Restore validates
the compatible base, prepares the ledger/Terrain candidate detached, applies deltas once
before required cell activation and publishes in the aggregate Runtime Save transaction.

### 7. Base replacement requires explicit delta compatibility

A cooked base replacement never clears durable mutations or reapplies them by array
position. The new Terrain candidate resolves each delta through an exact stable identity/
placement signature or a versioned migration/remap supplied by the owning cook/toolchain.

Unchanged exact signatures may retain tombstones/updates. A removed, split, merged,
ambiguous or meaning-changed base instance requires an explicit deterministic migration
outcome. Missing migration returns typed incompatibility and preserves the old Active
world/save state; it does not resurrect harvested foliage, delete an unrelated new tree
or guess by nearest position/name.

Ephemeral overlays have their separately declared replacement policy. Exact-compatible
session overlays may reproject using stable semantic coordinates/definition; cell-bound
overlays retire with the old cell. A product may explicitly drop ephemeral state during
world replacement, but the result is observable and cannot affect durable deltas.

### 8. Capacity and eviction never invent gameplay policy

Budgets distinguish baked decoded data, active ephemeral instances, durable active deltas,
dormant canonical deltas, render resources, Physics resources and Navigation resources.
Each owner charges/releases its allocation under ADR-137/138 and World Streaming. A
Terrain or renderer cache eviction may discard rebuildable decoded/native state but not
the baked content contract or canonical mutation.

The 1.0 mutation overflow baseline is `Reject`: admission returns a typed capacity error
with no partial state change. An optional eviction/replacement policy is legal only when
the product registers a deterministic policy ID/version, eligible state class, stable
priority/tie order, authority, persistence effect and observability before session start.
It cannot evict baked instances, durable mutations, another owner's state or required
collision/navigation merely because an instance is oldest, farthest or invisible.

Required baked content exceeding the active plan fails cook/activation unless an exact
authored/cooked lower-density variant is declared. Durable state exceeding active or
dormant capacity blocks the mutating command/save/eviction handoff. World Streaming may
evict a cell only after state handoff and consumer retirement; Terrain cannot evict a
cell privately, and cell eviction cannot erase session-persistent/durable meaning.

### 9. Consumer state is derived and joins aggregate readiness

Terrain owns logical foliage membership and mutation revision. Render derives candidate/
GPU state under ADR-139. Physics owns colliders/bodies; Navigation owns installed query
topology. A foliage command declares which consumers are required by product/definition.

The mutation becomes Active only after every required participant prepares the same
Terrain/content/mutation generation. Optional consumer absence is represented explicitly;
it is not inferred as success. Removing a baked collider/navigation blocker first
publishes a safe aggregate transition under ADR-108/TRF-005 policy; Terrain cannot delete
a native body or NavMesh polygon directly.

Consumer failure/cancellation leaves the prior logical generation and required consumer
state intact. After commit, old render/physics/navigation resources retire under their
owners' readers/fences. Their delayed physical release does not duplicate logical
foliage, and Terrain does not force-free them.

### 10. Multiplayer authority and replication do not create persistence owners

The authoritative server/local gameplay owner validates durable foliage commands. A
client may create explicitly cosmetic ephemeral foliage within local budgets, but cannot
write server canonical deltas or save replicated server truth as local restorable state.

Replication carries stable semantic command/state identity and source revisions; peers
derive local runtime/render/physics handles. Duplicate/out-of-order/stale commands are
rejected or idempotently acknowledged. Client visibility, streaming residency and GPU
culling do not authorize removal or persistence. Server save/restore follows the existing
Runtime Save quiescence and replication-incarnation fencing contract.

### 11. Shutdown preserves owned truth

Shutdown closes command admission, cancels detached candidates, fences late generation
evidence, publishes any required dirty durable roots before allowing cell/session
retirement, and asks Render/Physics/Navigation to release exact generations. Runtime Save
and ledger leases outlive encoding/spill work; Terrain snapshot/consumer dependencies
outlive their readers.

An ephemeral state class follows its declared owner shutdown. A durable delta cannot be
dropped merely because bounded shutdown reaches a deadline. The host reports typed
incomplete persistence/retirement and retains unsafe-to-free state; it never force-frees
referenced resources or reports a successful durable commit without owned canonical data.

### 12. Errors and observability preserve classification

Typed errors retain safe context such as Terrain/content/mutation/base revision, cell,
foliage type, baked/runtime/persistent identity class, command, authority, lifetime,
policy, capacity and nested consumer/save cause. Stable categories include:

```text
terrain.foliage.placement_invalid
terrain.foliage.command_stale
terrain.foliage.authority_denied
terrain.foliage.identity_kind_mismatch
terrain.foliage.capacity_exceeded
terrain.foliage.overflow_policy_invalid
terrain.foliage.persistence_handoff_failed
terrain.foliage.base_delta_incompatible
terrain.foliage.consumer_prepare_failed
terrain.foliage.retirement_incomplete
```

Metrics expose bounded aggregate baked/ephemeral/durable counts, placement/cook results,
mutation rate/rejection, per-state capacity, dormant-delta bytes, handoff latency/failure,
consumer readiness and retirement depth. Per-instance IDs, positions, asset paths and
native handles are not metric dimensions. Detailed records require a bounded authorized
diagnostic snapshot and never change eviction or persistence decisions.

### 13. Verification proves determinism, ownership and no-loss eviction

Required coverage includes:

- cross-worker-count/platform-locale/order deterministic placement golden fixtures,
  versioned PRNG/quantization/tie rules and cook-key invalidation for every semantic input;
- baked/runtime/persistent identity non-interchangeability, duplicate command idempotency,
  optimistic revision rejection and invalid target/lifetime/authority combinations;
- Spawn/Remove/Update failure/cancellation at validation, consumer prepare, canonical
  delta handoff, aggregate commit and retirement with the prior generation unchanged;
- explicit ephemeral lifetimes, cell/session/owner closure, simulation-time expiry and no
  accidental slot persistence or wall-clock causality;
- baked tombstone, durable spawn/update/removal canonical fixtures, single ownership at
  active-to-dormant handoff and one-copy SaveCaptureEpoch behavior;
- cell eviction with clean/dirty ephemeral/durable combinations, ledger capacity/spill
  failure and proof that durable state is never lost or implicitly user-saved;
- base replacement exact reuse, stable remap, missing/ambiguous migration and rollback
  without resurrection or nearest-position matching;
- overflow baseline rejection and every registered deterministic optional policy, proving
  no implicit oldest/furthest/visibility-based loss and no baked/durable victim;
- Render/Physics/Navigation required/optional preparation, failure, safe removal and
  lease/fence retirement without native state entering Terrain/Save data;
- save/restore for active, unloaded and evicting cells with candidate/transient/native
  exclusion and apply-exactly-once activation;
- standalone/server/client/PIE/headless matrices with client cosmetic isolation and no
  duplicate persistence authority; and
- repeated shutdown, late callbacks and deadline expiry preserving canonical dirty state
  and referenced resources.

## Consequences

- Cooked foliage remains a reproducible immutable base rather than a mutable runtime/save
  container.
- TerrainRuntime owns active logical overlays while Runtime Save/Persistent World remains
  the single durable transaction and dormant-delta authority.
- Products must declare runtime lifetime and durability before mutation; “dynamic” no
  longer ambiguously means persistent.
- Cell eviction and memory pressure cannot silently erase harvested/planted state or
  choose gameplay victims from render distance/age.
- Save data is compact base-relative semantic deltas and excludes decoded/native buffers,
  but base upgrades require explicit compatibility/remap policy.
- Durable mutations may block cell retirement when the bounded ledger cannot accept them;
  this cost makes the no-data-loss invariant visible.
- TRF-004.2 must freeze exact definition, placement, PRNG and quantization details within
  this ownership model.

## Rejected Alternatives

### Treat the cooked foliage instance buffer as mutable runtime truth

Rejected because it destroys deterministic artifact identity, reader safety and base-
relative save semantics. Runtime changes use separate revisioned overlays/deltas.

### Save the complete baked plus dynamic instance buffer

Rejected because it duplicates the immutable base and serializes derived layout/handles.
Save stores only product-approved canonical deltas.

### Let TerrainRuntime own a private durable foliage database

Rejected because Runtime Save/Persistent World already owns aggregate capture, dormant
cell deltas, spill and restore. Terrain contributes semantic validation/encoding only.

### Infer durability because an instance survives cell unload

Rejected because lifetime and persistence are different policies. Durability requires an
authorized canonical command/record before commit.

### Persist ephemeral runtime handles or cluster indices

Rejected because they are process/generation-local and can refer to unrelated replacements.
Durable records use stable typed world/base/mutation identity.

### Re-run random placement at runtime or restore

Rejected because toolchain, ordering or numeric differences could produce a different
world. Runtime loads exact cooked artifacts and applies canonical deltas.

### Seed placement from wall time, thread ID or filesystem order

Rejected because output would not be reproducible or cacheable. Seed derivation and order
are canonical, versioned semantic inputs.

### Drop oldest, farthest or currently invisible foliage under pressure

Rejected because age/distance/visibility are not gameplay or persistence authority and
can erase baked/durable meaning. Baseline overflow rejects atomically.

### Let Terrain evict World Streaming cells

Rejected because Terrain owns only feature-local disposable data. World Streaming owns
global demand, budget, cell commit and eviction after dirty-state handoff.

### Save dirty foliage by forcing a user-slot write on every cell eviction

Rejected because in-session recoverable ledger ownership and durable archive publication
are separate transactions. Eviction transfers to Persistent World; autosave policy stays
with Runtime Save/application.

### Match old durable deltas to the nearest instance after recook

Rejected because it can resurrect or delete unrelated content. Exact identity/signature
or an explicit deterministic migration is required.

### Let Renderer, Physics or Navigation own foliage persistence

Rejected because their resources are derived consumer state. They participate in
readiness/retirement but never become semantic or save authorities.

### Force-free dirty or consumer-referenced state at shutdown timeout

Rejected because it can lose canonical mutations or create use-after-free. Incomplete
shutdown is reported while ownership remains truthful.
