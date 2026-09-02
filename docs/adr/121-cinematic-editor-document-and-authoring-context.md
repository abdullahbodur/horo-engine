# ADR-121: Cinematic Editor Document and Authoring Context

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: Sequence asset document identity, tab and creation ownership, authoring scene context, reference staleness, command/history boundaries, save/autosave/recovery/external conflict and preview isolation
- **Issue**: [CIN-005.1](https://github.com/abdullahbodur/horo-engine/issues/1702)
- **Jira**: [HORO-1661](https://horo-engine.atlassian.net/browse/HORO-1661)
- **Related**: [ADR-014](014-sequencer-ownership-clock-authority-and-binding-boundary.md), [ADR-093](093-prefab-override-property-identity-and-delta-operations.md), [ADR-094](094-prefab-nested-composition-and-variant-inheritance.md), [ADR-117](117-playback-ownership-frame-order-and-determinism.md), [ADR-119](119-camera-authority-during-cinematics.md)
- **Normative documents**: [Editor Document Model](../architecture/editor/editor-document-model.md), [Cinematic Sequencer Architecture](../architecture/runtime/cinematic-sequencer-architecture.md), [Project Model](../architecture/editor/project-model.md)

## Context

Sequence assets are durable authored project content, but the cinematic architecture
currently describes only a timeline/curve surface. It does not say whether that
surface is a persistent document tab or a transient modal, which object owns dirty
state/history/save, or whether editing a referenced scene object mutates the sequence
document, scene document, or both.

The generic Editor Document Model already defines session/revision/state identity,
typed commands, semantic history, atomic save, recovery and external-file conflict.
Creating a parallel sequencer undo stack, autosave file or file-watcher reload path
would violate that contract. Conversely, treating a sequence as part of a scene
document would make reusable sequence assets acquire scene lifetime and prevent
authoring without an open scene.

Live scene references also need explicit staleness. A stable object ID survives
rename/reorder but not deletion, wrong-scene attachment, scene session replacement or
component/property schema change. Automatically clearing or rebinding such a
reference would silently change authored intent.

## Decision

### 1. A sequence asset is a first-class persistent document

Each editable sequence asset opens as one `SequenceDocument` rooted at its stable
`AssetId` and source revision. `EditorWorkspaceController` owns its
`DocumentSessionId`, committed revision/state IDs, semantic history, dirty state,
save/autosave/recovery/conflict state and derived compile/preview revisions. The
document tab host owns the persistent visible route and supplies document command,
query and subscription capabilities to the timeline, curve editor, details and
binding panels.

Only one writable document session for the same stable `AssetId` is admitted in one
workspace, regardless of which source revision was used to open it. Opening another
revision of that asset focuses the existing tab and enters the normal reload/conflict
flow. A deliberate read-only compare
or historical view has separate typed identity and cannot submit commands or save as
the canonical asset.

The sequence document stores authored timeline/range/settings, stable track/key IDs,
curves, binding declarations, payloads and logical asset references. It does not store
tab layout, selection, zoom/scroll, playhead, open popup, preview player/lease, live
scene/runtime handles, viewport camera state or backend objects. Workspace
presentation state may remember the open document route and UI state by stable asset
identity, but it is not sequence content.

### 2. Creation is transient; editing is tab-owned

`Create Sequence` is a transient modal/route owned by the editor workflow host. It
collects name/location/template and validates project boundary, collision, permission
and source-control policy. Confirmation runs one application asset-creation
transaction, atomically publishes the initial valid sequence asset, registers its
`AssetId`, then opens/focuses the persistent sequence document tab.

Cancel or failure leaves no document, tab, partial file, registry entry or recovery
record. The modal never becomes a hidden document and does not keep an independent
undo stack. Rename/move/delete/import are ordinary Asset application use cases with
open-document coordination; they are not raw tab/file mutations.

Track/key creation, dragging, curve edits, binding changes, recording and property
edits occur in the document tab through typed `SequenceEditorCommand` values. Popups
and modals are limited to transient choices/confirmation such as selecting a binding,
resolving a conflict or Save As.

### 3. SequenceDocument owns one command/history source of truth

Every authored sequence mutation validates and commits through the document command
executor. Continuous drags/recording use preview overlays or one reversible
transaction and create one semantic history entry on commit. Cancel restores the
exact pre-interaction state. UI widgets, viewport tools, MCP handlers, file watchers,
compile workers and runtime preview never mutate the document directly.

Undo/redo, dirty-state identity, save, Save As, autosave, recovery and external
conflict use the existing Editor Document Model unchanged. There is no sequencer-local
undo array, dirty boolean, autosave timer/file, serializer-owned history or direct
file save. Compilation/cook and preview snapshots are revision-correlated derived
work; success does not clear dirty state or advance saved state.

Save captures an immutable sequence revision, validates/cooks its source schema,
serializes deterministically and atomically replaces the canonical asset. If newer
edits commit while saving, only the captured state becomes saved and the document
remains dirty. Autosave writes recovery state and never overwrites or redefines the
canonical source.

### 4. Authoring scene context is a detachable projection

A `SequenceAuthoringContext` optionally attaches a SequenceDocument session to one
editor `SceneDocument` session/revision and immutable binding-resolution snapshot:

```cpp
struct SequenceAuthoringContext {
    DocumentSessionId sequenceSession;
    AssetId sequenceAsset;
    std::optional<DocumentSessionId> sceneSession;
    std::optional<AssetId> sceneAsset;
    DocumentRevision sceneRevision;
    BindingRegistryRevision bindingRegistryRevision;
    AuthoringContextGeneration generation;
};
```

The context enables scene-object picking, validation, viewport preview, property
enumeration and recording. It owns no sequence or scene content. A sequence remains
editable without a scene context; object-bound tracks display unresolved contextual
state while timeline/range/curve/payload edits remain available.

The context pins exactly one immutable scene/binding snapshot for one inspection or
authoring operation; it is not a long-lived mutable cache of the scene head. When the
scene head advances, `EditorWorkspaceController` installs a replacement context with
the new `sceneRevision`, registry revision and generation at the next document-safe
point. A command carrying an older expected revision fails stale or enters the explicit
rebase flow; no caller updates this context object in place.

Attaching/detaching/switching context changes editor presentation state, not authored
sequence data, unless the user explicitly commits a binding command. Opening a scene
does not rewrite bindings, and opening a sequence does not load or select an inferred
scene. A declared preview fixture/template may be chosen explicitly but remains a
logical asset reference, not a live session handle.

### 5. References retain intent and publish resolution state

Authored scene bindings store the sequence's declared scene-scope policy plus durable
`StableObjectId`, component/property binding identity and expected schema revision.
Resolution against the current context produces immutable `BindingInspectionState`:

| State | Meaning |
|---|---|
| `Resolved` | Scene scope, object generation and component/property schema match |
| `NoSceneContext` | No scene is attached; authored identity remains intact |
| `WrongSceneScope` | Attached scene identity is incompatible with the binding policy |
| `ObjectMissing` | Stable object identity is absent in the attached revision |
| `ComponentMissing` | Object exists but required component is absent |
| `SchemaIncompatible` | Property/binding identity exists with incompatible type/version |
| `AmbiguousLegacyReference` | Imported legacy identity cannot resolve uniquely and requires repair |

Rename, hierarchy reorder and component relocation preserve a reference when stable
identity/schema still match. Delete, component removal, schema replacement, external
reload and scene replacement increment their owner revisions/generations and
re-resolve the inspection snapshot. Stale state is shown on the track/key/binding
panel with cause and last-known bounded display context; it never clears, retargets by
name, chooses a same-named object or mutates document dirty state.

The user repairs a stale reference through an explicit typed binding command, which
is undoable and marks the sequence document dirty. If the old identity becomes valid
again before repair, a new snapshot may return to `Resolved` without an authored
mutation. Runtime activation independently validates cooked bindings and does not
trust editor inspection state.

### 6. Scene and sequence commands keep their owners

A timeline edit affects only `SequenceDocument` and enters its history. Creating,
renaming, moving, deleting or changing a scene object affects only `SceneDocument`
and enters scene history. Viewport recording samples immutable scene/editor evidence
but commits generated keys through one Sequence command; it does not make the
sequence history own the source scene transform.

An action that intentionally changes both documents uses the existing staged
multi-document application transaction. It pins both sessions/revisions, validates
both candidates, commits both semantic command groups atomically and publishes linked
transaction evidence. Failure/staleness leaves both unchanged. Each document retains
its own history/dirty/save identity; UI cannot simulate atomicity by running two
uncoordinated commands or maintain a third cross-document undo stack.

Undo that would violate the other document's current dependency fails with a typed
conflict and offers an explicit repair/rebase flow. Before applying an inverse, the
application transaction queries an immutable reverse-dependency index built from the
captured `BindingRegistryRevision` and both documents' current
`AuthoringContextGeneration` values. It revalidates every referenced stable object,
component/property schema and linked transaction revision against the pinned document
heads. Missing, changed or newly introduced dependants reject the inverse as a typed
cross-document conflict. Undo never silently rewrites the other document or a durable
file.

### 7. Reload and external conflict follow document policy

Asset Registry/file-watch notifications are revision hints. The document service
compares the canonical sequence file identity/hash and applies existing policy:

- a clean document may offer or perform policy-approved reload into a new revision;
- a dirty document offers compare, keep local, reload external or save local as;
- an active save detects external replacement before atomic publication; and
- reload starts a new history branch/clears redo unless an explicit semantic rebase
  rebuilds valid history.

External reload cannot inject a hidden command or replace an in-progress interaction.
Pending save/autosave/compile/preview/binding results must match sequence session,
document revision, source revision, authoring-context generation and relevant scene/
registry revisions before publication. Stale results are discarded with a typed
diagnostic.

Scene external reload/replacement is handled by the SceneDocument owner. The
SequenceDocument remains open and unchanged; its authoring context receives a new
scene generation and recomputes binding inspection state.

### 8. Preview is derived, isolated and disposable

Scrub/play preview compiles an immutable snapshot of one sequence revision and binds
it to one authoring-context generation. Preview player state, evaluation cursor,
temporary authority leases, camera proposal, Audio/VFX handles and diagnostics are
owned by the preview session, not the document/history. Closing the tab, changing
context, committing a relevant edit, scene replacement or project close cancels and
generation-fences the preview before retiring resources.

Preview cannot persist runtime state back into the sequence or scene. `Record` is an
explicit editor command workflow that samples permitted evidence and previews the
candidate keys before committing them to SequenceDocument. ADR-119 keeps the editor
viewport controller as final camera owner during preview.

### 9. Close, project and lifecycle behavior are guarded

A close request routes through the document tab host's dirty-document leave guard:
save, discard or cancel. Save failure keeps the tab/session open. Discard drops only
unsaved in-memory sequence changes after explicit confirmation; canonical files and
recovery retention follow existing policy. Project close resolves every dirty
document and running preview/operation before workspace teardown.

Asset deletion while a sequence document is open requires explicit application-level
coordination and cannot be inferred from closing the tab. Save As changes canonical
location/identity only after durable publication and project registry update; Save
Copy As does not change the active document identity.

### 10. Qualification covers document and context independence

Required implementation evidence includes:

- create confirm/cancel/name collision/permission/publication failure with exactly one
  asset and persistent tab only after successful commit;
- repeat-open focus, one writable session, tab close save/discard/cancel and project
  close with dirty documents and running previews;
- command symmetry, grouped drag/record transactions, dirty state through undo/redo,
  concurrent-save edits, Save As/Copy As and no alternate sequencer history/save;
- autosave/recovery retention, corrupt recovery, external clean/dirty/save-in-flight
  conflicts and reload history boundary;
- no scene context, correct/wrong scene, rename/reorder, object/component deletion,
  schema change, external scene reload/replacement and explicit undoable repair;
- stale async save/autosave/compile/preview/binding results across every sequence,
  scene, registry and authoring-context generation;
- isolated sequence-only, scene-only and atomic cross-document commands, including
  failure/undo conflicts without partial mutation; and
- preview/scrub/record teardown proving no runtime handle, authority lease or preview
  mutation enters source documents/history.

## Consequences

### Positive

- Sequence assets behave like other persistent editor documents with one ownership,
  history and persistence model.
- Scene context improves authoring without giving live scene lifetime to the asset.
- Stale references remain visible and repairable without silent data loss.
- Preview and viewport behavior cannot become a second source of authored truth.

### Costs

- SequenceDocument needs a specialized command set, validator and derived compile/
  preview projection within the shared document infrastructure.
- Binding inspection must track sequence, scene, registry and context generations.
- Cross-document actions require real staged coordination rather than two convenient
  UI callbacks.

## Rejected Alternatives

### Put sequence editing in a modal or scene tab panel

Rejected because a reusable durable asset needs persistent route identity, dirty
guards, history, recovery and external-conflict lifecycle. Modals remain transient.

### Store sequence data inside SceneDocument

Rejected because sequences are reusable assets, must be editable without a scene and
have independent save/revision/conflict identity.

### Give Sequencer its own undo and autosave implementation

Rejected because that duplicates Editor Document Model state and produces conflicting
dirty/save/recovery truth. Sequence commands and persistence use shared services.

### Clear or retarget stale references automatically

Rejected because deletion/reload could destroy authored intent or bind a same-named
wrong object. Inspection is derived; repair is an explicit undoable command.

### Let viewport recording mutate both scene and sequence directly

Rejected because two document owners could partially commit and cannot provide
coherent undo. Recording writes sequence keys; deliberate two-document changes use
the staged multi-document application transaction.

### Store live scene/runtime handles in SequenceDocument

Rejected because handles die on reload, scene replacement, PIE restart and project
close. Documents store stable authored identities; context/preview owns disposable
generation-checked resolution.
