# Current Gameplay Module Contract Audit

- Audit status: Implementation snapshot
- Date: 2026-09-02
- Issue: [[GAM-001.1]](https://github.com/abdullahbodur/horo-engine/issues/143)
- Jira: [HORO-143](https://horo-engine.atlassian.net/browse/HORO-143)
- Parent: [[GAM-001]](https://github.com/abdullahbodur/horo-engine/issues/61)

## Purpose

This report records the native gameplay module contract implemented at the audit
date. It is evidence for evolving that contract; it is not a replacement for the
normative [Gameplay Module Boundary](./gameplay-module-boundary.md). Where this
report and the normative architecture differ, the difference is an implementation
gap rather than permission to weaken the architecture.

The audit covers the exported SDK, generated descriptor bundle, build and
publication path, dynamic loading, registry ownership, editor discovery, Play
reload, scene persistence, platform behavior, callers, and tests.

## Executive Result

The implemented native boundary is one project-owned shared library intended to
be built against the exact Horo gameplay SDK generation. Four C-linkage symbol
names expose C++ objects and function pointers; this is an exact-generation C++
ABI, not a stable C ABI. The current fingerprint does not prove the full intended
identity. The only generated registrations are object-attached native behaviors.
The editor builds and discovers the library, merges native and Lua behavior
registrations into one frozen registry, and replaces native behavior instances at
a fixed-tick safe point.

The implementation does not yet provide the broader registration, capability,
service, component, asset-type, package, packaged-player, multi-module, degraded
mode, or transactional native hot-reload contracts described by the normative
architecture. Those gaps remain owned by focused follow-up tickets listed below.

## Implemented End-to-End Flow

```text
project CMake + source inputs
  -> horo_add_gameplay_module(HoroGameGameplay)
  -> regex annotation scanner
  -> generated behavior bundle translation unit
  -> exact-SDK shared library
  -> candidate_gameplay_module.json
  -> GameplayBuildService validates a shadow-loaded candidate
  -> gameplay_module.json + gameplay_build_state.json
  -> ProjectGameplayRegistry shadow-loads the published artifact
  -> native registrations + discovered Lua registrations
  -> frozen BehaviorRegistry
  -> editor Play behavior instances
  -> fixed-tick replacement with rollback to the previous registry
```

There is no production caller that composes this path into a packaged player.
Current production composition is editor-side; module host, registry, and build
service tests exercise the lower layers directly.

## Inventory And Status

Status terms mean:

- **Implemented**: an active caller and focused evidence cover the stated path.
- **Partial**: a usable path exists but omits part of the normative contract or
  validation surface.
- **Undocumented**: active behavior was found in code but was not stated precisely
  in the normative contract.
- **Missing**: the normative or planned capability has no implementation path.
- **Dead**: code has no production or test caller and does not participate in the
  current contract.

| Contract element | Status | Current evidence and boundary |
| --- | --- | --- |
| Exported gameplay SDK package | Implemented | `HoroEngineGameplay` exports `HoroEngine::GameplayApi`, CMake helpers, the generator, and exact compiler/platform fingerprint checks. |
| Primary native gameplay library | Implemented | `horo_add_gameplay_module` builds one conventional `HoroGameGameplay` target; build input and published manifest paths are project-global, so multiple primary modules are not composed. |
| C-linkage entry-point names | Implemented | Descriptor, generated bundle, create, and destroy symbols are mandatory. Their signatures cross C++ types and virtual interfaces. |
| Generated native behavior bundle | Partial | A complete array is emitted for scanner matches, but scanning is regex-based, descriptor revision is fixed at `1`, and migrations or generated diagnostics are absent. |
| Module artifact manifest | Partial | Schema `1` records module ID, fingerprint, descriptor revision, and absolute artifact path. It has no artifact digest, size, architecture, configuration, or toolchain identity. |
| Asynchronous project build | Implemented | Requests are coalesced; input hashing, project lock, cancellation, timeouts, configure/build, candidate load validation, and last-success preservation exist. |
| Build publication integrity | Partial | State records artifact hash and toolchain evidence, but the published manifest does not bind the artifact digest and discovery does not compare it with build state. `IsUpToDate` checks inputs and manifest existence, not artifact identity. |
| Platform dynamic loading | Partial | POSIX uses canonical paths with immediate, local `dlopen` resolution. Windows canonicalizes but uses `LoadLibraryA`; Unicode paths and dependency-search isolation are not covered. macOS uses the generic POSIX path without signing-specific policy. |
| Module descriptor validation | Implemented | Host checks exact struct size, boundary version, bounded text fields, exact fingerprint, module identity, bundle revision, and descriptor/bundle agreement before factory creation. |
| Behavior registration | Implemented | Native bundle records and Lua descriptors are validated into one registry; duplicates reject the candidate and the registry is sorted and frozen. |
| Registration transaction | Partial | Native and Lua contributions are accumulated before activation and any diagnostic blocks Play, but there is no general `GameRegistrationContext`, component/system/service/asset registration, or host-owned metadata copy separate from factory bindings. |
| Module lifecycle | Partial | Load calls `Create`, then `Start`; unload calls `Stop`, module-owned `Destroy`, registry release, library unload, and shadow deletion. `GameRuntimeContext` is empty and there is no separate `Register` phase. |
| Behavior runtime lifecycle | Implemented | Runtime creates instances through module-owned factories and calls defined behavior lifecycle phases; instances are destroyed before their registry/module owner is released. |
| Scene behavior persistence | Implemented | Scenes preserve instance ID, stable behavior type ID, schema version, enabled state, and tagged field values without requiring the module to be loaded. |
| Schema evolution and unknown payloads | Partial | Raw behavior data survives ordinary save/load and an unknown type can remain in the scene model, but activation requires a current descriptor and no field migration path exists. Strict parsing rejects unsupported field encodings. |
| Editor discovery | Implemented | Project discovery validates a bounded manifest, shadow-loads native code, discovers bounded Lua sources, freezes one combined registry, and gates Play on diagnostics. |
| Lua source reload | Implemented | Compatible program replacement occurs in place; incompatible replacements retain the previous valid program. Existing runtime instances keep their program binding. |
| Native code reload | Partial | Candidate activation occurs at a fixed-tick boundary and runtime reconstruction can roll back. Candidate module `Start` occurs during discovery, old and new modules overlap, runtime-only behavior state is not snapshotted, and there is no job/callback quiesce proof. |
| Packaged-player loading | Missing | No packaged-runtime composition, shipping manifest, signature policy enforcement, or packaged-player caller was found. |
| Game components, systems, services, assets | Missing | Only object-attached behavior descriptors are exposed. The normative registration and capability contexts do not exist in the public API. |
| Imported gameplay libraries | Missing | Project gameplay build does not consume reusable game libraries through the package graph. |
| Multi-module or mod composition | Missing | One project-global primary-module path is assumed; load ordering, trust, isolation, and dependency policy do not exist. |
| Dead production paths | None proven | `NO_MANIFEST` is test-only but actively used by the module-host fixture. Public host APIs have editor and test callers. The packaged-player promise is missing composition, not dead code. |

## Exact ABI Assumptions

`GameModule.h` defines boundary version `1` and requires these symbols:

```text
GetGameModuleDescriptor
GetGameplayDescriptorBundle
CreateGameModule
DestroyGameModule
```

The boundary relies on all of the following assumptions:

1. Host and module use the same SDK generation, compiler family and exact
   compiler version, target OS and processor, generator platform/toolset, C++20
   mode, standard library ABI, and ABI-affecting runtime settings. Configure
   checks the compiler, system, processor, and non-empty generator
   platform/toolset values. The load fingerprint includes engine version,
   boundary version, compiler identity, system, and processor, but omits source
   revision, build configuration, standard-library identity, CRT selection, and
   other compile/link flags. Those omitted properties are assumptions, not
   compatibility proofs.
2. C linkage stabilizes only the four exported names. Struct layout, virtual
   dispatch, `Result<void>`, and factory signatures remain C++ ABI.
3. Exact `sizeof` equality is required for descriptor and bundle structures.
   Structure growth is not append-compatible within boundary version `1`.
4. Descriptor strings, registration arrays, descriptors, and factory function
   pointers are borrowed from the loaded library. They are valid only while the
   library remains loaded.
5. The host copies module ID and fingerprint text, but the frozen behavior
   registry retains descriptors and factory bindings whose executable code is
   module-owned. Registry and every behavior instance must die before unload.
6. Objects created by module factories are destroyed only by their paired
   module-owned destroy functions. The host never applies `delete` across the
   boundary.
7. `CreateGameModule` returns a non-null object. `Start` must return a typed
   result; `Stop` and destroy must be safe after failed startup. Exceptions must
   not escape, but comprehensive exception translation is not implemented at
   every callback boundary.
8. The module ID and fingerprint are non-empty, at most 256 bytes, and exactly
   equal between descriptor, generated bundle, expected host fingerprint, and
   manifest validation.
9. The generated bundle revision is non-zero, but the current generator always
   emits revision `1`; no revision migration or compatibility range exists.
10. `GameRuntimeContext` is currently empty. Module `Start` therefore receives no
    scene, assets, jobs, diagnostics, configuration, or mediated platform
    capabilities.

## Generated Descriptor And Build Contract

The annotation scanner reads the declared source files and recognizes
`HORO_BEHAVIOR(SimpleIdentifier, "game.*")`. It rejects duplicate generated
symbols and type IDs, then emits descriptor and factory records into one static
array. This creates a complete snapshot only for matches the regex can see.

Current scanner limitations are contract-relevant:

- it is not compiler- or AST-aware and may observe text in comments or strings;
- the annotated C++ type must be a simple identifier, not a qualified name;
- preprocessor expansion and semantic C++ validity are left to compilation;
- input size and file count are not explicitly bounded by the generator;
- output replacement is not an explicit durable temp-file transaction;
- only behavior registrations exist; field migrations and bundle diagnostics do
  not.

The helper and build service exchange these machine-local artifacts:

| Artifact | Owner | Current role |
| --- | --- | --- |
| `.horo/local/gameplay_build_inputs.txt` | CMake helper | Project-global list of canonical extra inputs used by freshness hashing. |
| `.horo/local/build/gameplay-debug/` | Build service | Fixed development build root and candidate manifest location. |
| `candidate_gameplay_module.json` | Module post-build step | Candidate artifact path and identity used before publication. |
| `.horo/local/gameplay_module.json` | Build service | Last successfully published discovery manifest. |
| `.horo/local/gameplay_build_state.json` | Build service | Input, artifact, compiler, toolchain, and manifest hashes used as local build evidence. |
| `.horo/local/gameplay-shadow/` | Module host | Unique copies that permit replacing the original library while a candidate or active library is loaded. |

The build service hashes bounded aggregate inputs, strips inherited compiler and
CMake environment overrides, serializes builds with a project lock, and validates
the candidate by loading a shadow copy. Publication durably replaces the manifest
and state and restores the previous manifest if state publication fails. A failed
build leaves the previous successful pair available.

Candidate parsing does not apply a file-size bound before JSON decoding. The build
service validates manifest schema, fingerprint, and artifact-path type, then relies
on the module host for the artifact's ABI; it does not compare manifest module ID
or descriptor revision with the loaded module. The generated absolute artifact
path is not required to remain within the project or build root. Editor discovery
later applies a 64 KiB manifest bound and compares the manifest module ID and
descriptor revision with the loaded module.

The remaining integrity gap is between publication and later activation. Build
state contains the artifact SHA-256, size, and modification time, but the manifest
contains only an absolute path. Registry discovery validates ABI and descriptor
identity by loading whatever file is at that path; it does not bind that file to
the hash that passed build validation.

## Ownership And Lifecycle

### Successful load and unload

```text
canonical source artifact
  -> unique shadow artifact
  -> DynamicLibrary owner
  -> resolved entry points
  -> validated descriptor and bundle
  -> host-owned BehaviorRegistry containing module factory bindings
  -> module-owned IGameModule
  -> Start(empty GameRuntimeContext)
  -> module-created behavior instances

unload:
  behavior instances destroyed by module factory
  -> module Stop
  -> module DestroyGameModule
  -> registry release
  -> DynamicLibrary unload
  -> shadow artifact deletion
```

On `Start` failure the host calls `Stop`, then module destroy, then unload. On any
earlier validation failure no project factory executes. Shadow artifacts are
removed on both failed load and normal destruction where filesystem removal
succeeds.

`ProjectGameplayRegistry` owns the loaded native module longer than its combined
registry. `EditorWorkspaceController` owns the active registry; the play session
borrows it. This ordering keeps native factories executable until all instances
that use them have been destroyed.

### Native replacement during Play

The editor discovers and starts the candidate before the fixed-tick activation
point. At the safe point it shuts down and destroys old behavior instances,
creates all instances against the candidate registry and unchanged runtime scene,
then swaps registry owners. If candidate creation fails, it reconstructs all
instances from the previous registry. If rollback also fails, Play enters a
failed state and destroys the runtime scene.

This is behavior-runtime reconstruction, not the normative
quiesce/snapshot/unload/load/restore transaction. In particular:

- old and candidate module `Start` lifetimes overlap;
- module startup is not constrained to the runtime safe point;
- module-owned jobs, callbacks, services, and external resources cannot be
  enumerated or drained by the empty context;
- runtime-only instance state is lost; authored `BehaviorComponent` fields are
  used to recreate instances;
- no explicit restart fallback is selected when unload safety cannot be proven.

## Persistence And Missing-Code Behavior

Scene JSON stores behavior data independently from the loaded module:

```text
instanceId
typeId
schemaVersion
enabled
fields[] { name, value { type, value } }
```

Supported values are null, Boolean, signed 64-bit integer, number, string, Vec2,
Vec3, and quaternion. Load validates the namespaced type ID, payload shape, field
names, and per-object uniqueness of behavior instance IDs. A scene object may
contain at most 128 behaviors. Runtime conversion copies the authored behavior
components without consulting the registry.

This separation preserves a syntactically valid unknown behavior when gameplay
code is absent, but the current editor registry reports missing or incompatible
native code as a blocking diagnostic and gates Play. There is no typed unknown
component envelope, field-ID-based schema migration, or repair transaction.
Unsupported serialized value encodings fail scene load rather than being retained
opaquely.

## Platform Matrix

| Platform | Current loader | Proven gaps |
| --- | --- | --- |
| Linux and other POSIX | Canonical absolute regular file; `dlopen` with immediate local symbol resolution; `dlsym`; `dlclose`. | Native dependency and rpath policy is implicit; tests exercise only the current host platform. |
| macOS | Same generic POSIX implementation. | No explicit signing, hardened-runtime, quarantine, install-name, or replacement policy in the loader. |
| Windows | Canonical absolute regular file; `LoadLibraryA`; `GetProcAddress`; `FreeLibrary`. | ANSI loading can reject non-ASCII project paths; DLL dependency search is not isolated with a typed wide-character policy. |

Both implementations reject relative paths and lexical `..`, canonicalize before
loading, and require a regular file. The POSIX implementation additionally bounds
input path text to 1024 bytes; Windows has no equivalent explicit bound. Shadow-copy
roots must also be absolute and traversal-free.

## Validation Evidence And Coverage Gaps

Existing focused tests prove:

- relative module paths are rejected;
- fingerprint mismatch fails before activation;
- a valid fixture loads, produces a frozen registry, creates behavior instances,
  and unloads from a shadow copy;
- native and Lua registrations merge and duplicate/invalid discovery diagnostics
  gate activation;
- compatible Lua source replacement retains a usable program;
- a successful project build publishes state and manifest, is recognized as
  current, and remains the last success after a later broken build;
- scene persistence round-trips typed behavior payloads and runtime scene
  validation rejects invalid or duplicate instance data.

The current focused suite does not directly prove:

- every missing entry point and every descriptor/bundle size, version, identity,
  count, and revision rejection;
- exception containment, `Stop`/destroy exact-once ordering, or failure at each
  partial-start stage;
- generator behavior around comments, strings, preprocessing, qualified types,
  oversized input, or deterministic output;
- manifest/build-state artifact digest binding or artifact replacement races;
- Windows Unicode paths and dependency search, macOS signing/rpath, or explicit
  cross-platform fixture parity;
- job/callback/service quiescence, runtime-state snapshot/restore, or native
  restart fallback;
- packaged-player loading and shipping replacement restrictions;
- schema migration and lossless opaque preservation of unsupported payloads.

## Follow-Up Ownership

This audit does not redefine these responsibilities:

| Gap | Focused owner |
| --- | --- |
| Complete, deterministic, versioned generated identity, registrations, lifecycle callbacks, diagnostics, and validation | [HORO-144 / GAM-001.2](https://github.com/abdullahbodur/horo-engine/issues/144) |
| Stable game-owned component identities, serialization metadata, migration, missing-code behavior, and editor inspection | [HORO-145 / GAM-001.3](https://github.com/abdullahbodur/horo-engine/issues/145) |
| Project system and service registration, dependencies, scheduling, capabilities, cancellation, shutdown, and reload | [HORO-146 / GAM-001.4](https://github.com/abdullahbodur/horo-engine/issues/146) |
| Game-owned asset type identity, import, serialization, cook, editor representation, and missing-code fallback | [HORO-147 / GAM-001.5](https://github.com/abdullahbodur/horo-engine/issues/147) |
| Quiesce/snapshot/unload/load/restore transaction, lifetime proof, rollback, and restart fallback | [HORO-148 / GAM-001.6](https://github.com/abdullahbodur/horo-engine/issues/148) |
| Reusable gameplay libraries through package restore, deterministic symbols, and source/binary ABI policy | [HORO-149 / GAM-001.7](https://github.com/abdullahbodur/horo-engine/issues/149) |
| Lossless unknown payloads, actionable degraded mode, Play gating, and repair without data loss | [HORO-150 / GAM-001.8](https://github.com/abdullahbodur/horo-engine/issues/150) |
| Multiple gameplay modules and mods: supported use cases, trust, isolation, ordering, compatibility, distribution, and migration | [HORO-151 / GAM-001.9](https://github.com/abdullahbodur/horo-engine/issues/151) |

Cross-cutting implementation work under those tickets must also close the
packaged-player composition and platform evidence gaps where the owning contract
requires them. New umbrella tickets should not duplicate the responsibilities in
this table.

## Related Documents

- [Gameplay Module Overview](./gameplay-module.md)
- [Gameplay Module Boundary](./gameplay-module-boundary.md)
- [Gameplay Behavior Authoring](./gameplay-behavior-authoring.md)
- [Gameplay Runtime Integration](./gameplay-runtime-integration.md)
- [Gameplay Module Verification](./gameplay-module-verification.md)
- [Build System](../delivery/build-system.md)
- [Platform Abstraction](../foundation/platform-abstraction.md)
- [Ownership And Resource Lifetime](../foundation/ownership-and-resource-lifetime.md)
