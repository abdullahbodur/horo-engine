# ADR-153: PCG Pure Evaluation, Commit and Generated-Output Ownership

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: PCG evaluation purity, generation-plan provenance, preview isolation, offline/runtime commit, target-owner preparation, generated-set identity, regeneration, cleanup, rollback and retirement
- **Issue**: [PCG-4.1](https://github.com/abdullahbodur/horo-engine/issues/2074)
- **Jira**: [HORO-2028](https://horo-engine.atlassian.net/browse/HORO-2028)
- **Related**: [ADR-008](008-error-model-exception-boundary-and-registry.md), [ADR-010](010-job-waiting-and-operation-store-ownership.md), [ADR-012](012-world-streaming-partition-authority-and-subsystem-boundaries.md), [ADR-018](018-command-registration-permissions-threading-and-packaged-build-policy.md), [ADR-093](093-prefab-override-property-identity-and-delta-operations.md), [ADR-114](114-canonical-runtime-world-persistence-boundary.md), [ADR-140](140-foliage-placement-baked-dynamic-state-and-eviction-ownership.md), [ADR-150](150-pcg-graph-source-cooked-plan-cache-and-runtime-ownership.md), [ADR-151](151-pcg-ownership-authority-tier-and-lifecycle.md), [ADR-152](152-pcg-spatial-input-snapshot-and-node-library-ownership.md)
- **Normative documents**: [Procedural Generation Architecture](../architecture/runtime/procedural-generation-architecture.md), [Scene Runtime](../architecture/runtime/scene-runtime.md), [Editor Document Model](../architecture/editor/editor-document-model.md), [World Streaming Architecture](../architecture/runtime/world-streaming-architecture.md), [Terrain and Foliage Architecture](../architecture/runtime/terrain-and-foliage-architecture.md)

## Context

ADR-150 separates graph source, cooked plans, evaluation intermediates and generated
outputs. ADR-151 makes PCG evaluation externally pure, while ADR-152 defines immutable
spatial inputs. The remaining critical boundary is how a candidate becomes committed
feature state and how regeneration or cleanup can replace only content that the same
generation lineage still owns.

The current architecture says generated objects are tagged with a PCG graph for later
cleanup. A graph-level tag is insufficient: one graph may generate several outputs in
many cells/layers, multiple graph revisions may overlap, and a user may intentionally
adopt or edit a generated object. Scanning by graph ID, name, component shape, folder
or scene hierarchy can delete hand-authored or differently owned content.

Preview and offline bake introduce the same danger. A preview must never touch the
production world, and a bake result cannot become durable simply because evaluation
completed. Runtime generation additionally requires server/product authority and an
aggregate commit across Scene, Terrain/Foliage, Physics, Navigation, Render and
streaming readiness. This decision defines the candidate, provenance, commit receipt,
replacement and cleanup contract.

## Decision

### 1. Evaluation produces values, not mutations

An admitted evaluation consumes one exact cooked-plan lease, immutable input snapshot,
copied parameters, seed, scope, authority epoch and limit profile. It writes only
operation-owned intermediates, diagnostics and one immutable bounded
`PCGGenerationCandidate`.

```cpp
struct PCGGenerationCandidate {
    PCGEvaluationId evaluation;
    PCGRuntimeGeneration runtime;
    PCGPlanContentIdentity plan;
    PCGSpatialInputFingerprint inputs;
    PCGGenerationScope scope;
    PCGGenerationLineageId lineage;
    PCGExpectedTargetRevisions expectedTargets;
    PCGAuthorityEpoch authority;
    BoundedArray<PCGGeneratedSetCandidate> sets;
    PCGCandidateCostEnvelope costs;
    PCGCandidateDigest digest;
    BoundedPCGDiagnostics diagnostics;
};
```

The candidate contains typed Horo output descriptions and stable provenance. It does
not contain live entities/components, target-container iterators, native handles,
service locators, callbacks, filesystem paths or commands already submitted to target
owners. Completion changes no document, asset, scene, world, cell or provider state.

Workers cannot publish the candidate publicly. The PCG owner validates the exact
operation, plan/input/runtime generations, deterministic canonical ordering, complete
cost and cancellation state before exposing `CandidateReady` to the coordinator.

### 2. Generated-set and object identity are stable and scoped

Every output belongs to a `PCGGeneratedSetId` derived from a versioned namespace over:

- stable graph asset identity and output declaration identity;
- explicit generation scope such as document preview, scene, streaming cell or runtime
  overlay layer;
- target-owner kind and output semantic kind; and
- product-defined authority/layer namespace.

The set ID identifies a lineage, not one result revision. Each evaluation creates a
monotonic/unique `PCGGeneratedSetRevision` plus exact plan/input/seed/content
fingerprints. Individual proposed values have `PCGGeneratedObjectId` derived from the
set, stable output node/slot, stable source sample identity and versioned deterministic
ordinal policy. Display names, array positions, worker completion and target-native IDs
are never identity.

An object ID collision with different canonical content, kind, source provenance or
target owner is a typed hard failure. Implementations cannot resolve collisions by
arrival order, suffixing a name or overwriting an existing object.

### 3. Target owners own prepared and committed state

The application coordinator expands a candidate into target-specific preparation
requests. Each target owner validates exact identity, expected owner revision,
capability, schema, dependencies, authority and reserved cost, then creates detached
prepared state through its normal contract.

| Output kind | Committed-state owner |
|---|---|
| Entities, components, hierarchy and transforms | RuntimeScene/document scene model |
| Terrain/foliage placement or overlays | Terrain/Foliage owner |
| Solver shapes/bodies | Physics |
| Navigation topology/source updates | Navigation |
| GPU resources and render instances | Render |
| Cell residency/durable dormant records | World Streaming/Persistent World |
| Gameplay semantic objects | Product gameplay owner under explicit authority |

PCG owns only the candidate and provenance until retirement. It never stores a second
mutable mirror of committed objects or uses target handles as its truth. Target owners
return opaque generation-scoped preparation handles and readiness/retirement receipts,
not mutable references.

Required owners must prepare successfully before commit. Optional omission/fallback is
legal only when the cooked plan and product policy declare it and the resulting selected
output-plan identity is explicit. Failure cannot be translated into a partial success.

### 4. One aggregate transaction publishes generated outputs

The host coordinator owns the transaction and its state machine:

```text
CandidateReady
  -> Revalidating
  -> Reserved
  -> PreparingTargets
  -> ReadyToCommit
  -> Committed
  -> RetiringCandidate
  -> Retired
```

Before preparation it revalidates scene/world/cell, graph/plan/input, authority epoch,
expected target revisions, currentness policy and complete peak/overlap reservation.
Immediately before commit it revalidates the applicable fields again.

All required owners publish their prepared roots at the one declared owner-safe
aggregate boundary, or none publish. The no-fail section contains only prevalidated
root/index swaps and receipt publication; allocation, decode, native creation and
potentially failing validation happen during preparation.

On success the coordinator publishes one immutable `PCGGenerationCommitReceipt`
binding transaction, lineage/set revisions, exact target-owner revisions/receipts,
candidate digest, authority epoch and commit tick/document revision. The receipt proves
what was accepted; it does not transfer target state ownership back to PCG.

### 5. Target owners store exact provenance beside generated state

Each owner that commits a generated object stores a bounded typed provenance record in
its canonical metadata/index:

```cpp
struct PCGCommittedProvenance {
    PCGGenerationLineageId lineage;
    PCGGeneratedSetId set;
    PCGGeneratedSetRevision setRevision;
    PCGGeneratedObjectId object;
    PCGPlanContentIdentity plan;
    PCGSourceProvenance source;
    PCGOwnershipGeneration ownership;
    PCGGenerationLayer layer;
};
```

The target owner is authoritative for whether that object still exists and is still
PCG-managed. PCG may keep immutable receipts for operation status/diagnostics, but a
receipt is not permission to delete or replace current owner state without an exact
owner query/transaction.

Provenance is typed metadata, not a string tag. It cannot be inferred from name,
folder, parent, component set, asset path, graph ID alone or proximity to other output.
Owner snapshots expose bounded immutable provenance queries needed for regeneration;
they do not expose mutable containers.

### 6. Preview owns an isolated disposable target world

Editor preview evaluates the same cooked plan and immutable inputs but routes target
preparation into a distinct `PCGPreviewSession`. The preview host owns separate Scene,
Terrain/Foliage, Physics, Navigation and Render preview roots as applicable, with
distinct world/generation IDs, budgets, clocks and persistence/replication-disabled
policy.

Preview overlays, point clouds, diagnostics and prepared objects cannot enter the
production RuntimeScene, streaming ledger, save state, network capture or asset current
generation. The editor panel owns only presentation state and a lease to immutable
preview results; closing/restarting the panel is not itself proof that preview workers
or target resources have retired.

Accepting a preview starts a new explicit bake/document or runtime-authority operation
against current revisions. It never promotes preview native objects or swaps the
preview root into production. Failed, cancelled or stale preview replacement preserves
the last-good leased preview marked with its exact source revision or clears it by
policy; it cannot be presented as current.

### 7. Offline bake is a document/asset transaction

Offline evaluation returns a detached canonical bake candidate. The editor/document
owner revalidates document/source/dependency revision, capability, user command and
complete history/storage costs. Target document/asset owners prepare typed semantic
patches; native preview/runtime state is excluded.

Only an explicit `CommitPCGBake` operation creates one new document/asset revision,
history record and generated-set provenance index through the ordinary atomic
document/source publication boundary. Evaluation or preview success does not dirty,
save, publish or change undo history.

Undo/redo stores exact semantic before/after patches or immutable content leases. It
does not rerun PCG. Undo of a bake affects only objects whose exact ownership generation
belongs to that committed transaction; hand-authored and transferred objects remain.
Saving the document/asset and cooking its runtime form remain distinct operations.

### 8. Runtime commit requires current product/server authority

Runtime candidates carry the captured gameplay/server authority epoch, scene/world/cell
generation and expected owner revisions. The coordinator revalidates all of them at
commit. In multiplayer, only server-authoritative output may enter replicated semantic
state unless a separate contract identifies an isolated cosmetic client layer.

Clients may evaluate preview/prediction candidates under product policy, but cannot
commit them as server truth. Network replication captures each target owner's canonical
committed state and provenance through that owner's adapter; it does not replicate PCG
worker state, intermediates or native preparation handles.

Save/restore follows the same rule. Target owners serialize the minimal canonical
generated state/provenance required by product policy. Restore reconstructs target
owner roots against compatible content and may schedule a new PCG regeneration only
as an explicit policy operation; loading never replays an old evaluator task.

### 9. Regeneration is compare/prepare/replace, not delete-then-spawn

Regeneration captures the current target-owner provenance for one exact generated set
and expected ownership generation, evaluates a new desired set, and computes a typed
three-way plan:

- create desired IDs that do not exist;
- update/replace IDs still owned by the expected lineage/generation;
- retain IDs whose canonical desired content and complete dependency identity match;
- retire previously generated IDs still owned by that exact set/generation; and
- report conflicts for IDs whose ownership/content changed or is ambiguous.

The plan is prepared beside the old state and committed atomically. It never first
clears old output. Evaluation/preparation failure, cancellation, stale input/authority,
conflict or reservation denial preserves the complete prior committed generation.

Target-specific dependency closure is included: replacing a Scene placement may also
require new Render/Physics/Navigation preparation. PCG-5.1 specializes cross-system
readiness, but cannot weaken the exact ownership checks defined here.

### 10. Cleanup requires an exact ownership generation

Cleanup is an explicit target-owner transaction naming lineage, generated set, expected
set revision/ownership generation, scope/layer and expected target revision. Owners
delete only currently existing objects whose canonical provenance matches every field.

Cleanup never scans by graph ID, name prefix, tag string, folder, hierarchy, component
shape, source asset or spatial bounds. It does not delete:

- hand-authored content;
- output from a different graph/output set, scope, cell, layer or revision;
- an object adopted/transferred to another owner;
- an object the user replaced while preserving its display name; or
- a target-owned dependent value not covered by the prepared cleanup transaction.

Missing already-retired IDs are idempotent. A same-ID provenance mismatch is a
conflict, not success and not permission to overwrite/delete. Required conflict aborts
the aggregate cleanup; an explicitly optional conflict is reported in the commit
receipt and cannot be silently counted as cleaned.

### 11. Ownership transfer is explicit and irreversible by old cleanup

An editor/product command may convert a generated object into hand-authored or another
feature-owned state. The target owner validates authority and expected provenance,
creates the new canonical ownership generation, removes/archives PCG-managed provenance
as defined by its schema and publishes one atomic owner revision.

PCG does not initiate transfer automatically when a property changes. Product policy
may instead reject direct edits, record an override while retaining generated ownership,
or require explicit `AdoptAsAuthored`; the selected policy is typed and visible. Any
transfer advances ownership generation, so receipts and cleanup plans captured before
it become stale and cannot delete the adopted object.

Transferring one object does not implicitly transfer its siblings or target dependencies.
The owner transaction closes the exact affected dependency set and either transfers it
coherently or rejects the command.

### 12. Rollback and post-commit correction have separate semantics

Before aggregate commit, cancellation/failure retires prepared target candidates in
reverse dependency order. Each owner acknowledges retirement before reservations,
provider/module leases or candidate provenance release. No compensating public event is
needed because authoritative state did not change.

After commit, rollback is a new revisioned owner transaction using the commit receipt
and current ownership generations. It cannot restore cached pointers or unpublish an
observed revision invisibly. If another owner/user changed an object, exact conflicts
are reported and product recovery decides the next transaction.

Notifications are emitted only after commit and carry commit/owner revisions. Consumers
cannot observe `ReadyToCommit` or use worker completion as a lifecycle event. Duplicate
commit/cancel/cleanup messages are idempotent by transaction identity.

### 13. Replacement and shutdown drain every owner acknowledgement

A new graph/plan/input/runtime generation creates a detached candidate and never edits
old output in place. Current output stays active until regeneration's aggregate commit.
Old evaluation/candidate/preview/target preparation leases remain valid until the last
worker, completion and owner retirement acknowledgement drains.

Shutdown closes evaluate/preview/bake/runtime-commit/cleanup admission, invalidates the
PCG runtime generation, cancels task groups and drains completions. It rolls back
uncommitted target preparations, destroys preview sessions through their target owners,
then releases candidate/intermediate/plan/input/provider/module leases.

Committed target state remains owned by its target subsystem and follows product world/
document shutdown. Destroying PCG does not delete generated content by graph scan.
Explicit cleanup, scene/document destruction or owner restore handles it through
canonical provenance. A shutdown deadline reports typed incomplete work and never
force-frees storage reachable by workers or owners.

### 14. Contract tests protect commit and cleanup boundaries

Implementation must add deterministic automated coverage for:

- evaluation and preview success/failure/cancellation with zero authoritative mutation;
- stable set/object identity, collision rejection and canonical candidate ordering;
- target preparation success, every-owner failure, aggregate commit and complete
  pre-commit rollback;
- authority/scene/world/cell/input/target revision changes before commit;
- exact provenance storage and rejection of string/name/folder/tag inference;
- offline bake commit, dirty/history behavior and undo/redo without reevaluation;
- runtime standalone/server/client authority and save/replication owner boundaries;
- regeneration create/update/retain/retire/conflict sets and old-state preservation on
  failure;
- cleanup idempotency and proof that hand-authored, transferred, differently scoped and
  differently owned content survives;
- ownership transfer racing stale regeneration/cleanup;
- replacement and shutdown with evaluations, preview sessions and prepared targets in
  flight; and
- exactly-once receipts/notifications plus complete provider/module/owner lease
  retirement.

Tests use deterministic scheduling and fault injection at every validation, allocation,
preparation, commit and retirement acknowledgement. They assert canonical owner roots,
provenance, revisions and receipts, not UI text, native handles or timing.

## Consequences

### Positive

- Evaluation and preview cannot mutate authoritative state.
- Cleanup and regeneration can prove exact ownership and cannot delete hand-authored,
  adopted or differently scoped content.
- Offline and runtime outputs use the same candidate/provenance model while retaining
  distinct authority and persistence boundaries.
- Partial target preparation stays invisible and is completely retired on failure.
- Target owners remain the sole truth for generated feature state.

### Negative

- Every target owner needs typed provenance indexes, preparation APIs and retirement
  receipts.
- Aggregate regeneration requires old/new overlap reservation and conflict handling.
- Stable per-output identity derivation must be versioned and preserved across graph
  schema changes or migrated explicitly.
- User editing of generated content requires a deliberate retain/override/adopt policy.

## Rejected Alternatives

### Spawn or edit target objects directly from evaluation nodes

Rejected because failure/cancellation would expose partial state and bypass target
authority, safe points, budgets, rollback and native-resource lifetime.

### Tag every result with only the graph ID and scan during cleanup

Rejected because graph identity does not distinguish output sets, scopes, layers,
revisions or transferred ownership and can delete unrelated content.

### Delete old output before evaluating/preparing regeneration

Rejected because any later failure would cause visible/data loss and prevent atomic
replacement.

### Make PCG's commit receipt the authoritative object registry

Rejected because target owners know current existence and ownership after edits,
transfer, save/restore and subsystem replacement; a PCG mirror would become stale truth.

### Promote preview objects directly into production

Rejected because preview has different world IDs, policies, budgets and native owners
and may be based on stale/uncommitted document state.

### Treat any user edit as implicit ownership transfer

Rejected because the affected dependency set and undo/save semantics would be
ambiguous; transfer must be an explicit target-owner transaction.
