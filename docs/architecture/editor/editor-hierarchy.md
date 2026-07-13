# Editor Hierarchy Tree

## Status

The Hierarchy panel currently owns an in-memory mock tree. It deliberately does
not create scene entities or mutate the project document. This phase validates
the component contract and editor interaction model before scene integration.

The initial mock tree mirrors `editor-workspace.html`:

- Room
  - floor 000 (`Mesh`)
  - wall north (`Mesh`)
  - wall south (`Mesh`)
  - player spawn (`Empty`)
- Lighting
  - sun directional (`Light`)
- Cameras
  - main camera (`Camera`)

## Data and Ownership

`HierarchyModel` owns root nodes through `std::unique_ptr`. Every node owns its
complete child subtree. There is no editor-defined maximum depth. A stable
`HierarchyNodeId` identifies a node until deletion; UI state and drag payloads
must never use a vector index as identity.

This ownership model protects the core invariants:

- a node has zero or one parent;
- one node cannot appear in more than one subtree;
- deleting a node destroys its entire subtree;
- reparenting transfers the complete subtree without rebuilding descendants;
- reparenting a node below itself or one of its descendants is rejected before
  mutation;
- selection is cleared when its selected node belongs to a deleted subtree;
- failed rename/reparent/delete requests leave the tree unchanged.

The current recursive traversal has no product depth cap. Extremely deep,
malicious input can still exhaust the CPU call stack; the future serialized
scene loader must validate hostile input separately from the editor's semantic
model.

## Presentation Contract

The panel is a single Hierarchy tab. The tree content contains:

1. a full-width search input;
2. rows with 14 px indentation per depth;
3. branch guides and expand/collapse chevrons;
4. a selection fill with a 2 px accent edge;
5. right-aligned category pills for `Mesh`, `Empty`, `Light`, and `Camera`;
6. a scrollable root drop area below the visible rows.

Search is ASCII case-insensitive. A match keeps its complete ancestor path
visible and ignores collapsed state while the query is active. Search does not
mutate expansion state.

Type pills are presentation metadata, not runtime RTTI or component ownership.
Collections intentionally have no pill. Colors come from active DesignTokens:
mesh uses success, light warning, camera accent, and empty muted.

## Interaction Contract

- Left click selects a node.
- Clicking a node's chevron toggles its expanded state.
- Right click opens Rename, Move to Root, and Delete actions.
- `F2` renames the selected node.
- `Delete`, or `Cmd+Backspace` on macOS, deletes the selected subtree.
- `Enter` commits inline rename; `Escape` cancels it.
- Dragging onto the center 50% of a row makes the dragged subtree a child.
- Dragging onto the top or bottom 25% of a row moves the subtree to that row's
  parent level.
- Dragging into empty panel space moves the subtree to root level.

Keyboard shortcuts are active only while the Hierarchy child window owns focus.
Drag/drop and delete mutations are deferred until row traversal completes so
render-time pointers cannot be invalidated mid-frame.

Sibling-band drops currently append to the destination parent rather than
providing exact between-sibling ordering. Exact ordering belongs to the future
scene command/undo integration and must not be represented as an index-only UI
mutation.

## Scene Integration Plan

The mock model is a temporary composition root. Scene integration should retain
the visual and mutation contracts while replacing direct panel-owned mutation:

1. Introduce a read-only hierarchy snapshot/adapter sourced from the active
   `SceneDocument`, using authoritative scene object IDs.
2. Convert rename, delete, and reparent into typed editor commands handled by
   the document controller.
3. Make delete and reparent commands undoable transactions. Delete must capture
   the serialized subtree needed for restoration.
4. Validate scene-specific constraints in the controller: protected roots,
   prefab boundaries, locked objects, cross-scene references, and transform
   preservation policy.
5. Decide reparent transform semantics explicitly. The default editor behavior
   should preserve world transform unless the command requests local-transform
   preservation.
6. Publish document-change events and rebuild/incrementally update the snapshot;
   the panel must not mutate scene storage directly.
7. Synchronize hierarchy selection with the viewport and Inspector through the
   authoritative editor selection service.
8. Add exact sibling insertion/reordering with stable order keys or typed
   indices resolved by the document controller.

Public scene APIs should not depend on ImGui, badge colors, search buffers, or
panel-local expansion state. Expansion and search remain presentation state;
identity, ownership, ordering, and mutations belong to the scene/document
layer.

## Performance Assumptions

The visible-row vector is retained as panel scratch storage to avoid a fresh
allocation every frame under ordinary scene sizes. Empty-query flattening is
linear in visible nodes. Filtered traversal is linear in all hierarchy nodes
multiplied by the short query comparison cost.

For very large scenes, add profiling before introducing virtualization or an
incremental search index. The first escalation should be row clipping and a
versioned flattened snapshot, not virtual dispatch per node or heap allocation
per rendered row.

## Regression Coverage

The model tests cover:

- reference mock shape and categories;
- expansion and filtered ancestor visibility;
- validated rename with stable identity;
- subtree deletion and selection cleanup;
- subtree-preserving reparent;
- cycle rejection without mutation;
- moves back to root level.

The headless panel render test executes a complete ImGui frame and protects
window/font/style stack balance. Pixel geometry, context-menu behavior,
keyboard focus, and drag target bands still require a runtime visual/interaction
smoke in addition to automated model and render-stack tests.
