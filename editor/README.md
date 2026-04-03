# Editor Roadmap

This file tracks the current editor capabilities and the next UI tasks.
The goal is to land small, testable PRs that can be merged into `horo-engine#4`.

## Current State (analyzed from `feat/editor-asset-panel-polish`)

- Editor toggle and camera:
  - `F10` toggle editor mode
  - Fly mode toggle with `Tab` and toolbar button
  - View snap gimbal (Top/Bottom/Left/Right/Front/Back)
- Scene editing:
  - Object list with search
  - Multi-select via Shift
  - Delete selected (panel action and `Delete` key)
  - Duplicate selected object
- Asset workflow:
  - Asset registry panel with selected-row highlight
  - Asset search modal (spotlight style)
  - Add prop from selected asset
  - Create/Delete asset definitions
  - Import `.obj` (Windows native picker)
  - Bind selected object to asset from Properties panel
- Persistence and feedback:
  - Load/Save scene
  - Hot-reload overlay
  - Copy selection reference to clipboard (`Ctrl/Cmd+Shift+C`) with toast

## Observed Gaps

- No dedicated Help popup for shortcuts/keymap.
- No command palette for fast action discovery.
- No shortcut discoverability beyond small toolbar hints.
- Limited confirmation UX for destructive actions.

## TODO

- [x] Add `?` / `F1` Help popup with keyboard shortcuts grouped by category.
- [ ] Add in-popup search for shortcut names and command labels.
- [ ] Add `Ctrl/Cmd+P` quick-open for objects/assets.
- [ ] Add confirmation modal for destructive operations (Delete Object/Delete Asset).
- [ ] Add richer empty states in Objects/Assets panels.
- [ ] Add status bar (selection count, dirty state, fly mode, pending reload).
- [ ] Add keymap data source (single table in code or JSON) to avoid hardcoded duplicates.
- [ ] Add editor shortcut docs sync check (tests or lint-style validation).

## PR Strategy

- Keep each PR small and focused (one UX slice).
- Include manual test notes in each PR body.
- Merge feature PRs into `horo-engine#4`, then merge that PR into `main`.

## Parallel Tracks

- Track A (Shortcut UX):
  - Help popup (`?`/`F1`) and search
  - Keymap table centralization
- Track B (Safety UX):
  - Destructive action confirmation modals
  - Better error copy for import/save failures
- Track C (Navigation UX):
  - Quick open (`Ctrl/Cmd+P`) for objects/assets
  - Empty states and status bar polish
