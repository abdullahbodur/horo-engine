# ADR-017: Prefab Role, Ownership and Capability-Tier Decision

- **Status**: Proposed
- **Date**: 2026-08-28
- **Supersedes**: None
- **Scope**: Prefab asset definition, authoring templates, runtime spawnable templates (`CookedPrefab`), capability tiers (Tier 0, Tier 1, Tier 2), asset identity, project versioning, unknown component preservation, and lifecycle safety
- **Issue**: [#1008](https://github.com/abdullahbodur/horo-engine/issues/1008) ([PFB-001.1])
- **JIRA**: HORO-1008
- **Normative document**: [Prefab Architecture](../architecture/runtime/prefab-architecture.md)

## Context

`docs/architecture/runtime/prefab-architecture.md` initially defined a prefab strictly as an authoring-time template expanded into the containing scene's `RuntimeSceneDefinition` prior to runtime initialization, stating that release packages never ship `.prefab` files. However, Gameplay behavior authoring (`docs/architecture/extensions/gameplay-behavior-authoring.md`), the cinematic sequencer (`docs/architecture/runtime/cinematic-sequencer-architecture.md`), and dynamic VFX/projectile systems require dynamic runtime instantiation of entity templates during active gameplay.

This discrepancy created an architectural contradiction:

1. If prefabs are solely inlined and stripped at scene cook time, runtime gameplay and scene systems cannot dynamically spawn templated entities (such as projectiles, enemies, interactive props, or particle hierarchies) without inventing a duplicate ad-hoc runtime archetype or prototype system.
2. Conversely, if raw authoring `.prefab` files are parsed at runtime, release builds incur source parsing overhead, schema version migration baggage, unvalidated editor metadata, and path-authority drift.
3. Without explicit capability staging, baseline single-root authoring, multi-object authoring, runtime dynamic spawning, and live variant inheritance were conflated across disparate milestone goals.
4. Authoring documents historically mixed `prefabId` and `sourcePath`, risking broken references during file moves or renames unless anchored to the Asset Registry `AssetId` and project versioning rules.
5. Projects with custom gameplay components risked silent data stripping during authoring expansion and round-trip serialization.

[PFB-001.1] establishes the normative decision resolving these ownership, lifecycle, identity, and capability boundaries before downstream implementation tickets ([PFB-001.2] through [PFB-001.10]) implement the subsystem.

## Decision

**Horo Engine adopts a dual-role prefab architecture spanning two explicit lifecycles: an Authoring-Time Nested Template (`PrefabDocument` / `.prefab` source asset) in the Editor/Asset Pipeline, and an immutable Runtime-Spawnable Cooked Template (`CookedPrefab` binary asset) in SceneRuntime and Gameplay. The engine structures prefab capabilities into three sequential tiers (Tier 0: Authoring Expansion, Tier 1: Runtime Dynamic Spawn, Tier 2: Live Variant Inheritance). Prefab identity reuses Asset Registry `AssetId` and project versioning. Unknown project-owned component data is preserved verbatim across authoring workflows, and runtime dynamic spawning enforces fail-safe error handling.**

Milestone numbering in this ADR is the *product delivery* ladder for prefab capability, not a second architecture-baseline clock. [ADR-018](https://github.com/abdullahbodur/horo-engine/pull/2347) (companion PR; not in this tree until it lands) and [ADR-010](010-job-waiting-and-operation-store-ownership.md) describe the M0 architecture baseline (console, jobs, safe points). Prefab Tier 0 / 1 / 2 map to M1 / M2 / M3+ *after* that baseline. M0 does not block T0 authoring work in editor/tools; T1 runtime spawn must conform to the M0 threading and `OperationStore` contracts.

### Ratify-or-revise outcomes

| Area | Current state | Outcome |
|---|---|---|
| Prefab role in static scenes | Authoring template expanded into `RuntimeSceneDefinition` during scene cook/load | **Ratified.** Placed static scene instances are flattened at conversion/cook time for optimal runtime data locality and zero runtime expansion overhead. |
| Prefab role in dynamic gameplay | Stated as unhandled / no runtime prefab assets shipped in release packages | **Revised.** Prefabs referenced for dynamic spawning are compiled by the asset cooker into immutable `CookedPrefab` binary assets shipped in release packages and spawned dynamically by `SceneRuntime` / `Gameplay`. |
| Capability staging | Single-root initial limitation with informal future extensions list | **Revised.** Formalized into Tier 0 (Baseline / M1), Tier 1 (Engine Target / M2), and Tier 2 (Deferred / M3+). |
| Identity authority | Mixed `prefabId` and `sourcePath` | **Revised.** Authoritative identity is Asset Registry `AssetId` (128-bit UUID). `sourcePath` is an authoring index hint only. |
| Local object addressing | Ad-hoc or single-root string IDs | **Revised.** Local objects inside a prefab use deterministic zero-based `LocalObjectId` slots. Instantiated scene/runtime entity IDs are composed deterministically (`Hash(InstanceId, LocalObjectId)`) to prevent collisions. |
| Component data preservation | Undefined for custom/unregistered project components | **Revised.** Opaque component payloads (`RawComponentPayload`) are preserved verbatim across authoring serialization and expansion. |
| Dynamic spawn lifecycle | Mentioned conceptually in gameplay behavior hooks | **Revised.** `SceneCommandBuffer::RequestSpawnPrefab` enqueues from any thread (`OperationId`, ADR-018). `SceneRuntimeAccess::SpawnPrefab` commits on `OwnerThreadNextFrame` only. `OnCreate`/`OnStart` run after commit. |
| Spawn threading / async | Unspecified vs ADR-018 safe points | **Revised.** Spawn commit is `CommandThreadPolicy::OwnerThreadNextFrame`. Unloaded assets become an `OperationStore` load-then-spawn job, not a poll loop on `AssetNotLoaded`. |
| Runtime spawn recursion | Static cycle detection only (Tier 0) | **Revised.** Owner-thread spawn call stack rejects re-entrant `SpawnPrefab` of an in-flight `AssetId` (`PrefabError::SpawnRecursionDetected`). |
| CookedPrefab versioning | Source `.prefab` shares `ProjectVersion` only | **Revised.** Cooked blobs carry a distinct `cookedFormatVersion`. `ProjectVersion` migration invalidates cook cache and forces recook; runtime never migrates cooked bytes. |

### Capability Tiers

Capability delivery is partitioned into three discrete tiers:

```text
+-------------------------------------------------------------------------------+
| Tier 0: Authoring Template Expansion & Instantiation (Baseline / M1)          |
| - Source .prefab assets in assets/prefabs/ conforming to ProjectVersion       |
| - Single-root and multi-object hierarchy authoring                            |
| - Placed instances in SceneDocument (AssetId + root transform & shallow ovr)  |
| - Deterministic offline expansion into RuntimeSceneDefinition                 |
| - Cycle detection rejecting recursive self-references                         |
| - Opaque preservation of unknown project-owned component payloads             |
+-------------------------------------------------------------------------------+
                                    |
                                    v
+-------------------------------------------------------------------------------+
| Tier 1: Runtime Dynamic Spawn from Cooked Prefab (Engine Target / M2)         |
| - Cooker bakes .prefab into immutable binary CookedPrefab (core.prefab)       |
| - Registered in AssetRegistry and CookCatalog with AssetId                    |
| - Dynamic spawn APIs in SceneRuntime and Gameplay (SceneCommandBuffer)        |
| - Runtime EntityId allocation, hierarchy setup, component copy, lifecycle     |
| - Fail-safe error returns (missing asset, corrupted data, invalid component)  |
+-------------------------------------------------------------------------------+
                                    |
                                    v
+-------------------------------------------------------------------------------+
| Tier 2: Live Variant Inheritance & Dynamic Override Tracking (Deferred / M3+) |
| - Prefab Variants inheriting from base prefab asset with delta overrides      |
| - Multi-level variant chains with DAG cycle verification                      |
| - Live editor propagation across open documents upon base prefab mutation     |
| - Granular per-property override tracking (revert/apply to base prefab)       |
+-------------------------------------------------------------------------------+
```

#### Tier 0: Authoring Template Expansion & Instantiation (Baseline / M1)

- Authored `.prefab` files stored under `assets/prefabs/` as canonical JSON/structured source documents adhering to `ProjectVersion`.
- Supports single-root and multi-object parent-child hierarchies within explicit bounds (max hierarchy depth 16, max object count 256, max payload size 4 MiB).
- `SceneDocument` references prefabs via `ScenePrefabInstance` containing `AssetId` and root transform overrides.
- Conversion pipeline expands prefab instances into the scene hierarchy deterministically before runtime initialization or offline scene baking.
- Static cycle detection traps recursive inclusion loops (`A -> B -> A`) at validation time with clear diagnostics.
- Serializer and expansion pipeline preserve unrecognized gameplay/plugin components as opaque typed byte payloads without data stripping.

#### Tier 1: Runtime Dynamic Spawn from Cooked Prefab (Engine Target / M2)

- Asset Pipeline cooks `.prefab` source assets into platform-optimized, immutable binary `CookedPrefab` artifacts (`core.prefab` asset type).
- Cooked prefabs participate in the standard `AssetRegistry` and `CookCatalog`, loaded via `IAssetProvider`.
- Two APIs, one contract, reconciled with [ADR-018](https://github.com/abdullahbodur/horo-engine/pull/2347) (threading policy) and [ADR-010](010-job-waiting-and-operation-store-ownership.md) (`OperationStore`):
  ```cpp
  // Any thread. Enqueue only. Never mutates SceneRuntime.
  // Returns an OperationId immediately (ADR-010 OperationStore).
  OperationId SceneCommandBuffer::RequestSpawnPrefab(const PrefabSpawnRequest& request);

  // SceneRuntime owner thread only (CommandThreadPolicy::OwnerThreadNextFrame).
  // Requires a resident CookedPrefab. Gameplay uses the buffer, not this entry point.
  Result<SpawnedPrefabHandle, PrefabError> SceneRuntimeAccess::SpawnPrefab(
      const PrefabSpawnRequest& request);
  ```
  `SceneCommandBuffer::RequestSpawnPrefab` is the public gameplay seam. It records a spawn operation and returns. `SceneRuntimeAccess::SpawnPrefab` is the owner-thread drain of that queue. The access function does not wrap the buffer; the buffer wraps the access function.
- Spawn commit (owner thread) allocates fresh monotonic `EntityId`s from the scene pool, copies component data, establishes hierarchy, then **commits**. `OnCreate` runs only after commit; `OnStart` runs at the following scene synchronization point.
- Fail-safe: missing catalog entries, corrupted or version-mismatched cooked blobs, unregistered component types, component allocation failure, or spawn recursion return typed `PrefabError` without publishing entities or invoking behavior hooks.

#### Tier 2: Live Variant Inheritance & Dynamic Override Tracking (Deferred / M3+)

- Prefab Variants allow creating specialized prefabs that inherit from a base prefab asset and record delta overrides (property overrides, added/removed components, extra child entities).
- Multi-tier variant inheritance chains (`Base -> VariantA -> VariantB`) evaluated as a directed acyclic graph (DAG).
- Editor workspace listens for base prefab mutation events and propagates updates in real time to open variant documents and active viewport previews.
- Deep per-property override inspection, reverting, and pushing back to base prefab.

### Identity and Asset Model

1. **Single Source of Identity**: Prefab assets are identified exclusively by their 128-bit `AssetId` assigned in their asset sidecar metadata (`.prefab.meta`). Project paths (`assets/prefabs/enemy.prefab`) are editor convenience strings; moving or renaming a file preserves references through `AssetId`.
2. **Project Versioning**: Prefab source documents share the unified `ProjectVersion` and migration pipeline (`docs/architecture/foundation/project-versioning-and-migration.md`). Format migrations upgrade prefabs alongside scenes and project settings.
3. **Local Object Addressing**: Entities within a prefab are addressed by a zero-based `LocalObjectId` uint32 slot index stable across serialization.
4. **Collision-Free Identity Composition**:
   - During Tier 0 scene expansion: `ExpandedSceneObjectId = Hash64(InstanceSceneObjectId, LocalObjectId)`.
   - During Tier 1 runtime spawn: Root and child entities receive fresh, monotonic `EntityId` values allocated from the `SceneRuntime` entity pool. Allocation runs **only** on the scene owner thread while draining spawn commands. Worker jobs and other threads never touch the counter; because the owner-thread queue serializes commit, the counter is not atomic and needs no lock-free increment.

### Component Data Preservation & Fail-Safe Lifecycle Handling

1. **Unknown Component Preservation**:
   - The editor document model and prefab serializer represent unmapped or plugin-defined components as `RawComponentPayload` (preserving `componentTypeId`, `schemaVersion`, and serialized payload bytes).
   - Authoring operations (clone, save, expand, instance placement) preserve raw payloads verbatim.
2. **Fail-Safe Lifecycle Handling**:
   - Dynamic spawn operations never throw C++ exceptions across module boundaries.
   - Typed errors:
     - `PrefabError::AssetNotFound`: `AssetId` is not in `CookCatalog`. The operation completes failed; callers do not poll.
     - `PrefabError::AssetNotLoaded`: Direct `SceneRuntimeAccess::SpawnPrefab` was invoked while the blob was not resident. This is a programming error on the owner-thread path. The supported gameplay path (`RequestSpawnPrefab`) never returns this to the caller; it records an `OperationStore` load-then-spawn job instead (see below).
     - `PrefabError::UnsupportedCookedVersion`: Header `cookedFormatVersion` is newer or older than this runtime understands. Distinct from corruption.
     - `PrefabError::CorruptedPayload`: Magic header or cryptographic digest failed.
     - `PrefabError::ComponentTypeUnregistered`: Cooked template references a component type not registered in this runtime. Distinct from allocation failure.
     - `PrefabError::ComponentAllocationFailed`: Component pool or heap allocation failed while staging. Distinct from `ComponentTypeUnregistered`.
     - `PrefabError::SpawnRecursionDetected`: `SpawnPrefab` of an `AssetId` already on the owner-thread spawn call stack.
     - `PrefabError::HierarchyDepthExceeded` / `ObjectCountExceeded` / `PayloadTooLarge`: Bounds failed at authoring, cook, or runtime defense-in-depth.
   - Staging vs commit: entity handles and component bytes are staged first with **no** behavior hooks, bus events, or network emits. If staging fails, staged memory and handles are discarded. `OnCreate` has not run, so `OnDestroy` is not invoked. `OnCreate` / `OnStart` run only after commit; spawn rollback therefore never has to unwind subsystem registrations or unsend events. If `OnCreate` itself fails, the entities are already committed and later teardown uses the normal `OnDestroy` path; `OnCreate` must not be treated as part of the spawn transaction.

### Reconciliation with ADR-018 (Threading, Async, Recursion)

Tier 1 spawn is an ADR-018 `OwnerThreadNextFrame` mutation, not an `ImmediateConsoleThread` call and not a main-thread `Wait()` on a worker ([ADR-010](010-job-waiting-and-operation-store-ownership.md)).

1. **Who may call what.** Any thread (gameplay, CLI, MCP, jobs) may call `SceneCommandBuffer::RequestSpawnPrefab`. That call only enqueues. Only the SceneRuntime owner thread calls `SceneRuntimeAccess::SpawnPrefab`, during the deterministic frame phase that drains the command buffer (`PreUpdate` / `DebugPhase`).
2. **Unloaded assets.** If `RequestSpawnPrefab` finds the `CookedPrefab` absent from `IAssetProvider` but present in `CookCatalog`, it does **not** fail with `AssetNotLoaded` and it does **not** ask the caller to poll. It starts an ADR-018 `WorkerJob` that waits on the provider, then re-enqueues owner-thread commit. Progress is an `OperationId` in `OperationStore`. Completing the operation publishes `SpawnedPrefabHandle`. Catalog misses (`AssetNotFound`) fail the operation immediately.
3. **Runtime recursion.** Nested prefab *references* are flattened at cook time; runtime spawn of A does not recursively `SpawnPrefab` B. The remaining hazard is a behavior `OnCreate` (or a same-drain command) requesting another spawn of an in-flight `AssetId`, directly or through a chain. SceneRuntime keeps an owner-thread spawn call stack of `AssetId`. Push before staging, pop after commit or rollback. If the id is already on the stack, return `PrefabError::SpawnRecursionDetected` and do not stage. `OnCreate` may call `RequestSpawnPrefab` for a *different* asset; that enqueue is drained after the current stack frame pops (same safe point or next frame) and is still subject to the stack if it re-enters. Behaviors must not spawn their own `AssetId` from `OnCreate`.
4. **EntityId concurrency.** The monotonic pool counter is owner-thread-only. Worker jobs load bytes; they never allocate `EntityId`. No atomic increment, because ADR-018 already serializes scene mutations onto one drain.

### CookedPrefab Versioning and Cook Cache Invalidation

- Source `.prefab` documents share `ProjectVersion` and its migration pipeline.
- `CookedPrefab` carries a separate `cookedFormatVersion` in the binary header, independent of `ProjectVersion`. Runtime never runs authoring migrations on cooked bytes.
- Cook cache key: `(AssetId, sourceDigest, ProjectVersion, cookedFormatVersion, cookerRevision)`. A `ProjectVersion` migration of the source, a cooker bump, or a format bump invalidates the catalog entry and forces recook before the blob can be loaded.
- `UnsupportedCookedVersion` is not `CorruptedPayload`. Corruption is digest/magic failure. Version mismatch is a defined, recoverable catalog miss after recook.

### Authoring Bound Enforcement

`MaximumPrefabHierarchyDepth` (16), `MaximumPrefabObjectCount` (256), and `MaximumPrefabPayloadBytes` (4 MiB) are rejected at **two** gates, with runtime as defense in depth:

| Gate | When | On exceed |
|---|---|---|
| Authoring validation | Save, import, instance placement, editor expand | Document command fails with a typed diagnostic. The `.prefab` is not written / the instance is not placed. |
| Cook | Prefab cook | Cook fails. No `CookedPrefab` artifact is emitted. |
| Runtime | Header check before staging | `HierarchyDepthExceeded` / `ObjectCountExceeded` / `PayloadTooLarge`. No entities published. |

Static inclusion cycles (`A -> B -> A`) remain a validation-time rejection (`CyclicReferenceDetected`) at the authoring and cook gates.

## Consequences

- Static scenes continue to benefit from zero-cost runtime inlining and optimal contiguous memory layout.
- Dynamic gameplay systems obtain a first-class, memory-efficient runtime spawning mechanism via `CookedPrefab` without raw JSON parsing at runtime.
- A clear capability roadmap ensures baseline authoring (Tier 0) is delivered and stabilized in M1 without being blocked by complex live variant inheritance (Tier 2).
- Single asset identity (`AssetId`) eliminates broken references upon asset renames.
- Plugin developers and gameplay programmers can author custom components with guaranteed serialization preservation.

## Rejected Alternatives

- **Pure Authoring-Time Inlining Only**: Rejected. Precluding runtime template instantiation breaks dynamic gameplay requirements (projectiles, enemy spawners, inventory drop items, VFX hierarchies).
- **Shipping Raw `.prefab` JSON Files to Runtime**: Rejected. Parsing JSON and running authoring migrations on the client runtime incurs massive CPU/memory overhead, requires shipping full reflection metadata, and risks security vulnerabilities.
- **Runtime Prototype/ECS Archetype Inheritance**: Rejected. Resolving component property inheritance dynamically at runtime adds pointer indirection, cache misses, and invalidation hazards in performance-critical tick loops.
- **Path-Authoritative References**: Rejected. File paths break when assets are moved, reorganized, or localized across platforms.
- **Silent Stripping of Unknown Components**: Rejected. Stripping unparsed components causes silent data loss when opening projects with plugins or gameplay modules.
