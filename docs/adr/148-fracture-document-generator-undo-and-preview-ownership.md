# ADR-148: Fracture Document, Generator, Undo and Preview Ownership

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: Fracture authoring-document state, typed editor operations, generator transactions, bounded semantic history, source publication, cook separation, isolated preview, cancellation, replacement and shutdown
- **Issue**: [DFR-005.1](https://github.com/abdullahbodur/horo-engine/issues/2029)
- **Jira**: [HORO-1983](https://horo-engine.atlassian.net/browse/HORO-1983)
- **Related**: [ADR-008](008-error-model-exception-boundary-and-registry.md), [ADR-010](010-job-waiting-and-operation-store-ownership.md), [ADR-018](018-command-registration-permissions-threading-and-packaged-build-policy.md), [ADR-056](056-external-editor-ui-boundary.md), [ADR-085](085-physics-shape-authoring-cook-and-runtime-boundary.md), [ADR-121](121-cinematic-editor-document-and-authoring-context.md), [ADR-129](129-vfx-editor-document-live-preview-and-module-authoring.md), [ADR-142](142-terrain-foliage-document-tool-undo-and-preview-ownership.md), [ADR-144](144-destruction-ownership-authority-state-and-runtime-geometry-boundary.md), [ADR-145](145-destruction-source-chunk-geometry-collision-and-cook-ownership.md), [ADR-146](146-destruction-runtime-activation-physics-cleanup-and-rollback.md)
- **Normative documents**: [Editor Document Model](../architecture/editor/editor-document-model.md), [Editor Panel Host](../architecture/editor/editor-panel-host.md), [Destruction and Fracture Architecture](../architecture/runtime/destruction-and-fracture-architecture.md), [Asset Pipeline](../architecture/runtime/asset-pipeline.md), [Project Model](../architecture/editor/project-model.md)

## Context

ADR-145 separates authored fracture source/recipe intent from canonical cooked DFR
geometry and Physics/Render derived products. The Destruction architecture lists import,
Voronoi generation, preview and connectivity visualization, but does not assign the
editable working state, generator transaction, undo storage or preview world to owners.

Without a stricter boundary, an editor panel could mutate a mesh/graph directly, a
worker could publish generated chunks after the document advanced, or undo could rerun
a nondeterministic generator with current settings. A preview that inserts candidate
bodies into the production `PhysicsWorld` could affect gameplay queries, persistence,
streaming or shutdown even if the user cancels the operation.

Generation may produce large geometry and graph candidates. Copying every generated
mesh into every history entry is unbounded, while making generation implicitly
non-undoable breaks the shared Editor Document Model. Saving generated preview bytes as
the active artifact would also bypass Assets' source and cooked-generation publication
authorities.

This ADR specializes the shared document lifecycle. DFR-005.2 owns the exact fracture
asset document/chunk-graph schema, while DFR-002.2/DFR-002.3 own import and generator
algorithm details.

## Decision

### 1. One persistent document owns editable working state

Each editable fracture asset opens as one `FractureAssetDocument` rooted at stable
`AssetId`, `FractureAssetId`, accepted durable source revision and document-session
generation. The document service owns the in-memory working source, typed command
execution, monotonic revision, bounded history, dirty/saved state, validation,
save/autosave/recovery/conflict state and derived generation/cook/preview status.

The working source contains authored intent only:

- normalized source-mesh references and exact accepted dependency revisions;
- fracture algorithm/recipe, deterministic seeds, sites/planes/constraints and tier
  requirements;
- interior surface/material/UV policy;
- stable authoring identities plus chunk/support/connectivity annotations or overrides;
  and
- versioned import/generator settings required to reproduce and validate the source.

It does not contain native Physics shapes/bodies, solver blobs, GPU resources, runtime
entities, live `DestructionWorld` state, cache paths or a second copy of the published
DFR artifact. DFR-005.2 fixes the exact schema without moving these authorities.

Assets owns durable source identity/storage, dependency records, atomic source
publication and cooked artifact/cache/package publication. The open document owns an
unsaved working candidate, not the durable source of record. Successful Assets source
publication advances the document's saved-state identity; generator, preview or cook
success does not.

`EditorPanelHost` owns tab/route/focus/surface lifetime. The fracture editor tab owns
only bounded presentation state such as selection, filters, tree expansion, viewport
camera, gizmo mode and diagnostics layout. One writable session per canonical asset is
admitted; read-only compare/recovery/historical sessions have separate capabilities.

### 2. Editor surfaces emit typed operations and own no source

Toolbar, inspector, graph/tree, viewport gizmos, menus, CLI, MCP and automation all
submit through the same permissioned document operation boundary. UI code translates
intent into fixed-schema values; it never edits document containers, sets dirty flags,
pushes history entries, writes asset files, invokes a cooker directly or mutates preview
runtime objects.

```cpp
struct FractureDocumentOperationHeader {
    DocumentSessionId document;
    FractureDocumentRevision expectedRevision;
    FractureDocumentOperationId operation;
    FractureDocumentOperationKind kind;
    FractureAuthoringCapability capability;
    FractureOperationLimits limits;
};
```

Operation payloads use stable source, recipe, site/plane, chunk-authoring, graph-node,
edge, anchor, material-slot and property identities. They contain bounded semantic
values, never ImGui IDs, tree row indices, selection pointers, native handles, mutable
spans, callbacks, service locators, filesystem paths or arbitrary maps. Display labels
cannot substitute for stable identities.

Every operation validates document/session generation, expected revision, capability,
schema, dependencies, finite numeric values and complete cost before mutation. Invalid,
stale, unauthorized, empty or oversized operations return typed zero-mutation results;
there is no clamp, index guessing or direct-mutation fallback.

### 3. Generators produce detached immutable candidates

An import or procedural fracture command first captures one bounded immutable
`FractureGenerationInputSnapshot` containing the exact document revision, source mesh/
dependency revisions, recipe/settings, deterministic seed, target/tier envelope,
algorithm/schema generation and requested limits. Capturing the snapshot performs no
generation and changes no document state.

The document-owned operation starts a cancellable structured task group. Destruction
authoring/generator workers may normalize import mappings or compute candidate sites,
planes, chunks, interiors, graph/connectivity, collision-input previews and diagnostics.
They read only captured inputs and write only operation-owned reserved storage. They do
not access the live document, selection, runtime scene, production cache or production
Physics/Render state.

```cpp
struct FractureGenerationCandidate {
    FractureGenerationOperationId operation;
    DocumentSessionId document;
    FractureDocumentRevision baseRevision;
    FractureGeneratorGeneration generator;
    FractureInputFingerprint inputs;
    FractureCandidateCost cost;
    ImmutableFractureAuthoringPatch proposedSource;
    ImmutableFracturePreviewProduct preview;
    BoundedFractureDiagnostics diagnostics;
};
```

The candidate is derived operation state, not canonical document state, a published
asset, a cooked artifact or an undo entry. Completion returns to the document owner
boundary. The owner revalidates session, base revision, dependency fingerprints,
generator/schema generation, capability and reservation before exposing the candidate
as `ReadyForReview`. Stale completion is discarded after owned readers drain.

Only an explicit `AcceptGenerationCandidate` operation carrying the candidate's exact
`FractureGenerationOperationId` can apply the proposed authored patch. Acceptance first
closes its full affected identity/property/graph set, reserves
history/source storage and validates invariants, then commits one new document revision.
Reject/cancel drops the candidate. Generator completion alone never dirties, saves or
publishes the document and never replaces production runtime content.

### 4. Document commits are atomic semantic patches

The document executor expands a typed operation into one immutable plan with exact
affected stable fields, nodes, edges, anchors, mappings and dependency invalidations.
For accepted generation/import, the plan uses the candidate's proposed authored patch;
it does not copy transient preview/native products into source.

The owner captures exact semantic before values, applies the operation to detached or
journaled source storage, validates identity uniqueness, graph/source invariants and
limits, and captures exact after values. One no-fail owner-boundary swap publishes the
new working state, monotonic revision, dirty state, one history record and bounded
change summary. Failure/cancellation before the swap changes none of them.

```cpp
struct FractureDocumentHistoryRecord {
    FractureDocumentOperationId operation;
    FractureDocumentRevision baseRevision;
    FractureDocumentRevision committedRevision;
    FractureContentStateId resultingContentState;
    ImmutableFractureAuthoringPatch before;
    ImmutableFractureAuthoringPatch after;
    FractureDependencyInvalidation invalidation;
};
```

History records contain exact authored semantic changes or leases to immutable
content-addressed source sections. They never store full production cooked artifacts,
native resources, live candidates or borrowed document pointers. Compression and
deduplication are deterministic, lossless and accounted. A large replace/import uses a
checkpointed immutable source-section lease whose complete byte cost is reserved before
commit; it cannot silently become non-undoable.

### 5. Undo and redo restore results, never rerun generation

Undo/redo applies the record's exact before/after semantic patch through the same
document validation and atomic publication boundary. It never reruns Voronoi/import,
re-reads source files, uses current generator settings/selection or reuses preview
objects. Random seed and algorithm version in source remain ordinary restored values,
not instructions to synthesize the historical result during undo.

Undo/redo advances the monotonic document revision while restoring a prior content-state
identity. New edits after undo clear redo. Dirty state is
`current_state_identity != saved_state_identity`; it is not inferred from history depth,
generator status or cook/preview success.

History has independent item, byte, immutable-section and operation-size budgets.
Eviction removes only the oldest eligible history records and never changes canonical
working data or releases a section still leased by current/saved/recovery state. When a
new operation cannot reserve its complete history representation, it fails before
mutation or requires a separately authorized checkpoint workflow; normal UI cannot
bypass history with a confirmation checkbox.

### 6. Interaction and high-fidelity previews are isolated

A lightweight `FractureInteractionOverlay` may visualize seed/sites, cut planes,
candidate bounds, graph selection and estimated costs. It is keyed by document/session,
base revision, operation and overlay generation. It owns no canonical geometry, dirty
state, history, save/cook state or runtime object and disappears exactly on cancel,
revision change, tool switch or close.

High-fidelity preview captures one immutable document revision or reviewed generation
candidate and runs the ordinary ADR-145 Destruction Cook schemas in a transient
non-published namespace. The host then activates validated preview artifacts through the
ordinary RuntimeScene, Destruction, Physics and Render contracts in an isolated
`FracturePreviewSession`.

The preview session has distinct scene/world/content generations, budgets, command
authority, clocks, event dispatcher and persistence-disabled policy. Its preview host
composes a dedicated Physics world through normal Physics ownership; neither the editor
UI nor the preview controller owns native bodies/shapes, and no candidate enters the
production `PhysicsWorld`, `DestructionWorld`, World Streaming root, save state,
replication stream or asset current-generation pointer.

Preview may provide play/pause/step/reset, controlled impacts, chunk/support overlays,
collision visualization and diagnostic inspection. These controls submit typed preview
commands. Applying a preview observation is a new explicit document operation; runtime
motion, body transforms, broken sets, generated decals/VFX/audio and simulation results
never write back automatically.

New edits/candidates cancel or stale older preview cook/runtime work. The UI may retain
a leased last-good image/result marked with its source revision, but cannot call it
current. Preview capability absence yields typed unavailable/degraded results according
to an explicit preview profile; it never falls through to production state or invents
placeholder collision.

### 7. Source save, cook publication and preview publication are distinct

Source save captures one immutable validated working revision and asks the Assets-owned
source publication operation to durably and atomically replace the canonical fracture
source. Only its successful exact-revision acknowledgement advances saved state. A later
edit remains dirty even if the older save finishes; a failed/cancelled/stale save cannot
clear dirty.

Production cook consumes an accepted Assets source revision and exact dependencies under
ADR-145. Assets schedules, caches, stages, verifies and atomically publishes the complete
DFR artifact generation; Physics and Render own their separately keyed derived products.
The document observes typed operation/publication status but cannot select `current`,
write cache paths or publish worker outputs.

Preview cook consumes an explicitly captured unsaved revision/candidate under a
transient key. Its outputs cannot enter the production cache/current generation/package,
clear dirty state or satisfy a production readiness check. Saving source does not
publish a cook; production cook success does not save source; preview success does
neither.

Autosave/recovery stores bounded working-source recovery outside canonical source and
does not publish cooked artifacts. External source replacement creates a typed conflict
against dirty working state. Reload/merge/discard is an explicit document operation with
stable identities and schema-aware reconciliation, never last-writer-wins replacement.

### 8. Limits, failure and observability remain explicit

Product policy bounds open fracture documents, source/working bytes, sites/planes,
chunks/nodes/edges, candidate count/bytes, generator jobs/work units/scratch/output,
history items/bytes/checkpoint leases, preview sessions/artifacts/bodies, diagnostics
and simultaneous save/cook operations. Checked arithmetic and reservation occur before
work; generator-discovered output cannot grow past the accepted ceiling.

Typed results distinguish at least `WrongDocument`, `StaleDocumentRevision`,
`SourceDependencyChanged`, `GeneratorUnavailable`, `GeneratorVersionChanged`,
`GenerationLimitExceeded`, `CandidateStale`, `CandidateNotAccepted`,
`HistoryBudgetExceeded`, `SourcePublicationConflict`, `PreviewUnavailable`,
`PreviewStale`, `Cancelled` and `ShutdownInProgress`. Failures identify the owner stage,
operation/session/revision and bounded offending counts without exposing native handles
or arbitrary source data.

Metrics include operation/generator kind and result, source/graph affected counts,
reserved/actual candidate/history bytes, queue and operation latency, stale/cancelled
work, preview preparation/activation and teardown. Stable enums may be dimensions;
asset/document/chunk IDs, paths and user payloads may not. Diagnostics cannot mutate,
accept, save, publish or retry a candidate.

### 9. Close, replacement and shutdown drain owners in order

Close first stops UI/tool operation admission, releases capture, cancels overlays and
document task groups, fences generator completion, cancels transient save/cook requests
where owned and requests preview teardown. Preview presentation retires before its
RuntimeScene mappings, Physics/Render objects, artifacts and task storage. The document
and Assets retain immutable revisions/leases until every reader acknowledges release.

Project/source replacement and shutdown use new generations and the same order. A late
generator, save, cook, preview or consumer receipt cannot attach to a new session sharing
the same AssetId. Duplicate terminal acknowledgements are idempotent. A deadline reports
incomplete close/shutdown and never force-frees a candidate, source section, Physics
body, GPU resource or job scratch still referenced by its owner.

## Compatibility And Follow-Ups

This decision requires existing fracture editor sketches to become presentation over a
persistent `FractureAssetDocument`. Direct mesh/graph mutation, panel-local undo,
generator-to-cache publication and production-world preview are unsupported. Migration
imports existing fracture source into the versioned document schema; it does not keep a
parallel legacy editor/runtime path.

[DFR-005.2](https://horo-engine.atlassian.net/browse/HORO-1982) defines the exact
document and chunk-graph authoring model. DFR-002.2 defines pre-fractured import
validation, DFR-002.3 defines the generator algorithm, and later DFR-005 tickets define
tool UX, preview controls and diagnostics without moving ownership.

## Consequences

### Positive

- UI, CLI, MCP and automation share one typed revision-checked mutation path.
- Generator workers cannot publish stale source or production artifacts.
- Undo restores exact authored results without rerunning heavy/random algorithms.
- Preview fidelity uses normal cook/runtime contracts without touching production
  Physics, persistence or streaming state.
- Saved source, production cook and transient preview expose independent truthful state.

### Costs

- Large generation/import acceptance needs bounded immutable source-section checkpoints
  and explicit peak reservations.
- High-fidelity preview requires a separate scene/world composition and teardown.
- The editor must surface candidate/source/cook/preview revisions distinctly.

### Rejected Alternatives

- **Let panels/tools mutate fracture source directly**: creates parallel state/history
  authorities and bypasses permissions/revision checks.
- **Publish generator output directly as the current artifact**: bypasses source review,
  Assets verification and atomic production generation publication.
- **Store every cooked mesh/body in undo history**: makes history scale with derived and
  native output rather than authored change.
- **Rerun the generator during undo/redo**: algorithm, dependencies, versions or job
  ordering can differ from the accepted result.
- **Preview inside the production world**: leaks candidate state into gameplay queries,
  save/replication, budgets and native lifetime.
- **Treat preview or cook success as save**: confuses derived readiness with durable
  canonical source publication.

## Verification

Required contract and integration coverage includes:

- all editor surfaces producing typed operations with no direct source/dirty/history
  mutation;
- session/capability/revision validation and zero mutation for stale/invalid/oversized
  operations;
- deterministic immutable generator snapshots, bounded task groups and stale completion
  rejection across edits, dependency changes and generator reload;
- accept/reject/cancel of generation candidates and no dirty/publication change on
  completion alone;
- exact semantic apply/undo/redo without generator rerun or production artifact copies,
  including history/checkpoint exhaustion before mutation;
- dirty/saved identity across undo, redo, history eviction, stale save, autosave/recovery
  and external source conflict;
- production source/cook publication versus transient preview namespace isolation;
- preview parity through normal DFR/RuntimeScene/Physics/Render contracts and proof that
  production PhysicsWorld, save, replication and streaming state never changes;
- missing preview capability, stale last-good display, rapid edit/cancel/restart and
  repeated preview teardown; and
- close/replacement/shutdown with generator, save/cook jobs, source leases, preview
  Physics steps and GPU frames in flight.
