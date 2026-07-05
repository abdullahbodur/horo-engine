# Save Game And Persistence

## Purpose

This document defines the save-game and persistence subsystem for Horo Engine.
It covers serialization of game state to durable storage, save-game formats,
versioning and migration, platform save locations, cloud save integration, and
editor save workflows.

## Save Game Model

A save game is a portable archive containing:

- Scene state (object hierarchy, component data, asset references)
- Gameplay state (game mode variables, quest progress, inventory)
- Player profile (settings, achievements, stats)
- Thumbnail and metadata for the save-game browser

```cpp
struct SaveGameHeader {
    SaveGameVersion   schemaVersion;
    SaveGameId        saveId;
    std::string       displayName;
    Timestamp         createdAt;
    Timestamp         playedAt;
    Duration          playTime;
    uint32_t          slotIndex;
    AssetId           thumbnailId;
};
```

## Serialization Model

Game state is serialized through the scene document model:

- Scene objects and components implement `SerializeForSave` and
  `DeserializeFromSave`
- Transient runtime state (particle positions, physics velocities) is captured
  at the save point
- Asset references use stable logical IDs; the save file does not embed asset
  data
- Save data uses the same deterministic serialization as scene documents

```cpp
class ISaveableComponent {
public:
    virtual void SerializeForSave(ArchiveWriter& writer) const = 0;
    virtual void DeserializeFromSave(ArchiveReader& reader) = 0;
    virtual SaveableComponentFlags GetSaveFlags() const;
};
```

## Save File Format

Save files use the Horo archive format:

```text
savegame.horosave
├── header.json          # SaveGameHeader
├── scene_state.bin      # scene object and component data
├── gameplay_state.bin   # gameplay module state (versioned per module)
├── player_profile.bin   # settings, achievements
├── thumbnail.png        # optional save thumbnail
└── metadata.json        # engine version, DLC list, checksums
```

## Save Operations

### Save

1. Capture all saveable component state into a serialization buffer
2. Capture gameplay module state through `GameModule::SaveState`
3. Serialize player profile
4. Write archive to a temporary file
5. Atomically replace the previous save file
6. Update the save-game index

### Load

1. Validate archive integrity (checksums, schema version)
2. Load the default scene or the saved scene
3. Apply saved component state on top of default-initialized objects
4. Restore gameplay module state
5. Restore player profile
6. Reconcile references (removed DLC, missing assets produce diagnostics)

### Delete

1. Remove the save file
2. Remove entry from the save-game index
3. Optionally move to platform recycle bin

## Versioning And Migration

Save files carry a `schemaVersion`. When the engine's save schema advances:

- Older save files are migrated on load through a chain of migration functions
- Migration is deterministic and logged
- Migration failure preserves the original file and reports the failing step
- Forward compatibility is not supported (newer saves rejected with diagnostics)

```cpp
using SaveMigrationFn = std::function<Result<void>(ArchiveReader&, ArchiveWriter&, MigrationContext&)>;

struct SaveMigrationStep {
    SaveGameVersion fromVersion;
    SaveGameVersion toVersion;
    SaveMigrationFn migrate; // reads old archive, writes upgraded archive
};
```

## Save Locations

Save files are stored in platform-appropriate locations:

| Platform | Save Location |
| -------- | ------------- |
| Windows  | `%USERPROFILE%/Saved Games/Horo/<projectId>/` |
| macOS    | `~/Library/Application Support/Horo/<projectId>/` |
| Linux    | `$XDG_DATA_HOME/Horo/<projectId>/` |
| Console  | Platform-specific user save partition |

The save location is resolved through `PlatformServices`. Cross-platform
cloud save uses the platform's cloud save API when available.

## Cloud Save Integration

Platform cloud save services are integrated through `PlatformServices`:

- Save files are marked for cloud synchronization after local save
- Conflict resolution uses last-write-wins with conflict copy preservation
- Cloud quota is checked before upload
- Cloud save status is exposed through the save-game browser UI

## Auto-Save

The engine supports configurable auto-save:

- Time-based: save every N minutes of gameplay
- Event-based: save on scene transition, checkpoint, or gameplay trigger
- Suspend-based: save when the application is suspended (console, mobile)

Auto-save uses a rotating set of auto-save slots (default 3).

## Editor Save Workflow

In the editor, scene saving uses the scene document model, not the save-game
system:

- Scene save writes `.horo` scene documents to the project asset directory
- Scene auto-save writes to a temporary recovery location
- The save-game system is used for Play Mode saves (capturing runtime state
  for later debugging or gameplay testing)

## Security

- Save files are not encrypted by default; platform-level encryption is used
  on consoles
- Save files do not contain credentials or signing keys
- Save validation rejects saves with tampered checksums
- Online services may cryptographically sign save files for anti-cheat

## Related Documents

- [Save/Load Manager UI Reference](./save-load-manager.html)

- [Scene Runtime](./scene-runtime.md): scene state serialization
- [Project Model](../editor/project-model.md): scene documents and asset references
- [Platform Services Architecture](./platform-services-architecture.md): cloud save integration
- [Asset Pipeline](./asset-pipeline.md): asset ID stability for save compatibility
- [Error And Diagnostics](../foundation/error-and-diagnostics.md): save validation diagnostics
- [Application Security](../security/application-security.md): save file integrity
