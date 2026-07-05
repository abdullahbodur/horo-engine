# Project Model

## Purpose

This document defines the durable project and workspace model for Horo Engine.
It covers the on-disk project structure, editor workspace persistence, project
settings, and the data model shared by project-browser screens, the editor, CLI,
and MCP.

## Project Directory

A Horo project is a directory with a `.horo/` metadata folder:

```text
MyGame/
    .horo/
        project.json           # project identity and settings
        plugins.json           # portable requested plugin dependencies
        editor_workspace.json  # last editor layout and UI state
        asset_index.json       # derived asset lookup registry
        local/                 # optional machine-local overrides, ignored
    assets/                    # source assets
        models/
        textures/
        materials/
        shaders/
        scenes/
    src/                       # optional game code
    CMakeLists.txt             # optional project build file
    build/                     # generated build outputs and asset caches
```

The project root is the directory containing `.horo/`. All project-relative
paths are resolved from this root.

Durable portable metadata, machine-local state, and derived output remain
separate:

- `.horo/project.json`, `.horo/plugins.json`, and asset sidecars are portable
  source-controlled inputs
- `.horo/editor_workspace.json`, `.horo/local/`, and `.horo/asset_index.json`
  are local or derived state
- `build/` contains generated build outputs and content-addressed asset caches

## Project Format And Identity

`project.json` stores stable project metadata:

```json
{
  "formatVersion": 1,
  "projectId": "proj_2a4f...",
  "name": "MyGame",
  "projectVersion": "0.1.0",
  "createdAt": "2026-01-15T09:30:00Z",
  "settings": {
    "renderBackend": "opengl",
    "physicsEnabled": true,
    "targetFrameRate": 60,
    "defaultScene": "assets/scenes/main.horo",
    "buildProfile": "desktop-debug",
    "requiredToolchain": {
      "targetPlatform": "host",
      "compilerFamily": "default",
      "minimumCxxStandard": 20
    }
  }
}
```

`formatVersion` identifies the durable project schema and selects migrations.
`projectVersion` is the game/product version and does not select project-format
migrations. Unknown future format versions are rejected with a typed diagnostic;
the engine does not guess compatibility.

`projectId` is generated once at project creation and never changes. It is used
as a non-secret observability, crash-reporting, and workspace-correlation
identifier.

User activity such as `lastOpenedAt` does not belong in portable
`project.json`; it is stored in the user-level recent-projects model.

## Project Settings

Settings are typed and validated. They include:

| Setting              | Type              | Default            |
| -------------------- | ----------------- | ------------------ |
| `renderBackend`      | string            | `"opengl"`         |
| `physicsEnabled`     | bool              | `true`             |
| `targetFrameRate`    | int               | `60`               |
| `defaultScene`       | `ProjectPath`     | empty              |
| `assetCompression`   | string            | `"lz4"`            |
| `textureCompression` | string            | `"bc7"`            |
| `buildProfile`       | string            | `"desktop-debug"`  |
| `requiredToolchain`  | typed requirement | host/default/C++20 |

Settings are exposed through:

- editor settings panel
- CLI: `horo-engine project settings set --project <root> <key> <value>`
- MCP: `set_project_setting` tool

Invalid settings are rejected at load time with a typed error; the engine does
not silently fall back.

Settings persistence uses deterministic serialization, a sibling temporary
file, and atomic replacement. Concurrent writers use project identity and file
revision checks so one GUI, CLI, or MCP operation cannot silently overwrite a
newer settings commit.

## Toolchain Profiles

Portable project settings describe build intent and minimum toolchain
requirements. They do not store compiler paths, SDK installation paths, signing
identities, or machine-specific profile names.

User- or host-level toolchain profiles resolve those requirements to installed
tools:

```json
{
  "toolchainProfiles": {
    "linux-clang": {
      "targetPlatform": "linux",
      "compilerFamily": "clang",
      "generator": "Ninja",
      "compilerPath": "/usr/bin/clang++",
      "cmakePath": "/usr/bin/cmake"
    },
    "windows-msvc": {
      "targetPlatform": "windows",
      "compilerFamily": "msvc",
      "generator": "Visual Studio 17 2022",
      "compilerPath": "C:/Program Files/Microsoft Visual Studio/..."
    }
  }
}
```

Profiles are stored through the platform user-configuration directory. CI
provides equivalent profiles through validated invocation or CI configuration.
Resolution selects a compatible local profile from the portable project
requirements; a developer does not need a profile with an identical arbitrary
name.

Optional project-specific machine overrides live under
`.horo/local/toolchains.json` and are ignored by version control. Absolute paths
are legal only in user/CI/local-machine configuration, never in portable project
metadata. Signing identities and credentials remain credential references
resolved through the OS or CI credential provider.

## Editor Workspace Persistence

Editor workspace state is stored in `.horo/editor_workspace.json`. It includes:

- active scene path
- open tabs and active tab per tab stack
- layout-tree split ratios and collapsed state
- viewport camera state
- selected objects
- per-tab persisted UI state

```json
{
  "schemaVersion": 1,
  "scenePath": "assets/scenes/main.horo",
  "selection": ["obj_wall_north"],
  "panelLayout": {
    "schemaVersion": 1,
    "closedTabs": [],
    "root": {
      "type": "split",
      "id": "workspace_root",
      "axis": "vertical",
      "ratio": 0.78,
      "first": {
        "type": "split",
        "id": "main_row",
        "axis": "horizontal",
        "ratio": 0.18,
        "first": {
          "type": "split",
          "id": "left_column",
          "axis": "vertical",
          "ratio": 0.62,
          "first": {
            "type": "tabStack",
            "id": "hierarchy_stack",
            "tabs": ["hierarchy"],
            "activeTab": "hierarchy"
          },
          "second": {
            "type": "tabStack",
            "id": "project_stack",
            "tabs": ["project"],
            "activeTab": "project"
          }
        },
        "second": {
          "type": "split",
          "id": "content_row",
          "axis": "horizontal",
          "ratio": 0.78,
          "first": {
            "type": "panel",
            "id": "viewport_panel",
            "panel": "viewport"
          },
          "second": {
            "type": "tabStack",
            "id": "properties_stack",
            "tabs": ["properties"],
            "activeTab": "properties"
          }
        }
      },
      "second": {
        "type": "tabStack",
        "id": "bottom_tools",
        "tabs": ["assets", "console", "mcp", "performance"],
        "activeTab": "console"
      }
    }
  },
  "viewport": {
    "eye": [10, 10, 10],
    "target": [0, 0, 0]
  },
  "tabs": {
    "hierarchy": {
      "schemaVersion": 1,
      "payload": {
        "searchFilter": "",
        "expandedNodes": ["Room"]
      }
    }
  }
}
```

The complete workspace document is owned by `EditorWorkspaceController` and
persisted on editor close or explicit save. Ownership of its runtime slices is:

- `EditorPanelHost`: layout tree, tab placement, active tabs, and collapsed state
- `EditorSelectionModel`: selected objects and selected asset
- `EditorViewportModel`: editor camera and navigation state
- individual tabs: project-scoped presentation state under their stable tab ID

The controller gathers these slices into one versioned document and restores
them in dependency order. Workspace state is not part of the scene document and
is not versioned with scene changes.

Workspace restore is best-effort and must not prevent project opening:

- unsupported or corrupt workspace documents fall back to the versioned default
  layout
- deleted scenes are not opened and produce a typed diagnostic
- missing tabs and panels are reported and skipped while bounded opaque plugin
  state is preserved where supported
- missing selected objects or assets are removed when selection is reconciled
- invalid per-tab state is ignored without discarding otherwise valid layout
  state

The restored state is normalized before the next workspace save so stale
references do not persist indefinitely.

Open editor modals, their navigation step, focus, draft form values, and child
stack are transient GUI state and are not stored in the workspace document.

## Asset Index

`asset_index.json` is a derived lookup registry for imported assets:

```json
{
  "schemaVersion": 1,
  "assets": {
    "mesh_cube_001": {
      "type": "mesh",
      "sourcePath": "assets/models/cube.fbx",
      "metadataPath": "assets/models/cube.fbx.horo"
    }
  }
}
```

The committed sidecar next to each source asset is the source of truth for:

- stable logical asset ID and GUID
- metadata schema and importer version
- import and cook settings
- dependency identities

Scene documents reference the stable logical asset ID. `asset_index.json` may be
ignored because rebuilding it scans source assets and their committed sidecars;
rebuild never generates replacement IDs for valid sidecars.

Rebuild is deterministic and transactional:

- deleted source assets remove derived path entries but do not silently rewrite
  scene references
- moved assets retain identity when the sidecar moves with the source
- missing, corrupt, or manually edited sidecars produce typed diagnostics and
  require repair or explicit re-import
- duplicate or conflicting IDs fail validation rather than selecting one entry
- changed import settings invalidate the affected imported/cooked cache entries
- stale generated outputs are excluded until their source and sidecar validate

`AssetImportService` writes or updates the sidecar and commits the derived index
only after a successful import transaction. Developers do not edit the index
manually.

## Scene Documents

Scene documents live in `assets/scenes/` and are editor-authorable files. They
store:

- object hierarchy
- components and their properties
- asset references by logical ID
- editor-only metadata (gizmo state, camera bookmarks)

Scene documents are converted to runtime definitions before entering the
runtime. See [System Design](../foundation/system-design.md) for the scene boundary.

## Project Operations

Application use cases own project-level operations:

- `CreateProject`
- `OpenProject`
- `CloseProject`
- `SaveProjectSettings`
- `MigrateProject`
- `ValidateProject`

GUI, CLI, and MCP adapters invoke these same use cases where the operation is
exposed by that host. Host availability and presentation routing are explicit;
an adapter does not reimplement project business rules.

`OpenProject` opens or validates an application-level project session. GUI route
navigation remains owned by `GuiScreenHost`; an application use case never
silently replaces the active screen.

## Project Validation

Validation is operation-specific:

```cpp
enum class ProjectValidationMode {
    ReadOnly,
    Edit,
    Build
};
```

All modes require:

- `.horo/project.json` exists and parses correctly
- `formatVersion` is supported or has a valid migration path
- `projectId` is present
- referenced default scene exists or is empty
- asset sidecars can be scanned and the index can be loaded or reconstructed

`ReadOnly` requires only readable project inputs and supports project browsing,
information queries, validation, and read-only CI checks. A missing index is
reconstructed in memory without writing project state.

`Edit` validates writability per requested mutation: saving settings requires a
writable `project.json`, saving a scene requires that scene destination, and
importing requires the source sidecar and derived-index destination. Failure to
persist optional workspace state is a warning and does not make an otherwise
editable project invalid.

`Build` requires a compatible toolchain resolution and writable build/output
directories. A read-only source checkout remains valid when build outputs and
caches are redirected to writable locations.

Validation produces typed diagnostics, not silent failures.
Portable validation also detects project paths that collide under supported
case-folding rules even when the current filesystem would allow both names.

## Migration

When `formatVersion` is older than the current supported project schema,
migration use cases transform the project:

```text
project v1  ->  project v2  ->  project v3
```

Migrations are:

- deterministic
- logged
- reversible when possible
- tested with fixture projects for each supported version

Migration writes use staged files and atomic replacement. A failure preserves
the pre-migration portable metadata and reports the failing transformation.
Unknown future versions are never migrated downward or opened as writable
projects.

## Recent Projects

The graphical host maintains a recent projects list in the platform user-state
directory:

```text
<user-state>/horo/recent_projects.json
```

Entries include:

- project root path
- project name
- last opened timestamp
- a project thumbnail cache key when available

The list is bounded (default 20 entries) and prunes missing or invalid paths
lazily.

Project thumbnails live under the platform user-cache directory and are looked
up by cache key, normally derived from `projectId`, a canonical-root hash, and
thumbnail revision. The recent-projects file does not persist arbitrary
absolute thumbnail paths.

## Project Browser Screen

The project-browser screen inside `HoroEditor` uses the project model to:

- list recent projects
- create new projects from templates
- open existing projects
- validate project before opening

It does not load scene data or editor workspace state until the editor workspace
is activated. It is a screen in the graphical host, not a separate launcher
module or lifecycle.

## CLI Commands

```bash
# Create a new project
horo-engine create-project MyGame --template empty

# Open a project (validates and prints info)
horo-engine project info /path/to/MyGame

# Set a project setting
horo-engine project settings set --project /path/to/MyGame renderBackend vulkan

# Validate a project
horo-engine project validate /path/to/MyGame
```

## MCP Tools

- `create_project`
- `set_project_setting`
- `get_project_info`
- `validate_project`
- `open_project_session` (headless host)
- `open_project_workspace` (GUI host)

`get_project_info` and `validate_project` are read-only and do not change the
active project or GUI route. `open_project_session` opens an application-level
session in the headless host.

`set_project_setting` identifies an explicit project root or validated active
session and uses the same revision-checked `SaveProjectSettings` operation as
the GUI and CLI.

`open_project_workspace` is a GUI-only adapter over `GuiScreenHost::Navigate`.
It passes through route validation, dirty-document leave guards, running-work
policy, native-dialog deferral, and modal interaction policy. MCP transport code
never mutates GUI route state directly. Stateful open requests return typed
`Busy`, leave-denied, cancellation, or entry-failure results.

## Security And Portability

- Portable project files store normalized forward-slash `ProjectPath` values.
  Absolute paths, drive roots, UNC roots, and `..` traversal are rejected.
- Lexical normalization must not escape the project root. For an existing path,
  security-sensitive operations resolve symlinks and reject a target outside the
  canonical project root. For a new write target, the nearest existing parent is
  resolved and checked before creation.
- Symlinks that resolve inside the project root may be read only when the owning
  operation permits them. Import, delete, migration, and build-output operations
  reject symlink ambiguity by default.
- External files require an explicit user-approved external-reference type;
  ordinary project paths never gain external access implicitly.
- Portable project files must not contain secrets, credentials, signing key
  paths, compiler installation paths, or API tokens.
- Signing identities and credentials use opaque references resolved by the OS or
  CI credential provider.
- Workspace state is user-specific and should not be committed to version
  control.
- `.horo/project.json`, `.horo/plugins.json`, and asset metadata sidecars are
  portable and may be committed. `.horo/editor_workspace.json`,
  `.horo/asset_index.json`, `.horo/local/`, and generated build/cache output
  should be ignored.

Recommended `.gitignore`:

```gitignore
.horo/editor_workspace.json
.horo/asset_index.json
.horo/local/
build/
```

## Testing

Required coverage:

- project format and product version remain independent
- unknown future `formatVersion` values fail without mutation
- each supported migration fixture upgrades deterministically and atomically
- failed migration preserves the original portable metadata
- `defaultScene` resolves from the project root as `assets/scenes/...`
- settings exist in one canonical location and concurrent saves detect revision
  conflicts
- read-only validation succeeds without writing an index or workspace state
- edit and build validation check only the writable targets required by the
  requested operation
- missing index rebuild preserves sidecar asset IDs and import settings
- duplicate, missing, corrupt, and moved sidecars produce the documented results
- scene logical asset references survive deterministic index rebuild
- corrupt or stale workspace state cannot prevent project opening
- missing scenes, selections, tabs, and panels are skipped with diagnostics
- local toolchain resolution works with different profile names on two machines
- portable project files reject absolute paths, traversal, and case collisions
- symlink containment is enforced for reads, writes, imports, deletes, and build
  outputs
- recent-project thumbnails resolve through user cache keys
- GUI MCP project open passes through navigation leave guards
- headless project open does not mutate GUI route state

## Related Documents

- [Project Settings UI Reference](./project-settings.html)

- [New Project Wizard](./new-project-wizard.html): HTML reference design for
  project creation, template selection, path validation, and initial settings.
- [System Design](../foundation/system-design.md): host and module boundaries.
- [Asset Pipeline](../runtime/asset-pipeline.md): source to cooked asset flow.
- [Configuration System](../foundation/configuration-system.md): project setting schema,
  precedence, validation, and immutable snapshots.
- [Editor Document Model](./editor-document-model.md): scene save, autosave,
  recovery, and external-file conflict behavior.
- [Editor Panel Host](./editor-panel-host.md): workspace layout model.
- [GUI Screen Host](./gui-screen-host.md): project-browser and editor-workspace
  route lifecycle.
- [Platform Abstraction](../foundation/platform-abstraction.md): structured paths,
  user directories, atomic replacement, and symlink policy.
- [MCP Architecture](../interfaces/mcp-architecture.md): host-specific tool
  availability and main-thread dispatch.
- [Release Architecture](../release/release.md): packaging project artifacts.
- [Horo Package System](../packages/package-system.md): project package dependencies and lockfile
- [Observability Architecture](../observability/observability.md): safe project correlation and
  game-specific log storage.
