# Workspace Panel Drag Placement Implementation Plan

> **For Horo:** Execute this plan task-by-task with focused verification after each task.

**Goal:** Replace the fixed left/right/bottom workspace model with a persistent `LayoutNode`/`TabStackNode` panel host that lets users drag any activity-bar icon into valid split/tab drop targets while keeping the icon available in its original activity bar.

**Architecture:** Workspace panel placement and Activity Bar icon placement are separate but related editor-state domains. `WorkspacePanelHost` owns the `LayoutNode` tree and panel/tab placement. `ActivityBarHost` owns visually separated icon groups and ordered insertion slots; groups are not user-facing named entities, but upper/middle/lower regions separated by intentional spacing. Dragging an icon into the workspace changes panel layout placement. Dragging an icon into an Activity Bar group changes only its Activity Bar group/order. Both operations use typed controller commands and publish typed `EditorDataBus` events after successful mutation.

**Tech Stack:** C++20, Dear ImGui drag-and-drop, existing `EditorDataBus`, `EditorWorkspaceController`, `WorkspacePanelRegistry`, and semantic `Theme` tokens.

---

## Non-Negotiable Invariants

1. Every registered Activity Bar icon exists in exactly one visual group and one ordered slot; moving it removes it from the old slot and inserts it into the new slot without deleting the icon.
2. Activity Bar groups are presentation regions separated by spacing, not user-facing named panel groups.
3. Activity Bar icon placement and workspace panel layout placement are independent and can be changed by separate drops.
4. A panel ID is registered once and may be referenced by at most one visible layout node.
5. A tab can exist only in one `TabStackNode` at a time.
6. The viewport is a normal `PanelNode` in the default layout, not a hard-coded special renderer region.
7. A drop preview is transient view state; it never publishes an editor event.
8. A committed workspace move publishes exactly one `WorkspacePanelMovedEvent` after successful layout mutation.
9. A committed Activity Bar reorder publishes exactly one `ActivityBarItemReorderedEvent` after successful slot mutation.
10. Invalid targets, duplicate references, unknown IDs, and invalid insertion indices leave the old state unchanged.
11. Layout loading is versioned, bounded, validated, and can fall back to a default layout without corrupting registered panel state.
12. Panel lifecycle is independent from visibility: moving or hiding a panel does not detach it.
13. ImGui window IDs, drag payload pointers, and pixel coordinates are rendering details, never persisted identity.

---

## Target Core Types

Create a dedicated public header, preferably `include/Horo/Editor/WorkspaceLayout.h`:

```cpp
using LayoutNodeId = std::string;
using PanelId = std::string;
using TabId = std::string;

struct SplitNode
{
    LayoutNodeId id;
    enum class Axis : std::uint8_t { Horizontal, Vertical } axis;
    float ratio = 0.5F;
    float firstMinimumSize = 160.0F;
    float secondMinimumSize = 160.0F;
    std::unique_ptr<struct LayoutNode> first;
    std::unique_ptr<struct LayoutNode> second;
};

struct TabStackNode
{
    LayoutNodeId id;
    std::vector<TabId> tabs;
    std::optional<TabId> activeTab;
    bool collapsed = false;
};

struct PanelNode
{
    LayoutNodeId id;
    PanelId panel;
};

struct LayoutNode
{
    std::variant<SplitNode, TabStackNode, PanelNode> value;
};

struct WorkspaceLayout
{
    std::uint32_t schemaVersion = 1;
    LayoutNode root;
};
```

Use stronger wrappers than raw strings if the project’s existing ID conventions permit it. Do not introduce pointer-based identity. `LayoutNode` must have explicit deep-copy semantics because `SplitNode` owns children through `unique_ptr`.

A drop target is a typed placement request, not a pixel coordinate:

```cpp
struct TabPlacement
{
    LayoutNodeId stack;
    std::optional<std::size_t> index;
};

struct WorkspacePanelMoveRequest
{
    PanelId panel;
    TabPlacement placement;
};
```

The Activity Bar uses a separate persisted ordering model:

```cpp
enum class ActivityBarRail : std::uint8_t { Left, Right, Bottom };

struct ActivityBarSlot
{
    ActivityBarRail rail;
    std::size_t groupIndex;
    std::size_t itemIndex;
};

struct ActivityBarGroup
{
    std::vector<PanelId> items;
};
```

The default presentation may expose three visually separated groups on a rail (upper, middle, lower), but the group identity is an internal slot index, not a user-facing `GroupA/GroupB/GroupC` concept. Groups are separated by layout spacing. A drop between two icons creates an insertion placeholder at that exact slot; a successful drop inserts the icon there and shifts later icons by one position. The placeholder is a small square target rendered in the gap, matching the supplied reference image.

Workspace layout drops and Activity Bar drops must have distinct typed targets:

```cpp
enum class WorkspaceDropKind : std::uint8_t { TabCenter, SplitLeft, SplitRight, SplitTop, SplitBottom };
struct WorkspaceDropTarget { LayoutNodeId node; WorkspaceDropKind kind; };
struct ActivityBarDropTarget { ActivityBarRail rail; std::size_t groupIndex; std::size_t itemIndex; };
```

Dropping into a workspace target changes panel layout only. Dropping into an Activity Bar target changes icon rail/group/order only. If a future UX action intentionally performs both, it must be an explicit compound command rather than an implicit side effect.

---

## Task 1: Establish the layout data model

**Files:**
- Create: `include/Horo/Editor/WorkspaceLayout.h`
- Create: `src/editor/screens/workspace/WorkspaceLayout.cpp`
- Test: `tests/unit/editor/WorkspaceLayoutTests.cpp`

Implement:

- `SplitNode`, `TabStackNode`, `PanelNode`, `LayoutNode`, and `WorkspaceLayout`.
- Deep copy/move operations for the tree.
- Tree traversal by node ID.
- `FindNode(LayoutNodeId)` const/non-const.
- `FindPanel(PanelId)` returning its owning stack/node context.
- Validation for unique node IDs, unique panel references, valid split children, and active tabs belonging to their stack.

Tests must cover copy independence, duplicate IDs, missing child nodes, invalid active tabs, and panel lookup.

Do not add ImGui or DataBus dependencies to this model.

---

## Task 2: Add transactional layout operations

**Files:**
- Modify: `include/Horo/Editor/WorkspaceLayout.h`
- Modify: `src/editor/screens/workspace/WorkspaceLayout.cpp`
- Test: `tests/unit/editor/WorkspaceLayoutTests.cpp`

Implement operations that copy/validate/commit atomically:

- `OpenTab(panelId, TabPlacement)`
- `MoveTab(panelId, TabPlacement)`
- `CloseTab(panelId)`
- `SetActiveTab(stackId, tabId)`
- `SplitStack(stackId, axis, ratio, placementSide)` only when required by the drop target
- `SaveLayout()` value copy
- `ValidateLayout()` structured error list

A move into an occupied stack appends or inserts a tab and activates it. Moving a tab within the same stack adjusts the insertion index after removal. A failed operation must not partially remove the panel.

Use stable error codes, not error-message parsing.

---

## Task 3: Define the default layout as a tree

**Files:**
- Modify: `src/editor/screens/workspace/DefaultWorkspacePanels.cpp`
- Create or modify: `src/editor/screens/workspace/DefaultWorkspaceLayout.cpp`
- Modify: `include/Horo/Editor/DefaultWorkspacePanels.h`
- Modify: `include/Horo/Editor/EditorWorkspaceViewModel.h`
- Test: `tests/unit/editor/DefaultWorkspaceLayoutTests.cpp`

Replace the four independent active-panel strings as the authoritative layout state. Construct a default tree such as:

```text
RootSplit (Vertical)
├── MainRow (Horizontal)
│   ├── LeftColumn (Vertical)
│   │   └── LeftStack: Hierarchy
│   └── CenterRow (Horizontal)
│       ├── DocumentStack: Viewport
│       └── RightStack: Inspector
└── BottomStack: ContentBrowser
```

The exact default split hierarchy may be adjusted to match the current visual layout, but every visible surface must be represented by a node or stack. Keep compatibility accessors only during migration; do not maintain two mutable placement sources of truth.

---

## Task 4: Introduce `WorkspacePanelHost`

**Files:**
- Create: `include/Horo/Editor/WorkspacePanelHost.h`
- Create: `src/editor/screens/workspace/WorkspacePanelHost.cpp`
- Modify: `include/Horo/Editor/WorkspacePanelRegistry.h`
- Modify: `src/editor/screens/workspace/WorkspacePanelRegistry.cpp`
- Test: `tests/unit/editor/WorkspacePanelHostTests.cpp`

Create the host responsible for:

- registered panel lookup;
- layout tree ownership;
- panel attach/detach lifecycle;
- open/move/close/activate operations;
- placement validation against registered panels;
- layout save/load boundaries.

`WorkspacePanelRegistry` remains the registration/lifecycle source. The host owns layout references and must not create arbitrary panels during a drop.

The host should expose narrow operations:

```cpp
Result<void> OpenPanel(PanelId, TabPlacement);
Result<void> MovePanel(PanelId, TabPlacement);
Result<void> ClosePanel(PanelId);
Result<void> SetActiveTab(LayoutNodeId, TabId);
const WorkspaceLayout& Layout() const noexcept;
```

Do not expose ImGui window IDs or raw panel pointers in this API.

---

## Task 5: Replace fixed view-model placement state

**Files:**
- Modify: `include/Horo/Editor/EditorWorkspaceViewModel.h`
- Modify: `include/Horo/Editor/EditorWorkspaceController.h`
- Modify: `src/editor/screens/workspace/EditorWorkspaceController.cpp`
- Modify: `src/editor/screens/workspace/EditorWorkspaceScreen.cpp`
- Test: `tests/unit/editor/EditorWorkspaceControllerTests.cpp`

Route panel movement through typed commands:

```cpp
enum class EditorWorkspaceCommandKind
{
    MovePanel,
    OpenPanel,
    ClosePanel,
    SetActiveTab,
    ResizeSplit,
};
```

The controller must:

1. validate the panel ID and target node;
2. invoke the host transaction;
3. publish `WorkspacePanelMovedEvent` only after commit;
4. publish layout-change events for split ratio changes;
5. leave the tree unchanged on failure.

Remove or deprecate the old `activeLeftPanelId`, `activeRightPanelId`, `activeBottomPanelId`, and `activeDocumentPanelId` once all render paths use the host tree.

---

## Task 6: Add typed DataBus events

**Files:**
- Modify: `include/Horo/Editor/EditorWorkspaceEvents.h`
- Modify: `src/editor/screens/workspace/EditorWorkspaceController.cpp`
- Test: `tests/unit/editor/EditorWorkspaceControllerTests.cpp`

Add:

```cpp
struct WorkspacePanelMovedEvent
{
    static constexpr auto HoroEventTypeName = "WorkspacePanelMovedEvent";
    PanelId panel;
    std::optional<TabPlacement> previous;
    TabPlacement placement;
};

struct WorkspaceLayoutChangedEvent
{
    static constexpr auto HoroEventTypeName = "WorkspaceLayoutChangedEvent";
    WorkspaceLayout layout;
};
```

Preview changes must not publish these events. A valid drop publishes the movement event once, after the layout mutation succeeds.

Test event ordering and no-event-on-failure behavior.

---

## Task 7: Build pure layout geometry from the tree

**Files:**
- Create: `include/Horo/Editor/WorkspaceLayoutGeometry.h`
- Create: `src/editor/screens/workspace/WorkspaceLayoutGeometry.cpp`
- Modify: `src/editor/screens/workspace/EditorWorkspaceView.cpp`
- Test: `tests/unit/editor/WorkspaceLayoutGeometryTests.cpp`

Implement a pure tree-to-rectangle layout pass:

- split nodes divide their input rectangle by clamped ratio;
- minimum sizes are enforced;
- collapsed stacks receive no interactive bounds;
- tab stacks receive one rectangle and an internal tab strip;
- panel nodes receive the stack’s content rectangle;
- impossible small-window layouts collapse safely or report a geometry issue;
- no negative ImGui sizes are emitted.

The existing hard-coded left/right/bottom geometry and duplicated `DrawActivityBar` placement calculations must be removed after this pass.

---

## Task 8: Implement drag sources and tree-aware drop targets

**Files:**
- Modify: `src/editor/screens/workspace/EditorWorkspaceView.cpp`
- Modify: `src/editor/screens/workspace/EditorWorkspaceView.h`
- Modify: `include/Horo/Editor/WorkspacePanelHost.h`

Activity-bar icons become drag sources with a stable payload type such as:

```cpp
inline constexpr char kWorkspacePanelPayloadType[] = "HORO_WORKSPACE_PANEL_ID";
```

Payload contains only the stable panel ID. The icon remains visible in its original activity bar throughout the drag and after the drop.

For every rendered tab stack and split boundary, expose drop targets:

- tab insert before/after an existing tab;
- drop into an existing stack;
- split left/right/top/bottom of a stack or panel node;
- target the center of a stack to append/activate.

The target geometry must be derived from the actual layout tree rectangle, not fixed global coordinates.

---

## Task 9: Render transient drop preview/highlight

**Files:**
- Modify: `src/editor/screens/workspace/EditorWorkspaceView.cpp`
- Modify: `include/Horo/Editor/EditorTheme.h` only if required

While a valid drag payload is active:

- show only valid drop targets;
- highlight the hovered target with a low-alpha semantic accent fill and border;
- show the split direction/insert position clearly;
- do not obscure the existing panel content with an opaque overlay;
- remove all preview state on cancel, invalid payload, or drop completion;
- do not mutate the layout during hover.

Use RAII wrappers or tightly paired ImGui calls for all preview styling and drag/drop scopes.

---

## Task 10: Implement tab-stack behavior after drops

**Files:**
- Modify: `src/editor/screens/workspace/EditorWorkspaceView.cpp`
- Modify: `src/editor/screens/workspace/WorkspacePanelHost.cpp`
- Modify: existing panel rendering helpers
- Test: `tests/unit/editor/WorkspacePanelHostTests.cpp`

Implement deterministic behavior:

- center drop into a stack appends and activates;
- edge drop creates a split node with the required axis and side;
- dropping onto an existing tab insertion marker inserts at that index;
- moving a tab out of its old stack removes empty stacks and collapses redundant split nodes;
- moving the last panel out of a dedicated stack does not destroy the root viewport/document invariant;
- all activity-bar icons remain present.

Do not let individual panels mutate the layout tree.

---

## Task 11: Persist and migrate layout state

**Files:**
- Inspect/modify existing workspace settings serialization used by `EditorSettingsStore`
- Create/modify: workspace layout serialization implementation
- Modify: `include/Horo/Editor/WorkspaceLayout.h`
- Test: `tests/unit/editor/WorkspaceLayoutSerializationTests.cpp`

Persist:

- schema version;
- node IDs and node kinds;
- split axis, ratio, and minimums;
- tab stack order and active tabs;
- unavailable panel IDs as bounded unresolved references.

Load behavior:

- reject malformed trees without mutating the current layout;
- report unsupported schema versions;
- skip unavailable optional panels while preserving recoverable layout state;
- clamp ratios/minimums to current window/UI scale;
- fall back to the versioned default tree when structural validation fails.

Never persist ImGui IDs or raw screen coordinates as identity.

---

## Task 12: Regression and GUI verification

**Files:**
- Modify/create: `tests/unit/editor/*Workspace*Tests.cpp`
- Add GUI tests if the Dear ImGui Test Engine integration is available
- Modify: `docs/architecture/editor/editor-panel-host.md`
- Modify: `docs/architecture/extensions/plugin-system.md` if activity-item placement semantics change

Verify:

1. Drag every built-in activity icon into every valid tree drop target.
2. The source icon remains on its original activity bar.
3. Center drops append to a tab stack and activate the dropped panel.
4. Edge drops create the expected split direction.
5. Dragging a tab out removes empty stacks and collapses redundant splits.
6. Invalid payloads and invalid targets do not mutate layout or publish events.
7. Preview highlighting appears only during valid drag-over and disappears on cancel/release.
8. Viewport/document content remains reachable after all legal moves.
9. Save/reload restores node structure, tab order, and active tabs.
10. Small windows clamp safely without negative geometry or crashes.
11. `cmake --build cmake-build-debug --config Debug --parallel 4` passes.
12. `git diff --check` passes.

---

## Explicit Non-Goals

- OS-level drag-and-drop.
- Floating panels in separate native windows.
- Activity-bar icon reordering.
- Plugin hot-unload during an active drag.
- Unbounded layout-tree depth; enforce a reasonable depth/node count limit.
- Persisting arbitrary ImGui internal state.
