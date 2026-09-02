# ADR-152: PCG Spatial Input Snapshot and Node-Library Ownership

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: PCG spatial-provider snapshot ownership, coherent capture, query execution, numerical determinism, staleness, derived caching, built-in node catalog, extension trust, replacement and shutdown
- **Issue**: [PCG-3.1](https://github.com/abdullahbodur/horo-engine/issues/2070)
- **Jira**: [HORO-2024](https://horo-engine.atlassian.net/browse/HORO-2024)
- **Related**: [ADR-008](008-error-model-exception-boundary-and-registry.md), [ADR-010](010-job-waiting-and-operation-store-ownership.md), [ADR-012](012-world-streaming-partition-authority-and-subsystem-boundaries.md), [ADR-018](018-command-registration-permissions-threading-and-packaged-build-policy.md), [ADR-026](026-large-world-precision-and-floating-origin-strategy.md), [ADR-054](054-extension-and-package-authority-boundary.md), [ADR-105](105-navigation-asset-and-scene-ownership-boundary.md), [ADR-107](107-navigation-query-consistency-and-snapshot-ownership.md), [ADR-137](137-terrain-foliage-ownership-data-tier-and-lifecycle.md), [ADR-150](150-pcg-graph-source-cooked-plan-cache-and-runtime-ownership.md), [ADR-151](151-pcg-ownership-authority-tier-and-lifecycle.md)
- **Normative documents**: [Procedural Generation Architecture](../architecture/runtime/procedural-generation-architecture.md), [Scene Runtime](../architecture/runtime/scene-runtime.md), [World Streaming Architecture](../architecture/runtime/world-streaming-architecture.md), [Terrain and Foliage Architecture](../architecture/runtime/terrain-and-foliage-architecture.md), [Extension System](../architecture/extensions/plugin-system.md)

## Context

PCG graphs sample and filter scene geometry, terrain, splines, volumes, masks and other
spatial facts. Those facts are mutable and already have owners. Allowing evaluation
workers to retain live ECS components, terrain arrays, spline containers or provider
query objects would couple correctness to concurrent mutation and could keep logically
evicted state alive without a visible generation fence.

ADR-150 requires a closed immutable evaluation input and ADR-151 makes evaluation pure
with respect to external owners. The missing contract is how several providers publish
one coherent spatial snapshot, who executes spatial queries, how results stay
numerically deterministic, and what happens when provider state changes while an
evaluation runs. The built-in node set and extension boundary are also undefined; an
unbounded callback-style node registry could bypass snapshot, trust, determinism,
budget and module-lifetime rules.

This decision defines the baseline spatial input and node-library ownership. PCG-3.2
owns the concrete snapshot/token schemas, PCG-3.3 owns exact point/attribute schemas,
and PCG-3.4 owns detailed query-kernel algorithms. PCG-7.1 may change which optional
providers/extensions ship in 1.0, but not the authority and lifetime boundaries here.

## Decision

### 1. Source providers retain spatial truth

Each source subsystem remains the sole authority for its mutable state:

| Spatial fact | Authority | PCG receives |
|---|---|---|
| Entity transforms, tags and typed scene geometry bindings | RuntimeScene | Immutable scene-spatial projection |
| Terrain surface, layers, holes and foliage source facts | Terrain/Foliage Runtime | Immutable exact-revision terrain projection/query lease |
| Authored/runtime splines | Owning Scene/spline feature | Canonical immutable curve segments and attributes |
| Volumes, exclusion regions and masks | Owning Scene/gameplay feature | Bounded typed immutable primitives/fields |
| Physics overlap/raycast state when explicitly admitted | Physics | Horo-owned immutable query snapshot capability |
| Navigation coverage when explicitly admitted | Navigation | Exact immutable navigation snapshot capability |
| Cell residency and accepted provider generations | World Streaming | Committed cell/partition coverage fence |

PCG never retains mutable scene component storage, terrain tiles, foliage containers,
spline control-point arrays, Physics worlds/shapes, Navigation topology, native handles
or owner-private indexes. It cannot write back through a snapshot or infer source truth
from an old query result.

Source owners expose narrow capture adapters over Horo types. An adapter may copy
canonical bounded values or lend a move-only immutable read lease that pins an owner-
published root. It never lends a mutable reference. The provider retains physical
allocation/native-resource ownership; the PCG aggregate snapshot owns only copied
values and read leases.

### 2. The host captures one coherent aggregate input root

The application/World coordinator owns multi-provider snapshot capture. A request
declares the exact world/scene/cell scope, required and optional provider capabilities,
expected revisions, spatial bounds, origin epoch, determinism class and complete
input/query limits.

At the declared owner safe point, the coordinator captures an aggregate equivalent to:

```cpp
struct PCGSpatialInputSnapshot {
    PCGSpatialSnapshotId id;
    PCGRuntimeGeneration pcg;
    SceneRuntimeId scene;
    WorldGeneration world;
    PartitionEpoch partition;
    OriginEpoch origin;
    PCGSpatialBounds bounds;
    PCGSpatialCoordinateContract coordinates;
    BoundedArray<PCGProviderSnapshotEntry> providers;
    PCGSpatialCoverage coverage;
    PCGSpatialInputFingerprint fingerprint;
    PCGSpatialCostEnvelope costs;
};
```

Each provider entry names one stable provider-kind/contribution ID, exact owner
generation and semantic revision, covered region/cells, schema/capability revision,
content fingerprint, cost and an immutable copied value block or opaque Horo lease.
The aggregate owns no raw pointer, `std::function`, service locator, native provider
object or mutable span.

Required providers capture at one coordinator epoch or the whole request fails. The
coordinator prevents an interleaving owner publication or verifies the captured
revision tuple before publishing the aggregate. It never combines Scene N with Terrain
N+1 merely because both individually succeeded. Optional absence is represented by a
typed entry and may be used only when the cooked plan declares that exact policy.

### 3. Capture is bounded and causes no implicit residency

Snapshot admission validates requested bounds, cell coverage, provider capabilities,
coordinate/origin compatibility, expected revisions and complete worst-case bytes/work
before calling adapters. Unknown cost is not zero. Every copied block, read lease,
acceleration structure and old/new overlap charge belongs to one owner ledger.

Capture observes only committed Active provider state. It cannot synchronously load an
asset, request a cell, pin unadmitted residency, force a terrain decode, build
Navigation/Physics state or wait for missing data. Missing required coverage returns
typed `SpatialInputUnavailable` with exact missing regions/capabilities and optional
bounded demand hints for World Streaming. It is not an empty surface or a proven
negative query.

Provider adapters write into reserved detached storage. Failure, cancellation,
revision mismatch or overflow releases the candidate entries/leases and publishes no
aggregate. A completed snapshot may delay physical reclamation through its leases, but
does not keep a cell logically resident or prevent a newer provider root from
publishing.

### 4. Snapshot currentness and result applicability are distinct

Every evaluation selects one spatial consistency policy:

| Policy | Contract |
|---|---|
| `ExactSnapshot` | Evaluate the captured immutable root and return a truthful historical candidate. Intended for diagnostics, preview and explicitly revision-pinned bake. |
| `CurrentAtCommit` | Evaluate one root, then accept the candidate only if every relevant provider/coverage/origin/authority revision remains current. Default for runtime generation. |
| `CurrentOrRetry` | Apply `CurrentAtCommit`; on staleness the coordinator may recapture and reevaluate inside explicit attempt/work/deadline bounds. |

Memory validity from a read lease does not imply logical currentness. Cell eviction,
origin rebase, scene replacement, terrain mutation, spline edit, mask change or
provider/capability replacement may make a result stale while its input bytes remain
safe to read.

Each node query records the exact provider entries, regions/cells and revisions that
contributed to positive, negative or partial results. When exact dependency coverage
cannot be proven, it conservatively depends on the whole provider/aggregate generation.
Commit currentness checks those dependencies, not only the graph ID or aggregate
pointer. Missing evidence cannot be reported as “no points.”

### 5. PCG executes queries over immutable capabilities

PCG evaluation owns query admission, deterministic node order, operation-local scratch,
result storage and typed outcomes. Core query kernels operate on canonical immutable
PCG spatial values. When copying a large provider representation would be unreasonable,
the source owner may publish an immutable Horo query capability bound to its exact
snapshot root and module/provider lease.

Such a capability has a finite registered operation set, fixed schemas, declared
threading/concurrency, deterministic class, numeric contract and worst-case costs. It
accepts copied request values and operation-owned output storage. It cannot call back
into arbitrary gameplay, observe the provider's later current root, allocate without a
reservation, issue I/O/residency requests or return native refs.

Immediate tiny queries may execute synchronously inside the evaluation worker when the
capability certifies bounded allocation-free thread-safe reads. Larger/preprocessed
queries use the PCG operation task group and deterministic completion ordering. A
provider worker never publishes a PCG node completion, output candidate or target
mutation; PCG owner logic accepts query results against the captured token.

### 6. Numerical and ordering semantics are canonical

The spatial snapshot records world/unit/axis conventions, large-world cell-local
origin, origin epoch, scalar/quantization profile and boundary/tolerance policy. Inputs
are normalized to finite Horo semantic values before queries. NaN, infinity, invalid
transform, degenerate primitive, overflow or unsupported coordinate conversion returns
a typed failure rather than being silently clamped.

Portable deterministic kernels use a restricted fixed-point/integer and explicitly
quantized operation subset, stable traversal keys and total tie breaks. They never rely
on hardware transcendental results. A transcendental operation is portable only when
the node declares a versioned software reference implementation with golden vectors;
otherwise it is at most profile-deterministic under one exact certified floating-point
fingerprint.
Broadphase/BVH/hash-grid layout, provider-native hit order, pointer/address, thread
schedule and unordered-container iteration never define semantic output order.

All multi-hit/query outputs sort by a versioned key such as quantized distance,
provider kind, stable source identity, primitive/subelement identity and canonical
attribute identity. Equal-distance and boundary inclusion rules are explicit. Random
sampling derives streams from graph/node/scope/sample stable IDs plus the request seed;
query completion order never consumes the RNG stream.

Profile-deterministic capabilities carry their exact profile fingerprint.
Best-effort-only providers/nodes are rejected for stronger plan requirements and are
eligible only for the ADR-151 preview/cosmetic boundary.

### 7. The v1 core node library is PCG-owned and backend-neutral

The required built-in semantic families are:

- **Inputs**: graph inputs, constant values, seed/scope and admitted spatial snapshots;
- **Sampling**: grid, volume, spline and canonical surface/terrain sampling;
- **Queries**: bounds/volume containment, surface projection, distance/nearest and
  admitted mask/layer/attribute lookup;
- **Filters**: density/range, bounds, slope, distance/exclusion and typed attribute
  predicates;
- **Transforms**: transform modifier, surface alignment, deterministic offset/noise and
  attribute construction/remapping;
- **Point operations**: merge, intersect/difference by declared identity policy,
  deterministic sort, deduplicate and bounded selection; and
- **Output intents**: static-mesh placement, foliage placement and typed generic scene
  placement candidates without direct target-owner mutation.

This list fixes semantic families, not every algorithm or product guarantee. Actor/
gameplay spawning, destructive terrain writes, Physics-authoritative queries,
Navigation modification, arbitrary scripting, GPU-only evaluation, hierarchical graph
calls and third-party node execution require their later policy/implementation tickets
and are not silently part of the v1 core.

Core nodes live behind PCG-owned stable node type IDs, versioned input/output/parameter
schemas, determinism classification, capability requirements, dependency declaration,
cost function and pure evaluator function. A display name, editor category or palette
position is not identity.

### 8. One immutable node catalog is captured by cook and runtime

The host composes one candidate `PCGNodeCatalog` from built-in descriptors and any
explicitly enabled trusted contributions. It rejects duplicate IDs, ambiguous versions,
schema/type mismatch, missing dependencies, cycles, unsupported determinism claims,
unbounded cost, undeclared query/output capability or incompatible module ABI before
publishing one immutable catalog generation.

Descriptor validation and catalog publication are inert: no graph compilation,
provider query, asset scan, worker start or global registration occurs. Cook operations
and runtime plans capture the exact catalog and referenced descriptor generations.
Enabling, disabling or replacing a contribution creates a new catalog generation and
invalidates affected cook keys/plans; it never patches descriptors or function tables
used by active work.

Evaluation retains catalog/module/provider leases until every node invocation,
completion and candidate drains. Function pointers are internal implementation detail,
not serialized into cooked plans or exposed through the public API.

### 9. Optional node/provider extensions are trusted host policy

Third-party node/provider execution is not an ambient graph capability. Packages may
contribute inert manifests describing stable identity, versioned fixed schemas,
dependencies, determinism evidence, limits, required snapshot/query/output capabilities
and trust/permission requirements. The Extension Manager verifies package identity,
compatibility and project policy; the host explicitly adapts accepted contributions
into a candidate catalog.

Untrusted project/content data cannot load native code, register callbacks or select a
provider. An unknown node in a graph is a hard cook/load failure, never a request to
discover code online or skip the node. Closed/native SDKs remain behind owner-private
adapters and do not leak native types or allocation across the PCG boundary.

The initial implementation may ship built-in nodes only. A future C ABI or sandboxed
WASM/process provider requires a separate accepted decision covering memory, traps,
timeouts, determinism certification, version negotiation and shutdown. Until then,
plugin node evaluator C++ ABI is internal and trusted, and live unload is unsupported.

### 10. Derived query caches never become spatial truth

PCG may maintain bounded disposable memoization only for pure results keyed by the
complete plan/node/parameter/seed identity, spatial aggregate and exact provider
dependency fingerprints, coordinate/numeric policy, query schema/algorithm version,
determinism profile and output limit. Positive, negative and partial results include
their coverage evidence and pass the same currentness checks as fresh work.

Operation-local memoization retires with the evaluation. A shared runtime query cache,
if enabled by product policy, belongs to PCG Runtime, is charged to its granted budget
and stores immutable values only. It does not pin logical cell residency, publish a
provider revision, replace the Assets cook cache or survive world/service incarnation
without an exact compatible key.

Eviction changes performance only. Cache absence never changes semantics, and a cache
hit follows the same scheduled completion/publication path. Stale, corrupt, oversized
or incompatible entries are discarded rather than repaired or treated as empty/no-hit
spatial truth.

### 11. Replacement, cancellation and shutdown preserve leases

Publishing a new Scene/Terrain/spline/mask/provider root invalidates applicable
`CurrentAtCommit` candidates but does not mutate captured snapshots. New evaluations
capture the new aggregate after its safe-point publication. Old evaluations either
finish against `ExactSnapshot` policy or cancel/drain; they never switch provider roots,
origin epochs, node catalogs or numeric profiles mid-node.

Cancellation closes new query work, signals the evaluation task group, drains provider
query completions, retires operation-local caches/intermediates and then releases the
aggregate snapshot. Provider read leases release only after no worker can reach them.
A source owner may publish logical eviction first, but physical reclamation waits for
acknowledged leases and remains charged.

Shutdown closes capture/query/evaluation admission, invalidates the PCG generation,
cancels and joins operation task groups, drains completion queues, rolls back output
candidates and clears shared memoization. It then releases spatial aggregates,
provider/module/catalog leases and query arenas before unregistering adapters. Provider
owners and node modules remain alive until acknowledgement. A deadline reports typed
incomplete shutdown and never detaches a worker or force-frees reachable storage.

### 12. Contract tests cover spatial and catalog boundaries

Implementation must add deterministic automated coverage for:

- coherent multi-provider capture and rejection of mixed scene/terrain/cell/origin
  revisions;
- copy and immutable-lease adapters proving PCG cannot access mutable provider storage;
- missing/partial coverage, required versus optional providers and zero implicit
  residency/I/O;
- exact/current/retry policies, positive/negative dependency coverage and stale commit
  rejection;
- canonical boundary, tie, ordering, RNG and numerical behavior under changed provider
  index layouts and worker schedules;
- malformed/degenerate/non-finite/oversized queries and bounded result capacity;
- every required core node family and rejection of undeclared target side effects;
- duplicate/unknown/incompatible/unbounded node contributions and atomic catalog
  publication;
- provider/catalog replacement with evaluation in flight and module/read-lease safety;
- operation-local/shared cache hit equivalence, staleness, eviction and negative-result
  coverage; and
- cancellation/shutdown at capture, query, evaluation and completion phases with no
  provider, snapshot, module, catalog, worker or callback surviving its owner.

Tests use deterministic virtual scheduling and fault injection at every capture,
reservation, query, cache, catalog and retirement boundary. They compare canonical
typed outputs and identities, not provider-native hit order, pointers or wall-clock
timing.

## Consequences

### Positive

- PCG never owns or retains mutable scene, terrain, spline, mesh, Physics or Navigation
  storage.
- Evaluations observe one coherent, generation-fenced spatial world and can distinguish
  historical memory validity from current applicability.
- Core node semantics, costs and determinism are reviewable and backend-neutral.
- Optional extensions cannot bypass trust, schema, budget, snapshot or module-lifetime
  policy.
- Query caching is safe to evict and cannot become a second provider truth.

### Negative

- Source subsystems need immutable snapshot/query adapters and explicit lease/cost
  accounting.
- Portable determinism may require canonical query kernels instead of faster
  provider-native result ordering.
- Multi-provider coherent capture and stale-result validation add coordinator work.
- Live node/provider unload remains unsupported until all plans/evaluations drain and a
  separate ABI/lifecycle policy exists.

## Rejected Alternatives

### Let nodes query live subsystems during evaluation

Rejected because workers would observe mixed revisions, retain mutable/native state
and make results depend on concurrent mutation and provider scheduling.

### Copy every provider's complete world into PCG

Rejected because large immutable owner roots can be safely leased and bounded; full
duplication would create excessive memory/latency and tempt PCG to become spatial truth.

### Treat missing coverage as an empty spatial result

Rejected because unavailable data is not proof of absence and could cause destructive
generated-output commits.

### Use provider-native query order as canonical order

Rejected because broadphase/index/platform changes would alter deterministic output and
RNG consumption.

### Allow graphs to register or download unknown nodes on demand

Rejected because content would gain code-loading authority and bypass trust,
compatibility, limits, catalog atomicity and module lifetime.

### Cache by graph/node identity alone

Rejected because spatial revisions, coverage, numeric policy, seed, query algorithm and
output bounds all affect semantic results.
