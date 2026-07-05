# Gameplay Runtime Integration

## Purpose

This document defines how project-owned gameplay modules integrate with runtime systems: game-owned assets, input actions, scheduled systems, scene/play lifecycle, game-owned component persistence, and deferred runtime extension points.

## Game-Owned Asset Types

Game modules may register asset types for source assets owned by the project:

```cpp
struct GameAssetTypeDescriptor {
    AssetTypeId typeId;
    std::span<const FileExtension> sourceExtensions;
    uint32_t importerVersion;
    AuthoringMetadata authoring;
    AssetImporterFactory importer;
    AssetCookerFactory cooker;
    RuntimeAssetLoaderFactory runtimeLoader;
};
```

The asset pipeline owns import, cook, cache invalidation, and dependency
tracking. Game importers and cookers are deterministic functions over source
files, sidecar metadata, target profile, and declared dependencies. They do not
mutate scenes or editor state during import.

Editor asset browsers use the descriptor's authoring metadata to show
game-owned asset types, icons, validation diagnostics, and import settings.
Runtime code accesses loaded assets through `AssetAccess` handles or leases, not
raw file paths. Asset loads may be asynchronous through the asset system's task
contract; gameplay jobs request work through approved asset APIs instead of
spawning ad hoc import or load threads.

Hot reload of game-owned assets follows the Asset Pipeline contract. Stable
logical asset IDs survive recook and reload. Runtime asset handles either retain
a valid lease to the old asset until a synchronization point or re-resolve to the
new revision through a typed invalidation event. A module unload invalidates
runtime loaders and callbacks owned by that module before the dynamic library is
unloaded.

## Input Actions

Game modules register semantic input actions and default bindings through
`InputActionRegistry`. Bindings are data, not hardcoded polling:

```cpp
struct ActionDescriptor {
    ActionId id;
    ActionValueType valueType;
    InputContextId context;
    std::span<const InputBinding> defaultBindings;
    InputConsumptionPolicy consumption;
};
```

The input system resolves device bindings, context priority, conflict
diagnostics, and rebinding UI from descriptors. Duplicate action IDs fail
registration. Binding conflicts are reported through the input configuration
validator; the gameplay module does not resolve conflicts by observing raw
device state.

Behaviors consume input from an immutable, tick-assigned gameplay input view in
`BehaviorContext`. Fixed-step behavior sees the input frame assigned to that
simulation tick. Presentation update may read presentation-safe held values for
camera or visual response, but edge-triggered gameplay actions are consumed only
through fixed-step policy. Behavior code does not read live platform input or GUI
input directly.

`InputConsumptionPolicy` declares how transitions behave when one input frame
maps to several simulation ticks or when gameplay contexts overlap:

- `PerAssignedTick`: a transition is visible only to the fixed tick it was
  assigned to
- `BufferedUntilConsumed`: a transition remains available until one eligible
  consumer consumes it or a declared timeout expires
- `HeldOnly`: only current held value is exposed; no edge transition is emitted
- `BroadcastReadOnly`: multiple consumers may read the value but none consumes it

Context priority is resolved before gameplay receives an input frame. GUI,
editor tool, modal, and gameplay contexts do not race for the same raw device
transition. If two gameplay consumers observe the same action, behavior depends
on the action's policy and declared gameplay routing; it is not determined by
callback order.

## Gameplay Systems

Game systems follow [Scene Runtime](../runtime/scene-runtime.md):

- declare phase and component access
- use fixed update for deterministic simulation
- use presentation phases for interpolation and visual state
- buffer structural ECS changes
- do not depend on data-bus event ordering inside one tick

Systems may publish committed low-frequency lifecycle notifications through
approved process event types. Per-entity or per-contact tick traffic remains in
scene-owned buffers and direct runtime APIs.

System scheduling is descriptor-driven:

```cpp
struct SystemDescriptor {
    ScheduleNodeId id;
    SystemPhase phase;
    ComponentAccessSet reads;
    ComponentAccessSet writes;
    std::vector<ScheduleNodeId> after;
    std::vector<ScheduleNodeId> before;
};
```

Runtime phases are shared with Scene Runtime:

- `PrePhysics`: deterministic fixed-step preparation before physics
- `Physics`: physics-owned fixed-step simulation
- `PostPhysics`: deterministic fixed-step reads of physics results
- `Gameplay`: deterministic fixed-step gameplay simulation
- `Presentation`: variable-rate interpolation and visual-only state
- `RenderExtraction`: variable-rate render snapshot extraction

Fixed-step phases must not depend on render frame rate. Presentation and
render-extraction phases must not mutate simulation-authoritative state.
`reads` and `writes` declare component access for validation, ordering, and
future parallel execution. Structural entity/component changes are buffered and
committed only at synchronization points owned by Scene Runtime.

## System Instance Ownership

System registration binds a descriptor to module-owned logic:

```cpp
struct SystemRegistration {
    SystemDescriptor descriptor;
    SystemFactory factory;
    SystemLifetime lifetime = SystemLifetime::SceneScoped;
};
```

Runtime-created system instances are scene-scoped unless the registration
explicitly declares a narrower or wider lifetime. The host owns the runtime
handle that schedules a system, but the module owns the code and any
module-allocated state behind that system. All module-owned system instances,
callbacks, queued continuations, and jobs are destroyed or invalidated before
`Stop()` returns and before the dynamic library is unloaded.

System callbacks are invalid after module stop. A scene cannot keep a callable,
vtable pointer, function pointer, or type-erased deleter that points into an
unloaded game module.

## Scene And Play Lifecycle

Runtime scene activation creates system and behavior instances after all
required descriptors, assets, and services are available. Scene unload calls
behavior disable/destroy hooks and destroys scene-scoped systems before releasing
component pools and module-owned scene resources. The host guarantees this order
for normal scene unload, play-session stop, failed reload unwind, and module
stop.

Scene transitions are requested through scene/runtime use cases exposed by
`SceneRuntimeAccess`, not by directly replacing host state. A behavior may
request a transition, spawn, or additive scene operation only through the
declared command or request API. The runtime decides when the request commits and
which active scene, if any, remains valid during failure.

Additive scenes are separate `SceneRuntime` instances with distinct
`SceneRuntimeId` values, registries, behavior instances, and scene-scoped
services. A `SystemLifetime::SceneScoped` system is scoped to one runtime scene.
Cross-scene references use stable logical IDs or explicit runtime reference
handles and must be resolved before use:

```cpp
struct RuntimeEntityRef {
    SceneRuntimeId scene;
    EntityId entity;
};

Result<EntityId> ResolveEntityRef(SceneRuntimeAccess&, RuntimeEntityRef);
```

Resolution is generation-checked and scene-aware. If the target scene unloaded,
the entity generation no longer matches, or the reference is outside the
caller's allowed scene set, resolution returns a typed error. Behavior code may
cache `RuntimeEntityRef`, but must not cache a resolved `EntityId` across scene
unload or scene replacement.

Play-in-editor creates a runtime scene from a converted snapshot of the
authoring document. Runtime behavior mutates only the play-session scene, not
the authoring `SceneDocument`. Stopping play runs behavior shutdown, destroys the
runtime scene, releases module-owned play-session resources, and leaves the
authoring document at its pre-play revision unless an explicit editor command
imports a runtime result. `OnDisable()` and `OnDestroy()` are guaranteed during
normal PIE stop and other controlled scene shutdown paths. Forced process
termination, crash, or OS kill provides no gameplay callback guarantee; the OS
reclaims process resources. Host-level emergency shutdown still invalidates
module callbacks and releases owned host resources where the platform permits,
but behavior authors must not rely on `OnDestroy()` for durable persistence.

## Game-Owned Components

Each serializable game component has:

- stable type ID
- schema version
- typed authoring/runtime representation
- validation
- conversion or construction adapter
- deterministic serialization
- explicit upgrade support for persistent content formats

C++ type names, RTTI names, addresses, and compiler-specific layout are not
persistent identity.

Save-game serialization is not defined by this document. When a save system
persists game-owned component or service state, it must use stable component
IDs, schema versions, and explicit upgrade paths rather than C++ layout
identity.

Game modules may later participate in a save system through explicit save
descriptors or hooks. That contract is intentionally separate from scene
authoring serialization. `BehaviorComponent.fields` are authoring/default data;
runtime behavior instance state is included in a save snapshot only if the
behavior or service declares a stable save payload and schema version. Save data
uses the same stable component/behavior IDs and upgrade rules as persistent
content, but it is not allowed to serialize raw C++ object memory, function
pointers, entity runtime addresses, or module allocator ownership.

## Deferred Runtime Extension Points

Networking, save-game, memory budgeting, and advanced platform integration are
separate runtime contracts, but gameplay module contracts must leave room for them.

Future networking support must declare replicated component or behavior state
through stable descriptors, explicit authority policy, prediction/rollback
contracts where used, and schema-versioned network payloads. Behavior code must
not infer network authority from local process state or mutate replicated state
outside declared simulation phases.

Memory budget participation is required for production and console targets.
Development-only targets may run without enforced budgets, but allocations must
still be attributable when tracking is enabled. The concrete allocator and
budget API belongs to a future memory-system contract and must align with
[Ownership And Resource Lifetime](../foundation/ownership-and-resource-lifetime.md)
and [Observability Performance](../observability/observability-performance.md).
This document requires that module-owned long-lived allocations, asset caches,
behavior instances, services, and job scratch buffers are attributable to a
project/module category. A module may use a custom allocator only when it
reports budget ownership and obeys the binary-boundary allocation rules above.

## Related Documents

- [Gameplay Module Overview](./gameplay-module.md)
- [Gameplay Module Boundary](./gameplay-module-boundary.md)
- [Gameplay Behavior Authoring](./gameplay-behavior-authoring.md)
- [Scene Runtime](../runtime/scene-runtime.md)
- [Runtime Lifecycle](../runtime/runtime-lifecycle.md)
- [Asset Pipeline](../runtime/asset-pipeline.md)
- [Input Architecture](../runtime/input-architecture.md)
- [Horo Package System](../packages/package-system.md): library-provided services and game-owned asset types
