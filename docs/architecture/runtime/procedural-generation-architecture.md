# Procedural Generation Architecture

## Purpose

This document defines the procedural content generation (PCG) subsystem for
Horo Engine. It covers PCG graphs, node-based generation pipelines, spatial
queries, point-based generation, runtime vs offline generation, and editor
authoring tools.

[ADR-151](../../adr/151-pcg-ownership-authority-tier-and-lifecycle.md) is the
normative owner of subsystem authority, execution modes, determinism classes, feature
tiers, headless/null composition and lifecycle. [ADR-150](../../adr/150-pcg-graph-source-cooked-plan-cache-and-runtime-ownership.md)
owns graph source, cooked-plan, cache, evaluation-intermediate, publication,
replacement and destruction boundaries. The structures below describe domain
semantics; they do not authorize runtime source interpretation or direct scene
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

PCG owns plan validation and pure evaluation, not generated feature truth. Product
gameplay/server authority permits semantic runtime requests; a host transaction
coordinator revalidates authority and target revisions, while RuntimeScene,
Terrain/Foliage, Physics, Navigation and Render each prepare and own their committed
state. Successful evaluation alone never grants publication authority.

## Spatial Input And Node Catalog

[ADR-152](../../adr/152-pcg-spatial-input-snapshot-and-node-library-ownership.md)
is the normative owner of spatial capture, query execution, numerical determinism,
core-node catalog and extension policy. RuntimeScene, Terrain/Foliage, spline features,
Physics, Navigation and World Streaming retain their own mutable truth. A host safe
point captures one coherent immutable `PCGSpatialInputSnapshot` containing copied Horo
values and/or exact-generation read leases; PCG never retains their mutable storage or
native objects.

Evaluations select `ExactSnapshot`, `CurrentAtCommit` or bounded `CurrentOrRetry`
consistency. A lease preserves memory but not logical currentness. Positive, negative
and partial queries record exact provider/revision/coverage dependencies, and missing
coverage is an unavailable input rather than an empty result.

The v1 built-in catalog covers typed inputs, bounded sampling/query, filters,
transforms, point operations and output intents. Each node has a stable ID, versioned
schema, determinism class, capability declaration and complete cost function. The host
publishes one immutable catalog generation; unknown nodes and untrusted on-demand code
loading fail explicitly.

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

### Generated Output Ownership

[ADR-153](../../adr/153-pcg-pure-evaluation-commit-and-generated-output-ownership.md)
is the normative owner of candidate provenance, target preparation, aggregate commit,
regeneration, cleanup and ownership transfer. Every output belongs to a stable
generated-set lineage and per-object identity. RuntimeScene, Terrain/Foliage, Physics,
Navigation, Render or the applicable gameplay owner stores exact provenance beside its
committed state and remains authoritative for current existence/ownership.

Regeneration computes create/update/retain/retire/conflict operations against one exact
prior ownership generation, prepares new state beside old state and commits atomically.
Cleanup names the exact lineage, set revision, scope, layer and ownership generation;
it never scans by graph ID, display name, tag, folder, hierarchy, component shape or
spatial bounds. Hand-authored, explicitly adopted and differently owned content cannot
be removed by stale PCG cleanup.

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

The closed execution modes are:

- **Offline bake**: evaluates accepted authoring inputs and returns a detached bake
  candidate for an explicit document/asset transaction.
- **Editor preview**: publishes only into an isolated replaceable preview session.
- **Runtime**: prepares an aggregate candidate that requires product/server authority
  and target-owner commit.
- **Hybrid**: keeps baked base and runtime overlay provenance separate; the overlay
  cannot rewrite baked/source truth.
- **Validation only**: returns diagnostics, costs and readiness without output
  preparation or commit capability.

A headless host may install the real deterministic evaluator without Editor or Render.
`PCGNull` advertises no evaluation capability and completes requests with typed
unavailability; it never returns a fabricated empty success.

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

Plans and requests select one explicit determinism class:

- **Portable deterministic**: canonical output matches across every certified host for
  the declared semantic/numeric policy and is eligible for canonical bake or
  authoritative runtime state.
- **Profile deterministic**: output matches only inside one exact certified platform/
  toolchain/capability profile and is limited to product uses that pin that profile.
- **Best-effort preview**: invariants and bounds hold without output equivalence; it is
  restricted to isolated preview or explicitly cosmetic disposable output.

Determinism class is independent of workload tier. Runtime never silently downgrades a
request when a node, target or numeric policy cannot meet the required class.

## Integration

PCG integrates with:

- **Scene Runtime**: Generated objects are normal entities with components
- **World Streaming**: Generated content within a streaming cell is scoped
  to that cell's load/unload
- **Terrain**: Terrain height can be an input to PCG graphs
- **Foliage**: PCG can generate foliage placement
- **NavMesh**: Generated walkable surfaces trigger NavMesh re-baking

These integrations use immutable spatial snapshots and typed target-owner candidates.
They do not expose live provider storage to PCG or grant nodes authority to mutate the
integrated subsystem.

[ADR-154](../../adr/154-pcg-cross-system-authority-readiness-and-commit-boundary.md)
defines the integration adapter and readiness contract. Adapters depend on both the PCG
and target public APIs and are composed by the host; PCG Core does not depend on target
implementations. Each target prepares private state and returns a transaction/generation/
dependency/cost-scoped receipt. Required owners publish together at one aggregate safe
point or all prepared state rolls back.

Terrain/Foliage retains dataset, type, instance, cook, runtime placement, extraction and
eviction authority. World Streaming owns cell demand/reservation/publication; Scene/
Prefab owns entity expansion/identity; Navigation owns topology; VFX observes only
post-commit facts; Network and Runtime Save capture target-owner canonical state rather
than PCG workers/intermediates. Required unavailable capabilities fail the transaction;
declared optional omission is typed and changes the selected output-plan identity.

## Editor Authoring

The PCG editor provides a node-graph editing surface:

- Node palette with categorized node types
- Drag-and-drop node placement and connection
- Visual preview of point clouds at each node output
- Live generation preview in the editor viewport
- Seed randomization for variation exploration
- Bake-to-scene command

[ADR-155](../../adr/155-pcg-graph-document-preview-bake-and-undo-ownership.md)
defines the editor ownership boundary. A persistent graph document owns working source,
revision, typed commands, semantic history and save/recovery/conflicts; panels own only
presentation. Preview captures one immutable revision and uses the ordinary transient
cook/runtime path in an isolated world. Bake is a separate target-owner aggregate
transaction, and undo/redo restores exact semantic patches without reevaluation.

## Feature Tiers

[ADR-156](../../adr/156-pcg-scale-budgets-trust-and-release-scope.md) fixes three
provider-neutral version-1 operational profiles. They are not renderer APIs, hardware
detection results or authority levels. At composition/cook time they resolve to an
immutable Horo-owned profile containing supported modes, node/output families,
determinism classes and finite graph, point, memory, work, concurrency and overlap
limits.

| Feature | `PCGBaseline` | `PCGStandard` | `PCGHigh` |
|---|---:|---:|---:|
| Offline bake / validation | Yes | Yes | Yes |
| Isolated editor preview | Yes, bounded | Yes | Yes |
| Semantic runtime/headless commit | No | Yes | Yes |
| Nodes / edges per graph | 32 / 64 | 256 / 512 | 1,024 / 2,048 |
| Points in one node output | 16K | 256K | 2M |
| Total materialized point records | 64K | 1M | 8M |
| Concurrent evaluations / admitted queue | 1 / 4 | 4 / 32 | 8 / 64 |
| Total PCG-owned/charged memory | 112 MiB | 1,024 MiB | 4,096 MiB |
| Hierarchical graphs | No | No | No |
| Custom executable node providers | No | No | No |

Unknown cost is not zero. Complete plan/input/intermediate/candidate/target and old/new
overlap costs are reserved before admission. Saturation returns typed backpressure or
yields already admitted work; it never truncates points, drops authoritative requests,
skips nodes, chooses a new seed or commits a partial result.

## Trust, Observability And Release Scope

Graph/cooked/cache/package/spatial/save/network values are untrusted until their schema,
bounds, digest, dependency, cost, capability and semantic invariants pass. Built-in code
trust does not waive input/resource validation. Version 1 executes repository-built,
host-composed node types only; graph content cannot load native libraries, scripts,
WASM, processes or remote providers.

Normal metrics use finite dimensions such as mode, profile, determinism class, phase,
target kind and outcome. Graph/asset/node/object/cell/user IDs, paths, coordinates, seeds
and attribute values are prohibited metric dimensions. Detailed per-operation evidence
requires a bounded capability-gated diagnostic snapshot and follows process
Observability redaction, retention and export policy.

M5/1.0 qualifies the built-in single-DAG CPU path, immutable spatial inputs, offline/
preview/runtime/headless modes, exact target commit and the limits above through
PCG-7.2–PCG-7.6. Hierarchical/subgraph composition (PCG-7.7) and custom executable
providers (PCG-7.8) are post-1.0. They do not gate M5 and are rejected under version-1
plan/profile identities.

## Related Documents

- [PCG Ownership, Authority, Tier and Lifecycle](../../adr/151-pcg-ownership-authority-tier-and-lifecycle.md)
- [PCG Graph Source, Cooked Plan, Cache and Runtime Ownership](../../adr/150-pcg-graph-source-cooked-plan-cache-and-runtime-ownership.md)
- [PCG Spatial Input Snapshot and Node-Library Ownership](../../adr/152-pcg-spatial-input-snapshot-and-node-library-ownership.md)
- [PCG Pure Evaluation, Commit and Generated-Output Ownership](../../adr/153-pcg-pure-evaluation-commit-and-generated-output-ownership.md)
- [PCG Cross-System Authority, Readiness and Commit Boundary](../../adr/154-pcg-cross-system-authority-readiness-and-commit-boundary.md)
- [PCG Graph Document, Preview, Bake and Undo Ownership](../../adr/155-pcg-graph-document-preview-bake-and-undo-ownership.md)
- [PCG Scale Budgets, Trust and Release Scope](../../adr/156-pcg-scale-budgets-trust-and-release-scope.md)
- [PCG Graph Editor UI Reference](./pcg-graph-editor.html)

- [Scene Runtime](./scene-runtime.md): generated objects as entities
- [Terrain And Foliage Architecture](./terrain-and-foliage-architecture.md): terrain and foliage as PCG inputs/outputs
- [World Streaming Architecture](./world-streaming-architecture.md): PCG scope per streaming cell
- [Navigation And AI Architecture](./navigation-and-ai-architecture.md): NavMesh re-baking after PCG
- [Editor Document Model](../editor/editor-document-model.md): PCG graph asset editing
