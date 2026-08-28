# Save Game And Persistence Architecture

## Purpose

This document defines the save-game and runtime persistence subsystem for Horo Engine.
It establishes the normative contracts for durable game-state capture and restore,
save-game archive formats, schema versioning and migration chains, host execution
environments (Packaged, Play-In-Editor, Dedicated Server), storage integration,
cloud synchronization, failure rollback, and security.

This specification ratifies the architectural boundary separating runtime game saves
from editor `SceneDocument` authoring saves, editor autosaves, crash recovery persistence,
and workspace metadata.

## Core Decisions

- **Single Runtime Save Authority**: `RuntimeSaveService` (and its underlying
  `SaveGameAuthority`) is the sole coordinator for durable game-state snapshot capture,
  serialization, and restore operations on the runtime owner thread.
- **No Serialization Through Scene Documents**: Runtime game state is **never**
  serialized through the editor `SceneDocument` model or authoring AST. Runtime save state
  captures dynamic ECS and gameplay module state directly from the active runtime.
- **Strict Separation of Persistence Domains**: Four persistence categories
  (`Runtime Save`, `Scene Document`, `Editor Autosave / Recovery`, `Project / Workspace Metadata`)
  operate under separate lifecycles, schemas, storage locations, and mutation authorities.
- **Clear Ownership Boundaries**:
  - *Runtime* owns live ECS snapshots, dynamic entity registries, and simulation state.
  - *Editor* owns `SceneDocument`, semantic undo/redo history, and authoring workspace state.
  - *Storage / Platform Services* (`DurableFileSystem`, `PlatformServices`) owns atomic file I/O,
    directory path resolution, quota enforcement, and cloud sync.
  - *Gameplay Modules* own domain-specific serializable component schemas via stable type IDs
    and versioned codecs.
- **Environment Isolation**:
  - *Play-In-Editor (PIE)* executes in an isolated sandbox with dedicated temporary storage;
    it never mutates player production save slots or canonical project `.horo` files.
  - *Packaged Games* use platform-standard user profile directories with multi-slot management,
    rolling autosaves, and cloud synchronization.
  - *Dedicated / Headless Servers* enforce server-authoritative world persistence without
    client profile, viewport, or UI pollution.
- **Atomic Durability & Two-Phase Restore**: All save operations write to a sibling
  temporary file followed by an atomic replacement. All restore operations follow a two-phase
  prepare-then-commit transaction with safe rollback and cancellation support.

## Persistence Domain Boundaries

Horo Engine strictly isolates authoring persistence from dynamic runtime persistence:

| Dimension | Runtime Game Save (`.horosave`) | Editor Scene Document (`.horo`) | Editor Recovery (`.horo_recovery`) | Project / Workspace Metadata |
| --- | --- | --- | --- | --- |
| **Primary Authority** | `RuntimeSaveService` | `SceneDocumentPersistence` | `ProjectSceneRecoveryRecord` | `ProjectSession` / `Workspace` |
| **File Format** | Binary/JSON Archive (`.horosave`) | Structured JSON/Binary (`.horo`) | Recovery Journal (`.horo_recovery`) | JSON (`project.json`, `workspace.json`) |
| **Primary Location** | User save directory or server world path | Project asset tree (`assets/scenes/`) | Bounded recovery dir (`.horo/recovery/`) | Project root (`.horo/`) & local app data |
| **Content Serialized** | Dynamic ECS state, spawned entities, modified transforms/stats, gameplay progression, inventory, player profile | Authored scene hierarchy, default component values, logical asset IDs, primitive descriptors, editor visibility/locks | Unsaved dirty document snapshot, target canonical path, session identity, base/recovered revisions | Project configuration, active renderer, panel layouts, tab state, recent files |
| **Trigger / Lifecycle** | Gameplay checkpoints, manual save, quicksave, player profile updates | Explicit user save (`Ctrl+S`), `Save As`, asset commit | Periodic editor timer, dirty edit threshold, focus-loss checkpoint | Project creation, settings modification, editor shutdown |
| **Target Consumer** | Runtime game client, dedicated server, player profile loader | Editor viewport, level designer, asset compiler / cooker | Crash recovery startup prompt on unclean editor exit | Host composition root, editor workspace controller |
| **Safety & Rollback** | Temporary file atomic replace; candidate validation rollback | Pre-save validation; atomic replace; external conflict detection | Untrusted recovery validation; non-destructive to canonical file | Atomic JSON replace; fallback to schema defaults |

```text
+---------------------------------------------------------------------------------------+
|                                    PERSISTENCE BOUNDARIES                             |
+---------------------------------------------------------------------------------------+
|                                                                                       |
|   [ Editor Layer ]                                                                    |
|   +-------------------------------------------------------------------------------+   |
|   | SceneDocument  ===>  SceneDocumentPersistence  ===>  assets/scenes/*.horo     |   |
|   | (Authored AST)       (Atomic File Writer)            (Canonical Authoring)    |   |
|   |       |                                                                       |   |
|   |       +------------> WriteProjectSceneRecovery ===> .horo/recovery/*.recovery |   |
|   |                      (Autosave / Crash Guard)        (Transient Recovery)     |   |
|   +-------------------------------------------------------------------------------+   |
|           |                                                                           |
|           | Converted via RuntimeSceneDefinition (One-Way Handoff)                    |
|           v                                                                           |
|   [ Runtime Layer ]                                                                   |
|   +-------------------------------------------------------------------------------+   |
|   | SceneRuntime   <---+                                                          |   |
|   | (Dynamic ECS)      |                                                          |   |
|   |       +            | Capture & Restore (Transactional)                        |   |
|   | GameplayModule <---+                                                          |   |
|   | (Quests/Inventory) |                                                          |   |
|   |       +            |                                                          |   |
|   | PlayerProfile  <---+                                                          |   |
|   | (Settings/Stats)   |                                                          |   |
|   |                    v                                                          |   |
|   |           RuntimeSaveService / SaveGameAuthority                              |   |
|   +-------------------------------------------------------------------------------+   |
|                                |                                                      |
|                                v  (DurableFileSystem & PlatformServices)              |
|                     savegame_slot01.horosave                                          |
|                     (Player Save / Server World)                                      |
+---------------------------------------------------------------------------------------+
```

## Runtime Save Authority

### Architecture & Ownership

The `RuntimeSaveService` is a runtime-layer service that owns the execution of all
runtime save and restore transactions. It coordinates across runtime subsystems:

```cpp
namespace Horo::Runtime {
    /** @brief Unique identifier for a save game slot or archive. */
    struct SaveGameSlotId {
        std::string value; // e.g. "slot_01", "quicksave", "autosave_01"

        [[nodiscard]] bool IsValid() const noexcept { return !value.empty(); }
        [[nodiscard]] auto operator<=>(const SaveGameSlotId &) const noexcept = default;
    };

    /** @brief Current operational phase of the runtime save authority. */
    enum class SaveOperationPhase : std::uint8_t {
        Idle,
        CapturingSnapshot,
        Serializing,
        WritingDurableArchive,
        RestoringSnapshot,
        ApplyingState,
        Failed,
        Cancelled
    };

    /** @brief Status of an active or completed save/restore transaction. */
    struct SaveOperationProgress {
        SaveGameSlotId slotId;
        SaveOperationPhase phase{SaveOperationPhase::Idle};
        float progress{0.0f};
        std::optional<Error> diagnostic;
    };

    /** @brief Single runtime authority orchestrating save and restore transactions. */
    class RuntimeSaveService final {
    public:
        RuntimeSaveService(SceneRuntime &sceneRuntime,
                           std::span<IGameplayStateProvider *const> gameplayProviders,
                           PlayerProfileService &profileService,
                           PlatformServices &platformServices,
                           DurableFileSystem &fileSystem,
                           JobSystem &jobSystem);

        ~RuntimeSaveService();
        RuntimeSaveService(const RuntimeSaveService &) = delete;
        RuntimeSaveService &operator=(const RuntimeSaveService &) = delete;

        /** @brief Starts an asynchronous save operation to the specified slot. */
        [[nodiscard]] Result<SaveOperationHandle> SaveSlotAsync(SaveGameSlotId slotId,
                                                               SaveGameHeader headerMetadata,
                                                               const CancellationToken &cancellation);

        /** @brief Starts an asynchronous restore transaction from the specified slot. */
        [[nodiscard]] Result<RestoreOperationHandle> LoadSlotAsync(SaveGameSlotId slotId,
                                                                  const CancellationToken &cancellation);

        /** @brief Enumerates existing save slot headers for save/load browsers. */
        [[nodiscard]] Result<std::vector<SaveGameHeader>> EnumerateSaveSlots() const;

        /** @brief Durably deletes a save slot. */
        [[nodiscard]] Result<void> DeleteSlot(const SaveGameSlotId &slotId);

        /** @brief Pumps owner-thread safe points for transactional commits. */
        void PumpOwnerThread();

        /** @brief Cancels pending work and stops the save service safely. */
        void Shutdown() noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
}
```

### Operation Concurrency & Safe Points

- **Mutual Exclusion**: At most one save or restore operation may be active at any given time.
  Subsequent requests while an operation is in progress are immediately rejected with
  `save.operation.in_progress`.
- **Snapshot Safe Point**: Snapshot capture occurs strictly on the runtime owner thread during
  the `CommitDeferredLifecycleChanges` frame phase, guaranteeing that simulation ticks and
  deferred ECS structural batches are not mid-execution.
- **Asynchronous I/O Offload**: Serialization, chunk compression, and disk I/O execute on
  background worker threads via the `JobSystem`, preventing frame hitches on the main thread.
- **Transactional Commit**: Final state application during a load operation occurs strictly on
  the runtime owner thread at a verified lifecycle commit boundary.

## Save Game Archive Model

### Archive Structure (`.horosave`)

A Horo save game is packaged as a structured archive (either a compressed container or a
directory bundle on supported filesystems):

```text
savegame_slot01.horosave
├── header.json          # SaveGameHeader: metadata, slot info, timestamp, play time, checksum
├── runtime_ecs.bin      # Dynamic ECS state: entity IDs, spawned entities, modified components
├── gameplay_state.bin   # Registered gameplay module state: quest graphs, inventories, stats
├── player_profile.bin   # Player profile: user settings, achievements, stats, preferences
├── thumbnail.png        # Optional 16:9 RGBA PNG thumbnail captured from runtime viewport
└── manifest.json        # Engine build ID, schema versions per subsystem, chunk checksums
```

### Typed Header & Manifest Contracts

```cpp
namespace Horo::Runtime {
    using SaveGameVersion = std::uint32_t;

    /** @brief Header metadata required for save-game browser inspection without full payload deserialization. */
    struct SaveGameHeader {
        SaveGameVersion schemaVersion{1};
        SaveGameSlotId slotId;
        std::string displayName;
        std::string levelAssetName;      // Logical asset path or ID of base level definition
        Assets::AssetId baseSceneAsset;   // Stable asset identity of the root authored scene
        std::uint64_t createdAtUnixSeconds{0};
        std::uint64_t playedAtUnixSeconds{0};
        std::uint64_t playTimeSeconds{0};
        std::uint64_t archiveByteSize{0};
        std::string checksumSha256;
        bool isAutosave{false};
        bool isQuicksave{false};
    };

    /** @brief Manifest listing schema versions and chunk checksums for corruption validation. */
    struct SaveGameManifest {
        std::string engineVersion;
        std::string projectBuildId;
        std::unordered_map<std::string, std::uint32_t> moduleSchemaVersions;
        std::unordered_map<std::string, std::string> chunkChecksums;
    };
}
```

## Save & Restore Workflows

### Save Pipeline

```text
[ Trigger Save ]
       |
       v (Runtime Owner Thread - CommitDeferredLifecycleChanges safe point)
1. Capture In-Memory Snapshot
   - Freeze dynamic ECS entity table & component pools
   - Query registered IGameplayStateProvider instances
   - Capture PlayerProfile snapshot
       |
       v (Worker Thread - Job System)
2. Serialize & Compress Chunks
   - Serialize runtime ECS binary chunk
   - Serialize gameplay module chunks
   - Serialize player profile chunk
   - Write header.json and manifest.json with SHA-256 hashes
       |
       v (Worker Thread)
3. Write Sibling Temporary File
   - Write to "savegame_slot01.horosave.tmp"
   - Flush file buffers to physical storage
       |
       v (Runtime Owner Thread)
4. Atomic Replacement & Index Update
   - DurableFileSystem::AtomicReplace("savegame_slot01.horosave.tmp", "savegame_slot01.horosave")
   - Update in-memory save slot catalog
   - Notify PlatformServices for background cloud synchronization
       |
       v
[ Save Complete Event Published ]
```

### Restore Pipeline & Two-Phase Transaction

```text
[ Trigger Load ]
       |
       v (Worker Thread - Preflight Phase)
1. Preflight & Verification
   - Read and validate header.json and manifest.json
   - Verify chunk checksums (SHA-256)
   - Verify engine and schema compatibility; run migration chain if older
   - Validate base scene asset availability via AssetLoadService
       |
       v (Runtime Owner Thread - Staging Phase)
2. Prepare Candidate Restore Snapshot
   - Instantiate candidate RuntimeSceneDefinition for base level
   - Load required asset leases
   - Deserialize dynamic entity records & gameplay state blobs into candidate staging
       |
       v (Runtime Owner Thread - Commit Safe Point)
3. Atomic State Application
   - If candidate preparation succeeds:
       - Transition active scene to restored state
       - Apply dynamic entity transforms and component values
       - Restore gameplay module state providers
       - Restore player profile
       - Publish SceneRestoredEvent
   - If candidate preparation fails:
       - Abort transaction; discard candidate
       - Keep current scene running or fall back to safe Main Menu
       - Surface typed error diagnostic (e.g. save.corrupt_chunk, save.missing_asset)
```

## Execution Environments & Lifecycles

### 1. Play-In-Editor (PIE)

- **Isolated Sandbox**: PIE sessions execute with a sandboxed save directory
  (e.g., `<project>/.horo/saved/pie/` or in-memory virtual storage).
- **Zero Pollution**: PIE saves never write to or overwrite production player save slots.
- **Authoring Independence**: PIE save operations never modify canonical `.horo` authored scenes
  or dirty `SceneDocument` state.
- **Session Lifecycle**: On stopping PIE, test saves are discarded or preserved based on editor
  developer settings, restoring the editor document cleanly without side effects.

### 2. Packaged Standalone Game

- **Platform-Conforming Storage**: Save directories are resolved through `PlatformServices`
  using OS standard locations:
  - **Windows**: `%USERPROFILE%/Saved Games/<Vendor>/<ProjectName>/`
  - **macOS**: `~/Library/Application Support/<Vendor>/<ProjectName>/saves/`
  - **Linux**: `$XDG_DATA_HOME/<Vendor>/<ProjectName>/saves/` (or `~/.local/share/...`)
  - **Consoles**: Secure platform user save partition / mount point.
- **Slot Management**: Supports manual named slots, a dedicated quicksave slot, and a rotating
  ring buffer of autosave slots (e.g. `autosave_1.horosave` ... `autosave_N.horosave`).
- **Cloud Sync**: Completed local saves register with `PlatformServices` to synchronize with
  Steam Cloud, Xbox Cloud, PSN Cloud, or Apple iCloud.

### 3. Headless & Dedicated Server

- **Server-Authoritative World Save**: Serializes the canonical state of the persistent game world,
  dynamic world entities, NPC AI states, environment systems, and global game rules.
- **No Client Pollution**: Explicitly excludes client player profiles, local HUD/UI variables,
  viewport settings, client audio preferences, or client prediction buffers.
- **Storage Location**: Dedicated server world saves reside in the server instance storage path
  (e.g., `/var/horo/saves/<worldId>/` or configurable server argument `--save-dir`).

## Failure Semantics, Cancellation & Rollback

### Save Failure Semantics

1. **Storage Full / Write Error**: If disk space is exhausted or a write fails during chunk serialization:
   - The temporary `.tmp` archive is immediately removed.
   - The original `.horosave` file on disk is left unmodified and intact.
   - A structured diagnostic error (`Result<SaveOperationHandle>`) is returned.
2. **Cancellation**: If the user or engine requests cancellation via `CancellationToken`:
   - Serialization workers abort cleanly.
   - Partial temporary files are unlinked.
   - No catalog mutation or replacement occurs.

### Load Failure & Rollback Semantics

1. **Corruption or Tampering**: If a chunk checksum mismatches or JSON header is malformed:
   - Load preflight fails before touching the active runtime scene.
   - Simulation continues uninterrupted or cleanly displays an error modal.
2. **Missing Asset Dependencies**: If required DLC or asset packages referenced in the save are missing:
   - The preflight validation returns a descriptive missing-asset diagnostic.
   - The engine does not instantiate a half-loaded, corrupted world.
3. **Migration Failure**: If an older save fails to step through a schema migration function:
   - The migration transaction aborts; the original save file on disk is preserved unchanged.

## Schema Versioning & Migration Chains

Save game schemas evolve across engine releases. Forward migration is supported via
deterministic migration steps:

```cpp
namespace Horo::Runtime {
    struct MigrationContext {
        SaveGameVersion fromVersion;
        SaveGameVersion toVersion;
        std::string moduleName;
    };

    using SaveMigrationFn = std::function<Result<void>(ArchiveReader &reader,
                                                       ArchiveWriter &writer,
                                                       const MigrationContext &context)>;

    struct SaveMigrationStep {
        SaveGameVersion sourceVersion;
        SaveGameVersion targetVersion;
        std::string moduleIdentifier;
        SaveMigrationFn migrate;
    };

    class SaveMigrationRegistry {
    public:
        void RegisterStep(SaveMigrationStep step);
        [[nodiscard]] Result<void> MigrateArchive(ArchiveReader &source,
                                                  ArchiveWriter &target,
                                                  SaveGameVersion targetVersion) const;
    };
}
```

- **Forward Migration**: Older saves are migrated sequentially step-by-step to the current engine version.
- **No Backward Downgrade**: Saves created by newer engine builds are rejected cleanly when opened in older builds.
- **Non-Destructive Upgrade**: Migrated archives are written to a new temporary container and only replace the disk file upon explicit user confirmation or policy.

## Cloud Save & Conflict Policy

1. **Post-Save Registration**: On successful local save completion, `PlatformServices` marks the slot for cloud synchronization.
2. **Conflict Resolution**: If the cloud copy has a newer timestamp or different content hash upon game startup:
   - The engine queries user policy or presents a conflict resolution UI displaying local vs. cloud timestamps, character levels, and playtime.
   - The non-selected conflicting save is backed up to a local conflict slot (`<slotId>_conflict_<timestamp>.horosave`) before replacement.
3. **Quota Awareness**: `RuntimeSaveService` checks storage quotas before attempting large save writes.

## Testing & Verification

Save game architecture is verified by the following test suites:

- **Authority Isolation Tests**: Verify `RuntimeSaveService` operates without dependencies on `SceneDocument` or editor types.
- **Boundary Tests**: Verify `SceneDocumentPersistence` and `RuntimeSaveService` do not share file formats, paths, or mutation coordinators.
- **PIE Sandbox Tests**: Confirm PIE saves write exclusively to sandboxed test directories and leave user slots and `.horo` files untouched.
- **Dedicated Server Tests**: Confirm headless world saves serialize world ECS and game modules while excluding player profiles and client states.
- **Atomic Replace & Rollback Tests**: Simulate write failures, power-loss aborts, and corrupted chunks; verify pre-existing saves remain intact and failed restores roll back safely.
- **Schema Migration Tests**: Validate chained migration across multiple schema versions with deterministic data verification.

## Security & Anti-Cheat

- **Integrity Checksums**: Manifest contains SHA-256 hashes for all binary chunks.
- **Bounds Checking**: Binary archive readers enforce strict bounds checking on array lengths, string lengths, and payload allocations to prevent buffer overflow attacks.
- **Cryptographic Signatures**: Online multiplayer servers and secure titles may sign save manifests with a server private key; clients validate signatures before loading state.
- **No Embedded Credentials**: Save games never store authentication tokens, encryption private keys, user passwords, or account secrets.

## Related Documents

- [Scene Runtime](./scene-runtime.md): Dynamic ECS state, runtime identities, and lifecycle safe points
- [Runtime Lifecycle](./runtime-lifecycle.md): Frame phases, PIE modes, and host execution states
- [Editor Document Model](../editor/editor-document-model.md): Authoritative `SceneDocument` model, undo/redo, and editor save/autosave
- [Project Model](../editor/project-model.md): Project workspace and scene persistence
- [Platform Services Architecture](./platform-services-architecture.md): Cloud save synchronization and storage locations
- [Gameplay Module Boundary](../extensions/gameplay-module-boundary.md): Dynamic gameplay state serialization contracts
- [Error And Diagnostics](../foundation/error-and-diagnostics.md): Structured error handling and diagnostic reporting
- [Application Security](../security/application-security.md): Save file validation, checksums, and signing

