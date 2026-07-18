# Scene Runtime Architecture

## Purpose

This document defines the runtime scene model, ECS ownership, entity and
component identity, system scheduling, scene transitions, authoring conversion,
serialization boundaries, and runtime references.

## Core Decisions

- Editor documents and runtime scenes are separate models.
- `RuntimeSceneDefinition` is the immutable handoff into runtime construction.
- One scene runtime owns its registry, systems, physics bindings, and
  scene-scoped resources.
- Entities use generation-checked runtime identities.
- Structural ECS changes occur at explicit synchronization points.
- System order is declared and validated; it is not determined by event order.
- Persistent files store stable logical IDs, never runtime addresses or handles.
- Scene transitions are transactional and generation-aware.

## SCN-001 Phase 1 Contract

The implemented Phase 1 surface is the backend-neutral
`HoroEngine::RuntimeScene` target. It depends only on Foundation and Runtime;
it does not depend on editor documents, renderer backends, assets, physics, or
GUI types.

Phase 1 deliberately stops short of a complete ECS. It provides:

- immutable, validated `RuntimeSceneDefinition` values
- owner-thread `RuntimeScene` storage and allocation-free borrowed views
- `EntityRef { SceneRuntimeId, EntityId }` stale-reference validation
- transactional activation, replacement, unload, and structural command batches
- deferred `Create(RuntimeEntityCreateInfo)` and `Destroy(EntityRef)`
- editor preview extraction from the active runtime scene.

Every create command supplies the complete initial transform, optional existing
parent, optional authored identity, primitive descriptor, and initial typed
component set. Component add/remove, reparent, system scheduling, and parallel
iteration remain future ECS work.

Scene activation and structural batches commit only during
`CommitDeferredLifecycleChanges`. A candidate is prepared without touching the
active scene; a failed preparation or structural batch preserves the active
scene. Only one transition or structural batch may be pending at a time.

Entity generations start at one. Destroyed slots are reused in deterministic
LIFO order after incrementing the generation. A slot at the configured maximum
generation is retired permanently; generations never wrap and therefore cannot
make an ancient reference valid again.

The steady-state path with no pending transition or structural batch performs no
scene-owned heap allocation. Definition building, candidate preparation,
activation, and structural mutation are load/mutation paths and may allocate.

## Model Boundary

```text
SceneDocument
    |
    | validate and convert
    v
RuntimeSceneDefinition
    |
    | instantiate
    v
SceneRuntime
    +-- Registry
    +-- Systems
    +-- PhysicsWorld
    +-- ResourceLeases
```

`SceneDocument` may contain editor metadata, incomplete drafts, selection hints,
and authoring conveniences. Runtime modules do not parse it directly.

`RuntimeSceneDefinition` contains validated, typed, backend-neutral data needed
to instantiate a runtime scene.

## Runtime Identity

```cpp
struct EntityId {
    uint32_t index;
    uint32_t generation;
};
```

Runtime entity IDs are valid only within their owning `SceneRuntimeId`.
Cross-scene references include both identities or use stable logical scene
object IDs resolved during instantiation.

Serialization uses stable IDs:

```text
scene_id
object_id
component_id where required
asset_id
```

Generation counters prevent stale entity references from aliasing newly created
entities.

## Registry Ownership

The scene runtime owns:

- entity allocation and destruction
- component pools
- structural command buffer
- registered systems and execution schedule
- scene-local event queues
- runtime reference resolution

Component pools own component values. A component must not delete a resource
owned by physics, renderer, assets, or another service. Such relationships use
typed handles or explicit leases.

## Component Contract

Components are typed state carriers. They:

- have clear copy/move behavior
- avoid hidden thread synchronization
- do not invoke GUI or transport services
- do not own system scheduling
- declare serialization and runtime-only status
- use stable asset and entity references

Polymorphic gameplay behavior is owned through explicit behavior components
registered by the gameplay module boundary, not hidden in arbitrary component
constructors or destructors. A behavior component stores stable behavior type ID
and serialized fields; runtime behavior instances are created during scene
activation and are driven by declared scene phases.

## Structural Changes

Phase 1 records entity creation and destruction in an owner-thread command
buffer:

```cpp
class SceneCommandBuffer {
public:
    DeferredEntity Create(RuntimeEntityCreateInfo initialState);
    void Destroy(EntityRef entity);
};
```

The runtime applies the full batch transactionally at
`CommitDeferredLifecycleChanges`. A create token resolves to an `EntityRef`
only through a successful `StructuralCommitResult`. References to another
deferred entity in the same batch are not supported in Phase 1.

Component add/remove, reparent, and direct system-owned component writes require
the later ECS access/scheduling contract. They are not silently emulated by the
Phase 1 storage API.

## System Contract

```cpp
struct SystemDescriptor {
    SystemId id;
    SystemPhase phase;
    ComponentAccessSet reads;
    ComponentAccessSet writes;
    std::vector<SystemId> after;
    std::vector<SystemId> before;
};
```

Phases include:

- `PrePhysics`
- `Physics`
- `PostPhysics`
- `Gameplay`
- `Presentation`
- `RenderExtraction`

The scheduler validates cycles, duplicate ownership, and incompatible parallel
access. A stable order is produced for equal dependencies.

Initial implementations may execute systems serially. The access contract still
exists so future parallelism does not require redesigning system ownership.

## Runtime Scene Definition

The definition contains:

- schema version and scene identity
- entity definitions and stable object IDs
- typed component payloads
- asset dependencies
- primitive descriptors for built-in procedural meshes and shapes
- scene references
- required engine capabilities
- initial settings and spawn data

Conversion resolves authoring defaults, primitive descriptors, and imported
asset references. It emits diagnostics. Instantiation does not accept arbitrary
string property bags for built-in component behavior.

Prefab references placed in the authoring document are expanded before the
runtime scene is built. Runtime modules see only the expanded objects, never raw
prefab paths. See [Prefab Architecture](./prefab-architecture.md).

## Load State Machine

```text
Unloaded
  -> Preparing
  -> Activating
  -> Active
  -> Unloading
  -> Unloaded

Active -> PreparingReload -> ActivatingReload -> Active
```

Preparation may validate, load assets, and build CPU-side state on workers.
Activation occurs on the runtime owner thread and either commits a complete
scene or leaves the previous state active.

## Transactional Activation

Loading a replacement scene builds a candidate runtime. It becomes active only
after:

- definition validation succeeds
- required assets and capabilities are available
- systems initialize successfully
- physics and render bindings are ready
- startup hooks succeed

Failure destroys the candidate and preserves the previous active scene where
the operation contract allows it.

## Reload

Reload strategy is explicit per state category:

| State | Default behavior |
|---|---|
| Definition-owned component data | Replace from new definition |
| Runtime transient state | Recreate |
| Explicitly preservable state | Transfer through typed policy |
| Asset handles | Re-resolve or retain valid lease |
| Physics state | Rebuild unless preservation contract exists |

Reload does not copy arbitrary component memory by type name. Preservation uses
typed adapters and stable object IDs.

## Scene References

Scene references are logical and cycle-validated. The runtime defines whether a
reference is:

- embedded
- streamed
- activated as a subscene
- used only as a spawn/template source

Reference loading participates in the parent task group and cancellation tree.
An unloaded parent cannot receive a late child activation.

## Editor Interaction

The editor session owns `SceneDocument`, history, selection, and dirty state.
Document commands produce a new document revision. Runtime preview or play
sessions consume converted definitions.

Runtime notifications may update editor presentation through the explicit
engine-to-editor bridge. They do not mutate the authoring document unless an
editor command explicitly applies a change.

In Phase 1 each committed editor document revision is converted to a new
definition and fully reactivated. Gizmo drag state is an editor-owned render
overlay; cancel removes the overlay and commit first updates the document. The
new transform becomes authoritative only when the replacement scene reaches the
lifecycle commit boundary, so the viewport observes it on the following frame.

Render extraction and picking finish against the old active scene before a
replacement commit. Picking results carry their source `SceneRuntimeId` and are
discarded when that ID no longer matches the active runtime. Editor selection is
owned as logical `SceneObjectId`: it resolves to the replacement scene when the
object remains and is cleared when the authored object was deleted. Editor state
does not retain an old runtime's `EntityRef`.

## Data Bus Relationship

System-to-system behavior inside one deterministic tick uses direct scheduling,
typed queues, or declared runtime services. It does not depend on unordered
process event delivery.

After committed lifecycle changes, the runtime may publish:

- `SceneLoadedEvent`
- `SceneReloadedEvent`
- `SceneUnloadedEvent`
- `SceneRuntimeFailedEvent`

Payloads remain bounded and identify the authoritative runtime state to query.

## Serialization

Authoring serialization, cooked runtime scene serialization, and in-memory ECS
layout are separate formats.

Runtime binary formats are:

- versioned
- bounds checked
- endian and alignment explicit
- validated before allocation
- independent from C++ object memory layout

## Testing

Required tests cover:

- stale entity generation rejection
- component ownership and destruction
- deferred structural changes during iteration
- deterministic system order and cycle rejection
- document-to-runtime conversion diagnostics
- transactional load and failed activation rollback
- reload preservation policies
- nested reference cancellation and cycle detection
- scene unload with jobs, physics, and render leases
- binary format corruption and version behavior

## Related Documents

- [Runtime Lifecycle](./runtime-lifecycle.md)
- [Built-In Scene Primitives](./built-in-scene-primitives.md)
- [Project Model](../editor/project-model.md)
- [Asset Pipeline](./asset-pipeline.md)
- [Prefab Architecture](./prefab-architecture.md)
- [Physics Architecture](./physics-architecture.md)
- [Rendering Architecture](./rendering-architecture.md)
- [Editor Document Model](../editor/editor-document-model.md)
