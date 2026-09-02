# Procedural Generation Architecture

## Purpose

This document defines the procedural content generation (PCG) subsystem for
Horo Engine. It covers PCG graphs, node-based generation pipelines, spatial
queries, point-based generation, runtime vs offline generation, and editor
authoring tools.

[ADR-150](../../adr/150-pcg-graph-source-cooked-plan-cache-and-runtime-ownership.md)
is the normative owner of graph source, cooked-plan, cache, evaluation-intermediate,
publication, replacement and destruction boundaries. The structures below describe
domain semantics; they do not authorize runtime source interpretation or direct scene
mutation from graph nodes.

## Authority And Artifact Model

PCG separates four representations with different owners and lifetimes:

1. `PCGGraphSource` is versioned authored intent. Assets owns its durable identity,
   bytes and revision; the PCG document owns only an unsaved working candidate.
2. `CookedPCGPlan` is the one immutable executable representation. PCG Cook lowers a
   closed source/dependency/node-library snapshot while Assets owns scheduling, cache,
   staging and atomic generation publication.
3. Evaluation intermediates are bounded operation-local point sets, attributes and
   scratch. They are disposable and never become source or a global cache.
4. Generated outputs are typed immutable candidates. RuntimeScene, Terrain/Foliage,
   Physics, Render, Navigation or another target subsystem owns committed state after
   an explicit host-coordinated transaction.

Offline bake, live preview and runtime generation evaluate the same validated cooked
plan. Runtime never parses graph source, invokes a compiler, repairs a plan or chooses
a cache entry as active content.

## PCG Model

PCG is expressed as a directed acyclic graph (DAG) of nodes:

```cpp
struct PCGGraph {
    PCGGraphId            id;
    std::string           name;
    std::vector<PCGNode>  nodes;
    std::vector<PCGEdge>  edges;
    PCGGraphInputs        inputs;       // exposed parameters
};
```

Each node reads from input pins and writes to output pins. Data flows through
the graph as point clouds (spatial points with attributes).

### PCG Point

The fundamental data unit is a spatial point with attributes:

```cpp
struct PCGPoint {
    WorldTransform    transform;
    BoundingBox       bounds;
    float             density;          // 0-1, used for filtering
    int32_t           seed;             // per-point deterministic seed
    VariantMap        attributes;       // arbitrary typed attributes
};
```

### Node Types

**Spatial Nodes** generate or transform points in space:

- **Surface Sampler**: Sample points on mesh surfaces
- **Volume Sampler**: Sample points within a volume (box, sphere)
- **Spline Sampler**: Sample points along a spline
- **Grid Sampler**: Regular grid distribution
- **Random Scatter**: Poisson disc, random jitter, stratified sampling

**Filter Nodes** remove or modify points:

- **Density Filter**: Keep points with density above/below threshold
- **Bounds Filter**: Keep points within bounding volume
- **Slope Filter**: Filter by surface slope (for terrain)
- **Distance Filter**: Remove points too close to each other
- **Attribute Filter**: Filter by arbitrary attribute condition

**Transform Nodes** modify point attributes:

- **Transform Modifier**: Apply translation, rotation, scale
- **Random Offset**: Add random variation to transform
- **Align To Surface**: Orient points to match surface normal
- **Attribute Noise**: Add Perlin/Simplex noise to attributes

**Generation Nodes** create actual scene content:

- **Static Mesh Spawner**: Place static mesh instances at points
- **Foliage Spawner**: Place foliage instances at points
- **Actor Spawner**: Spawn gameplay entities at points
- **Decal Spawner**: Apply decals at points

```cpp
struct PCGMeshSpawnerSettings {
    std::vector<PCGMeshEntry> meshes;    // weighted random selection
    Vector2                    scaleRange;
    bool                       alignToSurface;
    uint32_t                   seed;
};

struct PCGMeshEntry {
    AssetId   meshId;
    float     weight;           // selection probability weight
};
```

## PCG Graph Evaluation

Graph evaluation is deterministic given the same cooked plan, seed and immutable
input snapshot:

1. Validate the cooked plan, exact dependencies, capabilities and cost envelope.
2. Capture immutable exposed and spatial inputs plus the deterministic seed domain.
3. Reserve the complete evaluation-intermediate and output-candidate budget.
4. Evaluate precomputed stages and pass operation-owned point data through typed edges.
5. Return typed generated-output candidates with stable provenance.
6. Revalidate and prepare every target owner, then publish all required outputs through
   one aggregate commit or roll back the candidate completely.

Generation nodes do not create scene objects as an evaluation side effect. Partial,
failed, cancelled, stale or over-budget evaluations publish no external state.

```cpp
struct PCGExecutionContext {
    PCGGraphId             graphId;
    uint32_t               seed;
    PCGGraphInputs         inputs;
    CancellationToken      cancelToken;
    PCGGenerationFlags     flags;
};
```

## Runtime vs Offline

PCG can run offline (editor bake) or at runtime:

- **Offline**: All generation is done in the editor and baked into the scene.
  Generated objects become regular static scene objects.
- **Runtime**: Generation runs during gameplay. Generated objects are
  dynamically created and destroyed. Used for dungeon generation, loot
  placement, procedural quest areas.

```cpp
enum class PCGGenerationMode {
    Offline,      // editor-only, baked to scene
    Runtime,      // gameplay-time generation
    Hybrid,        // offline base with runtime detail
};
```

Runtime PCG uses the same graph infrastructure as offline, with additional
constraints (time budget per frame, asynchronous evaluation). Both modes consume an
exact immutable cooked-plan generation. Publishing or evicting a cook-cache entry does
not replace a live plan; old plans and their dependencies remain leased until every
evaluation and generated-output candidate drains.

## Determinism And Reproducibility

PCG is designed for determinism:

- Same graph + same seed + same inputs = same output
- Seeds can be set explicitly or derived from world coordinates for
  coordinate-based determinism
- Random number generation uses a per-node deterministic RNG seeded from the
  execution context
- Determinism enables re-generation (modify graph, re-run, get consistent
  results)
- Non-deterministic inputs (e.g., gameplay state) are explicitly marked

## Integration

PCG integrates with:

- **Scene Runtime**: Generated objects are normal entities with components
- **World Streaming**: Generated content within a streaming cell is scoped
  to that cell's load/unload
- **Terrain**: Terrain height can be an input to PCG graphs
- **Foliage**: PCG can generate foliage placement
- **NavMesh**: Generated walkable surfaces trigger NavMesh re-baking

## Editor Authoring

The PCG editor provides a node-graph editing surface:

- Node palette with categorized node types
- Drag-and-drop node placement and connection
- Visual preview of point clouds at each node output
- Live generation preview in the editor viewport
- Seed randomization for variation exploration
- Bake-to-scene command

## Feature Tiers

| Feature              | `es3`      | `dx11` / `dx12_vulkan` | `high_end` |
| -------------------- | ----------- | ------------- | ------------ |
| PCG offline bake     | Yes         | Yes           | Yes          |
| PCG runtime          | No          | Simple        | Full         |
| Point cloud size     | 16K         | 256K          | 2M           |
| Graph node count     | 32          | 256           | 1K           |
| Live preview         | No          | Yes           | Yes          |
| Hierarchical graphs  | No          | Yes           | Yes          |

## Related Documents

- [PCG Graph Source, Cooked Plan, Cache and Runtime Ownership](../../adr/150-pcg-graph-source-cooked-plan-cache-and-runtime-ownership.md)
- [PCG Graph Editor UI Reference](./pcg-graph-editor.html)

- [Scene Runtime](./scene-runtime.md): generated objects as entities
- [Terrain And Foliage Architecture](./terrain-and-foliage-architecture.md): terrain and foliage as PCG inputs/outputs
- [World Streaming Architecture](./world-streaming-architecture.md): PCG scope per streaming cell
- [Navigation And AI Architecture](./navigation-and-ai-architecture.md): NavMesh re-baking after PCG
- [Editor Document Model](../editor/editor-document-model.md): PCG graph asset editing
