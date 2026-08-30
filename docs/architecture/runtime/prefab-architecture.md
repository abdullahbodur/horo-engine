# Prefab Architecture

## Purpose

This document defines Horo Engine's prefab system: how reusable entity hierarchies
are authored, validated, and versioned as project assets, how prefab instances are
placed and expanded in scenes, and how cooked prefabs are dynamically spawned at
runtime by Gameplay and SceneRuntime systems.

## Core Decisions And Dual-Role Model

Horo Engine resolves the tension between authoring convenience, static scene
optimization, and dynamic gameplay spawning through an explicit **dual-role**
architecture spanning two lifecycles:

1. **Authoring-Time Nested Template (`PrefabDocument` / `.prefab`)**:
   - Authored in the Editor and stored under `assets/prefabs/`.
   - Serialized as structured, project-versioned documents conforming to `ProjectVersion`.
   - Supports multi-object hierarchies, nested prefab instances, and shallow/deep overrides.
   - For static scene objects, authoring instances are **pre-expanded and flattened** into
     the containing `SceneDocument` (viewport preview) and baked into `RuntimeSceneDefinition`
     (scene cook). This delivers optimal contiguous runtime memory layout with zero runtime
     template expansion overhead.

2. **Runtime-Spawnable Cooked Template (`CookedPrefab`)**:
   - Compiled by the Asset Pipeline from source `.prefab` files into immutable,
     platform-optimized binary artifacts (`core.prefab` asset type).
   - Registered in the `AssetRegistry` and `CookCatalog` with a stable 128-bit `AssetId`.
   - Gameplay requests spawn via `SceneCommandBuffer::RequestSpawnPrefab` (any thread,
     `OperationId`). `SceneRuntimeAccess::SpawnPrefab` commits on the scene owner thread
     (`OwnerThreadNextFrame`, [ADR-018](https://github.com/abdullahbodur/horo-engine/pull/2347); job wait in [ADR-010](../../adr/010-job-waiting-and-operation-store-ownership.md)).
   - Commit allocates fresh `EntityId`s, copies components, and parents the hierarchy, then
     publishes. `OnCreate` / `OnStart` run only after commit.

```text
+-----------------------------------------------------------------------------------+
| AUTHORING LIFECYCLE (Editor / Tools)                                              |
|                                                                                   |
|  [Authored Object(s)] ---> (Create Prefab) ---> [assets/prefabs/enemy.prefab]     |
|                                                      |          ^                 |
|                                    Placed in Scene   |          | Nested ref      |
|                                          v           |          |                 |
|    [SceneDocument] <--- (Instance Ref + Overrides) --+          +-- [nested.prefab]
|           |                                                     |
|           v (Scene Cook / Flattening)                           v (Prefab Cook)
+-----------+-----------------------------------------------------+-----------------+
| RUNTIME LIFECYCLE (SceneRuntime / Gameplay)                     |
|           |                                                     |
|           v                                                     v
|   [RuntimeSceneDefinition]                             [CookedPrefab Asset]
|   (Static pre-expanded entities)                       (Immutable binary template)
|           |                                                     |
|           v                                                     v
|   [SceneRuntime Active] <====== (Dynamic Spawn at runtime) <----+
|   - Contiguous ECS pools         - Buffer enqueue any thread; commit owner thread
|   - Owner-thread EntityId pool   - OnCreate/OnStart only after commit
+-----------------------------------------------------------------------------------+
```

---

## Capability Tiers

Prefab capabilities are staged across three sequential, contract-stable tiers:

```text
+-------------------------------------------------------------------------------+
| Tier 0: Authoring Template Expansion & Instantiation (Baseline / M1)          |
| - Canonical source .prefab format governed by ProjectVersion                  |
| - Single-root and multi-object parent-child hierarchies                       |
| - Placed instances in SceneDocument (AssetId + root transform & overrides)    |
| - Deterministic offline expansion into RuntimeSceneDefinition                 |
| - Static cycle detection rejecting recursive inclusion loops                  |
| - Opaque roundtrip preservation of unknown project-owned component data      |
+-------------------------------------------------------------------------------+
                                    |
                                    v
+-------------------------------------------------------------------------------+
| Tier 1: Runtime Dynamic Spawn from Cooked Prefab (Engine Target / M2)         |
| - Asset Pipeline compiles .prefab into binary CookedPrefab (core.prefab)      |
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

### Tier 0: Authoring Template Expansion & Instantiation (Baseline / M1)

- **Source Asset Format**: `.prefab` files stored in `assets/prefabs/` as canonical JSON/structured
  documents governed by `ProjectVersion` (`docs/architecture/foundation/project-versioning-and-migration.md`).
- **Hierarchy Support**: Single-root and multi-object parent-child hierarchies within explicit bounds.
- **Scene Placement**: `SceneDocument` stores lightweight instance references (`ScenePrefabInstance`)
  comprising an `AssetId` and explicit root transform/property overrides.
- **Authoring Expansion**: The editor expands prefab instances during scene loading and viewport
  rendering. Scene cook flattens all static prefab instances into `RuntimeSceneDefinition`.
- **Cycle Detection**: Static validation traps recursive inclusion chains (`A -> B -> A`) before
  expansion or serialization.
- **Component Preservation**: Unknown/plugin-owned component payloads are retained opaquely
  (`RawComponentPayload`) without data loss.

### Tier 1: Runtime Dynamic Spawn from Cooked Prefab (Engine Target / M2)

- **Cooked Binary Artifact**: The Asset Pipeline compiles `.prefab` assets into immutable binary
  `CookedPrefab` artifacts registered under `core.prefab` in `CookCatalog`.
- **Runtime Asset Management**: Loaded through `IAssetProvider` via stable `AssetId`.
- **Dynamic Spawn API**: `SceneCommandBuffer::RequestSpawnPrefab` (any thread, returns `OperationId`)
  wraps `SceneRuntimeAccess::SpawnPrefab` (owner thread, `OwnerThreadNextFrame`).
- **Lifecycle Guarantees**: Owner-thread `EntityId` allocation, staged component copy, commit,
  then `OnCreate`; `OnStart` at the next scene sync. Recursion of an in-flight `AssetId` is rejected.
- **Fail-Safe Robustness**: Catalog misses, cooked-version mismatch, corruption, bounds, and
  spawn recursion fail the operation without publishing entities or invoking hooks.
- **Async load**: Unloaded-but-catalogued assets become an ADR-018 `WorkerJob` + `OperationStore`
  load-then-spawn. Callers do not poll `AssetNotLoaded`.

### Tier 2: Live Variant Inheritance & Dynamic Override Tracking (Deferred / M3+)

- **Prefab Variants**: A variant `.prefab` references a parent base prefab `AssetId` and stores
  only delta overrides (modified property fields, added/removed components, extra child objects).
- **Variant Inheritance DAG**: Multi-level variant chains (`Base -> VariantA -> VariantB`) validated
  for acyclicity.
- **Live Editor Propagation**: When a base prefab is modified and saved, open variant documents,
  scenes referencing the prefab, and active viewport sessions update in real time.
- **Granular Override Management**: Deep per-property diffing, revert-to-prefab, and apply-to-prefab
  workflows in the Inspector panel.

---

## Identity, Hierarchy, And Asset Model

### Single Source of Identity

1. **Asset Registry Integration**: Prefabs are first-class assets. Every prefab is identified
   authoritatively by its 128-bit `AssetId` declared in its companion sidecar (`.prefab.meta`).
2. **Path Decoupling**: Project paths (`assets/prefabs/weapons/rifle.prefab`) are editor convenience
   hints. If a prefab is moved or renamed, references remain valid through `AssetId`.
3. **Project Versioning**: Prefab documents share the unified project schema versioning and
   automated migration framework (`docs/architecture/foundation/project-versioning-and-migration.md`).

### Hierarchy Addressing And Scoped Identity Composition

A prefab defines a self-contained hierarchy of authored objects. To guarantee collision-free
identities across multiple instances and nested scopes:

```cpp
namespace Horo::Prefab {
    /** @brief Zero-based slot index identifying an object inside a prefab template. */
    struct LocalObjectId {
        std::uint32_t value{0};
        [[nodiscard]] constexpr bool IsRoot() const noexcept { return value == 0; }
        [[nodiscard]] constexpr auto operator<=>(const LocalObjectId&) const noexcept = default;
    };

    /** @brief Reference to a prefab asset in authoring documents. */
    struct PrefabAssetReference {
        Assets::AssetId assetId;
        std::string pathHint; // Advisory authoring path
    };
}
```

- **Tier 0 Authoring Expansion**:
  When a prefab instance with `SceneObjectId instanceId` expands into a `SceneDocument`, each
  expanded object receives a deterministic ID:
  $$\text{ExpandedObjectId} = \text{Hash64}(\text{instanceId.value}, \text{localId.value})$$
  This guarantees deterministic identity generation across editor reloads and prevents ID collisions
  between repeated instances of the same prefab.

- **Tier 1 Runtime Spawning**:
  When `CookedPrefab` is spawned into `SceneRuntime`, root and child entities receive fresh,
  monotonic `EntityId` values allocated from the scene entity pool **on the owner thread only**.
  Worker jobs never increment the counter; the ADR-018 command drain serializes allocation, so
  the counter is not atomic.

---

## Prefab Document Schema And Explicit Bounds

Prefab documents enforce strict structural bounds to prevent unbounded memory growth, stack
overflows during recursive expansion, and allocation spikes:

```cpp
namespace Horo::Prefab {
    inline constexpr std::size_t MaximumPrefabHierarchyDepth = 16;
    inline constexpr std::size_t MaximumPrefabObjectCount    = 256;
    inline constexpr std::size_t MaximumPrefabPayloadBytes   = 4 * 1024 * 1024; // 4 MiB
    inline constexpr std::size_t MaximumPrefabComponentsPerObject = 64;
    inline constexpr std::size_t MaximumRuntimeSpawnDepth = 8; // owner-thread spawn call stack
}
```

These limits are enforced at authoring validation (save / import / instance placement; the
document command fails and nothing is written), again at cook (no `CookedPrefab` is emitted),
and at runtime as a header check before staging (`HierarchyDepthExceeded`,
`ObjectCountExceeded`, `PayloadTooLarge`). Static inclusion cycles (`A -> B -> A`) are
rejected at the authoring and cook gates with `CyclicReferenceDetected`.

### Authoring Document Structure (`PrefabDocument`)

```cpp
struct PrefabObjectNode {
    LocalObjectId localId;
    std::optional<LocalObjectId> parentLocalId;
    std::string name;
    Vec3 localPosition{0.0f, 0.0f, 0.0f};
    Vec3 localScale{1.0f, 1.0f, 1.0f};
    Quat localRotation{Quat::Identity()};

    // Component payloads (typed built-in components + opaque custom payloads)
    std::optional<Runtime::MeshComponent> mesh;
    std::optional<Runtime::CameraComponent> camera;
    std::optional<Runtime::LightComponent> light;
    std::optional<Runtime::AudioSourceComponent> audioSource;
    std::vector<RawComponentPayload> customComponents;
    std::vector<Gameplay::BehaviorAttachmentDefinition> behaviors;
};

struct PrefabDocument {
    std::uint32_t schemaVersion{1};
    Assets::AssetId assetId;
    std::vector<PrefabObjectNode> objects; // objects[0] is root (LocalObjectId{0})
    std::vector<Assets::AssetId> referencedAssets;
};
```

---

## Runtime Cooked Prefab (`CookedPrefab`)

The Asset Pipeline cooks source `.prefab` files into immutable `CookedPrefab` artifacts.

Source `.prefab` files share `ProjectVersion` and its migration pipeline. Cooked blobs do **not**.
The header carries a distinct `cookedFormatVersion`. Runtime never migrates cooked bytes.
The cook cache key is `(AssetId, sourceDigest, ProjectVersion, cookedFormatVersion, cookerRevision)`.
A source migration, cooker bump, or format bump invalidates the catalog entry and forces recook.
`UnsupportedCookedVersion` is a version mismatch; `CorruptedPayload` is digest or magic failure.

### Binary Layout

- **Header**: Magic bytes (`HPFB`), `cookedFormatVersion`, cryptographic digest (SHA-256 of source and dependencies),
  object count, and payload size.
- **Hierarchy Table**: Flat array of parent-child slot indices and local transforms.
- **Component Tables**: Contiguous, aligned arrays of primitive component data and behavioral descriptors.
- **Asset Reference Table**: List of dependent `AssetId`s required for instantiation.

```cpp
namespace Horo::Runtime {
    class CookedPrefab final {
    public:
        [[nodiscard]] Assets::AssetId GetAssetId() const noexcept;
        [[nodiscard]] std::uint32_t GetObjectCount() const noexcept;
        [[nodiscard]] std::span<const PrefabSlotDescriptor> GetHierarchy() const noexcept;
        [[nodiscard]] std::span<const Assets::AssetId> GetDependencies() const noexcept;
        // ... internal typed component table accessors
    };
}
```

---

## Dynamic Runtime Spawning Contract

This contract conforms to [ADR-018](https://github.com/abdullahbodur/horo-engine/pull/2347)
(companion PR) and [ADR-010](../../adr/010-job-waiting-and-operation-store-ownership.md)
(in this tree). Spawn *commit* is
`CommandThreadPolicy::OwnerThreadNextFrame`. The main/editor thread never `Wait()`s a worker
for spawn completion.

```cpp
namespace Horo::Runtime {
    struct PrefabSpawnRequest {
        Assets::AssetId prefabAssetId;
        Transform spawnTransform;
        std::optional<EntityId> parentEntity{std::nullopt};
    };

    struct SpawnedPrefabHandle {
        EntityId rootEntity;
        std::vector<EntityId> childEntities;
    };

    // Any thread. Enqueue only. Returns OperationId immediately.
    OperationId SceneCommandBuffer::RequestSpawnPrefab(const PrefabSpawnRequest& request);

    // Owner thread only. Resident CookedPrefab required. Buffer drain calls this.
    Result<SpawnedPrefabHandle, PrefabError>
    SceneRuntimeAccess::SpawnPrefab(const PrefabSpawnRequest& request);
}
```

`RequestSpawnPrefab` is the gameplay seam. `SpawnPrefab` on `SceneRuntimeAccess` is the
owner-thread implementation. The buffer wraps access; access does not wrap the buffer.
Two functions sharing one request struct is intentional. They are not interchangeable
call sites.

### Loaded vs not-yet-loaded

- Catalog miss → operation completes with `PrefabError::AssetNotFound`. No polling.
- Catalog hit, blob not resident → `RequestSpawnPrefab` starts a `WorkerJob` that waits
  on `IAssetProvider`, then re-enqueues owner-thread commit. `OperationStore` is the
  progress surface. Callers subscribe to the `OperationId`; they do not poll
  `AssetNotLoaded`.
- Direct `SceneRuntimeAccess::SpawnPrefab` with a non-resident blob returns
  `PrefabError::AssetNotLoaded` as a misuse diagnostic. Gameplay must not use that path.

### Spawning Execution Sequence (owner-thread drain)

```text
SceneCommandBuffer::RequestSpawnPrefab(request)     // any thread
  -> if not in CookCatalog: fail operation (AssetNotFound)
  -> if not resident: WorkerJob load -> re-enqueue; return OperationId
  -> else enqueue OwnerThreadNextFrame drain; return OperationId

SceneRuntimeAccess::SpawnPrefab(request)            // owner thread only
  -> if AssetId is on the spawn call stack: SpawnRecursionDetected
  -> push AssetId
  -> check cookedFormatVersion, digest, bounds, entity capacity
  -> STAGE: allocate EntityIds, parent links, copy components (no hooks, no bus, no net)
  -> on staging failure: discard staged memory/handles; pop; fail operation
  -> COMMIT: publish entities into the live world
  -> pop AssetId
  -> OnCreate() on committed behaviors
  -> OnStart() at the next scene synchronization point
```

`OnCreate` is outside the staging transaction. Spawn rollback never calls `OnDestroy` and
never unsends events, because those side effects cannot have run. If `OnCreate` fails,
entities are already live and later teardown uses the normal `OnDestroy` path.

### Runtime spawn recursion

Cook flattens nested prefab *references* into one `CookedPrefab`. Runtime spawn of A does
not `SpawnPrefab` B. The remaining hazard is `OnCreate` (or another command in the same
drain) requesting spawn of an `AssetId` already on the owner-thread call stack.

- Push `AssetId` before staging; pop after commit or staging rollback.
- Re-entry of that id → `PrefabError::SpawnRecursionDetected`, no staging.
- `OnCreate` may `RequestSpawnPrefab` a *different* asset; that work runs after the
  current frame pops and is still subject to the stack.
- Behaviors must not spawn their own `AssetId` from `OnCreate`.
- `MaximumRuntimeSpawnDepth` caps the stack (direct plus indirect).

---

## Preservation Of Unknown Project Components

To support modular gameplay packages and project-specific C++ plugins:

1. **Opaque Preservation Contract**:
   ```cpp
   struct RawComponentPayload {
       std::string componentTypeId;
       std::uint32_t schemaVersion{1};
       std::vector<std::uint8_t> serializedBytes;
   };
   ```
2. **Editor Roundtrip**: If an authored prefab or scene contains custom component types unknown to
   the base editor binary, the data is preserved in `RawComponentPayload` verbatim. Saving, cloning,
   or expanding the prefab preserves these components without truncation.
3. **Runtime Spawning Safety**: If an unregistered component type is encountered during runtime
   spawn, the transaction aborts before publishing entities or invoking behavior lifecycle hooks.
   Staging discards those entities (no `OnCreate` has run, so no `OnDestroy`) and returns `PrefabError::ComponentTypeUnregistered`.

---

## Diagnostics And Error Model

Dynamic spawning and authoring expansion report structured, typed errors:

```cpp
enum class PrefabError : std::uint32_t {
    Success = 0,
    AssetNotFound,              // AssetId not registered in CookCatalog / AssetRegistry
    AssetNotLoaded,             // Direct owner-thread spawn while blob is not resident (misuse)
    UnsupportedCookedVersion,   // cookedFormatVersion not understood by this runtime
    CorruptedPayload,           // Checksum or magic header validation failed
    HierarchyDepthExceeded,     // Template exceeds MaximumPrefabHierarchyDepth (16)
    ObjectCountExceeded,        // Template exceeds MaximumPrefabObjectCount (256)
    PayloadTooLarge,            // Template exceeds MaximumPrefabPayloadBytes (4 MiB)
    CyclicReferenceDetected,    // Nested prefab inclusion contains a cycle (authoring/cook)
    SpawnRecursionDetected,     // Runtime SpawnPrefab re-entered an in-flight AssetId
    EntityAllocationExhausted,  // Scene runtime ran out of available EntityIds
    ComponentTypeUnregistered,  // Cooked template references an unavailable component type
    ComponentAllocationFailed   // Memory allocation failed for component pool
};
```

### Validation Matrix

| Case Category | Test Scenario | Expected Outcome |
|---|---|---|
| **Valid** | Single-root prefab with mesh & transform | Expands/spawns correctly; root entity created |
| **Valid** | Multi-object hierarchy (depth 3, 5 objects) | Preserves relative transforms and child parenting |
| **Valid** | Dynamic spawn with attached gameplay behavior | Calls `OnCreate` followed by `OnStart` |
| **Boundary** | Maximum hierarchy depth (exactly 16) | Expansion and spawn succeed |
| **Boundary** | Maximum object count (exactly 256) | Allocation and instantiation succeed |
| **Boundary** | Empty overrides on placed instance | Inherits base template values completely |
| **Malformed** | Cycle detection (`A -> B -> A`) | Rejection with `PrefabError::CyclicReferenceDetected` |
| **Malformed** | Hierarchy depth > 16 or count > 256 | Rejection with boundary error diagnostic |
| **Malformed** | Corrupted magic header or truncated payload | Rejection with `PrefabError::CorruptedPayload` |
| **Lifecycle** | Spawn non-existent `AssetId` | Operation fails `AssetNotFound`; no scene leak; no poll |
| **Lifecycle** | Spawn catalogued but not-yet-loaded prefab | `OperationId` load-then-spawn; not a sync `AssetNotLoaded` |
| **Lifecycle** | `OnCreate` spawns the same `AssetId` | `SpawnRecursionDetected`; no extra entities |
| **Lifecycle** | Staging fails after some entities allocated | Discard staged handles; no `OnCreate` / no `OnDestroy` |
| **Lifecycle** | Cooked format newer than runtime | `UnsupportedCookedVersion`; not `CorruptedPayload` |
| **Lifecycle** | Unknown component in cooked template | Staging abort; no hooks; `ComponentTypeUnregistered` |
| **Lifecycle** | Scene destruction with active spawned entities | Cleanly tears down entities and behavior instances |
| **Malformed** | Authoring depth > 16 on save | Save rejected; `.prefab` not written |
| **Malformed** | Cook of over-limit prefab | Cook fails; no `CookedPrefab` artifact |

---

## Related Documents

- [ADR-017: Prefab Role, Ownership and Capability-Tier Decision](../../adr/017-prefab-role-ownership-and-capability-tiers.md)
- [ADR-018: Command Registration, Permissions, Threading and Packaged-Build Policy](https://github.com/abdullahbodur/horo-engine/pull/2347) (companion PR; file is not in this branch until #2347 lands)
- [ADR-010: Job Waiting and Operation Store Ownership](../../adr/010-job-waiting-and-operation-store-ownership.md)
- [Scene Runtime Architecture](./scene-runtime.md)
- [Asset Pipeline Architecture](./asset-pipeline.md)
- [Gameplay Behavior Authoring](../extensions/gameplay-behavior-authoring.md)
- [Project Versioning and Migration](../foundation/project-versioning-and-migration.md)
- [Editor Document Model](../editor/editor-document-model.md)
