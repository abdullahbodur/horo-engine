# Editor Module

`editor/` provides the in-engine ImGui editor layer for scene authoring.

## Responsibilities

- Editor lifecycle + top-level UI orchestration (`EditorLayer`)
- Scene document model and editor state (`SceneDocument`)
- Scene serialization/deserialization (`SceneSerializer`)
- Entity/component inspection via schema metadata (`EditorSchema`)
- Asset import tooling (`EditorAssetImport`)
- Viewport picking and utility queries (`Raycaster`)
- Workflow UI helpers
    - quick-open search (`EditorSearch`)
    - transform gizmo manipulator (`TransformGizmo`)
    - UI logic/state machine (`EditorUiLogic`)
    - built-in MCP bridge and editor settings surface

## User-Facing Features

- Entity hierarchy and selection
- Component inspector/editing
- Create/delete/duplicate entities
- Scene dirty-state awareness and confirmations
- Keyboard shortcut help popup (`F1`/`?`) + searchable actions
- Quick-open (`Ctrl/Cmd+P`) for objects/assets
- Asset panel import workflow
- Viewport picking and transform manipulation
- Built-in MCP lifecycle, status tab, and settings modal
- Tabbed editor Settings modal (`File → Settings...`) with `MCP` and `Appearance` tabs

## Data Contracts

- Component schema defaults are sourced from `assets/editor_schema.json`.
- Scene files are JSON (`.horo` workflow through `SceneSerializer`).
- `SceneProjectBridge` converts editor-facing `SceneDocument` data into the engine-owned typed `SceneProjectModel`.
- `SceneRuntimeBridge` exposes the canonical authoring path from `SceneDocument` to engine-owned
  `RuntimeSceneDefinition`.
- `SceneRuntimeCoordinatorBridge` wires `SceneDocument` into the lifecycle-managed runtime coordinator path.
- User MCP settings are stored in `~/.horo/settings.json`.
- Global editor user preferences (theme preset) are stored in `~/.horo/editor_settings.json`.

## Settings Modal

The Settings modal is opened from `File → Settings...` and has two tabs.

- **MCP** — Built-in server settings. Preserves the prior fields: `Enable
  built-in MCP` toggle, `Auto-start when editor opens` checkbox, `Port`
  (clamped `[1, 65535]`), read-only `Host` pinned to `127.0.0.1`, and the
  derived read-only `Endpoint` `http://127.0.0.1:<port>/mcp`.
- **Appearance** — Theme preset selection. Exactly three presets ship:
  `Dark Blue` (default, the original Horo palette), `Graphite` (neutral
  dark workspace), and `High Contrast` (maximum contrast for readability).

Edits are staged per tab and preserved when switching tabs. The footer has
three actions: **Cancel** discards unsaved edits and closes, **Apply** saves
all dirty settings and keeps the modal open, **OK** saves and closes when
the save succeeds. Escape and the header close affordance behave like
Cancel. If a save step fails, the modal stays open and surfaces an inline
error; originals and the live theme remain unchanged.

## Notes

- Editor code is modularized to keep `EditorLayer` lightweight.
- The module is designed to run both in embedded game workflows and launcher engine development.
