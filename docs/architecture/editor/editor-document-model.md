# Editor Document Model Architecture

## Purpose

This document defines editor document ownership, commands, history
transactions, dirty state, save, autosave, crash recovery, external file
changes, and conversion to runtime scenes.

## Core Decisions

- `SceneDocument` is the authoritative editable scene state.
- All undoable mutations execute through typed editor commands.
- History stores semantic transactions, not arbitrary memory snapshots.
- Document revision, saved revision, and runtime preview revision are distinct.
- Save is validated and atomic.
- Autosave writes recovery state; it does not redefine the user's saved file.
- Recovery is explicit and never silently overwrites the source document.
- GUI panels, tabs, modals, MCP handlers, and background workers do not mutate
  document storage directly.

## Ownership

```text
EditorWorkspaceController
  +-- SceneDocument
  +-- EditorHistory
  +-- EditorSelectionModel
  +-- EditorViewportModel
  +-- EditorDataBus
  +-- DocumentSaveService
  +-- DocumentRecoveryService
```

The workspace controller owns the active document session. Tabs and panels
receive command, query, and subscription capabilities through their context.

One document session has a stable `DocumentSessionId`, a monotonic
`DocumentRevision`, and an immutable `DocumentStateId` for each committed
content state. Undo and redo create a new revision notification while restoring
the state ID of the semantic history entry they apply.

## Document Model

The document stores authoring state:

- stable scene object IDs and hierarchy
- typed component authoring values
- logical asset references
- primitive descriptors for built-in procedural meshes and shapes
- scene settings
- editor-authorable metadata

Primitive descriptors reference the `PrimitiveCatalog` and store parameters such
as radius, height, and tessellation rather than imported asset IDs. See
[Built-In Scene Primitives](../runtime/built-in-scene-primitives.md).

It does not store:

- ImGui widget state
- selected objects
- panel layout
- open modals
- runtime ECS handles
- physics body pointers
- renderer backend handles

Workspace presentation state belongs to [Project Model](./project-model.md).

After committed document changes that remove or invalidate selected objects,
the workspace controller reconciles `EditorSelectionModel` against the new
`SceneDocument` state and publishes a selection change if needed.

## Queries And Mutation

Read APIs expose immutable views or snapshots:

```cpp
class SceneDocumentQueries {
public:
    DocumentRevision Revision() const noexcept;
    SceneObjectView Find(SceneObjectId id) const;
    SceneDocumentSnapshot Snapshot() const;
};
```

Mutation is internal to the command executor and document services. Returning a
mutable object reference to arbitrary UI code is forbidden.

## Command Contract

```cpp
class EditorCommand {
public:
    virtual Result<void> Validate(const EditorCommandContext&) const = 0;
    virtual Result<CommandDelta> Apply(EditorCommandContext&) = 0;
    virtual Result<void> Revert(EditorCommandContext&, const CommandDelta&) = 0;
    virtual CommandMetadata Metadata() const = 0;
};
```

A command:

- validates before committing mutation
- owns enough typed data to undo its committed change
- has deterministic apply and revert behavior
- identifies affected objects and change category
- does not draw UI or publish success before commit
- does not launch untracked asynchronous work

Command application is all-or-nothing. If `Apply()` fails mid-execution, the executor restores the pre-command state through the command delta, transaction journal, or staged mutation mechanism; it must not rely on arbitrary whole-memory snapshots as the normal history representation. Failed commands do not increment document revision and do not publish document-change notifications.

Commands should prefer staged mutation or a transaction journal when failure
can occur before a complete undo delta is available.

Commands that require external engine operations coordinate through an
application use case and commit document changes only after the required result
is available.

## Transactions

History transactions group commands into one user-visible undo step:

```cpp
class EditorTransaction {
public:
    Result<void> Execute(std::unique_ptr<EditorCommand> command);
    Result<HistoryEntry> Commit();
    void Rollback();
};
```

Examples:

- one transform drag with many preview updates
- multi-object property edit
- asset drop that creates several scene objects
- hierarchy reparenting with transform preservation
- creating a prefab from the selected object and replacing it with a prefab instance reference

Only a committed transaction increments the authoritative document revision and
publishes one coherent change notification.

Intermediate transaction state is not observable by tabs or panels until the
transaction commits, unless the transaction is explicitly marked as a preview
transaction.

## Preview Edits

Continuous tools may maintain preview state during an interaction. Preview state
is either:

- local presentation state rendered without document mutation
- a document transaction with reversible intermediate values and one final
  history entry

Preview mutations may update an `interaction_preview_revision`, but they do not update
the authoritative document revision or dirty state until the transaction commits.
Subscribers that need live preview updates observe preview notifications and
query the current preview overlay/state.

Cancelling the gesture restores the exact pre-transaction state and publishes
the appropriate final revision notification.

## History

```cpp
struct HistoryEntry {
    HistoryEntryId id;
    CommandLabel label;
    DocumentStateId beforeState;
    DocumentStateId afterState;
    std::vector<CommittedCommand> commands;
};
```

History has bounded item and memory budgets. Evicting old entries never changes
the current document.

Committed history entries own immutable command deltas required for undo/redo.
Command objects must not depend on external mutable UI state or live pointers after commit.

`Undo` and `Redo`:

- execute only on the editor owner thread
- use the same validation and notification path as normal commits
- fail safely if an external dependency required by the operation is no longer
  available
- clear redo history after a new committed edit

Save is not an undoable document command.

## Revision And Dirty State

The document tracks:

```text
current_revision             monotonic committed-state notification sequence
current_state_id             identity of the currently visible authored state
saved_revision               revision captured by the last successful save
saved_state_id               identity written by the last successful save
autosaved_revision
runtime_preview_revision
interaction_preview_revision
```

The document is dirty when `current_state_id != saved_state_id`.

Undoing back to the saved state restores its `DocumentStateId` and clears dirty
status even though the monotonic `DocumentRevision` advances and history still
contains entries. Saving updates `saved_revision` and `saved_state_id` only
after the atomic file replacement succeeds.

## Save

Save flow:

1. capture an immutable snapshot and target revision
2. validate the snapshot
3. serialize deterministically
4. write a sibling temporary file
5. flush according to durability policy
6. atomically replace the destination
7. verify required metadata
8. mark the target revision saved if it is still current

Atomic replacement uses the platform-appropriate primitive and durability policy.
On POSIX-like systems, the destination directory is flushed when required by the
selected durability policy. On Windows, replacement preserves expected file
metadata according to the document format policy.

If edits occur while serialization is prepared, the successful save applies to
its captured revision. The document remains dirty relative to newer edits.

Failed save preserves the original file and returns structured diagnostics.

## Save As

`Save As` validates project boundaries, extension, identity rules, reference
updates, and destination conflicts. It commits the document's new location only
after the destination is durable.

Changing location is coordinated by the workspace controller and project model;
tabs do not update path state independently.

After successful `Save As`, the active document location is updated and `saved_revision` is set to the saved target revision if that revision is still current.
`Save Copy As` writes a separate copy without changing the active document
identity or saved revision.

## Autosave

Autosave is enabled by typed user/project policy and triggers from:

- elapsed dirty duration
- edit-count threshold
- focus or lifecycle checkpoints where safe

Autosave captures an immutable revision and writes a recovery document under the
user state or project recovery area. It does not overwrite the canonical scene
file and does not change `saved_revision`.

Autosave is coalesced, bounded, and does not run concurrently for the same
document. Repeated failures use backoff and surface one actionable status rather
than a notification flood.

Recovery storage has a retention policy bounded by document count, session
count, total storage, and record age. Clean shutdown removes obsolete recovery
records only after verifying that the canonical file contains the intended
document state or the user has explicitly discarded the recovered changes.

## Recovery

Recovery records contain:

- engine and document format version
- project, document, and session identity
- canonical document path
- saved revision and recovered revision
- timestamp and clean-shutdown marker relation
- checksum and safe recovery metadata

On startup after an unclean session, the editor compares recovery state with the
canonical file and offers:

- inspect recovered version
- restore into the active document
- save recovered content to a new file
- discard recovery state

Recovery loading treats recovered data as untrusted input and validates it using
the same document validation path as normal scene loading.

Restoration creates a new dirty editor session. It never silently overwrites the
canonical file.

## External File Changes

The document service tracks the file identity or content hash last read/written.
When the canonical file changes externally:

- clean document: offer or perform policy-approved reload
- dirty document: offer compare, keep local, reload external, or save local as
- active save: detect conflict before replacement

By default, external reload starts a new history branch, clears redo history, and prevents undo across the reload boundary. It is never injected as a hidden command. Rebase is allowed only through an explicit policy that rebuilds valid semantic history entries.

## Runtime Conversion

The document converts to `RuntimeSceneDefinition` through an editor service.
Conversion:

- reads a stable document snapshot
- validates runtime-required fields
- resolves logical asset references
- emits structured diagnostics
- never gives runtime systems mutable document access

Play and preview sessions record the document revision from which they were
built.

Runtime preview validity is keyed by the document revision and the identities
and revisions of resolved asset dependencies used during conversion. Runtime
preview caches are invalidated when `current_revision` differs from
`runtime_preview_revision` or any recorded dependency identity or revision no
longer matches.

## Data Bus

After commit, the document publishes:

```cpp
struct SceneDocumentChangedEvent {
    DocumentRevision revision;
    DocumentChangeKind kind;
    bool dirty;
    AffectedObjectSummary affected;
};
```

The event is bounded. Subscribers query document authorities for full state.
Only the document/history owner publishes committed document-change events.

`AffectedObjectSummary` is advisory for efficient UI invalidation. Subscribers
must tolerate incomplete summaries and query `SceneDocument` for authoritative
state.

## Threading

Document mutation and history execute on the editor owner thread. Workers may
validate, serialize, diff, or convert immutable snapshots.

Worker completion verifies document session and revision before committing save,
autosave, recovery, or preview results.

## Testing

Required tests cover:

- command apply/revert symmetry
- transaction rollback and one-entry grouping
- dirty state across save, undo, and redo
- bounded history eviction
- save with concurrent later edits
- interrupted and failed atomic writes
- autosave coalescing and backoff
- recovery retention bounds and clean-shutdown cleanup
- recovery after unclean shutdown
- external file conflict behavior
- stale worker result rejection
- document-to-runtime conversion isolation
- command failure after partial apply restores pre-command state
- preview transaction cancel restores exact pre-interaction document state
- delete object reconciles invalid selection
- Save As updates active document location only after durable write
- Save Copy As does not update active document location
- external reload while autosave/save worker is pending
- runtime preview invalidation when dependent asset identities or revisions
  change
- recovery file with stale/unsupported document format version
- bounded AffectedObjectSummary does not break subscriber correctness

## Related Documents

- [Editor Data Bus](./editor-data-bus.md)
- [Editor Panel Host](./editor-panel-host.md)
- [Editor Modal Host](./editor-modal-host.md)
- [Project Model](./project-model.md)
- [Built-In Scene Primitives](../runtime/built-in-scene-primitives.md)
- [Scene Runtime](../runtime/scene-runtime.md)
- [Asset Pipeline](../runtime/asset-pipeline.md)
- [Prefab Architecture](../runtime/prefab-architecture.md)
