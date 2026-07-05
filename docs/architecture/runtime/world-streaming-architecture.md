# World Streaming Architecture

## Purpose

This document defines the world-streaming subsystem for Horo Engine. It covers
spatial partitioning of large worlds, asynchronous cell loading and unloading,
streaming volumes, priority calculation, budget coordination with terrain and
foliage, cell lifecycle (including error and retry), world composition, editor
integration, and networking authority.

## Streaming Model

Horo Engine uses a cell-based world-streaming model:

- The world is divided into a grid of streaming cells
- Each cell contains scene objects, terrain tiles, and foliage clusters
- Foliage clusters are **cell-aligned**: a cluster's bounding box fits within
  a single cell. Clusters that would span cell boundaries are split during
  the foliage bake step, producing a cluster per cell. This ensures no
  cluster is gated on two cells' load decisions.
- Cells are loaded and unloaded based on streaming volumes (camera, gameplay
  relevance, network relevance)
- Streaming operations are asynchronous and cancellable

```cpp
struct StreamingCell {
    StreamingCellId   id;
    WorldCoordinate   origin;          // world-space cell origin
    float             cellSize;        // meters per side
    StreamingCellState state;          // Unloaded, Loading, Loaded, Unloading, Failed
    AssetId           cellAsset;       // baked scene asset for this cell
    StreamingPriority priority;        // cached computed priority, updated each frame
};
```

`priority` is a **cached computed value** updated each frame during volume
evaluation. It is stored in the cell struct to allow the streaming scheduler
to sort the load queue without re-evaluating every volume for every cell
every tick.

## Streaming Volumes

Streaming volumes define which cells should be loaded:

- **Camera volume**: Cells within a radius of the active camera
- **Gameplay volumes**: Pre-defined volumes attached to gameplay triggers
- **Network relevance volumes**: Cells relevant to the local player in
  multiplayer
- **Preload volumes**: Always-loaded cells for critical areas

```cpp
struct StreamingVolume {
    StreamingVolumeType type;
    WorldCoordinate     origin;
    float               radius;
    StreamingPriority   priority;
    bool                unloadOutside;
};
```

`unloadOutside` (bool): When `true`, cells that are inside this volume's
radius must remain loaded, but this volume does **not** prevent a cell from
unloading if all other volumes have also released it. When `false`, the
volume acts as a **pin**: cells within its radius are held loaded regardless
of other volumes' state. This allows "camera" volumes to release cells when
the camera moves away while "preload" volumes permanently pin critical
cells.

The streaming system evaluates all active volumes each frame and computes a
union of required cells. Cells not in the union but currently loaded are
scheduled for unloading after a configurable linger duration.

**Linger duration** is a global setting stored in `StreamingBudget` (not per
volume or per cell):

```cpp
struct StreamingBudget {
    uint32_t  maxConcurrentLoads;
    uint32_t  maxConcurrentUnloads;
    size_t    maxResidentMemoryMB;
    float     maxFrameTimeMS;        // time budget for streaming work per frame
    float     unloadLingerSeconds;   // delay before unloading cells that left all volumes
    size_t    terrainMemoryReservationMB;
    size_t    foliageMemoryReservationMB;
};
```

This prevents rapid load/unload cycles when the camera oscillates near a
cell boundary.

## Cell Lifecycle

```
                 ┌──────────────────────────┐
                 │                          │
                 ▼                          │
Unloaded ──→ Loading ──→ Loaded ──→ Unloading ──→ Unloaded
               │                                    ▲
               │        (max retries)               │
               └──→ Failed ─────────────────────────┘
```

**States**:

- **Unloaded**: No resources allocated
- **Loading**: Scene asset is being read from disk; terrain and foliage data
  are requested from the asset pipeline via async I/O with cancellation
  tokens
- **Loaded**: All objects and components are instantiated in the scene
  runtime. The cell remains in this state until all volumes release it and
  the linger duration expires.
- **Unloading**: Objects are being removed; references are cleaned up. Once
  complete, transitions to Unloaded.
- **Failed**: Loading encountered an unrecoverable error after exhausting
  retries. The cell stays in Failed until explicitly retried (editor
  command, streaming volume re-evaluation after a configurable cooldown).

**Retry policy**:

```cpp
struct StreamingRetryPolicy {
    uint32_t  maxRetries;             // default 3
    float     retryCooldownSeconds;   // delay before re-queuing a Failed cell
    bool      autoRetryOnVolumeChange;// re-attempt when a new volume covers this cell
};
```

When a `Loading` cell fails, the streaming system decrements a retry counter
for that cell. If retries remain, the cell is re-queued after the cooldown.
If retries are exhausted, the cell enters `Failed` state and a diagnostic
is emitted. When `autoRetryOnVolumeChange` is true, a Failed cell is
automatically re-queued (with a fresh retry counter) when a streaming volume
newly covers it.

Transitions are driven by `StreamingRequest` objects queued into the job
system:

```cpp
struct StreamingRequest {
    StreamingCellId  cellId;
    StreamingAction  action;          // Load or Unload
    StreamingPriority priority;
    CancellationToken cancelToken;   // from asset pipeline: job system CancellationToken
};
```

The `cancelToken` is derived from the asset pipeline's async I/O contract:
all asset read operations for this cell share a single token. If the cell
transitions to `Unloading` before the load completes, the token is
cancelled, aborting in-flight I/O and releasing partial resources.

## Priority And Budget

### Priority Calculation

Streaming priority is computed as a weighted sum each frame:

```
priority = (1.0 / (distance + epsilon))
         * volumeTypeMultiplier
         * explicitOverrideMultiplier
         + antiStarvationBoost
```

where:

| Factor | Description |
| ------ | ----------- |
| `distance` | Meters from cell center to nearest active streaming volume origin. Closer cells score higher. `epsilon` prevents division by zero. |
| `volumeTypeMultiplier` | Camera = 1.0, Gameplay = 0.9, Network = 0.8, Preload = 1.2 (preload cells are pinned, but within the preload set, closer cells still load first) |
| `explicitOverrideMultiplier` | Gameplay scripts can set a multiplier (0.5–2.0) on specific cells via `StreamingPriorityOverride` |
| `antiStarvationBoost` | Increases linearly with seconds spent queued as a pending load request. Prevents distant cells from never starting. |

When multiple volumes cover the same cell, the **highest** computed priority
across all covering volumes is used (not an average). This ensures a cell
covered by both a low-priority network volume and a high-priority camera
volume gets the camera priority.

The `StreamingCell.priority` field stores this cached computed value per
frame. The `StreamingVolume.priority` field defines that volume's **base**
contribution weight; it feeds into the formula above, scaled by distance
and the volume type multiplier.

### Streaming Budget

The canonical `StreamingBudget` is defined above in the streaming-volume section.
`terrainMemoryReservationMB` and `foliageMemoryReservationMB` are **sub-
allocations** carved out of `maxResidentMemoryMB`. The terrain and foliage
subsystems each have their own internal caches (`TerrainStreamingBudget`)
that are capped by these reservations. The remaining budget
(`maxResidentMemoryMB - terrainReservation - foliageReservation`) is
available for scene objects and other cell payloads.

This avoids double-counting: the world-streaming system owns the total cap,
and terrain/foliage operate within their allocated slices. If a terrain tile
cache exceeds its reservation, terrain evicts internally without asking the
world-streaming system.

## World Composition

Large worlds are composed from multiple layers:

- **Persistent layer**: Always-loaded core objects (sky, global audio, game
  mode)
- **Streaming layers**: Cell-based world content
- **Dynamic layers**: Runtime-spawned content (projectiles, VFX, temporary
  objects)

Layer ownership determines streaming responsibility:

```cpp
enum class WorldLayerOwnership {
    WorldStreaming,       // managed by the streaming system
    GameplayScript,       // managed by gameplay code
    NetworkReplication,   // managed by the networking layer
};
```

## Networking Integration

In multiplayer, the server owns streaming authority:

- Server determines which cells are relevant to each client
- Server sends cell load/unload notifications to clients
- Clients stream cells as directed by the server
- Network-relevant objects within cells are replicated normally

**Budget scope in multiplayer**: The `StreamingBudget` is applied **per
peer**:
- The server manages its own world view with a server-side budget (usually
  higher, since servers must keep relevant cells for all connected clients
  resident).
- Each client has its own budget, enforced locally. The server does not
  track per-client memory usage; it only sends load/unload directives.
- Bandwidth for cell state notifications is managed through the networking
  layer's bandwidth budget.

The server may keep all cells loaded (no server-side streaming) in
smaller-scale games, or apply server-side streaming for large open worlds.
This is a deployment configuration, not a code change.

## Editor Integration

Editor streaming tools register through the `EditorPanelHost` system:

- **World Composition View**: Registered as a standalone `EditorTab`
  (`WorldCompositionTab`). Displays layer list, cell grid with
  load-state color coding, and streaming statistics. Subscribes to
  `StreamingCellStateChangedEvent` on the `EditorDataBus`.
- **Streaming Volume Placement**: `StreamingVolume` objects are scene
  entities with a gizmo component. Volume editing uses the standard
  viewport gizmo system — no custom editor panel needed.
- **Cell Baking**: Exposed as an editor command in the Build menu
  (`BakeStreamingCells`). The command iterates cells, exports their
  content to streaming assets, and reports diagnostics.
- **Streaming Preview**: A viewport overlay toggle that color-codes cells by
  load state (green=loaded, yellow=loading, red=unloaded, gray=failed).
  Registered as a `ViewportPanel` overlay.
- **Streaming Budget HUD**: A bottom-dock panel showing real-time budget
  usage, load queue depth, and frame-time impact.

## Related Documents

- [Scene Runtime](./scene-runtime.md): cell loading integrates with the scene
  model
- [Asset Pipeline](./asset-pipeline.md): streaming cell assets, `CancellationToken`,
  and async I/O contract
- [Terrain And Foliage Architecture](./terrain-and-foliage-architecture.md):
  terrain tile cache and foliage cluster streaming; budget sub-allocation
- [Networking Architecture](./networking-architecture.md): server-authoritative
  streaming and per-peer budget scoping
- [Concurrency And Jobs](../foundation/concurrency-and-jobs.md): streaming job
  system and `CancellationToken`
- [Editor Panel Host](../editor/editor-panel-host.md): world composition tab
  registration, `StreamingCellStateChangedEvent` on `EditorDataBus`
- [Editor Document Model](../editor/editor-document-model.md): cell baking as
  editor commands
