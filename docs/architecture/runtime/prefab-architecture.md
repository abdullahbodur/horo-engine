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
   - Supports multi-object hierarchies, nested prefab instances, and shallow overrides in Tier 0; deep overrides and live variants belong to Tier 2.
   - For static scene objects, authoring instances are **pre-expanded and flattened** into
     a derived viewport projection of `SceneDocument` (without replacing authored instance references) and baked into `RuntimeSceneDefinition`
     (scene cook). This delivers optimal contiguous runtime memory layout with zero runtime
     template expansion overhead.

2. **Runtime-Spawnable Cooked Template (`CookedPrefab`)**:
   - Compiled by the Asset Pipeline from source `.prefab` files into immutable,
     platform-optimized binary artifacts (`core.prefab` asset type).
   - Registered in the `AssetRegistry` and `CookCatalog` with a stable 128-bit `AssetId`.
   - Gameplay requests spawn via `SceneCommandBuffer::RequestSpawnPrefab` (any thread,
     `Result<OperationId, PrefabError>`). `SceneRuntimeAccess::SpawnPrefab` commits on the scene owner thread
     (`OwnerThreadNextFrame`, [ADR-018](../../adr/018-command-registration-permissions-threading-and-packaged-build-policy.md); job wait in [ADR-010](../../adr/010-job-waiting-and-operation-store-ownership.md)).
   - Commit allocates fresh `EntityId`s, copies components, and parents the hierarchy, then
     publishes. `OnCreate` / `OnStart` run only after commit.

```text
Authoring: Editor / Tools
  [assets/prefabs/*.prefab] + [SceneDocument instance references / overrides]
          |                                   |
          |                                   +--> [Derived viewport projection]
          |                                   |
          |                                   +--> Scene cook / flatten
          |                                             |
          +--> Prefab cook / flatten                    v
                    |                         [RuntimeSceneDefinition]
                    v                                   |
              [CookedPrefab]                            v
                    |                          [SceneRuntime active]
                    +--> RequestSpawnPrefab             ^
                         -> bounded admission / load    |
                         -> owner-thread stage / commit-+
                         -> gameplay lifecycle after publication
```

---

## Capability Tiers

Prefab capabilities are staged across three contract-stable tiers. They describe subsystem
scope, not product milestone assignments or a global execution order; the
[Product Roadmap Model](../delivery/product-roadmap.md) and native issue dependencies
own those delivery decisions:

```text
+------------------------------------------------------------------------------+
| Tier 0: Authoring Template Expansion & Instantiation (Baseline)              |
| - Canonical source .prefab format governed by ProjectVersion                 |
| - Single-root and multi-object parent-child hierarchies                      |
| - Placed instances in SceneDocument (AssetId + root transform & overrides)   |
| - Deterministic offline expansion into RuntimeSceneDefinition                |
| - Static cycle detection rejecting recursive inclusion loops                 |
| - Opaque roundtrip preservation of unknown project-owned component data      |
+------------------------------------------------------------------------------+
                                    |
                                    v
+------------------------------------------------------------------------------+
| Tier 1: Runtime Dynamic Spawn from Cooked Prefab (Runtime Spawn)             |
| - Asset Pipeline compiles .prefab into binary CookedPrefab (core.prefab)     |
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

### Tier 0: Authoring Template Expansion & Instantiation (Baseline)

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

### Tier 1: Runtime Dynamic Spawn from Cooked Prefab (Runtime Spawn)

- **Cooked Binary Artifact**: The Asset Pipeline compiles `.prefab` assets into immutable binary
  `CookedPrefab` artifacts registered under `core.prefab` in `CookCatalog`.
- **Runtime Asset Management**: Loaded through `IAssetProvider` via stable `AssetId`.
- **Dynamic Spawn API**: `SceneCommandBuffer::RequestSpawnPrefab` (any thread, returns `Result<OperationId, PrefabError>`)
  wraps `SceneRuntimeAccess::SpawnPrefab` (owner thread, `OwnerThreadNextFrame`).
- **Lifecycle Guarantees**: Owner-thread `EntityId` allocation, staged component copy, commit,
  then `OnCreate`, `OnEnable` when enabled, and `OnStart` once before the first eligible fixed
  update after first enable. Created-disabled behaviors defer enable/start. Repeated IDs in
  inherited spawn lineage are rejected even across frames and asynchronous loads.
- **Fail-Safe Robustness**: Catalog misses, cooked-version mismatch, corruption, bounds, and
  spawn recursion fail the operation without publishing entities or invoking hooks.
- **Async load**: Unloaded-but-catalogued assets become an ADR-018 `WorkerJob` + `OperationStore`
  load-then-spawn. Callers do not poll `AssetNotLoaded`.

### Tier 2: Live Variant Inheritance & Dynamic Override Tracking (Deferred)

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

A prefab defines a self-contained hierarchy of authored objects. Identity rules across multiple instances and nested scopes are:

```cpp
namespace Horo::Prefab {
    /** @brief Persisted local slot identifying an authored object; not a vector index. */
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

- **Stable local slots**: Root slot zero is reserved. Surviving `LocalObjectId` values do not
  change when objects are reordered, inserted, or deleted; deleted slots are not reused while
  references may remain. Sparse authored slots are mapped to dense cooked table indices.
- **Tier 0 Authoring Expansion**: Generate deterministic candidate scene IDs with a fixed,
  versioned hash over canonical instance-scope/local-ID bytes. Each nested instance extends
  the scope with its owning local slot, distinguishing repeated nested references. Check all
  candidates against authored and expanded IDs before publication. A collision fails with
  `PrefabError::IdentityCollision`; a 64-bit hash does not guarantee uniqueness and never
  authorizes overwriting another object. Repeated expansion of unchanged input is stable.
- **Tier 1 Runtime Spawning**: Use the existing Scene Runtime `EntityId { index, generation }`
  allocator. Reused slots increment generation and retire rather than wrap. Public results
  carry scene-qualified `EntityRef { SceneRuntimeId, EntityId }`; no prefab-specific monotonic
  counter or hash replaces that identity model. Allocation remains owner-thread-only.

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
    inline constexpr std::size_t MaximumRuntimeSpawnDepth = 8; // inherited lineage, including target
}
```

Limits apply to the fully expanded hierarchy as well as individual inputs: root depth is 1,
object count includes all nested expansions, and component count includes built-in, opaque,
and behavior components. Bound source, expanded, and cooked payload sizes independently;
validate incrementally before expansion exceeds allocation limits. Limits are enforced at
save/import/placement, at cook (no artifact on failure), and before runtime staging.

Runtime validates the artifact envelope and actual table offsets, lengths, alignment, parent
indices, component counts, and hierarchy; header counts alone are not trusted. Require exactly
one root, unique local IDs, valid parents, and an acyclic connected hierarchy. Invalid authored
graphs return `InvalidHierarchy`; malformed cooked encoding returns `CorruptedPayload`;
well-formed over-limit input returns the relevant bounds error. Static nested-inclusion cycles
return `CyclicReferenceDetected` at authoring/cook time.

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
    HoroProjectVersion projectVersion; // Unified authoring version, not a prefab schema counter
    Assets::AssetId assetId;
    std::vector<PrefabObjectNode> objects; // first node is root; local IDs are persisted, possibly sparse
    std::vector<Assets::AssetId> referencedAssets;
};
```

---

## Runtime Cooked Prefab (`CookedPrefab`)

The Asset Pipeline cooks source `.prefab` files into immutable `CookedPrefab` artifacts.

Source `.prefab` files share `ProjectVersion` and its migration pipeline. Cooked blobs do **not**.
The header carries a distinct `cookedFormatVersion`. Runtime never migrates cooked bytes.
Prefab cooking preserves the Asset Pipeline's complete canonical versioned cache-key inputs:
asset identity/type, source and cooker-input metadata digests, effective settings/schema,
cooker contribution identity/version, typed target/profile, and artifact-envelope version.
Its dependency-aware extension also covers `ProjectVersion`, `cookedFormatVersion`, and
canonically ordered transitive dependency identities/artifact digests. Dependency-free
AST-001C `CacheKeyV1` rejects nested-prefab dependencies; Tier 1 requires the explicit
[dependency-aware cache extension](./asset-pipeline.md#incremental-cook-and-cache-reuse),
not an incompatible change to V1 or a parallel prefab-only cache.

Source migrations, dependency/target/settings changes, and cooker/format changes invalidate
output. `UnsupportedCookedVersion` fails the runtime operation; only authoring/build tools
recook and republish a compatible catalog/artifact. Runtime neither migrates nor recooks.
`CorruptedPayload` is a separate envelope, magic, digest, or structural validation failure.

### Binary Layout

- **Header / envelope**: Magic bytes (`HPFB`), `cookedFormatVersion`, object count, and payload
  size. The standard artifact envelope validates a cryptographic digest of the actual cooked
  payload bytes. Source/dependency digests are provenance/cache inputs, not substitutes for
  cooked-byte integrity verification.
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

This contract conforms to [ADR-018](../../adr/018-command-registration-permissions-threading-and-packaged-build-policy.md)
and [ADR-010](../../adr/010-job-waiting-and-operation-store-ownership.md).
Spawn *commit* is
`CommandThreadPolicy::OwnerThreadNextFrame`. The main/editor thread never `Wait()`s a worker
for spawn completion.

```cpp
namespace Horo::Runtime {
    struct PrefabSpawnRequest {
        SceneRuntimeId targetScene;
        Assets::AssetId prefabAssetId;
        Transform spawnTransform;
        std::optional<EntityRef> parentEntity{std::nullopt};
    };

    struct SpawnedPrefabHandle {
        EntityRef rootEntity;
        std::vector<EntityRef> childEntities;
    };

    class SceneCommandBuffer {
    public:
        // Any thread. Enqueue only. Typed admission error or an OperationId.
        Result<OperationId, PrefabError> RequestSpawnPrefab(const PrefabSpawnRequest& request);
    };

    class SceneRuntimeAccess {
    public:
        // Internal owner-thread drain; resident assets and captured context required.
        Result<SpawnedPrefabHandle, PrefabError> SpawnPrefab(const PrefabSpawnRequest& request);
    };
}
```

`RequestSpawnPrefab` is the planned scene-bound gameplay admission seam; `SpawnPrefab` is
its internal owner-thread implementation. These are Tier 1 target contracts, not existing
SCN-001 APIs. The extension adds thread-safe admission without changing the thread affinity
of existing `SceneCommandBuffer::Create` / `Destroy`. Hierarchy staging uses the later ECS
contract; it must not bypass Scene Runtime's transactional structural publication.

Successful admission returns an `OperationId`; a closed/full queue or full operation store
returns `AdmissionRejected` without accepting work. The host-composed spawn coordinator
exclusively mutates the authoritative `OperationStore`. It captures owned request values,
target scene identity, cancellation, diagnostic/configuration context, and explicit lineage
before enqueue; workers do not retain borrowed scene/parent pointers. The internal access
facade is bound to this captured context for the drain; the public request struct does not
let callers manufacture or erase ancestry.

### Loaded vs not-yet-loaded

- Catalog miss completes the accepted operation with `AssetNotFound`.
- A catalogued non-resident template and its required dependencies load through the composed
  `AssetLoadService` / `IAssetProvider` path. Workers perform bounded, cancellation-aware I/O;
  they do not synchronously wait for another queued worker or owner-thread commit. Completion
  schedules an owner-thread continuation holding immutable asset leases. The coordinator
  projects progress and terminal results through the original `OperationId`.
- Direct internal `SpawnPrefab` without resident required assets returns `AssetNotLoaded` as
  a misuse diagnostic. Gameplay uses admission; it never polls that error.
- Owner-thread publication revalidates cancellation, target `SceneRuntimeId`, parent generation
  and scene membership, and pinned dependencies. Scene unload/replacement closes admission,
  cancels pending operations, and rejects late continuations with `Cancelled` or
  `SceneUnavailable`; stale/cross-scene parents return `InvalidParent`.

### Spawning Execution Sequence

```text
SceneCommandBuffer::RequestSpawnPrefab(request)     // any thread; bound context
  -> validate/copy context and lineage; reserve bounded operation/queue capacity
  -> reject admission or return OperationId; never mutate SceneRuntime
  -> coordinator resolves catalog and schedules required asset loads
  -> asset completion enqueues owner-thread continuation with immutable leases

SceneRuntimeAccess::SpawnPrefab(request)            // internal owner-thread drain
  -> validate inherited lineage, cancellation, scene and parent identity
  -> validate cooked version, actual payload digest, hierarchy, bounds, capacity
  -> STAGE: IDs, component data, behavior descriptors/storage (no behavior instances)
  -> staging failure: discard unpublished storage and handles; fail operation
  -> recheck cancellation and lifetime at CommitDeferredLifecycleChanges
  -> COMMIT: atomically publish the complete hierarchy and successful handle
  -> construct behaviors; OnCreate; OnEnable if enabled
  -> OnStart once after first enable, before the first eligible fixed update
```

`OwnerThreadNextFrame` is the dispatch classification. Structural publication uses Scene
Runtime's `CommitDeferredLifecycleChanges`; it is not an extra mutation phase in
`PreUpdate` / `DebugPhase`. Preparation may precede publication but never leaks candidate
handles or partial entities. Work submitted by lifecycle callbacks is eligible only in a
subsequent bounded drain batch. Host-configured queue count/byte and per-drain work budgets
prevent unbounded fan-out in a single frame.

Staging does not construct gameplay behavior instances, invoke hooks, or emit bus/network
side effects. Therefore rollback has no `OnDestroy` obligation. After structural commit,
construction/hook faults follow normal gameplay fault handling and do not retroactively
fail or roll back the successful structural spawn. Faulted behaviors do not start/update;
every constructed behavior receives `OnDestroy` exactly once during normal teardown.
Created-disabled behaviors still run `OnCreate` but defer `OnEnable` / `OnStart` until enabled.

Commit and cancellation are serialized by the coordinator/owner-thread protocol: cancellation
accepted before publication leaves no entities; cancellation after publication does not undo
commit or change the successful spawn into a cancelled operation.

### Runtime spawn recursion

Cook flattens nested references; runtime does not recursively expand raw prefabs. Lifecycle
spawn chains still need protection across queues, frame boundaries, and asynchronous loads:

- Each accepted spawn carries immutable lineage ending in its target `AssetId`.
- Requests originating in `OnCreate`, initial `OnEnable` / `OnStart`, or their asynchronous
  continuations inherit that lineage through an explicitly bound command context. A caller
  cannot drop ancestry when submitting through that context. No global or thread-local stack
  is the authority, and the internal direct path must perform the same validation.
- Before appending the requested ID, a repeated ID fails with `SpawnRecursionDetected`;
  exceeding `MaximumRuntimeSpawnDepth` (8, including the new target) fails with
  `SpawnDepthExceeded`. This rejects both `A -> A` and delayed `A -> B -> A`.
- Independent later gameplay requests start new lineages, so repeated legitimate spawning
  remains supported. Child rejection does not roll back an already-committed parent.

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
   Staging discards unpublished storage (no gameplay behavior instance was constructed) and returns `PrefabError::ComponentTypeUnregistered`.

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
    SpawnRecursionDetected,     // Requested AssetId repeats in inherited spawn lineage
    SpawnDepthExceeded,         // Lineage exceeds MaximumRuntimeSpawnDepth
    AdmissionRejected,         // Queue/store capacity exhausted or admission closed
    Cancelled,                 // Cancellation accepted before structural commit
    SceneUnavailable,          // Captured target scene unloaded or replaced
    InvalidParent,             // Stale generation or parent outside target scene
    IdentityCollision,         // Deterministic authoring ID collides with another object
    InvalidHierarchy,          // Duplicate local IDs, invalid parents, roots, or graph
    ComponentCountExceeded,    // More than MaximumPrefabComponentsPerObject (64)
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
| **Valid** | Dynamic spawn with enabled gameplay behavior | `OnCreate`, `OnEnable`, then `OnStart` before first eligible fixed update |
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
| **Identity** | Reorder/insert/delete authored objects | Surviving local IDs and overrides remain stable |
| **Identity** | Repeated nested instances or injected hash collision | Distinct scoped IDs; collision fails atomically with `IdentityCollision` |
| **Identity** | Reuse a destroyed runtime entity slot | Old generation and old scene references remain invalid |
| **Boundary** | Lineage depth 8 then 9 across queued frames | Depth 8 accepted; depth 9 returns `SpawnDepthExceeded` |
| **Boundary** | Expanded nested count/payload or component count exceeds limit | Reject incrementally before oversized allocation or publication |
| **Malformed** | Duplicate local IDs, missing parent, multiple roots | `InvalidHierarchy`; no authoring expansion published |
| **Malformed** | Tampered cooked bytes with unchanged source digest | Actual artifact payload digest fails; `CorruptedPayload` |
| **Lifecycle** | Delayed `A -> B -> A`, including asset-load continuation | `SpawnRecursionDetected`; rejected child publishes nothing |
| **Lifecycle** | Independent later spawn of A | Fresh lineage; legitimate repeat succeeds |
| **Lifecycle** | Spawn queue or operation store full | `AdmissionRejected`; no orphan operation or accepted work |
| **Lifecycle** | Cancel/unload/replace scene while asset loads | No late publication into destroyed or replacement scene |
| **Lifecycle** | Parent destroyed/reused while asset loads | `InvalidParent`; no attachment to replacement entity |
| **Lifecycle** | Created-disabled behavior | `OnCreate` runs; `OnEnable`/`OnStart` wait for first enable |
| **Lifecycle** | Post-commit behavior construction or hook fault | Structural spawn remains committed; fault reported; normal teardown |
| **Cook** | Nested dependency, target, settings, or version changes | Full canonical cache key changes; no stale artifact reuse |

---

## Related Documents

- [ADR-017: Prefab Role, Ownership and Capability-Tier Decision](../../adr/017-prefab-role-ownership-and-capability-tiers.md)
- [ADR-018: Command Registration, Permissions, Threading and Packaged-Build Policy](../../adr/018-command-registration-permissions-threading-and-packaged-build-policy.md)
- [ADR-010: Job Waiting and Operation Store Ownership](../../adr/010-job-waiting-and-operation-store-ownership.md)
- [Scene Runtime Architecture](./scene-runtime.md)
- [Asset Pipeline Architecture](./asset-pipeline.md)
- [Gameplay Behavior Authoring](../extensions/gameplay-behavior-authoring.md)
- [Project Versioning and Migration](../foundation/project-versioning-and-migration.md)
- [Editor Document Model](../editor/editor-document-model.md)
