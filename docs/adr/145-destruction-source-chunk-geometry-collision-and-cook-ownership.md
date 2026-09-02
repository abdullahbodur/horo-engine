# ADR-145: Destruction Source, Chunk Geometry, Collision and Cook Ownership

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: Destruction source mesh and fracture recipe ownership, normalized input, generated chunk/interior geometry, connectivity, solver-neutral collision inputs, deterministic DFR artifact cooking, Assets cache/publication, Physics native shape cooking, runtime loading, replacement, cancellation and compatibility
- **Issue**: [DFR-002.1](https://github.com/abdullahbodur/horo-engine/issues/2004)
- **Jira**: [HORO-1958](https://horo-engine.atlassian.net/browse/HORO-1958)
- **Related**: [ADR-005](005-submodule-compatibility.md), [ADR-008](008-error-model-exception-boundary-and-registry.md), [ADR-010](010-job-waiting-and-operation-store-ownership.md), [ADR-023](023-world-index-and-cell-format-architecture-decision.md), [ADR-027](027-renderer-resource-identity-and-descriptors.md), [ADR-054](054-extension-and-package-authority-boundary.md), [ADR-085](085-physics-shape-authoring-cook-and-runtime-boundary.md), [ADR-087](087-scene-to-physics-ownership-and-conversion.md), [ADR-138](138-terrain-source-cooked-tile-cache-and-streaming-ownership.md), [ADR-144](144-destruction-ownership-authority-state-and-runtime-geometry-boundary.md)
- **Normative documents**: [Destruction and Fracture Architecture](../architecture/runtime/destruction-and-fracture-architecture.md), [Asset Pipeline](../architecture/runtime/asset-pipeline.md), [Physics Architecture](../architecture/runtime/physics-architecture.md), [Scene Runtime](../architecture/runtime/scene-runtime.md), [Rendering Architecture](../architecture/runtime/rendering-architecture.md)

## Context

ADR-144 makes core-1.0 destruction an offline-cooked chunk activation system and
excludes runtime geometry generation. The existing FractureAsset sketch still combines
source mesh references, generated render pieces, hierarchy and implied collision in one
unversioned object. It does not identify who owns interior surfaces, canonical chunk
geometry, convex inputs, native Physics shapes, cache keys or atomic publication.

Destruction and Physics need different derived products. DFR must define stable chunk
semantics and portable geometry/connectivity independent of a solver. Physics must own
solver-specific validation, cooking, native shape construction and lifetime. If DFR
serializes a Jolt shape, or Physics regenerates fracture topology from a render mesh,
upgrading either domain can silently change the other and invalidate saves/network IDs.

Assets is the physical source/cache/package authority, but it cannot become the owner
of fracture semantics merely because it schedules work. Renderer likewise consumes
geometry but does not decide topology, interior material assignment or collision input.

This ADR fixes the representation strata, cook fingerprints and handoff. DFR-002.2
freezes pre-fractured import/naming validation, DFR-002.3 freezes the offline Voronoi
algorithm, and later tickets define connectivity details and artifact schemas.

## Decision

### 1. Source, semantic cook and native realization have separate owners

| Representation or responsibility | Authority |
|---|---|
| Tracked source bytes, `AssetId`, dependency graph, operation scheduling, physical cache/package storage and atomic publication | Assets |
| Normalized source-mesh schema and mesh import evidence | Mesh/Asset import domain |
| Authored fracture recipe, seed, sites, constraints, interior intent and stable local authoring IDs | Fracture asset document |
| Chunk generation, canonical geometry/interior surfaces, topology, hierarchy/support/connectivity, collision-input derivation and DFR artifact schema | Destruction Cook |
| Solver-neutral convex input records associated with stable chunks | Canonical DFR artifact |
| Physics semantic validation, solver/profile/platform cook key, private native payload and runtime shape cache/leases | Physics |
| Render mesh lowering, GPU buffers, acceleration structures and render-resource retirement | Mesh/Render owners |
| Live semantic chunk selection and aggregate activation | DestructionRuntime/RuntimeScene under ADR-144 |

Assets invokes registered cook contributions and publishes their immutable results. It
does not interpret fracture planes, repair topology, assign chunk IDs, cook Jolt bytes or
select a renderer. Destruction Cook does not open paths, own a private disk cache/package
root, activate a runtime world or create native Physics/Render resources.

### 2. Inputs are immutable, normalized and explicitly bound

A fracture cook request captures exact immutable identities/revisions for:

- one normalized source mesh with canonical Horo coordinates, units, winding, vertices,
  indices, submeshes, material slots and source topology diagnostics;
- one authored fracture recipe with stable identity, algorithm, deterministic seed,
  site/plane/constraint data, hierarchy/support policy and finite limits;
- interior surface/material/UV policy and external logical asset dependencies;
- selected DFR feature tier/limits, target envelope and toolchain generation; and
- requested neutral collision-input and render-geometry product schemas.

The source mesh is input, not the fracture artifact's identity and not mutable scratch.
Renderer LODs, GPU buffers and Physics native shapes are not valid source inputs.
Imported pre-fractured pieces pass through the same normalized semantic model after
DFR-002.2 validation; names may assist import mapping but never remain runtime identity.

Missing, stale, malformed, non-finite, open/non-manifold where disallowed, self-
intersecting, degenerate, oversized or inconsistent source/recipe input fails before
publication. A reviewed authoring repair is a versioned explicit operation and changes
the normalized source fingerprint; cook does not improvise silent weld/fill/rename.

### 3. DFR owns one canonical portable fracture artifact

The result is one immutable `CanonicalFractureArtifact` envelope containing:

- artifact/schema/algorithm versions and complete semantic fingerprint;
- source mesh and fracture recipe identities/revisions/digests;
- stable destructible-local chunk identity table and parent/support/connectivity graph;
- canonical chunk boundary and interior vertices/indices, submesh/material-slot mapping,
  normals/tangents/UV data as required by the selected schema;
- local bounds, center-of-mass integration inputs, volume and density/material intent;
- solver-neutral convex collision inputs or deterministic convex-piece descriptors per
  chunk, including stable child/subshape IDs;
- exact finite counts, offsets, alignments, encoded/decoded sizes and integrity digests;
  and
- dependency, tier/limit and compatibility metadata required by consumers.

Chunk identity is derived by the DFR algorithm/schema from canonical semantic inputs and
stable hierarchy/local ordering, never vector position, display name, file path, worker
completion, floating address, renderer submesh or native Physics ID. DFR-001.2 freezes
the exact derivation.

Canonical geometry is Horo-space portable data. The artifact contains no Jolt object or
serialization, graphics API buffer/format, backend handle, process pointer, editor state,
job callback, file path or service locator. It is the membership/topology authority;
consumers never reconstruct the set by enumerating cache files.

### 4. Interior surfaces and material mapping are DFR semantics

Destruction Cook owns which cut faces are interior, their canonical orientation,
winding, smoothing/tangent policy, UV parameterization, material-slot identity and
adjacency to exterior faces. The authored recipe selects only registered finite policies.
Renderer/material systems consume the result and may lower it to target variants; they
cannot merge/drop/reclassify faces or infer an interior slot from names.

Unknown/missing required materials fail cook/package or activation according to the
declared boundary. An explicit optional fallback material is part of the product/recipe
plan and fingerprint. It is not selected silently by Renderer at draw time.

### 5. Collision inputs are neutral; Physics owns native cooking

DFR derives closed finite convex source regions or deterministic convex-piece input for
each dynamic chunk, with canonical vertices/indices, stable subshape/material binding,
bounds, volume/density intent and validation evidence. These records express fracture
semantics but are not Physics runtime shapes.

Physics consumes the exact DFR artifact revision through ADR-085. It independently
validates dynamic-convex eligibility, mass/inertia policy, material/filter mapping,
tolerances and limits, then produces one separately identified
`CookedPhysicsShapeArtifact` set keyed by DFR chunk/revision plus the canonical Physics
solver/profile/platform/toolchain fingerprint. Private Jolt serialization, native shape
construction, sharing/cache and retirement remain entirely Physics-owned.

DFR cannot call native Physics during its semantic cook, publish native shape bytes in
its artifact or claim collision ready. Physics cannot fracture/remesh the source, change
chunk membership, reassign stable IDs or silently replace an invalid chunk with a box,
mesh or recomputed hull. Failure blocks the required product; optional collision absence
must be declared before cook/activation and cannot affect DFR topology.

### 6. Render lowering is also derived and separately owned

Mesh/Render cooking consumes canonical chunk geometry and material slots to produce
target-specific vertex/index layouts, meshlets/LODs where explicitly configured, shader
inputs and package dependencies. RenderRuntime owns GPU realization and retirement.

Changing render LOD/compression/backend does not change canonical chunk identity,
connectivity or collision input. Render cook cannot alter topology to make a draw path
fit. DFR artifact identity may be shared across compatible render targets while each
derived render product has its own complete target fingerprint.

### 7. Fingerprints cover all semantic causes without coupling consumers

The DFR fingerprint includes exact normalized source digest/schema; recipe identity and
canonical fields; algorithm/version/seed; coordinate/tolerance/repair policy; material/
interior/UV rules; hierarchy/support/connectivity policy; requested tier/limits; DFR
artifact schema/encoding; dependency digests; and applicable target-neutral toolchain
identity. Locale, time, path, machine name, job order and consumer-native versions are
excluded.

Physics and Render fingerprints include the accepted DFR artifact identity/digest plus
their own complete solver/backend/product inputs. A Physics solver upgrade invalidates
Physics shape artifacts without changing DFR topology. A DFR semantic change invalidates
all dependent products. Assets records these dependency edges and never guesses reuse
from file timestamps or filenames.

### 8. Cooking is deterministic, bounded and staged

The Assets operation validates declared upper bounds and reserves input, scratch,
candidate, output, queue and publication capacity before starting. Destruction Cook may
parallelize pure bounded units, but canonical chunk/face/edge order, reduction, floating-
point policy, seed expansion and serialization are independent of scheduling. Same
semantic inputs and toolchain fingerprint produce byte-identical DFR artifacts.

The operation state is `Queued`, `Reading`, `Normalizing`, `Validating`, `Fracturing`,
`BuildingInteriors`, `BuildingConnectivity`, `Encoding`, `Verifying`, `Publishing` and a
single terminal `Succeeded`, `Failed` or `Cancelled`. Each stage owns finite progress and
cancellation points. Worker completion returns immutable candidate records; workers do
not publish cache/registry state.

All DFR output and dependent manifests are written under an operation-owned staging
namespace, checksummed and reread/validated. Assets atomically publishes the complete
DFR artifact/manifest only after success. Dependent Physics/Render cooks may then publish
separate products linked to that exact revision. A package requiring all products is
admitted only when the complete dependency closure exists and verifies.

Failure/cancellation leaves the prior published generation current and removes only
candidate-owned state after workers release it. There is no plausible partial artifact,
mixed old/new chunk set or directory enumeration fallback.

### 9. Cache ownership and preview do not create parallel truth

Assets owns immutable content-addressed cache storage, index, eviction and leases.
Destruction Cook owns DFR keys/schema/validation but no private “fracture cache.” Physics
and Render own their semantic derived-product keys while Assets still owns physical
storage/publication. Eviction of a DFR entry waits for dependent/runtime leases and does
not imply a native Physics/GPU object is retired.

Editor preview captures an immutable document/source/recipe revision and runs the same
DFR semantic cook in a transient operation namespace. It may derive preview Physics/
Render products through their normal owners. Preview success does not publish canonical
source/artifacts or clear dirty state; accepting a recipe change is an editor command.
Stale preview completion cannot replace a newer document or published artifact.

### 10. Runtime loads only validated immutable products

RuntimeScene/Destruction resolves the exact DFR artifact plus required Physics/Render
product identities/digests before preparation. It validates envelopes and bounded tables
before allocation, acquires immutable leases, and constructs no source model or cook
service. Required absence/corruption/incompatibility fails candidate activation.

Hot replacement prepares a complete new DFR/dependent-product generation beside the old
one. ADR-144 aggregate publication changes semantic/content bindings only after required
consumers prepare; old leases remain through Scene readers, Physics steps/queries and
Render fences. Runtime never recooks, patches or migrates artifacts in place.

### 11. Compatibility and lifecycle are explicit

DFR artifact schema declares major/minor compatibility and required feature bits.
Unknown required features, unsupported major versions, excessive limits, digest mismatch
or inconsistent graphs fail. Older source recipes migrate in authoring/tooling before
cook; runtime migration does not invent geometry. Unknown optional tables may be skipped
only through length-delimited validated schema rules.

Shutdown closes cook/load admission, cancels task groups, invalidates candidates, drains
publication completions and retains source/artifact/cache/module leases until workers and
runtime consumers acknowledge release. A timeout reports incomplete retirement and
cannot force-delete a staged namespace or unload code/data still in use.

Errors include source/recipe/artifact identity and stage with bounded diagnostics:
`SourceMissing`, `SourceInvalid`, `RecipeInvalid`, `TopologyInvalid`,
`LimitExceeded`, `BudgetDenied`, `NonDeterministicOutput`, `PhysicsInputInvalid`,
`DependencyMissing`, `ArtifactCorrupt`, `IncompatibleVersion`, `StaleRevision`,
`Cancelled`, `PublishFailed` and `RetirementStalled`. Native handles, raw geometry and
unbounded paths are excluded.

## Consequences

- DFR becomes the single authority for canonical fracture geometry, interiors,
  connectivity and solver-neutral collision inputs.
- Physics remains the single authority for solver-specific cooking, native shape cache/
  storage and runtime retirement; Renderer has the equivalent native render boundary.
- Solver/backend upgrades invalidate only their derived products unless DFR semantics
  changed, improving cache reuse without coupling identities.
- Core runtime activation stays bounded and source/cook free as required by ADR-144.
- Assets owns one physical cache/publication graph instead of DFR, Physics and Render
  inventing parallel roots.
- Existing assets containing native collision bytes or name/index-based chunk identity
  require explicit authoring recook/migration.

Required coverage includes malformed/oversized mesh and recipe inputs; stable chunk/
face/connectivity order across worker counts/locales; byte-identical recook; interior and
material golden fixtures; solver-neutral collision validation; proof that DFR artifacts
contain no native types; independent DFR/Physics/Render invalidation; cache hit/miss/
eviction leases; partial publication failure; stale preview; package dependency closure;
runtime corruption/replacement; and cancellation/shutdown at every stage.

## Rejected Alternatives

### Put native Physics shapes inside the DFR artifact

Rejected because solver/platform changes would become fracture topology changes and
native serialization would leak through a portable canonical boundary.

### Let Physics generate chunks or connectivity

Rejected because collision realization would become canonical destruction semantics and
could diverge from render, save and network chunk identity.

### Treat rendered submeshes or imported names as chunk identity

Rejected because LOD/import/backend changes and rename/reorder would alter durable state.

### Give Destruction Cook a private disk cache

Rejected because Assets already owns dependency-aware content-addressed storage,
publication, packaging, eviction and leases. A second cache would split truth.

### Repair bad topology silently during cook

Rejected because output could change across algorithm versions without source revision
or user review. Repair is an explicit versioned authoring operation.

### Cook fracture or collision on runtime activation

Rejected because runtime work would be unbounded, target/toolchain-dependent and unable
to preserve aggregate rollback. Required artifacts are prepared offline.
