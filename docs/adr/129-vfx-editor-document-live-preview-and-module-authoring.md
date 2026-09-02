# ADR-129: VFX Editor Document, Live Preview and Module Authoring

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: Persistent VFX effect document ownership, transient creation workflows, stack/graph authoring scope, runtime-pipeline preview parity, preview lifecycle and decal projection manipulation
- **Issue**: [VFX-008.1](https://github.com/abdullahbodur/horo-engine/issues/1756)
- **Jira**: [HORO-1713](https://horo-engine.atlassian.net/browse/HORO-1713)
- **Related**: [ADR-011](011-vfx-effect-ownership-simulation-domain-and-renderer-boundary.md), [ADR-056](056-external-editor-ui-boundary.md), [ADR-121](121-cinematic-editor-document-and-authoring-context.md), [ADR-123](123-vfx-cpu-stage-order-determinism-and-gameplay-coupling.md), [ADR-124](124-vfx-gpu-simulation-readback-and-compute-fallback.md), [ADR-125](125-vfx-transparency-sorting-and-pass-placement.md), [ADR-126](126-vfx-graph-compilation-and-runtime-representation-convergence.md), [ADR-127](127-vfx-decal-projection-lifetime-and-rendering-path-policy.md), [ADR-128](128-vfx-spawn-event-mapping-pooling-and-budget-enforcement.md)
- **Normative documents**: [Editor Document Model](../architecture/editor/editor-document-model.md), [Editor Panel Host](../architecture/editor/editor-panel-host.md), [VFX and Particles Architecture](../architecture/runtime/vfx-and-particles-architecture.md), [Asset Pipeline](../architecture/runtime/asset-pipeline.md)

## Context

The VFX architecture shows a particle editor with emitter modules, curves, preview,
budget inspection and a module graph. ADR-126 already decides that stack and graph
sources are authoring frontends for one compiled runtime representation. It does not
decide whether an effect is a persistent document or modal, which owner holds dirty
state/history/save, or whether preview may interpret unsaved source directly.

The generic Editor Document Model and Panel Host already separate authoritative
document sessions from tab presentation lifetime. VFX must specialize that contract,
not create a particle-editor undo array, autosave file or direct serializer. The same
applies to decals: effect-spawned decal modules and scene-placed projection volumes
need editor tooling, but neither justifies a second persistence mechanism.

Preview fidelity is an architectural concern. An editor-only simulator, graph
interpreter, immediate renderer or special particle kernel could look plausible while
disagreeing with runtime stage order, budgets, fallbacks and pass placement. Preview
must differ only in its isolated host inputs and controls, not in compilation or VFX
execution semantics.

Finally, the baseline stack editor can ship before a production graph UI. The absence
of graph widgets must not create a second runtime format, force stack/graph round-trip,
or leave module ownership undecided.

## Decision

### 1. Each effect asset opens as one persistent document

Every editable VFX effect asset opens as one `VfxEffectDocument` rooted only at its
stable `VfxEffectAssetId`/`AssetId`.
Existing top-level particle-system assets migrate through the VFX source schema into
this document identity; they do not remain a second live document class.

`EditorWorkspaceController` and its document services own `DocumentSessionId`,
committed state/revision, typed command execution, semantic history, dirty/saved
revision, validation, save/autosave/recovery/external-conflict state and derived
compile/preview revisions. `EditorPanelHost` owns the persistent tab route, placement,
focus and presentation lifecycle. The tab owns only selection, filters, expansion,
scroll/zoom, viewport camera and other bounded presentation state.

Only one writable session for the same stable canonical effect `AssetId` is admitted in
a workspace, regardless of source revision. Reopening another revision focuses the
existing tab and enters the normal reload/conflict flow. Read-only compare, recovery and
historical views carry separate typed identities and cannot submit canonical commands
or save over the source.

The document stores stable emitter/module/node/port/output IDs, typed parameters and
curves, logical asset references, authoring source kind, decal descriptors and other
source required by ADR-126 compilation. It never stores ImGui state, open popups,
preview clocks/particles, runtime handles, native renderer objects, compiled-kernel
leases or transient diagnostics.

### 2. Creation and transient choices remain modal workflows

Create, duplicate, import, template selection, Save As, destructive confirmation and
conflict resolution use transient routes/modals owned by the editor workflow host.
Create validates name/location, project boundary, permissions, source control,
template/source schema and registry collision, then atomically publishes and registers
one valid asset before opening its document tab.

Cancel or failure leaves no asset, registry record, document, tab, recovery file or
hidden history. A modal does not become a document owner. Rename, move and delete are
application asset operations coordinated with the open document, never raw tab or
filesystem mutations.

### 3. The document has one command, history and persistence path

Emitter/module/node edits, parameter and curve changes, material/output settings,
fallback/budget policy and decal projection/lifetime edits use typed
`VfxEditorCommand` values. Continuous drags use a transient preview overlay or one
reversible transaction and create exactly one semantic history entry on commit.
Cancel restores the prior state.

UI widgets, module providers, file watchers, cook workers and preview runtime never
mutate document storage. Undo/redo, dirty state, save, autosave, recovery, Save As/Copy
As and external conflicts use the shared Editor Document Model unchanged. Compilation
and preview are derived work: success neither clears dirty state nor advances the
saved revision.

Save captures an immutable document revision, validates/canonicalizes the VFX source
schema and atomically publishes the canonical source through the Asset/Project owners.
Edits committed during save leave the document dirty. Autosave writes recovery state,
not a competing VFX source or cooked artifact.

### 4. Stack and graph authoring are independent frontends with one target

`VfxAuthoringSourceKind` is an exhaustive source-document discriminator:

```cpp
enum class VfxAuthoringSourceKind : uint8_t {
    EmitterStack,
    NodeGraph
};
```

The baseline `EmitterStack` surface authors ordered emitters and stage-valid modules,
fixed connection points, parameters/curves, typed event edges, materials, outputs,
fallbacks and finite cost declarations. It cannot express arbitrary graph topology or
store hidden graph nodes to emulate it.

The `NodeGraph` surface authors typed nodes/ports and supported explicit dependency
edges. Its production UI may ship later. Until available, the editor reports the graph
authoring surface as unavailable or opens an explicitly read-only inspection route; it
does not reinterpret, flatten, auto-convert or save the graph as a stack.

Both frontends use the same catalog identities, semantic validators, compiler-owned
lowering and `CompiledVfxEffectDescriptor` target defined by ADR-126. They may share
Horo-owned property, curve, module-catalog, diagnostic and preview controls, but their
source command schemas and layout metadata remain distinct. Equivalent semantics under
identical inputs must still produce identical descriptor/kernel fingerprints.

Deferring graph UI therefore does not defer or fork runtime loading, simulation,
fallback, extraction or rendering. Stack-to-graph or graph-to-stack conversion is a
future explicit migration tool with preview and loss reporting, not an invariant of
this decision.

### 5. Module providers contribute schemas, not editor/runtime ownership

The editor resolves a captured module catalog generation containing stable provider,
module/node, schema, stage, port, parameter, capability and kernel identities. Catalog
entries are inert metadata plus bounded editor presentation descriptors. They do not
draw arbitrary UI, retain document references, mutate a source during validation or
register runtime callbacks from a tab.

Adding a module executes a typed document command against the captured catalog and
document revision. Missing/unloaded providers preserve unknown source records for
inspection/recovery where safe and block validation/cook when required; they are not
silently deleted. The cook/runtime provider boundary remains ADR-126 authority.

### 6. Decal authoring uses the same document infrastructure

An effect-spawned or reusable decal output is authored as a typed module/output inside
`VfxEffectDocument`, including ADR-127 projection, placement-space, lifetime, material,
receiver and rendering-path intent. There is no separate `DecalDocument`, decal undo
stack, sidecar save file or preview simulator. A decal-only reusable effect is still a
valid effect document with a decal output.

A scene-placed decal component remains authored state of `SceneDocument`, because the
scene owns its durable object placement and component identity. Its projection-volume
manipulator uses the same Horo viewport/gizmo controllers: drag writes only a transient
preview overlay, commit sends one typed command to the owning document, and cancel or
invalid geometry restores the exact prior value. It supports ADR-127 Box/OrientedBox,
WorldLocked/OwnerLocal, positive extents and owner/cell identity without exposing
renderer handles.

Editing a referenced reusable effect opens/focuses `VfxEffectDocument`; moving its
scene instance remains a Scene command. An intentional action changing both sources
uses the staged multi-document application transaction. The viewport cannot simulate
atomicity through two UI callbacks or create a third cross-document history.

### 7. Live preview compiles and runs the ordinary runtime pipeline

Preview captures an immutable `VfxEffectDocument` revision and submits it to the same
VFX parse/migrate/canonicalize/validate/lower compiler used by Asset Pipeline. Unsaved
source may use an editor-owned transient cook envelope, but the compiler phases,
semantic inputs, target capability fingerprint and `CompiledVfxEffectDescriptor`/
kernel encoding are identical. Preview never interprets stack modules or graph nodes
as executable behavior.

After successful compile, an isolated `VfxPreviewSession` activates the descriptor
through the ordinary runtime chain:

```text
CompiledVfxEffectDescriptor
  -> VfxWorld admission and EffectSystem lifecycle
  -> SimulationDomainResolver and CPU/GPU/Null simulators
  -> immutable RenderWorldSnapshot extraction
  -> RenderFrontend graph scheduling and selected backend
```

There is no editor particle simulator, direct immediate draw, preview-only stage
order, permissive budget bypass, special fallback or private module kernel. ADR-123
through ADR-128 stage, RNG, payload, compute/readback, sorting/pass, decal, pooling and
budget rules apply unchanged.

The preview host may supply isolated inputs that runtime normally receives from a
scene/application: preview transform, bounded parameter fixture, deterministic seed,
clock controls, camera, background and explicitly selected quality/capability profile.
Those inputs and their revisions are shown in diagnostics. They cannot mutate a live
scene, publish authoritative gameplay output, use production event bindings or make a
missing capability appear present.

### 8. Preview state is isolated, cancellable and generation-fenced

The preview session owns play/pause, fixed-step, restart, seed, fixture, clock,
diagnostic capture and runtime/resource leases. It is disposable derived state, never
document/history content. Scrubbing/restart begins from an admitted prepared initial
state or bounded checkpoint policy; it does not reverse GPU work or replay an unbounded
number of missed ticks in one frame.

Document commit, source/provider/dependency change or quality/capability change
invalidates the matching derived preview generation. Work may be debounced, but every
compile, activation, frame and diagnostic result must match document session/revision,
source revision, dependency/catalog generation, preview session/generation, cook
target and host policy/capability revision before publication. Stale work is cancelled
or discarded and its leases retire normally.

Closing/hiding the tab stops visible preview updates according to host policy; closing
the document, switching project or destroying the editor closes admission, cancels
jobs and generation-fences callbacks before CPU/GPU retirement. No raw document/tab
pointer survives in jobs, renderer snapshots or completion callbacks. A hidden tab
does not silently consume continuous preview budget unless an explicit bounded
background-capture operation owns it.

Preview Audio, sub-events and gameplay outputs are disabled or routed to bounded
preview-only inspection sinks. They cannot reach live `AudioWorld`, gameplay services
or the application event binding table. Importing a preview observation requires an
explicit reviewed document command.

### 9. Preview parity has executable evidence

Parity means that the same accepted source/dependencies/target inputs produce the same
compiled descriptor and kernel fingerprints and are executed by the same runtime
services and scheduling contracts. It does not claim bitwise-identical GPU pixels
across devices or profiles.

Qualification must cover:

- create/cancel/failure and repeat-open behavior, one writable session, tab focus,
  dirty close save/discard/cancel and project teardown with compile/preview in flight;
- typed command symmetry, grouped module/curve/decal-gizmo drags, dirty state through
  undo/redo, concurrent save edits, recovery and clean/dirty external conflicts;
- stack and graph fixtures with equivalent semantics producing identical compiled
  descriptor/kernel fingerprints, plus no stack/graph source interpretation reachable
  from preview or packaged runtime;
- editor preview and runtime harness activation from the same artifact with matching
  resolver choices, tick/seed inputs, CPU committed state, logical lifecycle, budget/
  fallback outcomes, extracted batch semantics and typed diagnostics;
- every supported CPU/GPU/Null and raster/profile combination, with image/performance
  comparison only under qualified device/backend/resolution/build conditions;
- unavailable graph UI preserving source without conversion, mutation or alternate
  persistence and stack authoring remaining fully usable;
- effect-spawned, decal-only and scene-placed Box/OrientedBox manipulation with preview,
  commit/cancel, invalid extents, owner/scene staleness and atomic cross-document failure;
- stale compiler/preview/GPU completion across every document, dependency, catalog,
  preview, project, scene, policy and capability generation; and
- allocation/budget instrumentation proving preview uses the same prepared-pool and
  retirement rules rather than hidden editor-only growth.

## Consequences

- Effect assets gain the same predictable tab, history, save and recovery behavior as
  other persistent editor documents.
- Runtime preview is trustworthy because it consumes the same compiled artifact and
  execution pipeline, with differences recorded as host inputs.
- The stack editor can ship independently without making graph UI a runtime dependency
  or forcing lossy source conversion.
- Decal modules and scene projection tools reuse document/command infrastructure and
  do not introduce a second persistence mechanism.
- Preview host composition, transient cooking and qualification fixtures require
  explicit generation, cancellation, budget and retirement plumbing.
- A graph asset may be temporarily uneditable when its authoring UI/provider is absent;
  preserving source is preferred to silent flattening or mutation.

## Rejected Alternatives

### Use a modal particle editor

Rejected because effect editing is persistent, dirty, undoable project work. Modal
ownership would conflict with tab lifecycle, recovery and multi-document coordination.

### Interpret source modules or nodes directly for preview

Rejected because the editor would acquire a second execution model that could diverge
from cook validation, runtime kernels, budgets, fallbacks and render scheduling.

### Make graph UI a prerequisite for the stack editor

Rejected because ADR-126 already converges both sources below the UI. Shipping order
does not require a shared canvas or source round-trip.

### Store decals in an independent editor document and sidecar

Rejected because effect decal outputs belong to the effect source and scene placement
belongs to SceneDocument. A third owner would split identity, history and save policy.

### Keep preview running whenever its tab is hidden

Rejected as an implicit CPU/GPU budget consumer with unclear lifetime. Background work
requires an explicit bounded operation; otherwise visibility change suspends/stops it
under the preview host policy.
