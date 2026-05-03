# HORO-38 Editor Panel Theme Design

## Goal

Bring the editor chrome into the same visual language as the refreshed launcher by sharing the launcher color palette and core ImGui component styling. The editor should feel like the same product as the launcher while preserving editor density and workflow speed.

## Branch

`feat/HORO-38_editor_panel_theme`

## Scope

This work targets all editor chrome:

- Toolbar and status bar
- Hierarchy panel
- Assets panel
- Properties panel
- Bottom dock tabs and content surface
- Command palette, quick-open, settings, and other modal surfaces

The launcher should remain visually unchanged except where it reads the same shared theme tokens it already effectively owns today.

## Non-Goals

- Do not redesign editor layout or panel arrangement.
- Do not increase editor spacing enough to reduce information density.
- Do not move large launcher/editor product modules into a new folder as part of this ticket.
- Do not build a broad widget framework.
- Do not rename ImGui IDs, panel labels, or automation selectors unless required and covered by tests.

## Direction

Use **shared theme tokens plus a targeted editor chrome pass**.

The launcher already has a cohesive palette and component language: deep navy backgrounds, translucent panel surfaces, soft borders, muted text, blue accents, rounded cards, and clear hover/active states. HORO-38 should extract that visual language into a small shared `ui/` module, then apply it across editor chrome with compact editor-specific density.

## Shared UI Module

Add a top-level `ui/` module for shared ImGui presentation utilities.

Recommended initial files:

- `ui/HoroTheme.h`
- `ui/HoroTheme.cpp`
- `ui/UiComponents.h`
- `ui/UiComponents.cpp`

### `HoroTheme`

`HoroTheme` should define shared tokens rather than product-specific behavior:

- Background colors
- Panel and soft-panel surfaces
- Card surfaces
- Border color
- Muted and primary text colors
- Accent, hover, and active colors
- Selection colors
- Input colors
- Button colors
- Modal surface colors
- Rounding tokens for panel, card, button, input, and tab
- Density tokens for launcher and editor variants

The editor should use compact density tokens. The launcher may use roomier density tokens where it already has larger surfaces.

### `UiComponents`

`UiComponents` should stay narrow and only contain helpers that reduce real duplication:

- Scoped style helpers for panels/cards/modals
- Section header helpers
- Primary and secondary button styling
- Search/input styling helpers
- Tab/chip style helpers if they simplify bottom dock styling

These helpers must preserve ImGui style stack safety. Prefer small RAII wrappers or explicit, easy-to-audit push/pop helpers.

## Folder Strategy

Use `ui/` for shared theme and common components now.

Keep existing `launcher/` and `editor/` folders as product modules during HORO-38. Moving launcher/editor-specific files under `ui/launcher/` and `ui/editor/` should be a separate cleanup after the shared module proves useful. This avoids turning a visual theme ticket into a large directory migration.

## Editor Application Plan

### Toolbar and Status Bar

- Apply shared background, border, and accent tokens.
- Preserve current button sizes and menu spacing.
- Improve active and disabled state clarity without changing behavior.

### Hierarchy Panel

- Use shared panel surface and border styling.
- Apply shared input styling to search.
- Use shared accent tokens for hover and selected rows, keeping selected and hovered states visually distinct.

### Assets Panel

- Use shared panel/card colors for asset rows, tiles, and empty states.
- Preserve current asset density and drag/drop behavior.

### Properties Panel

- Use compact section-card treatment for grouped properties.
- Use muted labels, clearer dividers, and shared input/button styling.
- Preserve existing property editing behavior and selection logic.

### Bottom Dock

- Apply shared panel and tab chrome.
- Make active tab state clearer with accent tokens.
- Preserve dock layout, tab names, and content behavior.

### Modals and Popups

- Apply shared modal surface, border, and input focus styling to command palette, quick-open, settings, and related modal surfaces.
- Preserve keyboard flows and existing close/apply/cancel behavior.

## Visual Constraints

- Prefer visual consistency with the launcher, not a full launcher-sized layout treatment.
- Keep editor information density roughly unchanged.
- Avoid overusing bright blue accents; reserve strong accent for active/focused/primary states.
- Maintain contrast for text, selected rows, hover states, and disabled controls.
- Validate at typical editor sizes, especially 1280x720 and 1440x920.

## Testing and Validation

Required validation:

- Build the touched targets.
- Run targeted launcher/editor/unit tests affected by shared theme changes.
- Run UI automation suites that cover launcher and editor chrome.
- Capture or inspect launcher and editor visuals after changes to confirm shared visual language and preserved editor density.

Suggested UI automation coverage:

- `launcher-basic`
- `properties-workflows`
- `mcp-project`
- `modals-mcp`
- `properties-close`

Suggested visual checks:

- Launcher home still matches the refreshed design.
- Editor with an active project shows coherent toolbar, hierarchy, assets, properties, bottom dock, and modal styling.
- Empty states, long asset names/paths, multi-selection, and keyboard-driven modals remain readable.

## Risks

- Large files such as `editor/EditorLayer.cpp` and `launcher/LauncherEditorShell.cpp` can make broad style changes risky.
- Inline ImGui style stacks are easy to imbalance.
- Copying launcher spacing directly into editor panels could reduce density.
- Shared helpers that are too generic could become a premature UI framework.
- Changing labels or IDs could break UI automation.

## Acceptance Criteria

- A shared `ui/` module owns the launcher/editor theme tokens.
- Launcher and editor use the same palette source where practical.
- Editor chrome adopts launcher-inspired colors, borders, rounding, and component styling while preserving density.
- Launcher design remains visually intact.
- UI automation selectors remain stable.
- Relevant tests and UI automation pass, with visual screenshots/manual review confirming launcher/editor consistency.
