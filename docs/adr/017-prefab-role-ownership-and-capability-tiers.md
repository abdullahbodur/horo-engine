# ADR-017: Prefab Role, Ownership and Capability-Tier Decision

- **Status**: Proposed
- **Date**: 2026-08-28
- **Supersedes**: None
- **Scope**: Prefab asset definition, authoring templates, runtime spawnable templates (`CookedPrefab`), capability tiers (Tier 0, Tier 1, Tier 2), asset identity, project versioning, unknown component preservation, and lifecycle safety
- **Issue**: [#1008](https://github.com/abdullahbodur/horo-engine/issues/1008) ([PFB-001.1])
- **Jira**: [HORO-1008](https://horo-engine.atlassian.net/browse/HORO-1008)
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

Capability tiers describe subsystem scope, not product milestone assignments or a global execution order. The [Product Roadmap Model](../architecture/delivery/product-roadmap.md) owns whole-product checkpoints; issue milestones and native dependency links determine delivery requirements. Tier 1 runtime spawn conforms to [ADR-018](018-command-registration-permissions-threading-and-packaged-build-policy.md) dispatch and [ADR-010](010-job-waiting-and-operation-store-ownership.md) operation ownership, regardless of its delivery milestone.

### Ratify-or-revise outcomes

| Area | Current state | Outcome |
|---|---|---|
| Prefab role in static scenes | Authoring template expanded into `RuntimeSceneDefinition` during scene cook/load | **Ratified.** Placed static scene instances are flattened at conversion/cook time for optimal runtime data locality and zero runtime expansion overhead. |
| Prefab role in dynamic gameplay | Stated as unhandled / no runtime prefab assets shipped in release packages | **Revised.** Prefabs referenced for dynamic spawning are compiled by the asset cooker into immutable `CookedPrefab` binary assets shipped in release packages and spawned dynamically by `SceneRuntime` / `Gameplay`. |
| Capability staging | Single-root initial limitation with informal future extensions list | **Revised.** Formalized into Tier 0 (Baseline), Tier 1 (Runtime Spawn), and Tier 2 (Deferred). |
| Identity authority | Mixed `prefabId` and `sourcePath` | **Revised.** Authoritative identity is Asset Registry `AssetId` (128-bit UUID). `sourcePath` is an authoring index hint only. |
| Local object addressing | Ad-hoc or single-root string IDs | **Revised.** Persisted `LocalObjectId` slots identify authoring objects; deterministic expansion IDs are collision-checked. Runtime spawning uses the existing generation-checked scene entity allocator, not hashed or globally monotonic IDs. |
| Component data preservation | Undefined for custom/unregistered project components | **Revised.** Opaque component payloads (`RawComponentPayload`) are preserved verbatim across authoring serialization and expansion. |
| Dynamic spawn lifecycle | Mentioned conceptually in gameplay behavior hooks | **Revised.** `SceneCommandBuffer::RequestSpawnPrefab` admits from any thread (`Result<OperationId, PrefabError>`). Owner-thread commit uses `CommitDeferredLifecycleChanges`; gameplay lifecycle hooks follow publication and enable-state rules. |
| Spawn threading / async | Unspecified vs ADR-018 safe points | **Revised.** Spawn commit is `CommandThreadPolicy::OwnerThreadNextFrame`. Unloaded assets become an `OperationStore` load-then-spawn job, not a poll loop on `AssetNotLoaded`. |
| Runtime spawn recursion | Static cycle detection only (Tier 0) | **Revised.** An immutable spawn lineage follows queued requests, loads, and lifecycle continuations; repeated `AssetId`s and excessive lineage depth are rejected before staging. |
| CookedPrefab versioning | Source `.prefab` shares `ProjectVersion` only | **Revised.** Cooked blobs carry a distinct `cookedFormatVersion`. `ProjectVersion` migration invalidates cook cache and forces recook; runtime never migrates cooked bytes. |

### Capability Tiers

Capability delivery is partitioned into three discrete tiers:

```text
+------------------------------------------------------------------------------+
| Tier 0: Authoring Template Expansion & Instantiation (Baseline)              |
| - Source .prefab assets in assets/prefabs/ conforming to ProjectVersion      |
| - Single-root and multi-object hierarchy authoring                           |
| - Placed instances in SceneDocument (AssetId + root transform & shallow ovr) |
| - Deterministic offline expansion into RuntimeSceneDefinition                |
| - Cycle detection rejecting recursive self-references                        |
| - Opaque preservation of unknown project-owned component payloads            |
+------------------------------------------------------------------------------+
                                    |
                                    v
+------------------------------------------------------------------------------+
| Tier 1: Runtime Dynamic Spawn from Cooked Prefab (Runtime Spawn)             |
| - Cooker bakes .prefab into immutable binary CookedPrefab (core.prefab)      |
| - Registered in AssetRegistry and CookCatalog with AssetId                   |
| - Dynamic spawn APIs in SceneRuntime and Gameplay (SceneCommandBuffer)       |
| - Runtime EntityId allocation, hierarchy setup, component copy, lifecycle    |
| - Fail-safe error returns (missing asset, corrupted data, invalid component) |
+------------------------------------------------------------------------------+
                                    |
                                    v
+------------------------------------------------------------------------------+
| Tier 2: Live Variant Inheritance & Dynamic Override Tracking (Deferred)      |
| - Prefab Variants inheriting from base prefab asset with delta overrides     |
| - Multi-level variant chains with DAG cycle verification                     |
| - Live editor propagation across open documents upon base prefab mutation    |
| - Granular per-property override tracking (revert/apply to base prefab)      |
+------------------------------------------------------------------------------+
```

#### Tier 0: Authoring Template Expansion & Instantiation (Baseline)

- Authored `.prefab` files stored under `assets/prefabs/` as canonical JSON/structured source documents adhering to `ProjectVersion`.
- Supports single-root and multi-object parent-child hierarchies within explicit bounds (max hierarchy depth 16, max object count 256, max payload size 4 MiB).
- `SceneDocument` references prefabs via `ScenePrefabInstance` containing `AssetId` and root transform overrides.
- Conversion pipeline expands prefab instances into the scene hierarchy deterministically before runtime initialization or offline scene baking.
- Static cycle detection traps recursive inclusion loops (`A -> B -> A`) at validation time with clear diagnostics.
- Serializer and expansion pipeline preserve unrecognized gameplay/plugin components as opaque typed byte payloads without data stripping.

#### Tier 1: Runtime Dynamic Spawn from Cooked Prefab (Runtime Spawn)

- Asset Pipeline cooks `.prefab` source assets into platform-optimized, immutable binary `CookedPrefab` artifacts (`core.prefab` asset type).
- Cooked prefabs participate in the standard `AssetRegistry` and `CookCatalog`, loaded via `IAssetProvider`.
- Two APIs, one contract, reconciled with [ADR-018](018-command-registration-permissions-threading-and-packaged-build-policy.md) (threading policy) and [ADR-010](010-job-waiting-and-operation-store-ownership.md) (`OperationStore`):
  ```cpp
  class SceneCommandBuffer {
  public:
      // Any thread; typed admission error or an OperationId. Never mutates SceneRuntime.
      Result<OperationId, PrefabError> RequestSpawnPrefab(const PrefabSpawnRequest& request);
  };

  class SceneRuntimeAccess {
  public:
      // Internal owner-thread drain; resident assets and captured context required.
      Result<SpawnedPrefabHandle, PrefabError> SpawnPrefab(const PrefabSpawnRequest& request);
  };
  ```
  `SceneCommandBuffer::RequestSpawnPrefab` is the planned gameplay admission seam; successful admission returns an `OperationId`, while a full queue/store or closed scene returns a typed error without creating an operation. A host-composed spawn coordinator exclusively mutates the authoritative `OperationStore`. `SceneRuntimeAccess::SpawnPrefab` is the internal owner-thread drain, never a gameplay bypass. This Tier 1 extension adds a thread-safe admission facade without making existing SCN-001 owner-thread command-buffer methods thread-safe.
- Spawn staging uses the existing scene allocator for fresh generation-checked `EntityId`s, copies component data, and establishes hierarchy before **commit**. `OnCreate` runs after commit, then `OnEnable` if enabled; `OnStart` runs once after first enable and before the first eligible fixed update. Created-disabled behaviors defer `OnEnable`/`OnStart` until enabled, as required by Gameplay Behavior Authoring.
- Fail-safe: missing catalog entries, corrupted or version-mismatched cooked blobs, unregistered component types, component allocation failure, or spawn recursion return typed `PrefabError` without publishing entities or invoking behavior hooks.

#### Tier 2: Live Variant Inheritance & Dynamic Override Tracking (Deferred)

- Prefab Variants allow creating specialized prefabs that inherit from a base prefab asset and record delta overrides (property overrides, added/removed components, extra child entities).
- Multi-tier variant inheritance chains (`Base -> VariantA -> VariantB`) evaluated as a directed acyclic graph (DAG).
- Editor workspace listens for base prefab mutation events and propagates updates in real time to open variant documents and active viewport previews.
- Deep per-property override inspection, reverting, and pushing back to base prefab.

### Identity and Asset Model

1. **Single Source of Identity**: Prefab assets are identified exclusively by their 128-bit `AssetId` assigned in their asset sidecar metadata (`.prefab.meta`). Project paths (`assets/prefabs/enemy.prefab`) are editor convenience strings; moving or renaming a file preserves references through `AssetId`.
2. **Project Versioning**: Prefab source documents share the unified `ProjectVersion` and migration pipeline (`docs/architecture/foundation/project-versioning-and-migration.md`). Format migrations upgrade prefabs alongside scenes and project settings.
3. **Local Object Addressing**: Entities within a prefab have persisted `LocalObjectId` uint32 slots, with root slot zero. Reordering, insertion, and deletion must not renumber surviving slots or reuse deleted slots while references may remain; slots may be sparse and are not vector positions. Cook builds a separate dense slot remap.
4. **Scoped Identity Composition**:
   - During Tier 0 scene expansion, a fixed versioned hash over canonical instance-scope and local-ID bytes produces a deterministic candidate `ExpandedSceneObjectId`. Each nested instance extends the scope with its owning local slot. Validate all candidates against authored and expanded scene IDs before publication; collisions return `PrefabError::IdentityCollision` rather than overwriting an object. A 64-bit hash alone does not guarantee uniqueness.
   - During Tier 1 runtime spawn: use the existing `EntityId { index, generation }` allocator and scene-qualified `EntityRef { SceneRuntimeId, EntityId }` from Scene Runtime. Reused slots increment generations and retire on exhaustion; no new monotonic counter is introduced. Allocation and publication remain owner-thread-only. Requests capture the target scene identity and optional parent `EntityRef`; commit rejects an unloaded/replaced scene or stale/cross-scene parent.

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
     - `PrefabError::SpawnRecursionDetected`: requested `AssetId` already appears in the inherited spawn lineage, including delayed load and lifecycle continuations.
     - `PrefabError::SpawnDepthExceeded`: appending the target exceeds `MaximumRuntimeSpawnDepth` (8).
     - `PrefabError::AdmissionRejected` / `Cancelled` / `SceneUnavailable` / `InvalidParent`: bounded admission, cancellation, or captured scene/parent lifetime validation fails.
     - `PrefabError::EntityAllocationExhausted` / `IdentityCollision` / `InvalidHierarchy` / `ComponentCountExceeded`: allocator capacity, authoring identity, graph structure, or per-object component limits fail.
     - `PrefabError::HierarchyDepthExceeded` / `ObjectCountExceeded` / `PayloadTooLarge`: Bounds failed at authoring, cook, or runtime defense-in-depth.
   - Staging vs commit: stage entity handles, component bytes, validated behavior descriptors, and reserved behavior storage with **no constructed gameplay behavior instances**, hooks, bus events, or network emits. Staging failure discards unpublished storage and handles, so no `OnDestroy` is due. After publication, construct behaviors and run the normal gameplay lifecycle. Spawn success means structural commit; post-commit construction/hook faults are reported through gameplay fault handling and do not change a committed spawn into a rolled-back failure. Faulted behaviors do not start/update; every constructed instance receives normal `OnDestroy` exactly once at teardown.

### Reconciliation with ADR-018 (Threading, Async, Recursion)

Tier 1 spawn is an ADR-018 `OwnerThreadNextFrame` mutation, not an `ImmediateConsoleThread` call and not a main-thread `Wait()` on a worker ([ADR-010](010-job-waiting-and-operation-store-ownership.md)).

1. **Admission and owner-thread commit.** Any thread may submit an owned request through the scene-bound admission facade. The coordinator captures target `SceneRuntimeId`, optional parent `EntityRef`, cancellation, diagnostics/configuration, and explicit spawn lineage before enqueue. The owner thread validates and stages; publication occurs only at Scene Runtime's `CommitDeferredLifecycleChanges`. `OwnerThreadNextFrame` describes dispatch to that commit phase, not a second structural-mutation phase in `PreUpdate` / `DebugPhase`.
2. **Unloaded assets and ownership.** Catalog misses fail accepted operations with `AssetNotFound`. Non-resident templates and required dependencies load through the host-composed `AssetLoadService` / `IAssetProvider` path; workers perform bounded, cancellation-aware I/O and never wait for another queued worker or owner-thread commit. Completion schedules an owner-thread continuation holding immutable asset leases. The coordinator owns one operation from admission through terminal commit/failure/cancellation; runtime code and workers do not write `OperationStore` independently.
3. **Recursion across asynchronous boundaries.** Each accepted spawn carries immutable lineage ending in its target `AssetId`. Requests originating from its `OnCreate`, initial `OnEnable`/`OnStart`, or their asynchronous continuations inherit that lineage through an explicit bound command context. Before appending a target, reject a repeated ID with `SpawnRecursionDetected`, or depth above 8 with `SpawnDepthExceeded`. A call stack or thread-local scope alone is insufficient: lineage survives frame changes, loads, and queue deferral. Independent later gameplay requests start fresh lineages, so spawning the same prefab at different times is valid. The internal direct entry cannot bypass lineage validation. Child admission failure leaves an already-committed parent intact.
4. **Drain and lifetime safety.** Lifecycle-created requests are eligible only in a subsequent bounded drain batch, never recursively executed in the current batch. Admission and per-drain work have host-configured count/byte budgets. Before publication, recheck cancellation, scene identity, parent generation, and pinned dependency validity. Unload/replacement closes scene admission, cancels its pending operations, and invalidates late continuations. Cancellation before commit publishes nothing; commit and cancellation are serialized so cancellation after commit never pretends the entities were rolled back.

### CookedPrefab Versioning and Cook Cache Invalidation

- Source `.prefab` documents share `ProjectVersion` and its migration pipeline.
- `CookedPrefab` carries a separate `cookedFormatVersion` in the binary header, independent of `ProjectVersion`. Runtime never runs authoring migrations on cooked bytes.
- Prefab cooking extends the Asset Pipeline's canonical versioned cache key; it does not replace it with a prefab-only tuple. Preserve asset identity/type, exact source and cooker-input metadata digests, effective settings/schema, cooker identity/version, typed target/profile, and artifact-envelope version. Add `ProjectVersion`, `cookedFormatVersion`, and canonically ordered transitive dependency identities/artifact digests through the explicit dependency-aware extension. The dependency-free AST-001C `CacheKeyV1` cannot cook nested prefabs; Tier 1 requires that extension first.
- Source migration, dependency changes, target/settings changes, and cooker/format changes invalidate affected cooked output. Runtime rejects `UnsupportedCookedVersion` and never recooks; authoring/build tools rebuild and republish a compatible catalog/artifact before a later runtime request can succeed.
- Source/dependency digests establish cook provenance. A separate artifact-envelope digest verifies the actual cooked payload bytes, with length/offset checks; hashing source alone does not detect corruption in shipped cooked bytes.

### Authoring Bound Enforcement

`MaximumPrefabHierarchyDepth` (16), `MaximumPrefabObjectCount` (256), `MaximumPrefabPayloadBytes` (4 MiB), and `MaximumPrefabComponentsPerObject` (64) are rejected at **two** gates, with runtime as defense in depth:

| Gate | When | On exceed |
|---|---|---|
| Authoring validation | Save, import, instance placement, editor expand | Document command fails with a typed diagnostic. The `.prefab` is not written / the instance is not placed. |
| Cook | Prefab cook | Cook fails. No `CookedPrefab` artifact is emitted. |
| Runtime | Envelope and full table validation before staging | Typed bounds/structure error. No entities published. |

Limits apply to the fully expanded hierarchy, not only each source file: count the root as depth 1, all expanded objects, all component kinds per object, and bounded source/expanded/cooked payloads. Validate incrementally before exceeding allocation limits. Reject duplicate local IDs, missing/invalid parents, multiple roots, cycles, and invalid table offsets with `InvalidHierarchy` or `CorruptedPayload` as appropriate. Static inclusion cycles (`A -> B -> A`) remain `CyclicReferenceDetected` at authoring and cook gates.

## Consequences

- Static scenes continue to benefit from zero-cost runtime inlining and optimal contiguous memory layout.
- Dynamic gameplay systems obtain a first-class, memory-efficient runtime spawning mechanism via `CookedPrefab` without raw JSON parsing at runtime.
- A clear capability roadmap ensures baseline authoring (Tier 0) can be delivered and stabilized without being blocked by complex live variant inheritance (Tier 2).
- Single asset identity (`AssetId`) eliminates broken references upon asset renames.
- Plugin developers and gameplay programmers can author custom components with guaranteed serialization preservation.

## Rejected Alternatives

- **Pure Authoring-Time Inlining Only**: Rejected. Precluding runtime template instantiation breaks dynamic gameplay requirements (projectiles, enemy spawners, inventory drop items, VFX hierarchies).
- **Shipping Raw `.prefab` JSON Files to Runtime**: Rejected. Parsing JSON and running authoring migrations on the client runtime incurs massive CPU/memory overhead, requires shipping full reflection metadata, and risks security vulnerabilities.
- **Runtime Prototype/ECS Archetype Inheritance**: Rejected. Resolving component property inheritance dynamically at runtime adds pointer indirection, cache misses, and invalidation hazards in performance-critical tick loops.
- **Path-Authoritative References**: Rejected. File paths break when assets are moved, reorganized, or localized across platforms.
- **Silent Stripping of Unknown Components**: Rejected. Stripping unparsed components causes silent data loss when opening projects with plugins or gameplay modules.
