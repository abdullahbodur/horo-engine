# Editor UI Components

## Purpose

Document the actual shared component library at
`include/Horo/Editor/EditorUiComponents.h` and
`src/editor/design_system/components/EditorUiComponents.cpp`.

This file supplements [ui-design-system.md](./ui-design-system.md) with the
concrete component API surface that is already implemented.

## Namespace

All components live in `Horo::Editor::Ui`.

## Implemented Components

### Primitives

| Component | Signature | Description |
|---|---|---|
| `Button` | `bool Button(const ButtonProps &)` | Primary / Secondary semantic button |
| `ScopedCard` | RAII class | Child surface with padding and background |
| `IconCloseButton` | `bool IconCloseButton(id, size)` | Vector close icon |
| `SectionTitle` | `void SectionTitle(label, fonts)` | Uppercase section heading (18px monoBold) |
| `FieldLabel` | `void FieldLabel(label, fonts)` | Uppercase field label (12px monoBold) |
| `Hint` | `void Hint(text, fonts)` | Muted wrapped helper text |
| `DashedSeparator` | `void DashedSeparator(dash, gap)` | Dashed horizontal rule |

### Form Controls

| Component | Signature | Description |
|---|---|---|
| `SettingGroup` | `void SettingGroup(label, fonts, first)` | Section group heading + divider |
| `SettingRow` | `template<ControlFn> void SettingRow(label, desc, fonts, control)` | Two-column form row |
| `ComboControl` | `void ComboControl(id, value, items, count, fonts)` | Dropdown with shared frame styling |
| `InputTextControl` | `void InputTextControl(id, buffer, size, fonts)` | Text input |
| `InputIntControl` | `void InputIntControl(id, value, fonts)` | Integer input |
| `InputFloatControl` | `void InputFloatControl(id, value, fonts)` | Float input |
| `SliderIntControl` | `void SliderIntControl(id, value, min, max, suffix, fonts, step)` | Custom range slider |
| `ToggleControl` | `bool ToggleControl(id, value, fonts, showLabel)` | iOS-style toggle switch |

### Composite

| Component | Signature | Description |
|---|---|---|
| `PluginRow` | `void PluginRow(name, version, desc, enabled, fonts)` | Plugin entry with version + toggle |
| `ShortcutDisplay` | `void ShortcutDisplay(a, b, c, fonts)` | Keyboard shortcut keycaps |
| `ThemeChip` | `bool ThemeChip(label, swatch, active, fonts)` | Color theme chip button |

### Dock & Workspace

| Component | Signature | Description |
|---|---|---|
| `DrawDockTabs` | `int DrawDockTabs(tabs, activeTab, fonts)` | Draws the tab bar for a dock and returns clicked index |
| `DrawObjTitle` | `void DrawObjTitle(title, badgeText, badgeBg, badgeFg, fonts)` | Inspector object title header |
| `DrawPropSection` | `void DrawPropSection(label, fonts)` | Inspector property section header |
| `DrawPropRow` | `void DrawPropRow(label, value, fonts)` | Inspector property key-value row |
| `BeginContextMenu` / `EndContextMenu` | popup scope | Shared styled context-menu surface |
| `BeginContextWindowMenu` | popup scope | Empty-window-space context surface |
| `BeginContextSubmenu` / `EndContextSubmenu` | nested popup scope | Standard ImGui nested-menu measurement, placement, and arrow behavior |
| `ContextMenuItem` | `bool ContextMenuItem(label, shortcut, fonts, tone, iconToken)` | Standard menu action with optional semantic danger tone; layout is not manually offset |
| `DrawFloat3PropRow` | `Float3PropertyEditResult DrawFloat3PropRow(label, id, value, fonts, speed)` | Inspector X/Y/Z editor with explicit interaction commit state |

## Usage

Modals and screens import via `#include "Horo/Editor/EditorUiComponents.h"`
and use `using namespace Horo::Editor::Ui;` or qualify with `Ui::`.

```cpp
SettingRow("Label", "Hint text.", fonts, [&]() {
    ComboControl("##id", &value, items, count, fonts);
});
```

## Module Layout (Current)

```text
include/Horo/Editor/
    EditorUiComponents.h       ← public header (all components)
    EditorTheme.h              ← Theme:: fonts, colors, layout

src/editor/
    design_system/components/
        EditorUiComponents.cpp ← implementations
    app/
        EditorTheme.cpp        ← theme palette + preset switching
```

The current layout is flat; subdirectories for primitives/layout/composite
will be introduced when the component count exceeds ~30.

## Theme File Format (Current)

Custom theme JSON files use a simple flat format:

```json
{
    "name": "Monokai",
    "WindowBg": "#272822",
    "ChildBg": "#1e1f1c",
    "Text": "#f8f8f2",
    "Border": "#49483e",
    ...
}
```

Discovery path: `~/.horo/themes/*.json`

The `schemaVersion`, `id`, `displayName`, `extends`, and `tokens` nesting
described in ui-design-system.md is the **target format**. The current
implementation accepts the flat format for rapid iteration. Migration to
the versioned schema is planned.
