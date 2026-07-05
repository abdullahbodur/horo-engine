# Game UI And HUD Architecture

## Purpose

This document defines Horo Engine's runtime game UI system: menus, HUDs,
overlays, in-game screens, focus/navigation, input routing, rendering,
serialization, templates, editor authoring, and package boundaries.

Game UI is runtime game content. It is not the same system as HoroEditor panels,
tabs, modals, inspectors, or the editor design-system widgets.

```text
HoroEditor UI:
  editor tabs, panels, modals, inspector, asset browser, project browser

Game UI:
  main menu, pause menu, HUD, health bar, ammo counter, inventory panel,
  dialogue box, crosshair, interaction prompt, loading screen
```

The editor may provide authoring tools for game UI, but the authored UI itself
belongs to runtime scene/game content.

## Core Decisions

- Game UI and HUD are runtime content, not editor UI.
- The runtime owns a UI element tree, layout, focus, input routing, rendering,
  serialization, and scene integration.
- HoroEditor authoring tools invoke the same application/runtime UI creation use
  cases as CLI and MCP adapters.
- Game UI input uses the input action system and supports keyboard, mouse, touch,
  and gamepad navigation.
- UI rendering is a declared render pass or overlay pass, not hidden immediate
  rendering from gameplay code.
- UI elements use typed components and stable IDs; serialized UI does not store
  renderer backend handles, live input focus pointers, or editor widget state.
- Templates are presets over core UI elements, not special runtime systems.

## Ownership Boundary

```text
Scene / Game Runtime
  +-- UiWorld
      +-- UiCanvas
      +-- UiScreen
      +-- UiElementTree
      +-- UiLayoutEngine
      +-- UiFocusGraph
      +-- UiInputRouter
      +-- UiRenderExtractor
      +-- UiBindingStore
```

`UiWorld` is owned by the active game or scene runtime. HoroEditor owns only the
authoring session, previews, inspector panels, and editor commands that modify
serialized UI content.

## Core Runtime UI Primitives

The engine core provides these UI primitives without packages:

| Primitive | Purpose |
|---|---|
| Canvas | Root coordinate space for screen-space or world-space UI. |
| Screen | Full-screen route/page under a canvas, such as main menu or pause menu. |
| Panel | Rectangular container with background, padding, and child layout. |
| Frame | Panel variant with optional border, title/header, and content region. |
| Text | Localized text display. |
| Image | Texture/sprite display. |
| Button | Focusable press action. |
| Progress Bar | Bounded scalar value display, such as health or loading progress. |
| Slider | Focusable scalar value editor. |
| Checkbox / Toggle | Boolean control. |
| Input Field | Text input control. |
| Scroll View | Clipped scrollable content container. |
| Layout Group | Horizontal, vertical, grid, or stack layout container. |

Core primitives are intentionally small. Inventory systems, quest trackers,
dialogue systems, minimaps, rich text, and animated widget packs build on top of
these primitives through packages or gameplay modules.

## Canvas And Coordinate Spaces

A `UiCanvas` declares:

- render mode: screen-space overlay, screen-space camera, or world-space
- reference resolution
- scaling policy
- safe-area policy
- DPI/font scale policy
- sorting layer and order
- input scope

```cpp
struct UiCanvasDescriptor {
    UiCanvasId id;
    UiRenderMode renderMode;
    Vec2 referenceResolution;
    UiScaleMode scaleMode;
    UiSafeAreaPolicy safeAreaPolicy;
    UiInputScope inputScope;
};
```

Screen-space UI is resolved after world rendering unless a render graph pass
explicitly composes it earlier. World-space UI produces normal render instances
and participates in visibility and depth policy declared by the canvas.

## Element Tree And Layout

UI content is a retained tree:

```text
Canvas
  +-- Screen
      +-- Frame
          +-- Text
          +-- Button
          +-- ProgressBar
```

Elements own stable authoring IDs. Layout is computed from typed constraints:

- anchors
- margins
- padding
- min/preferred/max size
- alignment
- layout direction
- flex/grid policy
- clipping policy

The layout engine produces an immutable layout snapshot for rendering and hit
testing. Gameplay code does not mutate render quads directly.

## Panel And Frame Contract

`Panel` and `Frame` are core UI building blocks.

```text
Frame
  - background
  - border
  - padding
  - corner radius
  - optional title/header
  - child content area
```

Frames are used for inventory windows, settings pages, dialogue boxes, pause
menus, confirmation dialogs, and other runtime game screens. Editor modals use a
separate HoroEditor modal system and do not share runtime `Frame` state.

## Input, Focus, And Navigation

Game UI consumes normalized input actions:

```text
ui.navigate
ui.submit
ui.cancel
ui.tab_next
ui.tab_previous
ui.scroll
ui.pointer_move
ui.pointer_press
ui.text_input
```

Focus and navigation rules:

- opening a modal-like game screen may block gameplay input according to policy
- focus moves to the screen's declared default element
- gamepad D-pad/left-stick navigation uses the focus graph
- `submit` activates the focused control
- `cancel` closes the current screen or triggers the declared back action
- pointer interaction and keyboard/gamepad focus remain synchronized
- text input is explicit and scoped to focused input fields

A pause menu example:

```text
Pause menu opens
  -> gameplay input is blocked
  -> focus moves to Resume
  -> D-pad navigates controls
  -> South/A submits
  -> East/B cancels
```

High-frequency pointer movement does not travel through data buses. The UI input
router consumes input snapshots during the runtime frame.

## Rendering Contract

Game UI rendering is backend-neutral and extracted into render data:

```cpp
struct UiRenderSnapshot {
    std::span<const UiDrawCommand> commands;
    std::span<const UiTextRun> textRuns;
    std::span<const UiClipRect> clips;
    UiRevision revision;
};
```

The UI renderer supports:

- colored rectangles and borders
- textured images and sprites
- signed-distance-field or atlas-backed text
- clipping and scroll masks
- opacity and simple transitions
- screen-space and world-space canvas projection

UI render data uses the renderer frontend. It does not call backend APIs from UI
components.

## Serialization

Game UI is serialized as project/scene content:

- stable UI element IDs
- element type and typed properties
- hierarchy and layout constraints
- style references
- text localization keys
- image/font/material asset references
- binding declarations
- screen route metadata

It does not serialize:

- live focus object pointers
- transient hover/pressed state
- renderer handles
- editor inspector state
- ImGui widget state

## Data Binding

Runtime UI may bind to gameplay state through declared binding descriptors:

```text
Health Bar Template
  = Panel
    + Text "HP"
    + ProgressBar bound to player.health / player.maxHealth
```

Bindings are explicit, typed, and validated. They must not perform stringly-typed
reflection work in hot rendering paths. Gameplay modules may register binding
providers through the gameplay boundary.

## Templates And Presets

Templates are authoring conveniences over core primitives. They do not add new
runtime element kinds unless explicitly declared by a package.

Core templates may include:

```text
Create > UI > Main Menu Template
Create > UI > Pause Menu Template
Create > UI > Basic HUD Template
Create > UI > Health Bar Template
Create > UI > Dialogue Box Template
Create > UI > Loading Screen Template
Create > UI > Interaction Prompt Template
```

Example:

```text
Main Menu Template
  Canvas
    Screen
      Frame
        Title Text
        Continue Button
        New Game Button
        Settings Button
        Quit Button
```

## Create Menu Integration

The built-in create menu exposes runtime UI separately from scene meshes and
editor panels:

```text
Create
├── Empty Object
├── Camera
├── Light
├── 3D Object
│   ├── Cube
│   ├── Sphere
│   ├── Capsule
│   ├── Cylinder
│   ├── Cone
│   └── Plane
├── Physics
│   ├── Trigger Volume
│   └── Collider
├── Audio
│   └── Audio Source
└── UI
    ├── Canvas
    ├── Screen
    ├── Panel
    ├── Frame
    ├── Text
    ├── Image
    ├── Button
    ├── Progress Bar
    ├── Slider
    ├── Checkbox
    ├── Input Field
    ├── Scroll View
    ├── Main Menu Template
    ├── Pause Menu Template
    ├── HUD Template
    └── Dialogue Box Template
```

The create menu is generated from runtime UI descriptors and scene primitive
descriptors. It must not hardcode divergent object lists in editor, CLI, and MCP
adapters.

## Editor Authoring

HoroEditor provides authoring tools for runtime UI:

- UI hierarchy view
- canvas preview at reference resolutions
- anchor/layout inspector
- style inspector
- font and localization preview
- focus/navigation graph visualization
- template insertion
- binding validation
- play-mode preview

The editor authoring UI uses HoroEditor panel/modal systems, but edited content
remains runtime UI data.

## Core Vs Package Boundary

Core engine provides:

- Canvas, Screen, Panel, Frame
- Text, Image, Button
- Progress Bar, Slider, Checkbox/Toggle, Input Field, Scroll View
- Layout groups, anchors, scaling, safe-area handling
- Focus/navigation and UI input actions
- UI rendering pass
- UI serialization
- basic templates

Packages or future features may provide:

- RPG inventory system
- dialogue system
- quest tracker
- minimap
- rich text and markup
- advanced UI animation/timeline
- WebView
- marketplace widget packs
- domain-specific HUD frameworks

## Metrics And Observability

Game UI exposes bounded metrics:

- element count
- layout time
- text shaping time
- draw command count
- overdraw estimate when supported
- atlas usage
- input focus changes
- binding update count

Metrics follow [Observability Metrics And Profiling](../observability/observability-performance.md).

## Testing

Required tests cover:

- layout determinism across reference resolutions
- safe-area and scaling behavior
- focus graph navigation with keyboard and gamepad
- pointer hit testing and clipping
- submit/cancel routing
- serialization round-trip
- template expansion into core primitives
- binding validation failures
- null/headless UI render extraction
- editor/CLI/MCP create equivalence

## Related Documents

- [UI Canvas Editor UI Reference](./ui-canvas-editor.html): widget palette, hierarchy, anchors, and design-time canvas preview panel.

- [Input Architecture](./input-architecture.md): action maps, gamepad, focus, and
  interaction scopes.
- [Rendering Architecture](./rendering-architecture.md): render extraction,
  frontend/backend boundaries, and pass composition.
- [Scene Runtime](./scene-runtime.md): runtime scene definitions and ECS
  conversion.
- [Built-In Scene Primitives](./built-in-scene-primitives.md): scene object and
  primitive creation catalog.
- [Editor Panel Host](../editor/editor-panel-host.md): HoroEditor panels and tabs
  that are separate from runtime UI.
- [Editor Modal Host](../editor/editor-modal-host.md): HoroEditor modal system,
  separate from game menus.
