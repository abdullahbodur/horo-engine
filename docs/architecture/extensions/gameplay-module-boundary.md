# Gameplay Module Boundary

## Purpose

This document defines the native gameplay module boundary: project-owned C++ module loading, binary ownership, registration, capability exposure, services, hot reload, and diagnostics.

## Project Module

```text
MyGame/
  src/
    GameModule.cpp
    systems/
    components/
  CMakeLists.txt
```

The project links public engine targets through `add_subdirectory()` or an
installed SDK package as defined by the build architecture.

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
  -> CreateGameModule
  -> Register
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
    GameModuleId moduleId;
    BuildFingerprint buildFingerprint;
    DescriptorSetRevision descriptorRevision;
    std::span<const BehaviorDescriptorMetadata> behaviors;
    std::span<const BehaviorFactoryBinding> nativeFactoryBindings;
    std::span<const BehaviorFieldMigrationDescriptor> behaviorMigrations;
    std::span<const GeneratedDescriptorDiagnostic> diagnostics;
};
```

The host accepts the bundle only when it matches the loaded module fingerprint
and SDK boundary. The bundle is a complete descriptor snapshot for that
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

## Native Hot Reload

Data, scene, shader, and asset hot reload follow their owning subsystem
contracts.

Native gameplay code reload, when enabled in development:

- occurs only at a runtime safe point
- stops affected systems and joins module-owned jobs
- serializes only explicitly preservable state
- invalidates all module-owned callbacks and function pointers
- reloads through a versioned module boundary
- recreates systems and restores compatible state

If any unload precondition cannot be proven, the editor requests a play-session
or process restart instead of unloading unsafe C++ code.

Shipping builds do not load unsigned replacement gameplay code.

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
