# Gameplay Module Boundary

## Purpose

This document defines the native gameplay module boundary: project-owned C++ module loading, binary ownership, registration, capability exposure, services, hot reload, and diagnostics.

## Project Module

```text
MyGame/
  source/gameplay/
    GameModule.cpp
  CMakeLists.txt
```

The project consumes the exported gameplay SDK package. Project scaffolding must
not compile the engine with `add_subdirectory()`:

```cmake
find_package(HoroEngineGameplay CONFIG REQUIRED)
horo_add_gameplay_module(HoroGameGameplay
    MODULE_ID game.my_game
    SOURCES ${HORO_GAMEPLAY_SOURCES}
    INPUTS ${PROJECT_OWNED_TRANSITIVE_INPUTS}
)
```

`HoroEngineGameplay` exports `HoroEngine::GameplayApi`, the annotation code
generator, module helper, and exact SDK/toolchain metadata. Consumer configure
rejects a mismatched compiler family/version, target OS/architecture, generator
platform/toolset, C++ standard, or ABI-affecting runtime contract. Module load
then validates the exact SDK fingerprint before any project factory executes.

`INPUTS` is the explicit dependency boundary for project-owned inputs outside
`source/`, `include/`, and `cmake/`. Every path is canonicalized inside the
project root; traversal and symlink escape are rejected. The helper writes the
resolved input manifest consumed by freshness checks.

## Binary Boundary

Native modules are created and destroyed through exported entry points, not by
letting the host `delete` an object allocated by the module:

```cpp
struct GameModuleDescriptor {
    GameModuleId moduleId;
    uint32_t sdkBoundaryVersion;
    BuildFingerprint buildFingerprint;
};

class IGameModule {
public:
    virtual ~IGameModule() = default;

    virtual Result<void> Register(GameRegistrationContext&) = 0;
    virtual Result<void> Start(GameRuntimeContext&) = 0;
    virtual void Stop(GameRuntimeContext&) = 0;
};

extern "C" HORO_GAME_EXPORT const GameModuleDescriptor* GetGameModuleDescriptor();
extern "C" HORO_GAME_EXPORT IGameModule* CreateGameModule();
extern "C" HORO_GAME_EXPORT void DestroyGameModule(IGameModule*) noexcept;
```

The C++ virtual interface crosses the module boundary only within one compatible
SDK/build generation. It is not a stable long-term plugin ABI. Native modules
must be rebuilt when the SDK boundary version, compiler family, standard
library, or incompatible build settings change.

The exported names are stable for one SDK generation. A module is loaded only
when its SDK boundary version and build fingerprint are compatible with the
host. The host reads `GetGameModuleDescriptor()` before creating the module
object; incompatible descriptors fail before gameplay code is started. The
pointer returned from `CreateGameModule()` is valid only until
`DestroyGameModule()` returns. The host must not retain module object
references, callbacks, function pointers, type-erased deleters, or service
objects after destruction.

Module-allocated memory is released by the module. Host-allocated memory is
released by the host. Registries copy descriptor data they need after
`Register()` returns and do not retain references, `std::string_view` values,
spans, callbacks, or containers that point into temporary module-owned storage
unless the descriptor explicitly declares a module-owned lifetime that is shorter
than module unload.

Hot reload follows the same boundary: the host stops the module, drains or
invalidates module-owned callbacks and jobs, destroys the module object through
`DestroyGameModule()`, unloads the dynamic library only after no module-owned
code can be called, and then creates a fresh module instance from the replacement
library.

Lifecycle order is:

```text
LoadLibrary
  -> GetGameModuleDescriptor
  -> GetGameplayDescriptorBundle
  -> validate manifest identity, complete registrations, diagnostics, and lifecycle callbacks
  -> CreateGameModule
  -> Start
  -> Stop
  -> DestroyGameModule
  -> UnloadLibrary
```

If `Register()` fails, `Start()` is not called and the module object is
destroyed. If `Start()` fails, the host calls `Stop()` before destruction.
`Stop()` must be safe after partial startup, idempotent for module-owned
services, and must not throw.

The idiomatic startup pattern is staged acquisition with registered cleanup. A
module records each successfully acquired startup resource in a module-owned
startup scope before acquiring the next resource. Failed `Start()` unwinds only
the resources that were actually acquired, then `Stop()` observes an already
safe partially-started state. The concrete helper type for startup scopes is
defined by the SDK, but gameplay code should not implement cleanup by guessing
which line of `Start()` failed.

## Registration

Game registration may contribute:

- component descriptors and serialization adapters
- scene conversion adapters for game-owned component payloads
- gameplay systems and their phase/access descriptors
- game service descriptors and factories
- input actions and default bindings
- asset type handlers owned by the game
- project settings descriptors
- console or diagnostic commands approved for runtime use

Registration does not mutate an active scene or start background work.
Duplicate IDs and incompatible descriptors fail module startup.

Registration receives only descriptor registries and diagnostics:

```cpp
struct GameRegistrationContext {
    GameModuleId moduleId;
    ComponentRegistry& components;
    SceneConversionRegistry& sceneConversion;
    SystemRegistry& systems;
    GameServiceRegistry& services;
    InputActionRegistry& inputActions;
    AssetTypeRegistry& assetTypes;
    SettingsRegistry& settings;
    RuntimeCommandRegistry& commands;
    RuntimeDiagnostics& diagnostics;
};
```

`GameRegistrationContext` does not expose active scenes, jobs, assets, editor
state, renderer backends, or global service lookup. Descriptor registration is
declarative and is frozen before scene activation. Registries validate stable
IDs, schema versions, dependency references, and descriptor compatibility before
`Start()` is called.

Game-owned IDs use the `game.<project_or_module>.*` namespace. Engine-owned IDs
use `engine.*`. Duplicate IDs across all loaded engine and game descriptors fail
registration; registration never selects one conflicting descriptor by load
order.

Manual `Register()` code is for native/static non-behavior descriptors that
cannot be discovered from project assets or annotated source. Object-attached
behaviors are never manually registered in project code. Native behaviors
declared with `HORO_BEHAVIOR`, script-authored behaviors, and visual graphs are
registered only by the build/script discovery pipeline after scanning their
project inputs.

Generated behavior descriptor registration still commits through the same
internal validation rules before `Start()` is called, but the public
`GameRegistrationContext` does not expose `BehaviorRegistry`. Discovery owns
finding behavior sources; the host owns accepting or rejecting the generated
behavior descriptors.

Generated behavior descriptors are delivered to the host as a build artifact,
not through project-authored registration code:

```cpp
struct GeneratedGameplayDescriptorBundle {
    uint32_t schemaVersion;
    uint32_t sdkBoundaryVersion;
    GameModuleId moduleId;
    BuildFingerprint buildFingerprint;
    DescriptorSetRevision descriptorRevision;
    std::span<const BehaviorDescriptorMetadata> behaviors;
    std::span<const BehaviorFactoryBinding> nativeFactoryBindings;
    std::span<const BehaviorFieldMigrationDescriptor> behaviorMigrations;
    std::span<const GeneratedDescriptorDiagnostic> diagnostics;
    GameModuleLifecycleCallbacks lifecycle;
};
```

The host accepts the bundle only when its module ID, fingerprint, descriptor
revision, schema, and SDK boundary match the validated artifact manifest and
module descriptor. It validates those fields, bounded counts, factory/type-ID
pairings, diagnostics, and lifecycle callbacks before invoking project code.
The bundle is a complete descriptor snapshot for that
fingerprint and descriptor revision; partial behavior bundles are not accepted.
Incremental builds may regenerate only changed files internally, but the
artifact handed to the host represents the full current generated behavior set.
The bundle is validated in the same registration transaction as native/static
descriptors. Duplicate IDs, schema conflicts, invalid schedule dependencies,
stale generated output, or descriptor diagnostics reject the bundle before
runtime scene activation.

Generated behavior bundles are metadata snapshots plus module-owned factory
bindings. Metadata such as type IDs, fields, dependencies, phase access,
schedule nodes, and migrations is copied into host registries during validation.
Native factories are resolved from the loaded module through
`BehaviorFactoryBinding` records and are invalidated before `Stop()`,
`DestroyGameModule()`, or dynamic-library unload. The host must not keep factory
function pointers, deleters, spans, or string views into module memory after the
module lifetime ends.

When a new complete bundle omits a previously known behavior ID, the generated
descriptor is withdrawn at the next reload safe point. Scene data that still
references the removed ID is preserved as an unknown behavior payload and cannot
activate until a compatible descriptor returns or an explicit editor migration
updates the reference.

Examples of non-behavior descriptors live in
[Gameplay Runtime Integration](./gameplay-runtime-integration.md). This boundary
document only defines when registration happens, which capabilities it receives,
and which ownership rules it must obey.

Script-discovered behavior registration is generated from assets:

```text
assets/scripts/DoorController.horo_script
assets/scripts/DoorController.horo_script.meta
  -> ScriptBehaviorScanner
  -> generated BehaviorDescriptor {
         typeId = stable ID from sidecar
         authoring = fields/dependencies from script declaration
         factory/runner = scripting runtime binding
     }
  -> BehaviorRegistry
```

Annotated native behavior registration follows the same generated path:

```text
src/gameplay/DoorControllerBehavior.cpp
  -> NativeBehaviorScanner
  -> generated BehaviorDescriptorMetadata {
         typeId = stable ID from HORO_BEHAVIOR
         authoring = display fields/dependencies from HORO_BEHAVIOR
         phases = [ phase descriptors from HORO_BEHAVIOR ]
     }
  -> generated BehaviorFactoryBinding {
         typeId = stable ID from HORO_BEHAVIOR
         factory = MakeBehaviorFactory<DoorControllerBehavior>
     }
  -> BehaviorRegistry
```

## Capability Context

```cpp
struct GameRuntimeContext {
    SceneRuntimeAccess& scenes;
    AssetAccess& assets;
    JobSubmission& jobs;
    RuntimeConfiguration& configuration;
    RuntimeDiagnostics& diagnostics;
};
```

The context exposes narrow capabilities. It does not expose `Application`,
`EditorLayer`, renderer backend objects, global service lookup, or unrestricted
platform APIs.

Capability objects declare whether they are main-thread only, runtime-thread
only, or thread-safe. Game jobs must not capture non-thread-safe capabilities or
scene access objects unless the capability explicitly provides a thread-safe
snapshot or command queue. Background work returns results through declared job
continuations and synchronization points rather than mutating runtime scenes
directly.

Platform access is mediated through approved SDK wrappers. Gameplay modules do
not call unrestricted filesystem, thread, clock, locale, process, or platform
certification APIs directly. See
[Platform Abstraction](../foundation/platform-abstraction.md) for path,
directory, atomic file, and platform-service contracts.

### Cinematic Control Outcomes

[ADR-118](../../adr/118-animation-character-and-gameplay-authority-during-cinematics.md)
requires gameplay modules to submit ordinary typed animation-parameter, movement,
heading, stance and action commands even while a cinematic runs. The receiving owner
resolves each channel against the tick's immutable authority claims and returns
`AcceptedGameplay`, `AcceptedForOwnerBlend`, `SuppressedByCinematic`,
`StaleAuthority` or `AuthorityDenied` as appropriate.

Modules do not poll a global cinematic flag, write poses/transforms directly or queue
suppressed edge actions until a sequence stops. Unclaimed channels and unrelated
simulation continue when fixed ticks run. Whole-game pause attempts no Gameplay,
Animation, Character or Physics tick; collision-aware cinematic movement instead
uses scoped control transfer while simulation remains active.

### Cinematic Playback Capability

[ADR-122](../../adr/122-cinematic-trigger-sources-and-capability-policy.md)
forbids gameplay modules and scripts from discovering `CinematicRuntimeService` or
constructing players. A module that declares and receives
`cinematic.playback.start` gets a narrow `ICinematicPlaybackCapability` in its runtime
context. Requests are scoped to the caller principal, runtime session, world role,
allowed sequence/effect set and owner authority; knowing an asset ID is not a grant.

The same typed application admission serves native/script gameplay, cooked scene
autoplay and committed gameplay-event adapters in development and packaged profiles.
A client capability cannot acquire server-owned Character/gameplay/Physics/pause
authority. Module/script unload and grant revocation close admission and generation-
fence late preparation/start results. Denial returns a typed result and creates no
player, lease, event, Audio/VFX request or partial effect.

### VFX Gameplay Payload Capability

[ADR-123](../../adr/123-vfx-cpu-stage-order-determinism-and-gameplay-coupling.md)
keeps CPU particle storage and stage execution inside `VfxWorld`. Gameplay modules and
scripts can submit only declared schema/range-checked `GameplayInput` parameters before
the VFX tick cutoff and consume bounded `GameplayOutput` occurrences or effect-level
aggregate snapshots after VFX commit at their owner boundary. They never receive a
mutable SoA span, particle slot/index, stage callback or arbitrary force/collision
kernel hook.

Private/render channel access, wrong-stage writes, stale scene/emitter/schema identity,
nonfinite values and output-capacity failure return typed results with zero mutation.
GPU readback and Null/visual output cannot drive authoritative gameplay. A behavior
needing particle-derived gameplay declares a CPU-mandatory compiled unit and admits
its worst-case work/output capacity.

[ADR-128](../../adr/128-vfx-spawn-event-mapping-pooling-and-budget-enforcement.md)
places event-to-effect choice in an application-owned cooked
`GameplayVfxBindingTable`. Ordinary modules/scripts receive a capability scoped to
declared semantic event IDs or typed effect tags; they do not discover internal
emitters, inspect the binding table or gain spawn authority merely by knowing an asset
or tag ID. The application adapter resolves one immutable binding generation and
submits bounded requests carrying stable occurrence/layer identity.

The binding fixes payload schema, ownership/request class, finite fan-out, compatible
quality variants and overload policy. Load rejects duplicate semantic ownership,
unbounded mappings or required behavior paired with cosmetic delay/eviction. Queue,
pool and budget denials return typed correlated results; gameplay cannot block, grow a
pool or bypass the reserved required capacity.

## Services

A game module may create project- or runtime-scoped services through explicit
owned factories. Every service declares:

- owner scope
- dependencies
- thread affinity
- startup and shutdown
- scene replacement behavior
- observability category

Services cannot hide lifetime in static initialization.
Service descriptors and factories are registered during `Register()` through
`GameRegistrationContext::services`. Service instances are created during
`Start()` or scene activation according to their declared scope. Project-scoped
services outlive scene-scoped systems and behaviors that depend on them;
scene-scoped services are created before dependent scene instances and destroyed
after dependent behaviors, systems, and jobs are drained.

## Canonical Runtime Persistence

Gameplay modules contribute runtime-save state only through the subsystem-owned
`CanonicalStateParticipantDescriptor` and `ICanonicalStateAdapter` contract in
[ADR-114](../../adr/114-canonical-runtime-world-persistence-boundary.md). The
descriptor is inert metadata declaring stable participant/schema identity, scope,
required policy, dependencies, bounds and owned field/type IDs. Host composition
binds the adapter and rejects duplicate semantic ownership.

A component or service does not gain persistence authority from reflection, a
serialized authoring field or trivially-copyable layout. Its owning gameplay adapter
captures stable semantic values/references at the aggregate save safe point and
prepares detached restore state for one no-fail publication. Pointers, behavior/service
instances, callbacks, jobs, queues, native handles and module-local indices are never
durable payloads. Unknown or incompatible required adapters fail before live mutation.

One semantic field belongs to one participant. A gameplay service and ECS component
cannot both save authoritative copies. Account/profile values remain outside runtime
slots, and client modules cannot persist replicated server state as local authority.

## Native Hot Reload

Data, scene, shader, and asset hot reload follow their owning subsystem
contracts.

Native gameplay code reload, when enabled in development:

- occurs only at a runtime safe point
- stops affected systems and joins module-owned jobs
- transfers only explicitly preservable state through a hot-reload adapter
- invalidates all module-owned callbacks and function pointers
- reloads through a versioned module boundary
- recreates systems and restores compatible state

The project module build publishes `.horo/local/gameplay_module.json` only after
the complete dynamic library and generated descriptor bundle succeed. The
manifest contains the absolute local artifact path, module ID, exact host SDK
fingerprint, and descriptor revision. It is machine-local build evidence and is
never scene or source identity.

The editor loads each candidate from a unique shadow copy. It validates the
manifest, SDK fingerprint, exported module descriptor, and complete descriptor
revision before changing the active registry. Native and script descriptors are
then committed into one frozen registry transaction. A duplicate ID across the
two sources rejects the candidate rather than selecting by load order.

During Play, an accepted candidate waits for the next fixed-tick boundary. The
old behavior runtime is shut down while its module remains loaded, replacement
instances are created against the unchanged runtime scene, and only then is the
old module released. Candidate activation failure reconstructs the previous
runtime against the same scene; if rollback also fails, the play session enters
the explicit failed state.

If any unload precondition cannot be proven, the editor requests a play-session
or process restart instead of unloading unsafe C++ code.

Shipping builds do not load unsigned replacement gameplay code.

Hot-reload preservation is an implementation/session migration and does not
implicitly register a durable canonical participant, schema or compatibility promise.

## Errors And Diagnostics

Game registration, startup, and scene construction return typed errors with
game-namespaced stable codes. Unhandled exceptions are caught at the module
boundary and converted to fatal module errors.

Gameplay errors use the shared `Result<T>` and `Error` model from
[Error And Diagnostics](../foundation/error-and-diagnostics.md). Branching uses
stable `ErrorCode` values, not developer-facing messages. Game-owned codes are
namespaced by project or module:

```text
game.<project>.module.incompatible_sdk
game.<project>.module.missing
game.<project>.registration.duplicate_component
game.<project>.registration.incompatible_system_access
game.<project>.component.unsupported_schema
game.<project>.component.unknown_descriptor
game.<project>.behavior.unknown_type
game.<project>.behavior.lifecycle_failed
game.<project>.scene.reference_unloaded
game.<project>.scene.reference_stale
game.<project>.dependency.missing
game.<project>.build.failed
game.<project>.script.compile_failed
game.<project>.script.reload_incompatible
game.<project>.visual_script.invalid_graph
game.<project>.startup.service_dependency_missing
game.<project>.hot_reload.unsafe_unload
```

Host adapters may map these codes to CLI exit status, MCP error responses, or
editor dialogs, but the gameplay module does not branch on localized text.
Diagnostics may include bounded metadata such as component type ID, system ID,
schema version, module ID, or phase. They must not include secrets, raw source
files, unbounded serialized component payloads, or environment dumps.

Log and metric categories use a project namespace:

```text
game.<project>.gameplay
game.<project>.ai
game.<project>.save
```

## Related Documents

- [Gameplay Module Overview](./gameplay-module.md)
- [Gameplay Behavior Authoring](./gameplay-behavior-authoring.md)
- [Gameplay Runtime Integration](./gameplay-runtime-integration.md)
- [Horo Package System](../packages/package-system.md): imported game library modules
- [Build System](../delivery/build-system.md)
- [Ownership And Resource Lifetime](../foundation/ownership-and-resource-lifetime.md)
- [ADR-114](../../adr/114-canonical-runtime-world-persistence-boundary.md): canonical
  runtime-state adapters and single semantic ownership.
- [ADR-118](../../adr/118-animation-character-and-gameplay-authority-during-cinematics.md):
  cinematic control claims, typed gameplay suppression and owner-preserving pose/
  movement integration.
