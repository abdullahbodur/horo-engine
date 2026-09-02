# ADR-154: PCG Cross-System Authority, Readiness and Commit Boundary

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: PCG producer/consumer dependency direction, typed target commands, Terrain/Foliage, World Streaming, Navigation, Prefab/Scene, VFX, networking and persistence authority, readiness generations, receipts, unavailable capability, rollback, replacement and shutdown
- **Issue**: [PCG-5.1](https://github.com/abdullahbodur/horo-engine/issues/2087)
- **Jira**: [HORO-2041](https://horo-engine.atlassian.net/browse/HORO-2041)
- **Related**: [ADR-008](008-error-model-exception-boundary-and-registry.md), [ADR-010](010-job-waiting-and-operation-store-ownership.md), [ADR-012](012-world-streaming-partition-authority-and-subsystem-boundaries.md), [ADR-023](023-world-index-and-cell-format-architecture-decision.md), [ADR-093](093-prefab-override-property-identity-and-delta-operations.md), [ADR-095](095-prefab-cook-boundary-and-artifact-model.md), [ADR-099](099-replication-ownership-authority-and-compatibility.md), [ADR-105](105-navigation-asset-and-scene-ownership-boundary.md), [ADR-114](114-canonical-runtime-world-persistence-boundary.md), [ADR-128](128-vfx-spawn-event-mapping-pooling-and-budget-enforcement.md), [ADR-137](137-terrain-foliage-ownership-data-tier-and-lifecycle.md), [ADR-140](140-foliage-placement-baked-dynamic-state-and-eviction-ownership.md), [ADR-150](150-pcg-graph-source-cooked-plan-cache-and-runtime-ownership.md), [ADR-151](151-pcg-ownership-authority-tier-and-lifecycle.md), [ADR-152](152-pcg-spatial-input-snapshot-and-node-library-ownership.md), [ADR-153](153-pcg-pure-evaluation-commit-and-generated-output-ownership.md)
- **Normative documents**: [Procedural Generation Architecture](../architecture/runtime/procedural-generation-architecture.md), [System Design](../architecture/foundation/system-design.md), [Terrain and Foliage Architecture](../architecture/runtime/terrain-and-foliage-architecture.md), [World Streaming Architecture](../architecture/runtime/world-streaming-architecture.md), [Navigation and AI Architecture](../architecture/runtime/navigation-and-ai-architecture.md), [Scene Runtime](../architecture/runtime/scene-runtime.md), [VFX and Particles Architecture](../architecture/runtime/vfx-and-particles-architecture.md), [Multiplayer Replication Architecture](../architecture/runtime/multiplayer-replication-architecture.md), [Save Game and Persistence](../architecture/runtime/save-game-and-persistence.md)

## Context

PCG intentionally spans existing feature owners. It can propose terrain/foliage
placements, prefab-backed Scene entities, navigation-affecting geometry and committed
presentation facts, while runtime generation is scoped by World Streaming and may be
saved or replicated. PCG-1.1 through PCG-4.1 establish pure evaluation, immutable
inputs and target-owned output provenance, but do not freeze the integration direction,
readiness receipt or aggregate barrier for these concrete systems.

Without a cross-system decision, PCG could create a second global streaming scheduler,
mint foliage/entity identities, write GPU buffers, mutate NavMesh, spawn VFX before
semantic commit, or serialize evaluator intermediates. Conversely, target systems could
call back into a live evaluator while committing and create circular ownership. A
provider reporting “ready” without exact generation/cost/retirement evidence could let
the aggregate commit mix old Scene state, new foliage data and stale navigation.

Terrain/Foliage is especially sensitive: it already owns dataset/type/instance
identities, cook artifacts, logical runtime state, render extraction and feature-local
eviction under World Streaming. PCG can propose placements but must not duplicate any
of those authorities. This ADR specializes the PCG integration boundary without
revising the target subsystem decisions.

## Decision

### 1. Integration adapters depend on both public contracts

PCG Core/API remains backend-neutral and does not depend on Terrain/Foliage, World
Streaming, Navigation, Prefab/Scene, VFX, NetworkRuntime, Runtime Save or their concrete
implementations. Each integration is a narrow adapter target that depends on the PCG
public candidate/receipt contract and the target owner's public command/snapshot
contract. The application composition root constructs and connects adapters explicitly.

```text
PCG Runtime/API <--- pcg-target adapter ---> Target public API/runtime owner
                         ^
                         |
                application coordinator
```

Adapters translate immutable typed values and identities. They own no canonical graph,
evaluation, target state or global transaction. They never discover services, select
providers, inspect service locators or register by static initialization. Descriptor
construction/validation is inert; activation occurs only at the host boundary.

PCG cannot retain target-native handles or target-owned mutable containers. Target
owners cannot invoke graph nodes or synchronously re-enter evaluation from prepare or
commit. Cross-system notifications occur only after owner commit.

### 2. One coordinator owns the aggregate transaction

The application/runtime transaction coordinator is the sole authority for admitting,
preparing, committing and retiring a multi-owner PCG transaction. It captures:

```cpp
struct PCGAggregateTransactionPlan {
    PCGTransactionId transaction;
    PCGRuntimeGeneration pcg;
    PCGGenerationLineageId lineage;
    PCGCandidateDigest candidate;
    PCGAuthorityEpoch authority;
    SceneRuntimeId scene;
    WorldGeneration world;
    PartitionEpoch partition;
    PCGGenerationScope scope;
    BoundedArray<PCGTargetPlanEntry> targets;
    PCGAggregateCostEnvelope peakCosts;
};
```

```cpp
struct PCGTargetPlanEntry {
    PCGTargetOwnerId owner;
    PCGTargetOwnerGeneration ownerGeneration;
    PCGTargetCapabilityGeneration capabilityGeneration;
    PCGTargetRequirementPolicy requirement;
    PCGTargetCommandKind kind;
    BoundedArray<PCGTargetOwnerId> dependencies;
    PCGTargetCostEnvelope costs;
    PCGTargetCommitPhase phase;
};
```

Each target entry identifies one registered owner/adapter, exact expected owner and
capability generations, required/optional policy, typed command kind, dependencies,
complete prepare/resident/overlap/retirement costs and legal commit phase. The plan has
one canonical dependency order and contains no callbacks or mutable target refs.

Admission closes the full dependency graph and reserves all owner ledgers before any
preparation. Unknown cost, owner, dependency, phase or capability is a failure. PCG
evaluation order and adapter registration order never decide target preparation or
commit order.

### 3. Readiness is exact generation-scoped evidence

Each target owner returns a move-only opaque preparation handle plus an immutable
receipt equivalent to:

```cpp
struct PCGTargetReadinessReceipt {
    PCGTransactionId transaction;
    PCGTargetOwnerId owner;
    PCGTargetCapabilityGeneration capability;
    PCGTargetExpectedGeneration expected;
    PCGPreparedGeneration prepared;
    PCGTargetReadiness readiness;
    PCGTargetCostReceipt costs;
    PCGTargetDependencyFingerprint dependencies;
    PCGTargetPreparedDigest digest;
    PCGTargetDiagnosticCode diagnostic;
};
```

`readiness` distinguishes `PreparedRequired`, `PreparedOptional`, `Unavailable`,
`Unsupported`, `Stale`, `Failed` and `Retired`. It is never a single boolean. The
receipt proves preparation only for the named transaction, owner, generations,
dependencies and reservation. It cannot be reused after replacement or treated as the
target's canonical state.
For `Failed`, `Unsupported` or `Stale`, `diagnostic` is mandatory bounded typed evidence
that identifies the failed predicate/class; successful readiness uses `None`. It is
diagnostic only and cannot select fallback or mutate policy.

The coordinator accepts the aggregate only when every required target has compatible
readiness, all cross-target dependency fingerprints agree and the total receipt costs
fit the captured reservations. It revalidates current owner/capability/authority/world
revisions immediately before the no-fail commit section.

### 4. Commit order follows owner dependency and safe-point rules

Preparation may run concurrently where the closed dependency graph permits, but commit
publishes at one host-defined safe point with deterministic owner order. For the common
runtime path:

1. RuntimeScene prepares structural/entity/component roots and stable bindings.
2. Terrain/Foliage prepares logical placement/cluster state where requested.
3. Physics, Navigation and Render prepare their owner-native/derived resources from
   exact semantic candidates.
4. World Streaming verifies cell generation, aggregate reservation and required
   provider readiness.
5. The coordinator revalidates authority/revisions and performs the bounded no-fail
   root/index swaps.
6. Owners publish commit receipts; post-commit Network/Save/VFX observers see the new
   revisions in their normal phases.

The exact dependency graph may omit owners or add a product owner, but no target can
publish before its semantic dependency is prepared. A commit does not block on I/O,
jobs, allocation, GPU completion or provider callbacks. If an owner cannot guarantee a
no-fail publication primitive, it is not ready.

### 5. Terrain/Foliage authority is preserved completely

PCG may output typed foliage/terrain placement or overlay intent containing PCG
generated-object provenance, stable referenced `TerrainDatasetId`/`FoliageTypeId`,
transforms/attributes, scope/cell and desired semantics. It cannot:

- mint or reuse canonical `FoliageInstanceId`, tile, cluster or dataset revisions;
- edit authored terrain/foliage source or run Terrain cook/import;
- write TerrainRuntime containers or GPU instance buffers;
- create Render/Physics/Navigation native resources for terrain/foliage;
- decide global cell demand, runtime spawn authorization or feature-local eviction; or
- remove foliage without an exact TRF ownership-generation command.

The Terrain/Foliage adapter validates dataset/type/content/mutation/capability and cell
generations, product mutation authority, quotas and spatial invariants. TRF derives/
allocates its canonical instance identities under its own versioned policy, stores the
ADR-153 PCG provenance mapping, prepares logical clusters/overlays and returns its own
receipt. Render consumes later TRF extraction and owns GPU buffers; PCG never sees them.

Offline PCG foliage bake submits a semantic document/source patch to the Terrain/
Foliage document transaction. Runtime PCG submits an authorized runtime placement/
overlay command. These paths do not share mutable state or bypass TRF cook/publication.
TRF alone performs feature-local retirement/eviction inside the slice granted by World
Streaming, preserving stable identity and dirty/authoritative state policy.

### 6. World Streaming owns demand, scope and cell publication

PCG evaluations are scoped to already admitted cell/world generations and consume only
committed spatial coverage. A missing input/output capability may produce a bounded
demand hint, but PCG cannot load, pin, prioritize, commit or evict a cell and cannot
start a private distance/camera scheduler.

World Streaming converts accepted demand into its own request, grants aggregate
reservations and supplies exact `PartitionEpoch`, cell/request generation and required
readiness policy. PCG/target adapters prepare inside that grant. World Streaming admits
the cell aggregate only when all required Scene/TRF/Physics/Navigation/Render receipts
match the same transaction and cell generation.

Cell eviction first closes new PCG admission for the scope and invalidates applicable
current-at-commit candidates. Committed generated target state follows each owner's
durable handoff/retirement contract. PCG evaluation cancellation or receipt deletion is
not proof that a cell or target resource is safe to reclaim.

### 7. Scene and Prefab own entity expansion and identity

A PCG prefab/scene output references an exact validated cooked prefab/asset identity,
binding inputs, placement semantics, generated-object provenance and scope. PCG neither
parses prefab source/cooked payloads nor expands entities/components.

The Scene/Prefab adapter validates artifact schema/dependencies, expected Scene
revision, parent/binding slots, authority, identity collisions and complete expansion
cost. RuntimeScene owns stable entity identities, hierarchy, components and structural
safe-point publication. It stores the mapping from PCG provenance to current entities;
PCG does not retain entity handles as truth.

Replacement/cleanup uses exact Scene ownership generations. A hand-authored entity,
prefab instance owned by another system or PCG result explicitly adopted by the user is
never deleted by graph/asset/name/hierarchy matching. Prefab hot reload and PCG
regeneration remain separate candidates joined only by an explicit coordinator plan.

### 8. Navigation owns topology and query readiness

PCG may consume an ADR-152 immutable Navigation snapshot when the plan declares it and
may produce navigation-affecting geometry/area/source intent. It cannot mutate provider
tiles, overlays, crowds or query topology, call Recast/Detour directly or report a cell
navigation-ready.

After semantic Scene/TRF geometry is prepared, the Navigation adapter validates the
exact source/content/cell/topology/capability generations and prepares its own neutral/
provider artifacts or explicit dynamic overlay through ADR-105/106/108. Its readiness
receipt states the exact topology generation that will publish with the aggregate.

If Navigation is required and unavailable/stale/fails, the aggregate does not commit.
If product policy explicitly marks it optional, the selected output-plan identity and
receipt record the omission; missing navigation is never represented as a ready empty
NavMesh or inferred from successful PCG evaluation.

### 9. VFX observes committed facts and owns presentation

PCG evaluation and target preparation cannot directly spawn VFX, decals, audio or
other presentation. After aggregate commit, the owning semantic subsystem/application
dispatcher may publish a bounded typed committed occurrence carrying transaction,
lineage, owner revision and stable subject identity.

The application-owned VFX adapter maps accepted occurrences to VFX spawn requests under
ADR-128. VFX owns effect instances, pooling, simulation, GPU resources, budgets and
retirement. Cosmetic failure cannot roll back already committed semantic generation,
and VFX completion cannot become PCG readiness or gameplay authority.

Preview VFX remains inside the isolated ADR-153 preview world. It is never dispatched
to production consumers or recorded as a canonical occurrence.

### 10. Network and persistence capture target truth, not PCG work

Runtime Save and NetworkRuntime integrate with each target owner's canonical state and
typed ADR-153 provenance through owner adapters. They do not serialize/replicate graph
worker state, evaluation queues, spatial leases, intermediates, preparation handles,
readiness receipts, target-native objects or the PCG query cache.

Product schema chooses one explicit reconstruction policy per generated set:

- **Materialized**: persist/replicate target-owner canonical state plus provenance;
- **Regenerable**: persist/replicate the exact compatible graph/plan/input/seed/scope
  reconstruction reference and regenerate through a new authorized transaction; or
- **Cosmetic disposable**: intentionally omit state under declared product policy.

Regenerable is legal only when all inputs are durable/reconstructible, the determinism
class is sufficient, the exact plan/node/numeric semantics are compatible and authority
permits regeneration. Failure does not fall back to empty output or a newer graph. A
save/network snapshot is coherent at one target-owner/world revision cut.

In multiplayer, the server is authoritative for semantic commits. Clients apply
target-owner snapshot/delta candidates through normal NetworkRuntime and aggregate
owner publication; they do not rerun PCG unless the protocol/product contract names a
verified deterministic regenerable class and validates the resulting content digest.

### 11. Unavailable capability has explicit required/optional behavior

Every target/provider capability in a plan is `Required` or names one finite optional
alternative selected at cook/admission. Required `Unavailable`, `Unsupported`, `Stale`,
capacity denial or preparation failure aborts the transaction before publication.

An optional omission returns a typed receipt, advances the selected output-plan
identity and remains observable to diagnostics/save/network compatibility. It never
means empty success, “ready,” zero cost or permission for PCG to implement the target
feature itself. Runtime cannot silently substitute a different prefab, foliage type,
NavMesh behavior, renderer path or authoring fallback.

Headless hosts may omit Render/VFX when the plan declares them optional, while still
requiring Scene/TRF/Physics/Navigation semantic owners. `PCGNull` cannot manufacture
target readiness. A target null provider follows its own contract and receipt semantics.

### 12. Failure, rollback and replacement are aggregate

Preparation failure/cancellation/staleness closes the transaction and retires prepared
targets in reverse dependency order. Owners retain handles/resources/reservations until
their retirement receipts arrive. The coordinator releases aggregate reservation and
PCG candidate/input/plan/module leases only after every required acknowledgement.

Before commit, rollback changes no canonical owner state. After commit, correction is
a new revisioned aggregate transaction; an old preparation handle or readiness receipt
cannot undo observed state. Partial post-commit manual deletion is forbidden.

Replacement prepares a complete compatible aggregate beside the active generation.
Old state remains active on failure. At commit, all participating owner roots advance
under one transaction receipt; nonparticipating owners retain explicitly proven
compatible revisions. Old/new roots and native resources remain leased and charged
until frames, queries, save/network captures and owner retirement acknowledgements
drain.

### 13. Shutdown respects target dependency order

Shutdown closes PCG transaction/evaluation admission for the world, invalidates
generations and cancels in-flight operations. The coordinator drains completion queues
and rolls back prepared targets in reverse dependency order: presentation/native
consumers first, then Navigation/Physics/Render as applicable, TRF/Scene semantic
candidates, World Streaming reservations and finally PCG candidates/snapshots/plans.

Committed target state follows each owner's normal scene/world shutdown; destroying
PCG never scans/deletes it. Network/Save capture admission closes at their owner-defined
barrier and existing immutable captures drain their target leases. Modules and adapters
remain loaded until all handles, callbacks and receipts acknowledge retirement.

A deadline reports typed incomplete shutdown and preserves reachable ownership. It does
not detach workers, fabricate receipts, drop reservations or force-release target/native
storage.

### 14. Cross-system contract coverage is mandatory

Implementation must add deterministic automated coverage for:

- dependency direction and inert adapter composition without service discovery/re-entry;
- complete target graph/reservation closure and canonical preparation/commit order;
- exact readiness receipt generation, dependency, cost and stale/duplicate validation;
- every target preparation failure with aggregate zero-publication rollback;
- TRF identity/cook/buffer/spawn/eviction ownership and PCG provenance mapping;
- World Streaming admission, missing coverage/demand hints, cell commit and eviction
  fencing;
- Prefab expansion/Scene identity plus survival of hand-authored/adopted entities;
- Navigation required/optional readiness, topology replacement and unavailable provider;
- VFX only after semantic commit and cosmetic failure isolation;
- materialized, regenerable and cosmetic save/network policies plus compatibility/
  authority failures;
- required versus declared optional unavailable capability with no empty-success path;
- aggregate replacement and post-commit correction boundaries; and
- shutdown with every target phase in flight and no worker, native resource, adapter,
  reservation, receipt or lease surviving its owner.

Tests use deterministic virtual scheduling and bounded fault injection. They assert
typed owner generations, receipts, roots, costs and acknowledgements, not native handles,
registration order or wall-clock timing.

## Consequences

### Positive

- PCG remains a recipe/plan/evaluation producer and cannot become a second owner of
  Scene, TRF, Navigation, Prefab, VFX, network or persistence state.
- Exact readiness generations and receipts prevent mixed-generation aggregate commits.
- Terrain/Foliage retains all identity, cook, buffer, runtime-spawn and eviction
  authority.
- Required/optional absence is explicit and compatible across headless, save and network
  compositions.
- Failures preserve the old complete generation and retirement is ownership-safe.

### Negative

- Each target needs a narrow PCG adapter, preparation API and generation-scoped receipt.
- Aggregate reservations and old/new overlap increase coordination and peak memory.
- Save/network schemas must explicitly choose materialized versus regenerable policy per
  generated set.
- Some nominally independent target preparation must wait for semantic dependency
  candidates to become available.

## Rejected Alternatives

### Put all target integrations inside PCG Runtime

Rejected because it reverses dependencies, exposes concrete/native owners to PCG and
creates a second state/lifecycle authority.

### Let targets call back into graph evaluation during commit

Rejected because it creates re-entrancy, unbounded work and circular ownership inside a
safe-point publication boundary.

### Let PCG own foliage identities and GPU buffers

Rejected because TRF owns canonical instance/cluster state and Render owns GPU resources;
duplicating either would break cook, streaming, eviction and replacement authority.

### Treat a provider's ready boolean as sufficient evidence

Rejected because readiness without transaction, generation, dependency, cost and
retirement identity can be stale or reused across incompatible replacements.

### Re-run PCG implicitly during save load or network apply

Rejected because inputs/semantics/authority may differ; regeneration is legal only
under an explicit compatible deterministic reconstruction policy.

### Commit available targets and add missing targets later

Rejected because Scene, collision, navigation, render and streaming state would expose
mixed generations and rollback could no longer preserve invariants.
