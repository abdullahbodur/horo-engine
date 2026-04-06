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

## Data Contracts

- Component schema defaults are sourced from `assets/editor_schema.json`.
- Scene files are JSON (`.horo` workflow through `SceneSerializer`).
- User MCP settings are stored in `~/.horo/settings.json`.

## Notes

- Editor code is modularized to keep `EditorLayer` lightweight.
- The module is designed to run both in embedded game workflows and standalone engine development.
