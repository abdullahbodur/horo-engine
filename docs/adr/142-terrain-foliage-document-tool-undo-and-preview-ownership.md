# ADR-142: Terrain/Foliage Document, Tool, Undo and Preview Ownership

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: Terrain/Foliage authoring-document identity, editor tool routing, bounded tile-patch operations and history, live preview isolation, source save/cook boundaries, stale work, cancellation, replacement and shutdown
- **Issue**: [TRF-006.1](https://github.com/abdullahbodur/horo-engine/issues/1969)
- **Jira**: [HORO-1925](https://horo-engine.atlassian.net/browse/HORO-1925)
- **Related**: [ADR-008](008-error-model-exception-boundary-and-registry.md), [ADR-010](010-job-waiting-and-operation-store-ownership.md), [ADR-012](012-world-streaming-partition-authority-and-subsystem-boundaries.md), [ADR-056](056-external-editor-ui-boundary.md), [ADR-093](093-prefab-override-property-identity-and-delta-operations.md), [ADR-121](121-cinematic-editor-document-and-authoring-context.md), [ADR-129](129-vfx-editor-document-live-preview-and-module-authoring.md), [ADR-137](137-terrain-foliage-ownership-data-tier-and-lifecycle.md), [ADR-138](138-terrain-source-cooked-tile-cache-and-streaming-ownership.md), [ADR-139](139-terrain-render-extraction-material-lod-and-tier-boundary.md), [ADR-140](140-foliage-placement-baked-dynamic-state-and-eviction-ownership.md), [ADR-141](141-terrain-foliage-cross-system-ownership-and-readiness.md)
- **Normative documents**: [Editor Document Model](../architecture/editor/editor-document-model.md), [Editor Panel Host](../architecture/editor/editor-panel-host.md), [Terrain and Foliage Architecture](../architecture/runtime/terrain-and-foliage-architecture.md), [Asset Pipeline](../architecture/runtime/asset-pipeline.md), [Project Model](../architecture/editor/project-model.md)

## Context

Terrain sculpting, layer painting, hole carving, spline deformation and foliage
painting can touch large source datasets continuously. The existing architecture says
that editor documents own source mutation and undo, while Assets owns cooking and
TerrainRuntime owns live data. It does not define the concrete authoring document,
which owner expands a brush into affected tiles, how history remains bounded, or how
an interactive preview is kept separate from canonical source and a live runtime.

That ambiguity permits incompatible implementations. A viewport tool could write
directly into a heightfield, store one whole-heightfield copy per stroke, ask a worker
to mutate the document after the gesture ends, or preview by modifying active runtime
state. Save could then publish preview state, cook success could incorrectly clear
dirty state, and undo could depend on current brush settings or random generation.

Terrain operations also cross tile seams and can invalidate dependent layer,
collision, navigation, foliage-placement and preview products. The affected closure
must be known and charged before mutation. This ADR specializes the shared Editor
Document Model. TRF-006.2 owns the final serialized edit-operation and patch schemas;
it may refine layouts without moving these authorities.

## Decision

### 1. Canonical authoring state lives in persistent domain documents

Each editable canonical terrain dataset opens as one persistent
`TerrainAuthoringDocument` rooted at stable `AssetId`, `TerrainDatasetId` and accepted
source revision. Dataset-local foliage placement source is an explicit section of that
same document; it never becomes a parallel document or hidden state in a palette,
viewport or runtime instance. Reusable foliage-type definitions remain separate asset
documents because their material, mesh and placement-rule content can be referenced by
multiple datasets.

Editor document services own session/source identity, canonical height/weight/hole/
spline/placement state, committed revision, typed commands, bounded history, dirty and
saved state, save/autosave/recovery/conflict, close guards, and derived validation/cook/
preview revisions. `EditorPanelHost` owns route, focus and surface lifetime. Tools and
palettes own presentation only. Assets owns cook/artifact publication; TerrainRuntime
and Render/Physics/Navigation own only derived runtime generations.

One writable session per canonical asset is admitted in a workspace. Reopening focuses
it. Read-only compare, recovery and historical views have distinct capabilities and
cannot edit or save over canonical source.

### 2. Tools emit typed intent and own no source storage

Sculpt, layer, hole, spline and foliage controllers translate routed input into a typed
`TerrainEditOperation`. They may own brush radius/falloff/strength, active layer/type,
pointer capture, gizmo geometry and an ephemeral stroke accumulator. They own no
canonical samples, placements, dirty state, history, serializer or cooked artifact.

```cpp
struct TerrainEditOperationHeader {
    DocumentSessionId document;
    TerrainDocumentRevision expectedRevision;
    TerrainEditOperationId operation;
    TerrainEditKind kind;
    TerrainAuthoringCapability capability;
    TerrainEditLimits limits;
};
```

Payloads use stable dataset, tile, patch, layer, spline, foliage-type and placement
identities plus bounded semantic parameters. They contain no ImGui state, native file/
GPU/Physics/Navigation handle, mutable document pointer, runtime object, service locator
or arbitrary callback. Toolbar results remain presentation intent. GUI, CLI, MCP and
automation submit through the same permissioned document/application path.

### 3. Preflight freezes an exact bounded affected closure

Before mutation, the document executor validates session, expected revision,
capability, source schema, coordinates, parameters and finite limits. It expands intent
deterministically into canonically ordered affected tile/patch rectangles:

```cpp
struct TerrainAffectedPatch {
    TerrainTileId tile;
    TerrainPatchRect rect;
    TerrainChannelMask channels;
};

struct TerrainEditPlan {
    TerrainEditOperationHeader header;
    std::vector<TerrainAffectedPatch> patches;
    TerrainDependencyInvalidation invalidation;
    TerrainEditCost upperBound;
};
```

The set includes every sample apron, shared seam, dependent placement region and source
channel needed for deterministic apply/revert. Rectangles are clipped, merged and
sorted; overlap cannot record or apply one source element twice. Dependency
invalidation names affected cook inputs but never mutates cooked/native state.

The executor rejects empty, malformed, oversized, out-of-bounds or unsupported plans.
It reserves patch bytes, temporary work, history bytes and completion capacity before
reading mutable state. Large import/fill/procedural work uses an explicitly admitted
bounded batch whose complete patch set and atomic commit group are known up front. It
does not bypass history or commit partially completed tiles.

### 4. One transaction records exact before and after patches

For an accepted plan, the document owner captures canonical `before` values, applies
to detached or journaled candidate storage, validates seams and invariants, and captures
canonical `after` values. It atomically publishes source state, a new state identity/
revision, dirty state, one history entry and one bounded change summary.

```cpp
struct TerrainEditRecord {
    TerrainEditOperationId operation;
    TerrainDocumentRevision baseRevision;
    TerrainDocumentRevision committedRevision;
    std::vector<TerrainPatchSnapshot> before;
    std::vector<TerrainPatchSnapshot> after;
    TerrainDependencyInvalidation invalidation;
};
```

Snapshots contain only exact affected canonical regions and stable placement deltas.
Compression/deduplication must be lossless, deterministic and bounded. A full-
heightfield or whole-dataset copy per operation is prohibited. Brush settings alone are
not history, and committed records hold no live pointers or external leases.

Failure, cancellation or stale revision before commit leaves source, revision, dirty
state, selection and history unchanged. No observer sees a partial patch set.

### 5. Undo and redo replay committed results, not algorithms

The document/history owner applies `before` or `after` patches through the same atomic
publication boundary. Undo never reruns brush, noise, spline or foliage-scatter code and
does not read current tool parameters, random seeds or viewport state.

Undo/redo advances the monotonic revision while restoring the appropriate content state
identity. A new edit after undo clears redo. History has item and byte budgets; eviction
never changes canonical data. Dirty remains `current_state_id != saved_state_id` and is
not inferred from history depth.

A continuous gesture coalesces only while capture, document, expected revision,
capability and budget remain valid. Its final closure and before values cover the whole
gesture exactly once. Release commits one entry. Escape, modal capture, tool switch,
document/revision change, capacity denial or shutdown cancels and restores the
committed view.

### 6. Heavy work stages immutable candidates and returns to the owner

Expensive preflight/apply work runs in a document-owned cancellable task group against
immutable inputs and reserved candidate storage. Completion carries operation, session,
base revision and generation plus a typed candidate/diagnostic. Only the document owner
may revalidate and commit it.

A result for a closed/replaced session, advanced revision, cancelled operation,
unloaded provider, changed capability or expired generation is stale and discarded.
Workers never publish document state, dirty flags, history, artifacts or preview/runtime
generations. Conflicting mutations are rejected or serialized by explicit policy; a
completion is never silently merged into a newer revision.

### 7. Interaction overlays and cooked previews are separate

Low-latency brush feedback is an `InteractionPreviewOverlay` keyed by document session,
base revision, operation and generation. It may show a candidate, affected bounds or an
estimate, but does not advance revision, dirty state, history, source save or registry.
Cancel discards it exactly.

A high-fidelity preview captures an immutable document revision and uses ordinary
Terrain validation/cook schemas under a transient editor envelope. Its artifacts
activate through normal TerrainRuntime, World Streaming, Render, Physics and Navigation
contracts in an isolated `TerrainPreviewSession`, never in a live gameplay world.

Preview identity includes source revision, dependency fingerprint, target/tier plan,
settings and generation. New edits cancel or stale older work. The UI may retain a
leased last-good result marked stale, but cannot call it current. Camera, visualization,
selection, brush cursor, wind toggle, clock, fixture and runtime handles are disposable
preview/presentation state. Applying preview-derived content is a new explicit edit.

### 8. Save, recovery, cook and runtime persistence stay distinct

Source save captures one immutable canonical revision and uses the shared deterministic,
durable atomic replacement flow. Only successful source publication updates saved state.
Cook/preview success does not clear dirty; save success does not claim a later cook or
preview succeeded.

Autosave writes recovery state outside canonical source. Recovery restores a validated
dirty document and need not restore evicted history; it never overwrites source
silently. Assets/Pipeline observes a published source revision or an explicit immutable
unsaved preview snapshot and owns cook/cache/artifact publication. Cook callbacks cannot
mutate the document. Runtime Save/Persistent World owns ADR-140 gameplay foliage deltas;
it never saves editor source or becomes an undo owner.

Scene documents store typed terrain/foliage bindings, not source copies. A
`TerrainAuthoringDocument` references reusable foliage-type assets by stable typed
identity and does not copy or mutate their definitions. An operation that changes the
terrain source plus a Scene binding or reusable foliage-type document uses a staged
multi-document application transaction with sessions, revisions, permissions and
publication effects declared before commit. The viewport cannot simulate this through
two callbacks.

### 9. Close, replacement and shutdown retire owned work

Close first stops tool admission, releases capture, cancels overlays and document task
groups, invalidates generations, and requests preview teardown. The document retains
snapshots, task records, artifacts, module leases and runtime dependencies until their
owners acknowledge release. Project replacement and shutdown use the same order.

A deadline may report incomplete close with retained ownership; it cannot force-free
data referenced by a job, renderer fence, Physics step, Navigation query or runtime
snapshot. Duplicate acknowledgements are idempotent, and stale ones cannot retire a new
session sharing an asset or tile identity.

### 10. Errors, observability and limits are explicit

Operations return typed results such as `WrongDocument`, `StaleRevision`,
`InvalidPatch`, `AffectedSetTooLarge`, `HistoryBudgetExceeded`,
`CapabilityUnavailable`, `Cancelled`, `SourceConflict`, `PreviewStale` and
`ShutdownInProgress`. There is no silent clamp, partial edit, fallback to direct
mutation/full-dataset history, or runtime/backend/provider switch.

Bounded metrics include operation kind/result, patch/tile counts, reserved/actual bytes,
latency, cancellation reason and stale-result count. They exclude raw height/weight
payloads, user paths and unbounded placement lists. Frame-hot overlay rendering performs
no unbounded allocation, I/O or synchronous cook/runtime wait.

## Consequences

- Every terrain/foliage mutation has one authoritative transaction and one revision-
  correlated result for GUI, CLI, MCP and automation.
- History cost scales with affected patches, while exact before/after values make undo
  independent of algorithms and UI state.
- Tools and previews are replaceable adapters because they own no canonical source,
  history or persistence.
- Source save, transient preview cook, published cook and runtime persistence expose
  separate UI states rather than one ambiguous saved/ready flag.
- Large operations may be refused or require an admitted batch plan; this makes their
  cost explicit instead of allowing hidden unbounded memory or partial commits.

Required coverage includes canonical seam-aware closure for every tool; exact apply/
undo/redo without whole-heightfield copies or algorithm replay; one commit per gesture;
malformed/oversized operation rejection before mutation; cancellation/failure at every
stage; stale work across edit/reload/close/shutdown; dirty state across save and history
eviction; save/recovery/cook/runtime-persistence separation; isolated preview parity;
cross-document failure/reconciliation; and bounded memory, queues and teardown.

## Rejected Alternatives

### Store a complete heightfield snapshot for every undo entry

Rejected because cost scales with total dataset size rather than the edit and hides
affected-tile dependencies.

### Re-execute the tool algorithm during undo/redo

Rejected because settings, algorithms, dependencies, job order or randomness can differ.
History restores the exact committed before/after result.

### Let viewport tools mutate document or runtime buffers directly

Rejected because presentation code would become a second owner, bypass revision checks
and expose partial state to history, save or runtime consumers.

### Use a live world as the authoring preview

Rejected because source edits would inherit runtime streaming, mutation and save
lifetimes. Preview is isolated and generation-fenced.

### Treat cook or preview success as source save

Rejected because derived artifact publication and canonical source durability are
independent states.

### Keep an undo stack in each tool or palette

Rejected because cross-tool ordering, dirty state, recovery and automation would have
competing histories.

### Let oversized edits bypass undo or commit tile by tile

Rejected because failure would leave partial source that cannot be reverted atomically.
Oversized work is rejected or admitted as one fully bounded batch transaction.
