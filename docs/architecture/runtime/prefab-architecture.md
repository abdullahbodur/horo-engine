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
   - Dynamically instantiated at runtime via `SceneCommandBuffer::SpawnPrefab` or
     `SceneRuntimeAccess::SpawnPrefab` during active gameplay (e.g. projectiles, dynamic enemies,
     VFX hierarchies, procedural loot drops).
   - Instantiation allocates fresh runtime `EntityId`s, initializes components, sets up
     hierarchy parenting, and triggers standard behavior lifecycle events (`OnCreate`, `OnStart`).

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
|   - Contiguous ECS pools         - SceneCommandBuffer::SpawnPrefab
|   - Pre-allocated entity IDs     - Allocates fresh EntityIds & runs OnCreate/OnStart
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
- **Dynamic Spawn API**: `SceneCommandBuffer` and `SceneRuntimeAccess` expose transactional spawn
  requests returning `Result<SpawnedPrefabHandle, PrefabError>`.
- **Lifecycle Guarantees**: Fresh runtime `EntityId` allocation, component instantiation, hierarchy
  assembly, followed by `OnCreate` and `OnStart` behavior hooks during tick synchronization.
- **Fail-Safe Robustness**: Missing assets, invalid formats, or allocation limits fail safely with
  typed error diagnostics without crashing the runtime or corrupting existing entities.

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
  monotonic `EntityId` values allocated directly from the active runtime scene's entity pool.

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
}
```

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

### Binary Layout

- **Header**: Magic bytes (`HPFB`), schema version, cryptographic digest (SHA-256 of source and dependencies),
  and object count.
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

Gameplay behaviors and runtime systems request prefab spawning through transactional command buffers
or direct runtime access:

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
}
```

### Spawning Execution Sequence

```text
SceneCommandBuffer::SpawnPrefab(request)
  -> Verify prefab asset is loaded in AssetRegistry / IAssetProvider
  -> Check scene entity capacity (reject if capacity exceeded)
  -> Allocate monotonic EntityId for root and each child object
  -> Apply spawnTransform to root; set up parent-child entity relations
  -> Copy component data into scene contiguous component pools
  -> Attach gameplay behaviors and invoke OnCreate()
  -> Commit transaction at SceneRuntime tick synchronization point
  -> Invoke OnStart() for all newly spawned behaviors
```

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
   spawn, the runtime skips the unrecognized component, records a structured diagnostic warning,
   and completes valid entity and behavior instantiation without aborting.

---

## Diagnostics And Error Model

Dynamic spawning and authoring expansion report structured, typed errors:

```cpp
enum class PrefabError : std::uint32_t {
    Success = 0,
    AssetNotFound,              // AssetId not registered in CookCatalog / AssetRegistry
    AssetNotLoaded,             // Async asset load still pending
    CorruptedPayload,           // Checksum or magic header validation failed
    HierarchyDepthExceeded,     // Template exceeds MaximumPrefabHierarchyDepth (16)
    ObjectCountExceeded,        // Template exceeds MaximumPrefabObjectCount (256)
    CyclicReferenceDetected,    // Nested prefab inclusion contains a cycle
    EntityAllocationExhausted,  // Scene runtime ran out of available EntityIds
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
| **Lifecycle** | Spawn non-existent `AssetId` | Returns `PrefabError::AssetNotFound`; no scene leak |
| **Lifecycle** | Unknown component in cooked template | Preserves/skips component safely without crashing |
| **Lifecycle** | Scene destruction with active spawned entities | Cleanly tears down entities and behavior instances |

---

## Related Documents

- [ADR-017: Prefab Role, Ownership and Capability-Tier Decision](../../adr/017-prefab-role-ownership-and-capability-tiers.md)
- [Scene Runtime Architecture](./scene-runtime.md)
- [Asset Pipeline Architecture](./asset-pipeline.md)
- [Gameplay Behavior Authoring](../extensions/gameplay-behavior-authoring.md)
- [Project Versioning and Migration](../foundation/project-versioning-and-migration.md)
- [Editor Document Model](../editor/editor-document-model.md)
