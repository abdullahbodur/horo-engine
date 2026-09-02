# ADR-155: PCG Graph Document, Preview, Bake and Undo Ownership

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: PCG graph editor document authority, workspace presentation state, typed commands, semantic history, save/autosave/recovery/conflicts, background-result application, transient cook, isolated preview, explicit bake and shutdown
- **Issue**: [PCG-6.1](https://github.com/abdullahbodur/horo-engine/issues/2092)
- **Jira**: [HORO-2046](https://horo-engine.atlassian.net/browse/HORO-2046)
- **Related**: [ADR-008](008-error-model-exception-boundary-and-registry.md), [ADR-010](010-job-waiting-and-operation-store-ownership.md), [ADR-018](018-command-registration-permissions-threading-and-packaged-build-policy.md), [ADR-056](056-external-editor-ui-boundary.md), [ADR-093](093-prefab-override-property-identity-and-delta-operations.md), [ADR-121](121-cinematic-editor-document-and-authoring-context.md), [ADR-129](129-vfx-editor-document-live-preview-and-module-authoring.md), [ADR-142](142-terrain-foliage-document-tool-undo-and-preview-ownership.md), [ADR-150](150-pcg-graph-source-cooked-plan-cache-and-runtime-ownership.md), [ADR-151](151-pcg-ownership-authority-tier-and-lifecycle.md), [ADR-152](152-pcg-spatial-input-snapshot-and-node-library-ownership.md), [ADR-153](153-pcg-pure-evaluation-commit-and-generated-output-ownership.md), [ADR-154](154-pcg-cross-system-authority-readiness-and-commit-boundary.md)
- **Normative documents**: [Editor Document Model](../architecture/editor/editor-document-model.md), [Editor Panel Host](../architecture/editor/editor-panel-host.md), [Procedural Generation Architecture](../architecture/runtime/procedural-generation-architecture.md), [Asset Pipeline](../architecture/runtime/asset-pipeline.md), [Scene Runtime](../architecture/runtime/scene-runtime.md)

## Context

The PCG editor surface includes a graph, node palette, property inspector, validation,
live preview and bake-to-scene action. The architecture does not yet assign durable
graph state, workspace UI state, preview overlays, background operation state, bake
output and undo history to distinct owners. A widget or graph canvas could therefore
mutate source/scene containers directly, while preview or a late compiler result could
overwrite a newer edit.

PCG's runtime decisions establish one authored graph truth, one cooked-plan path, pure
evaluation, isolated preview targets and exact generated-output provenance. The editor
must specialize the shared Editor Document Model around those contracts. It cannot
invent a second source serializer, preview interpreter, graph-specific undo array or
“bake successful means saved” rule.

Preview and bake intentionally observe similar graph results but have different
authority. Preview is disposable and isolated; bake is an explicit document/asset/
scene transaction with semantic history. This ADR defines those boundaries, including
background work, save/recovery, conflicts, cancellation and shutdown.

## Decision

### 1. One persistent document owns editable graph state

Each editable graph asset opens as one `PCGGraphDocument` rooted at stable `AssetId`/
`PCGGraphAssetId`, accepted durable source revision and a never-reused
`DocumentSessionId`. The document service owns:

- canonical in-memory working graph source and monotonic document revision;
- typed command validation/execution and semantic invariant enforcement;
- bounded undo/redo history and dirty/saved-state identity;
- source validation plus derived cook/preview/bake operation status;
- save, autosave, recovery and external-conflict state; and
- background candidate admission/application against exact revisions.

The working graph contains stable node/pin/edge/exposed-input/output identities, typed
parameters, semantic dependency references, seed policy and authoring metadata allowed
by PCG-2.2. It contains no live Scene objects, runtime plan pointers, evaluation
intermediates, preview native resources, cache paths, jobs, callbacks or service
locators.

Assets owns durable source identity/bytes and atomic publication. The open document owns
an unsaved candidate. Successful source publication advances its saved-state identity;
validation, compilation, preview, bake evaluation or scene commit does not.

### 2. Workspace and panels own presentation only

`EditorWorkspaceController` owns document routing and writable-session uniqueness.
`EditorPanelHost` owns tab placement, visibility, focus, docking and surface lifetime.
The graph canvas, node palette, inspector, diagnostics, preview and output panels own
bounded presentation state such as selection, hover, expanded groups, filters, scroll/
zoom, viewport camera, active tool and popup state.

Presentation uses stable document/node/pin/output identities and immutable document
snapshots. ImGui/widget/tree row IDs, indices, pointers and display labels never become
domain identity. Closing or moving a panel does not close the document, cancel a bake
or release a preview worker unless the owning workflow explicitly requests it.

Reopening an already writable graph focuses its existing session. Read-only historical,
recovery, conflict and comparison routes have distinct capabilities and cannot submit
canonical commands or save over the source.

### 3. All edits use one typed command executor

Graph canvas gestures, inspector changes, palette actions, keyboard commands, menus,
CLI, MCP and automation submit the same closed `PCGDocumentCommand` schemas. The header
contains document/session, expected revision, command identity/kind, capability and
complete limits. Payloads use stable semantic IDs and bounded values, never UI indices,
raw pointers, arbitrary maps or callbacks.

Commands include node/edge/input/output create/remove/replace, typed property changes,
semantic layout/group changes where source-owned, dependency/seed/tier policy changes
and explicit background-candidate acceptance. The document owner validates session,
revision, permissions, node catalog/schema, graph invariants, complete cost and source
compatibility before mutation.

Invalid, stale, unauthorized, empty, oversized or conflicting commands return typed
zero-mutation results carrying a stable `PCGDocumentCommandFailureReason`, the bounded
offending semantic identities/predicate and optional localized-message arguments. Raw
provider strings are diagnostic attachments, never UI policy. UI code cannot edit graph
vectors, set dirty flags, append
history, write files, call the cooker, mutate a preview world or submit a Scene command
directly.

Continuous drag/connect/property interactions use a transient document-owned overlay or
one coalesced transaction. Commit creates exactly one semantic command/history record;
cancel restores the exact prior visible state and creates none.

### 4. Document commits and history are atomic semantic patches

The executor expands a command into an immutable plan closing every affected node,
pin, edge, input, output, property and dependency identity. It reserves the complete
new state/history representation, captures exact semantic before values, applies to
detached/journaled storage, validates the full graph and computes exact after values.

One no-fail owner swap publishes the new working root, monotonic revision, dirty state,
bounded change summary and history record. Failure/cancellation before the swap changes
none. History stores exact reversible semantic patches or leases to immutable
content-addressed source sections, not cooked plans, preview objects, target native
state or evaluator work.

Undo/redo applies stored before/after semantics through the same validation/publication
boundary and advances document revision. It does not rerun graph evaluation, compile a
plan, resample spatial inputs or recreate a prior preview/bake. Dirty state is
`working_state_identity != saved_state_identity`, not history depth or operation status.

History has explicit item/byte/section/operation limits. A command that cannot reserve
its reversible form fails before mutation or enters a separately authorized checkpoint
workflow; normal UI cannot bypass undo guarantees with an implicit large-edit path.

### 5. Save, autosave and recovery preserve one source authority

Save captures one immutable document revision and exact node-catalog/schema/dependency
context, canonicalizes/validates source and asks Assets to atomically publish a new
source revision. The document revalidates session and captured working-state identity
before marking it saved. Edits committed during save remain dirty.

Save failure leaves durable source and saved-state identity unchanged. It does not
publish a cooked generation, clear history or replace preview/baked Scene output.
Cook-after-save is a separately visible policy operation against the published revision.

Autosave stores bounded editor recovery state through the shared recovery service. It
is not the canonical graph asset, an Assets cook-cache entry or a runtime plan.
Recovery opens as a candidate/read-only comparison first and applies only through an
explicit revision-checked document command/save.

External source change compares the document's accepted durable base, working state and
incoming revision. Clean documents may reload through an explicit root replacement;
dirty documents enter conflict state. Merge/reload/keep/save-as decisions are typed
workflows with preview and loss reporting, never silent overwrite by a file watcher.

### 6. Background work captures immutable revisions

Validation, layout analysis, cook, spatial snapshot, preview and bake operations capture
one immutable input tuple naming document/session/revision, canonical source identity,
node catalog, dependencies, target/tier profile, seed and finite limits. They run in
owned cancellable task groups and write only operation-owned candidates.

Workers never read the live document, selection, UI, production Scene or mutable target
containers. Completion returns to the document/workflow owner with operation and input
fingerprints. The owner revalidates session, current revision, catalog/dependencies,
authority and operation supersession before accepting it.

Stale completion may be retained as explicitly historical diagnostics keyed to its
revision or discarded by bounded policy. It cannot update current errors, preview,
bake readiness, saved state or Scene content. A cache hit follows the same completion
and revalidation path; it never completes by re-entering a widget callback.

### 7. Preview is isolated, replaceable and never authoritative

Preview captures one immutable working document revision, compiles it through the
ordinary PCG Cook semantics into an editor-owned transient non-published generation and
evaluates the resulting `CookedPCGPlan` through the ordinary PCG Runtime. It never
interprets graph nodes directly.

The `PCGPreviewSession` owns distinct scene/world/cell/PCG/target generations, budgets,
clocks, authority-disabled policy and exact source/plan/input identity. It may display
point clouds, per-node intermediates, output candidates, diagnostics and high-fidelity
target-owner previews, but cannot mutate the production Scene, document, Assets current
generation, streaming ledger, save state or network replication.

Preview controls submit typed session commands for seed/input selection, play/pause/
step/reset and visualization. They do not edit graph source unless the user invokes a
separate document command. A new document revision or preview request cancels/supersedes
older work. A leased last-good preview may remain visible only when marked with its exact
stale revision; it cannot be labeled current.

Accepting a preview starts a new explicit bake command against current document/target
revisions. Preview native objects and operation intermediates are never promoted into
production.

### 8. Bake is an explicit multi-owner transaction

`BakeToScene` captures an immutable graph document revision, exact spatial input
snapshot, target Scene/document revision, scope/layer, authority/capability and complete
cost. It runs the same transient/published cooked plan and pure evaluator used by
preview/runtime, producing an ADR-153 candidate rather than mutating Scene.

The workflow coordinator prepares exact semantic document/Scene/target-owner patches
and PCG provenance. Immediately before commit it revalidates graph/session revision,
target revisions, authority, input currentness and reservation. Required targets
publish through the ADR-154 aggregate boundary or none publish.

Bake success creates one explicit target document/Scene revision and semantic history
record. It does not save the graph document or convert preview state into source. The
graph may remain dirty independently; the bake receipt records the exact graph working/
source revision used so the UI can truthfully show `BakedCurrent`, `BakedFromUnsaved`,
`BakeStale` or `NeverBaked`.

“Bake and save” is a composed workflow with separately reported graph-save and target-
bake results; it does not pretend two different document authorities committed as one
unless a supported multi-document transaction prepared both.

### 9. Bake undo restores exact target semantics without rerunning PCG

The target document/Scene owner stores the exact before/after semantic patch or
immutable-section leases plus ADR-153 ownership/provenance generations. Undo/redo of a
bake applies those exact values through the target owner's command boundary. It never
reruns the graph, reuses current inputs/seed, resolves a newer plan or copies preview
objects.

Undo removes/reverts only content whose exact ownership generation belongs to the bake
transaction. Hand-authored, adopted, overridden or differently scoped/generated
content survives. A provenance/ownership conflict rejects or enters explicit merge/
resolution policy; it never broadens cleanup by graph ID, name, tag or hierarchy.

Undoing target bake does not undo graph edits, change graph dirty/saved state or restore
an old preview. A deliberate operation changing graph and target documents must use a
staged multi-document application transaction with one coordinated history policy;
two UI callbacks cannot simulate atomicity.

### 10. Create, duplicate and destructive workflows are transient routes

Create graph, template selection, import, duplicate, Save As, delete and conflict
resolution are transient modal/routes owned by the application workflow host. Creation
validates project boundary, name/path, permissions, source-control state, schema,
template dependencies and asset collision, then atomically publishes one valid graph
asset before opening its document.

Cancel/failure leaves no partial source, registry record, document, tab, recovery file,
cook generation or preview session. Rename/move/delete are Assets/Project operations
coordinated with open documents and active plan/preview/runtime leases, never raw graph
tab or filesystem actions.

Deleting a graph asset does not scan/delete baked or runtime output. Those target-owned
objects follow explicit provenance-aware cleanup/product policy and may intentionally
outlive authoring source in a packaged build.

### 11. Close, replacement and shutdown drain owned operations

Closing a dirty graph uses the shared save/discard/cancel workflow. Discard drops only
the unsaved working candidate after preview/cook/bake work has been cancelled and
drained; it does not delete durable source or target-owned baked output. Closing a tab
does not implicitly discard a document.

Document replacement invalidates its session generation, closes command/background
admission, cancels task groups and detaches presentation. Workers/completions retain
source/catalog/dependency/plan/input leases until joined and owner queues drain.
Preview target owners retire in reverse dependency order and acknowledge before the
session releases its roots.

Editor shutdown closes new commands/save/preview/bake admission, resolves or records
dirty recovery by explicit policy, cancels/joins work without owner locks, rolls back
uncommitted bake targets, retires preview worlds, then releases documents/panels/catalog
leases. A deadline reports typed incomplete shutdown and never detaches workers,
force-frees reachable data or silently claims a save/bake succeeded.

### 12. Contract tests cover document and workflow boundaries

Implementation must add deterministic automated coverage for:

- writable-session uniqueness and strict document/workspace/panel state separation;
- every typed graph command, stale/invalid/unauthorized/oversized rejection and atomic
  history publication;
- drag/connect/property coalescing, cancel and exactly one semantic undo record;
- undo/redo exact patches without compiler/evaluator invocation;
- save while editing, failure, autosave/recovery and external three-way conflicts;
- background validation/cook/cache-hit completion after edit, close, catalog/provider
  replacement and session restart;
- preview isolation, transient ordinary cook/runtime parity, stale labeling and target
  retirement;
- bake success, every-target prepare failure, stale input/authority/target revision and
  aggregate rollback;
- independent graph dirty/save and bake-current states;
- bake undo/redo plus survival of hand-authored/adopted/differently owned output;
- create/duplicate/delete cancellation with no orphan source/registry/preview output;
  and
- close/shutdown at each operation phase with no worker, callback, preview target,
  prepared bake, module/provider/input lease or document registration surviving its
  owner.

Tests use deterministic virtual scheduling and fault injection. They assert typed
session/revision/state/provenance identities, atomic roots, history and retirement
receipts, not widget IDs, diagnostic wording or wall-clock timing.

## Consequences

### Positive

- Editor UI cannot directly mutate graph or Scene storage.
- Durable source, workspace presentation, background operations, preview and bake have
  distinct owners and revision identities.
- Preview exercises the ordinary cooked/runtime path without becoming authoritative.
- Bake and bake undo use exact provenance-aware target transactions, protecting
  hand-authored/adopted content.
- Save/recovery and stale background behavior follow the shared Editor Document Model.

### Negative

- Preview of unsaved work requires a bounded transient cook namespace and isolated
  target world.
- Bake needs target-owner preparation, multi-owner reservations and target semantic
  history rather than direct scene spawning.
- Graph-save state and bake-current state are independent and require clear UI.
- Large semantic edits/bakes may be rejected when reversible history cannot be reserved.

## Rejected Alternatives

### Let graph widgets mutate nodes/edges or Scene objects directly

Rejected because it bypasses revision, validation, permission, history, save, rollback
and multi-surface command parity.

### Give preview a lightweight graph interpreter

Rejected because preview could disagree with cooked runtime semantics, node versions,
limits, determinism and target preparation.

### Treat preview success as a bake or save

Rejected because preview is isolated disposable state and cannot grant durable source
or target-owner authority.

### Implement bake as delete old output then spawn new objects

Rejected because failure would lose visible/durable data and graph-level cleanup can
delete hand-authored or transferred content.

### Rerun PCG during bake undo/redo

Rejected because current graph, inputs, seed, node catalog and provider state may differ
from the committed historical result.

### Store cooked plans or preview objects in document history

Rejected because they are derived, large, target/runtime-owned and incompatible with
semantic source history and deterministic recook.
