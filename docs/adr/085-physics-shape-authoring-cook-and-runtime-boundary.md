# ADR-085: Physics Shape Authoring, Cook and Runtime Boundary

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: Authored collider descriptors, analytic and asset-derived shape identity, normalized source models, platform/solver/profile-keyed cooked artifacts, primitive/convex/mesh/heightfield/compound policy, deterministic cooking, immutable runtime shape leases, scaling/materials, scene activation/replacement, errors, compatibility, limits, packages, unload, and qualification
- **Issue**: [PHY-002.1](https://github.com/abdullahbodur/horo-engine/issues/846)
- **Jira**: [HORO-846](https://horo-engine.atlassian.net/browse/HORO-846)
- **Parent**: [PHY-002](https://github.com/abdullahbodur/horo-engine/issues/829)
- **Related**: [ADR-005](005-submodule-compatibility.md), [ADR-008](008-error-model-exception-boundary-and-registry.md), [ADR-023](023-world-index-and-cell-format-architecture-decision.md), [ADR-054](054-extension-and-package-authority-boundary.md), [ADR-084](084-canonical-physics-solver-units-and-tolerances.md)
- **Normative documents**: [Physics Architecture](../architecture/runtime/physics-architecture.md), [Asset Pipeline](../architecture/runtime/asset-pipeline.md), [Built-In Scene Primitives](../architecture/runtime/built-in-scene-primitives.md), [Scene Runtime](../architecture/runtime/scene-runtime.md), [World Streaming](../architecture/runtime/world-streaming-architecture.md)

## Context

Physics collision geometry has three different lifecycles. Authoring needs portable,
editable descriptions and stable source references. Cooking needs expensive bounded
validation, mesh processing and target-specific solver preparation. Runtime needs
immutable shapes that can be shared safely by bodies without parsing source assets
or exposing native solver ownership.

Treating those representations as one type would either serialize Jolt-native data
into projects, perform unbounded mesh cooking during scene activation, or retain
mutable editor/source buffers beneath stepping worlds. Treating a renderer mesh as
an implicit collider would also make gameplay collision change with render import,
LOD or backend decisions.

ADR-084 selects one pinned private Jolt CanonicalV1 solver but explicitly keeps
native serialization derived and rebuildable. This decision defines the Horo-owned
semantic envelope around any private solver payload and the exact version key that
prevents a stale artifact from reaching a runtime shape.

## Decision

### 1. Authoring, cook and runtime representations have separate owners

The responsibility split is:

| Representation/responsibility | Owner |
|---|---|
| Portable collider descriptors, stable local IDs and user intent | Scene/prefab/terrain or reusable Physics Shape asset authoring model |
| Source asset identity, byte delivery, dependency graph, cook scheduling/cache/package publication | Assets |
| Collider schema, normalized geometry model, semantic validation/fingerprint and cook algorithm | Physics Shape domain |
| Solver/profile/platform preparation and private payload encoding | Canonical Physics Shape cooker |
| Artifact envelope validation, native immutable shape construction/cache and retirement | Scene-owned Physics runtime plus process/profile shape cache |
| Body/shape binding and transform authority | PhysicsWorld under scene conversion/lifecycle |
| Visualization/debug geometry | Editor/Renderer projection from Horo descriptors/snapshots |

Neither Assets nor Renderer interprets collider semantics. Physics does not open
source paths, manage package/cache roots or publish artifact generations. Editor
does not retain native shapes as document state.

[ADR-087](087-scene-to-physics-ownership-and-conversion.md) defines how explicit
authored body/collider/constraint components become a canonical Physics scene plan,
detached world candidate and atomically published scene-generation binding table.

### 2. Authored collider identity is portable and typed

`ColliderShapeDescriptor` is a closed typed variant. Inline analytic descriptors
use stable `ColliderDescriptorId` within the owning document/object/component.
Reusable or source-derived geometry uses stable `PhysicsShapeAssetId` (`AssetId`)
and exact typed subresource/variant identity.

The baseline descriptor kinds are:

- analytic `Box`, `Sphere`, `Capsule` and `StaticPlane` core primitives;
- `ConvexHull` derived from an explicit source mesh/submesh or authored point set;
- `TriangleMesh` derived from explicit source mesh/submeshes;
- `HeightField` derived from an explicit height sample/hole/material source;
- `Compound` containing bounded child descriptors and local transforms.

`Cylinder`, tapered shapes, convex decomposition and mutable/custom native shapes
are not silently inferred. They require a later registered Horo schema/profile or
an explicit compound/convex asset produced by an admitted authoring operation.

References use IDs, not paths, display names, vector indexes, renderer mesh handles,
Jolt subtypes or pointers. Paths remain authoring diagnostics. Surviving local child
and material-slot IDs do not change when arrays reorder.

### 3. Descriptors use Horo SI and local-shape space

Geometry is authored in ADR-084 SI meters and Scene Math's right-handed Y-up local
space. Body pose is separate; a shape descriptor may contain a finite local child
transform only where the kind/compound schema admits it.

Primitive dimensions are positive finite semantic values:

- Box stores full size or explicitly named half extents, never ambiguous `extent`;
- Sphere stores radius;
- Capsule stores radius and cylindrical half-height, aligned to local `+Y`;
- StaticPlane stores normalized local normal and signed distance;
- HeightField stores sample spacing/scale/origin through typed fields;
- mesh/hull sources store no hidden import-axis or unit multiplier.

Imported geometry normalizes axes, winding and units before the Physics source
model. Physics never guesses centimeters, flips based on renderer API or applies
negative scale to a native shape.

### 4. Scaling is resolved before cook/runtime shape identity

Collider scale is semantic input and participates in the fingerprint. Analytic
scale is folded into validated dimensions. Mesh/hull/heightfield/compound scale is
baked by the Physics cooker under a declared supported policy.

Negative/reflection scale is never passed to runtime native shapes. Import/cook may
bake a declared reflection by transforming vertices, winding, normals/material
faces and nested transforms deterministically; otherwise it rejects. Zero, near-
zero, non-finite or singular scale always rejects.

Non-uniform scale is admitted only when the shape kind/cook profile explicitly
supports a baked result. Runtime body transform scale is identity for Physics; a
scene transform scale change may resolve only an exact compatible artifact that was
already cooked and admitted for that scale. If no such artifact is resident, the
runtime command rejects and requests an authoring/asset recook; it never invokes a
runtime cooker. A resolved artifact uses safe-point replacement, not an in-place
native scale mutation.

### 5. Motion-mode compatibility is semantic policy

CanonicalV1 admits:

| Shape kind | Static | Kinematic | Dynamic |
|---|---|---|---|
| Box, Sphere, Capsule | yes | yes | yes |
| ConvexHull | yes | yes | yes with valid mass properties |
| Compound | yes | yes | yes only when every leaf is admitted dynamic convex geometry |
| TriangleMesh | yes | no | no |
| HeightField | yes | no | no |
| StaticPlane | yes | no | no |

Scene conversion validates the pair before constructing a body. It never makes an
unsupported mesh dynamic, silently computes a convex hull, changes body mode or
replaces collision with a box. Kinematic concave support and deforming/mutable mesh
collision are outside the baseline.

Dynamic shapes must be closed/finite and produce finite positive mass/inertia under
the body mass policy. Center-of-mass/inertia are Horo semantic results or verified
private derived data; native values do not become serialized authoring identity.

### 6. Source normalization is deterministic and bounded

The Physics Shape importer/preprocessor consumes immutable source payloads and emits
a normalized `PhysicsShapeSourceModel` with:

- finite canonical-meter vertices/samples and stable subresource IDs;
- validated integer indices, triangle/material-slot mapping and topology evidence;
- duplicate/degenerate/non-manifold/winding diagnostics;
- explicit hull/decomposition/mesh/heightfield policy and bounds;
- source, importer/schema and dependency fingerprints.

Triangle mesh/hull cooking validates index ranges, checked byte/count arithmetic,
finite positions, minimum edge/area under CanonicalV1 tolerances and declared
winding/two-sided policy. Degenerate triangles are errors by default; a declared
repair profile may remove/weld them only during authoring import and records the
exact algorithm/version/result. Runtime cook never improvises repairs.

Heightfields validate dimensions, sample encoding/range, spacing, hole mask and
material indices. Compounds validate one connected finite child tree, stable child
IDs, acyclic asset references and fully expanded bounds.

### 7. Cook outputs one Horo envelope with a private rebuildable payload

`CookedPhysicsShape` is a Horo-owned versioned envelope containing:

- magic, envelope/schema/cooker algorithm versions and required feature bits;
- source/descriptor semantic fingerprint and stable asset/subresource identity;
- shape kind, bounds, mass-property capability and material-slot table;
- exact `PhysicsShapeCookTargetKey`;
- bounded payload/table offsets, counts, alignments and integrity digests;
- private solver-ready bytes or canonical tables needed to construct the shape.

The target key includes OS/architecture/endianness, CanonicalV1 solver tag+commit,
all Jolt compile/precision/CPU-feature definitions, Horo Physics/shape schemas,
tolerance profile, cook algorithm and applicable platform capability profile. It is
independent of Renderer backend unless a future explicit non-physics dependency is
approved; OpenGL/Metal/Vulkan do not select collision bytes.

The private payload may use pinned Jolt shape serialization internally, but it is
never a public/durable Horo project/save/network format. Any solver/profile mismatch
invalidates and recooks it; Horo does not migrate opaque native bytes across pins.

### 8. Cooking is offline/authoring work, never runtime fallback

Cook runs through the normal Assets operation with immutable captured inputs,
cancellation, bounded writers and target identity. It may use JobSystem for isolated
CPU work but owns no global thread pool, mutable catalog or output path. Completion
posts to the cook owner; workers cannot publish artifacts or touch an active world.

Packaged/runtime compositions contain no lightweight or partial cooker. Arbitrary
runtime scale edits are therefore unsupported unless their exact scaled artifact
was produced ahead of time and included in the cook catalog.

Cook order is deterministic by stable dependency/subresource/child/material IDs.
For the same complete inputs and qualified target, the Horo envelope and semantic
tables are byte-identical; if private upstream encoding is not byte-stable, its
digest remains target-keyed and semantic qualification compares reconstructed Horo
results rather than claiming portable bytes.

Missing/incompatible/failed shape artifacts block scene/cell/package readiness when
collision is required. Runtime never reads source meshes, performs convex hull/
decomposition, downloads data, invokes editor importers or substitutes render LOD
geometry.

### 9. Limits prevent geometry and cook amplification

Canonical limits are profile-owned and enforced on source, normalized, fully
expanded and cooked forms independently. The initial maxima are:

| Resource | Limit |
|---|---:|
| source/normalized vertices per shape | `1,000,000` |
| triangles per TriangleMesh | `2,000,000` |
| output vertices per ConvexHull | `256` |
| Compound direct children | `256` |
| fully expanded Compound leaves | `1024` |
| Compound nesting depth | `8` |
| HeightField samples | `16,777,216` |
| material slots per shape | `4096` |
| cooked payload per shape | `256 MiB` |

Projects/targets may lower limits, not raise them without a new qualified profile.
Incremental checked accounting rejects before allocation/amplification exceeds the
limit. Cook never truncates triangles, children, samples or material mappings.

World/scene activation also budgets total resident shape bytes, shapes and native
construction scratch; a valid individual artifact can still fail admission under
the candidate/world budget without corrupting the active scene.

### 10. Runtime shapes are immutable leased resources

Runtime first validates the complete envelope, target key, digests, offsets/counts,
kind/capability and semantic dependencies. It then constructs one private immutable
native shape and publishes a generation-checked Horo `ShapeHandle`/lease only after
success.

A process/profile cache may share identical immutable native shapes across worlds,
but cache identity includes the full cooked/target/profile digest and ownership is
lease-based. Worlds/bodies hold leases, never owning pointers. A handle remains
world-qualified even if storage is shared; it cannot be used in another world
without explicit resolution.

Construction failure publishes nothing and retains the prior scene/world. In-flight
steps, queries, debug snapshots and bodies pin the old shape generation. Retirement
waits until all leases close; reference-count release is never performed from a
solver callback or while native locks make destruction unsafe.

### 11. Shape replacement is a safe-point body transaction

Editing/reload/collider-scale/material-slot topology change prepares and validates a
complete new artifact/native shape privately. A Physics owner-thread command names
world/body/old shape/expected revisions and an explicit replacement policy.

At the pre-step safe point, replacement validates motion-mode compatibility, local
pose, mass/inertia and filter/material dependencies, installs the new shape,
recomputes or preserves mass properties only under the declared body policy,
invalidates affected contacts/query acceleration and applies an explicit wake or
preserve-sleep decision. It commits completely or retains the old shape.

[ADR-086](086-collision-layer-profile-and-query-channel-policy.md) defines the
stable profile/layer/channel identities and immutable filter-schema generation used
by those filter dependencies; shape artifacts do not embed packed/native masks.

Replacement cannot occur during step/query callback, mutate a shared shape in place
or patch bodies one-by-one across frames as a single logical scene transaction.
Changes that would start interpenetrating use typed policy/diagnostics; Physics does
not teleport bodies invisibly to make them fit.

### 12. Materials and face mapping stay Horo-owned

Descriptors/cooked shapes carry stable `PhysicsMaterialSlotId` values and a finite
face/child/sample-to-slot mapping. A body/collider binding resolves slots to exact
Horo physical-material asset generations before world activation.

The native shape may carry private user-data/table indexes for callbacks, but no
material pointer, path, editor object or gameplay callback crosses the boundary.
Contact events expose stable resolved material IDs/summaries under their own
contract, not Jolt material/subshape types.

Changing a material's scalar behavior may replace a material table generation
without recooking geometry when the fingerprint contract permits. Changing slot
topology or face assignment recooks the shape. Renderer material/submesh assignment
is not collision material authority.

### 13. Built-in primitive descriptors remain source-of-truth conveniences

Core `primitive.collider.box`, sphere, capsule and static plane descriptors map
one-to-one to the analytic Horo shape schema. They are not project assets and do not
receive arbitrary `AssetId`s. Scene/prefab data serializes the versioned descriptor
and parameters; cook embeds the corresponding target-keyed analytic shape record in
the owning scene/collider artifact.

Imported render meshes require an explicit collider descriptor/Physics Shape asset.
Default primitive collider suggestions remain authoring policy, not automatic
runtime generation. Cylinder, cone and quad still have no implicit exact core
collider; authoring must choose an explicit admitted shape.

### 14. World streaming and packages use exact artifact dependencies

Streaming cells list required cooked Physics Shape artifact identities/digests for
critical collision. A cell cannot become active with missing/corrupt/wrong-target
collision; it remains preparing/failed and preserves the last-good world state.
Visual mesh readiness does not imply collider readiness.

Packages use ADR-054 identity, lock, verification and lifecycle. Package-provided
shape assets are ordinary verified Assets contributions. Templates/extensions do
not register arbitrary native Jolt shape factories in CanonicalV1. New schema kinds
or cook strategies need a future typed, trust/capability/versioned extension
decision; unknown required kinds fail rather than loading code from asset data.

Package disable/update/uninstall closes shape/cook admission and drains source,
artifact, world/body/query/debug leases before bytes or code disappear.

### 15. Compatibility and errors are explicit

Authoring shape schema, normalized source schema, cook envelope, cook algorithm,
solver/profile target and runtime Horo handle ABI are separate versions. Source
migrations preserve authored intent/IDs. Older compatible source may recook; newer
required semantics reject. Cooked artifact incompatibility always requests recook,
not best-effort native deserialization.

Results follow ADR-008 with stable errors for missing/mistyped source, invalid kind/
dimensions/scale/topology/winding/index/sample/material, unsupported motion mode,
degenerate/non-manifold geometry, hull/compound/heightfield failure, cycle/depth/
limit/budget, cook cancellation, target/solver/schema/digest mismatch, native
construction, stale world/body/shape/revision, replacement conflict and shutdown.

Diagnostics carry bounded asset/subresource/descriptor/child/material/target/
revision/count evidence. Raw vertices, proprietary asset bytes, native pointers and
unbounded triangle lists are not logged or metric dimensions.

### 16. Lifecycle and shutdown are ordered

Scene/cell preparation captures immutable asset registry, artifact generation,
target/profile, material and scene revisions. Workers never access active worlds.
Owner-thread activation publishes all required shapes/bodies atomically before the
first eligible Physics tick; late completion cannot enter a replacement scene.

Unload closes body/shape/query admission, cancels and joins preparation, drains
step/query/debug/body leases, destroys world-native bodies/constraints, releases
world shape handles and then releases shared cache/artifact/source/package leases.
Process shutdown destroys all worlds before the private shape cache, Jolt types,
Assets or JobSystem disappear. Partial activation and repeated shutdown are
idempotent.

### 17. Verification is part of the contract

Required coverage includes:

- stable descriptor/asset/subresource/child/material identities across rename,
  reorder, import revision and package path changes;
- every analytic kind at scale/tolerance edges, explicit SI/Y-up mapping, negative/
  zero/singular/non-uniform scale policy and no runtime transform scaling;
- source mesh invalid indexes/non-finite values, degenerates, winding, duplicate/
  non-manifold data, repair policy, hull limits and deterministic normalization;
- HeightField dimension/sample/range/hole/material validation and Compound nested
  transforms, cycles, depth/leaves/materials;
- exact static/kinematic/dynamic compatibility matrix and no implicit convex/
  primitive/body-mode substitution;
- target-key difference for platform/arch/solver commit/compile definitions/
  tolerance/schema/cooker algorithm and independence from Renderer backend;
- artifact envelope truncation/offset/alignment/count/digest/version/target fuzzing,
  private native payload mismatch and mandatory recook;
- deterministic cook/cache identity, cancellation, output limits, source isolation
  and no worker publication/active-world access;
- immutable shape cache sharing, world-qualified handles, old/new concurrent leases,
  safe-point replacement, mass/inertia/wake/contact policy and rollback;
- critical streaming readiness, missing/wrong artifact, package update/unload and no
  visual-readiness collision fallback;
- editor/CLI/MCP/headless validation parity, debug extraction and no Renderer/
  Editor/Jolt types in public/scene/cooked semantic contracts;
- scene replacement/unload, pending jobs/queries/debug frames, non-finite/native
  construction failure and shutdown after every partial lifecycle state.

Fuzzing targets authoring/source/cooked envelope parsers and checked geometry
arithmetic. Golden fixtures compare Horo semantic bounds, mass-property capability,
material mapping and query/contact results; they do not make native bytes a public
format.

## Consequences

Projects retain portable editable collider intent, while expensive geometry work is
cached and qualified for an exact solver/platform profile. Runtime worlds receive
immutable lease-safe shapes without source I/O or native leakage. Solver upgrades
invalidate derived artifacts cleanly instead of breaking project/save data.

The boundary requires explicit collider assets/descriptors and target cooks. A
render mesh alone is insufficient, and unsupported/missing collision blocks
required scene/cell readiness rather than producing invisible gameplay drift.

## Rejected Alternatives

### Use renderer mesh assets directly as runtime colliders

Rejected because render LOD/import/backend/material decisions are not authoritative
collision topology, scale, material mapping or readiness.

### Cook hulls/meshes during scene activation

Rejected because work is expensive/unbounded, platform behavior can diverge and
failure would make runtime load latency and collision nondeterministic.

### Store Jolt shapes or binary state in scene/project files

Rejected because a private solver/version/compile profile would become durable
authoring identity and prevent safe upgrades or alternative private implementation.

### Ship one cooked shape blob for every platform/profile

Rejected because native layout/precision/CPU/solver/schema compatibility and
qualification are target-specific even when semantic source is portable.

### Make runtime shapes mutable to update all sharing bodies

Rejected because shared in-flight bodies/queries/steps would observe partial state.
Replacement publishes a new immutable generation at a safe point.

### Automatically convert dynamic triangle meshes to convex hulls

Rejected because it changes gameplay geometry/mass and can lose detail without an
explicit authoring/cook operation, parameters, preview and accepted result.

### Expose a custom native-shape plugin ABI now

Rejected because it would expose Jolt lifetime/callback/serialization semantics and
create the premature multi-backend/native ABI ADR-084 avoids.
