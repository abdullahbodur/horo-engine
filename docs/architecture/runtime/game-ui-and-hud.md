# Game UI And HUD Architecture

## Purpose

This document defines Horo Engine's runtime game UI system: menus, HUDs,
overlays, in-game screens, focus/navigation, input routing, rendering,
serialization, templates, editor authoring, and package boundaries.

Game UI is runtime game content. It is not the same system as HoroEditor panels,
tabs, modals, inspectors, or the editor design-system widgets.

[ADR-073](../../adr/073-runtime-ui-ownership-scope-and-update-order.md) is the
single normative owner of RuntimeUiService, game/player/scene/viewport scopes,
instance lifecycle, frame update order, pause/suspension, input/presentation
revisions, unload, compatibility and shutdown.
[ADR-074](../../adr/074-runtime-ui-layout-units-and-measure-arrange.md) owns the
logical unit, box-model, constraint-precedence, measure/arrange, overflow and
deterministic-rounding contract.
[ADR-075](../../adr/075-runtime-ui-font-asset-family-and-fallback.md) owns runtime
font source/face/family identity, deterministic face/fallback selection, cook and
platform-discovery boundaries. This document projects those decisions into the
element, authoring and product model.

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
- One game runtime owns `RuntimeUiService`; every UI instance has exactly one
  game-instance, player, scene, or viewport semantic owner scope.
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
Application / Game Runtime
  +-- RuntimeUiService
      +-- GameInstance scope
      +-- Player scopes
      +-- Scene scopes
      +-- Viewport scopes and attachments
      +-- UiDocument/runtime-instance registry
      +-- UiLayout/interaction snapshots
      +-- UiInputRouter
      +-- UiRenderExtractor
      +-- UiBindingStore
```

Each runtime instance chooses one scope and cannot silently migrate. Game-instance
UI such as loading/main menus survives scene replacement. Player UI survives only
with its generation-checked local player/session. Scene UI ends with its exact
`SceneRuntimeId`. Viewport UI ends with that logical viewport attachment. Attaching
game/player/scene UI to a viewport does not transfer semantic ownership.

UI elements have stable authored `UiElementId` values and transient generation-
checked runtime handles; they are not ordinary ECS entities. HoroEditor owns only
authoring documents, previews, inspector state and revision-checked editor commands.
It never owns or lends widget pointers to a game runtime instance.

## Instance Lifecycle And Frame Order

Documents, cooked data, mutable runtime trees, immutable interaction/layout
snapshots and render snapshots are separate generations. Preparation validates and
resolves every required dependency privately; activation publishes one complete
tree at an owner-thread safe point. Failure retains the prior active/last-good
generation. Scope create/destroy, document replacement and structural transactions
commit only at ADR-073 lifecycle cutoffs, never during layout or rendering.

Runtime UI uses the existing host phases:

1. owner commands commit lifecycle/resource/structural work before simulation;
2. fixed simulation publishes gameplay state but does not update the UI tree;
3. VariableUpdate reads committed binding snapshots, routes input against the
   last successfully presented interaction layout, applies bounded UI-local state,
   advances declared UI time, resolves bindings/layout/focus/hit tests, and
   publishes an immutable generation;
4. RenderExtraction creates per-view `UiRenderSnapshot` values;
5. RenderExecution composes world- and screen-space UI; `RenderGui` remains
   editor/development GUI, not the game UI implementation;
6. successful presentation adopts the next interaction revision, then deferred
   lifecycle retirement releases old generations after frame leases close.

UI actions are typed application/gameplay commands. A button handler cannot write
ECS, renderer, asset or scheduler state directly. Failed or skipped presentation
suppresses interaction for that viewport until a matching layout is presented, so
the player cannot click geometry that was never visible.

## Pause, Suspension And Teardown

Gameplay pause stops fixed simulation, not Runtime UI lifecycle, input, layout or
rendering. UI presentation time is explicitly `PresentationUnscaled`,
`FollowGameplay` or `Manual`; menus/navigation default to unscaled. Pause/resume is
requested through the application pause capability, not owned by a UI element.
Single-step advances one fixed tick followed by one ordinary UI VariableUpdate.

Host suspension skips UI variable/extraction/render work and releases interactive
capture according to Input policy. Resume resets presentation delta and revalidates
attachments without catching up suspended wall time. Focus loss is a separate
product policy and does not automatically pause or destroy UI.

Scene/player/viewport/game teardown closes command and input admission, cancels and
joins preparation/binding work, releases focus/capture, publishes removal snapshots,
waits for render/resource leases, destroys runtime state, then releases assets.
Unrelated scopes survive. Shutdown retires all scopes before Renderer, Assets,
Input, Localization or Platform dependencies disappear and is idempotent after
partial activation.

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

The canvas does not choose semantic owner lifetime. Its containing runtime
instance already names one ADR-073 owner scope, while each viewport attachment
supplies logical extent, DPI, safe area, view identity and presented revision.

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

- `Auto`, logical `Dip`, or same-axis `Percent` preferred lengths;
- margins, border, padding and explicit content-box sizing;
- min/preferred/max size and positive aspect ratio;
- anchors, offsets and alignment;
- flow, flex and grid allocation;
- overflow, clipping and scroll policy.

`Intrinsic` is a revisioned measurement source for `Auto`, not a serialized length
kind. Runtime layout uses signed fixed-point `UiScalar` values at 1/64 DIP. Checked
arithmetic, ties-to-even division and stable authored-order remainder distribution
make logical boxes independent of frame rate, thread schedule and renderer backend.

Constraint precedence is fixed: validate; take a parent-assigned or authored
definite size; otherwise measure intrinsic `Auto`; derive the one remaining auto
axis from aspect ratio; clamp min/max with minimum winning an inverted bound; then
apply parent allocation/alignment and compute boxes/overflow. Property edit or
serialization order never changes the result.

Anchors define a containing segment. Two anchors plus auto stretch, one anchor plus
auto uses intrinsic size, and definite size is aligned within the anchor segment.
Absolute/anchored elements do not contribute to flex/grid flow but do contribute
to post-arrange overflow.

Layout has two logical phases:

```text
Measure(available range)
  -> intrinsic min/preferred/max contributions
Arrange(final content rectangle)
  -> final boxes, overflow, clip chains and interaction geometry
```

An indefinite parent percentage behaves as auto during intrinsic measurement and
may trigger one bounded arrange-time remeasure after the axis becomes definite.
Further change is a typed non-convergence failure. Flow, flex and grid share the
same unit/min/max/aspect precedence; flex freezes items at bounds while
redistributing space, and grid resolves definite, intrinsic, then fractional tracks
with stable remainder order.

Arrangement publishes one complete immutable `UiLayoutSnapshot` for rendering and
hit testing. Required failure publishes nothing and retains the prior last-good
generation according to ADR-073. Gameplay code does not mutate boxes or render
quads directly. Physical pixel snapping is downstream derived render data and
cannot feed back into logical layout, scroll extent or serialized state.

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
router consumes input snapshots during VariableUpdate through one per-player/
viewport `RuntimeUiInputContextId`. It hit-tests the last successfully presented
interaction revision. Split-screen focus/capture is independent unless an explicit
game-instance modal policy blocks multiple contexts.

## Runtime Text And Font Contract

Runtime text/style documents reference stable `FontFamilyAssetId` or typed semantic
font tokens, never filenames, installed-family strings, collection indexes, native
font handles or glyph-atlas slots. A Horo family descriptor owns ordered faces with
weight, stretch, normal/italic/oblique style, variation defaults and explicit
synthetic-style policy.

[ADR-075](../../adr/075-runtime-ui-font-asset-family-and-fallback.md) matches a
requested face deterministically by style compatibility, stretch distance, weight
distance and stable authored order. It expands the primary, family and locale/
script fallback families into one finite acyclic chain and selects the first face
whose immutable coverage supports a complete shaping cluster. Fallback never scans
the operating system or splits a grapheme cluster codepoint by codepoint.

Shipping UI declares `SelfContained` or explicitly capability-gated
`SystemAugmented` font policy. Installed-font discovery is optional Platform
capability data and is never appended as a hidden fallback. Editor preview can show
a discovered face as non-portable, but portable save/cook requires an authorized
tracked font source.

Assets publishes versioned cooked family/face artifacts and dependencies; Runtime
UI owns semantic matching, coverage and logical metrics; later text shaping owns
glyph-run generation; Renderer owns only glyph atlas/raster resource realization.
Font/locale/style/content changes prepare a complete new font/layout generation.
Old face bytes, metrics, shaped runs, layout and atlas resources remain leased by
old interaction/render snapshots until retirement.

Missing required family/artifact fails candidate activation and retains last-good
state. Unsupported later content follows the declared replacement,
omit-with-advance or strict policy with bounded redacted diagnostics; it cannot
perform runtime source I/O, remote download or platform fallback.

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
components. Viewport attachments and snapshots contain only Horo view/extent/DPI/
safe-area/resource identities; native surfaces, swapchains, command buffers and
editor GUI texture IDs remain private to Platform/Renderer adapters.

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
remains runtime UI data. Preview creates an isolated ordinary game runtime and
ADR-073 scopes/attachments. Preview/play mutations apply back only through an
explicit document-revision-checked editor command with undo.

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

- game/player/scene/viewport scope lifetime and unrelated-scope persistence
- lifecycle activation rollback, scene/viewport unload and repeated shutdown
- canonical owner-command/VariableUpdate/layout/extraction/presentation order
- last-presented hit testing across skipped/failed presentation
- gameplay pause, single-step, UI time policy and host suspension/resume
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

- [Runtime Lifecycle](./runtime-lifecycle.md): frame phases, pause, suspension and
  shutdown order specialized by ADR-073.
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
