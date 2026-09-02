# ADR-087: Scene-to-Physics Ownership and Conversion

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: Authored Physics component producers, stable body/collider/constraint binding, Physics-owned semantic conversion, scene activation participation, detached world candidates, atomic publication, rollback, reload, unload, errors, limits and qualification
- **Issue**: [PHY-006.1](https://github.com/abdullahbodur/horo-engine/issues/887)
- **Jira**: [HORO-887](https://horo-engine.atlassian.net/browse/HORO-887)
- **Parent**: [PHY-006](https://github.com/abdullahbodur/horo-engine/issues/833)
- **Related**: [ADR-008](008-error-model-exception-boundary-and-registry.md), [ADR-017](017-prefab-role-ownership-and-capability-tiers.md), [ADR-023](023-world-index-and-cell-format-architecture-decision.md), [ADR-061](061-animation-ownership-update-order-and-clock.md), [ADR-084](084-canonical-physics-solver-units-and-tolerances.md), [ADR-085](085-physics-shape-authoring-cook-and-runtime-boundary.md), [ADR-086](086-collision-layer-profile-and-query-channel-policy.md)
- **Normative documents**: [Scene Runtime](../architecture/runtime/scene-runtime.md), [Physics Architecture](../architecture/runtime/physics-architecture.md), [Runtime Lifecycle](../architecture/runtime/runtime-lifecycle.md), [Editor Document Model](../architecture/editor/editor-document-model.md), [Built-In Scene Primitives](../architecture/runtime/built-in-scene-primitives.md)

## Context

Scene Runtime already requires immutable `RuntimeSceneDefinition` input and
transactional activation. Physics Architecture assigns a `PhysicsWorld` to the
scene, ADR-085 defines immutable runtime shape leases and ADR-086 defines project-
stable filtering. The remaining ownership seam is ambiguous: which authored
components request a body, shape binding or constraint, which module converts
them, and how a partially built native world participates in scene rollback.

The implemented scene model currently carries a minimal `TriggerVolumeComponent`
described as awaiting Physics conversion. A future implementation also needs rigid
bodies, collider sets and constraints. If editor conversion creates native objects,
headless/runtime paths diverge and the editor gains solver dependencies. If
`RuntimeScene` interprets every Physics field, its backend-neutral core must depend
on Physics. If Physics discovers live ECS components after scene publication, a
failure can leave visible entities without required collision or orphan handles.

Convenient inference is dangerous. Automatically treating a render mesh as a
collider, adding a static body for a collider with no owner, binding to the nearest
ancestor rigid body or interpreting a trigger name makes hierarchy edits change
simulation silently. Constraints that bind process-local entity handles cannot be
prepared before publication or survive scene replacement.

Activation must also preserve two invariants: observers see either the complete old
scene bundle or the complete new bundle, and no failed candidate consumes a public
scene/world/body identity. Native construction, shape/material/filter resolution,
constraint endpoint validation, budgets and startup hooks can fail, so publication
cannot be the phase that performs them.

This ADR fixes ownership and transaction semantics. PHY-006.2 owns the exact public
component field schemas, PHY-006.3 implements activation/rollback, and later
PHY-006 tickets own play reload, gameplay capability, trigger migration, materials,
animation/ragdoll, terrain/streaming, save/network and package contributions.

## Decision

### 1. Authored components express intent; runtime bindings are separate

The portable scene/prefab component model admits these Physics producers:

- `RigidBodyComponent` requests exactly one Horo runtime body with a stable body
  slot, explicit static/kinematic/dynamic mode and body policy;
- `ColliderComponent` contributes one stable collider slot, exact shape descriptor
  or shape asset, local pose, material bindings and collision profile to one
  explicit body slot;
- `TriggerVolumeComponent` is a legacy/authoring convenience that must migrate or
  normalize to an explicit sensor body plus collider binding before runtime
  activation;
- `PhysicsConstraintComponent` requests exactly one stable constraint between two
  explicit body endpoints or one body and the typed world-anchor endpoint.

The exact serializable fields and migration versions belong to PHY-006.2 and
PHY-006.6, but these producer semantics are normative. Physical-material assets,
render meshes, primitive mesh descriptors, animation components and scene hierarchy
do not independently create Physics objects.

Portable components store Horo IDs and values only. Runtime `BodyHandle`,
`ShapeHandle`, `ConstraintHandle`, native IDs/pointers, filter indices and lease
objects live in a scene-generation-owned `PhysicsSceneBindingTable`, never in the
authored component payload or save source of truth.

### 2. Every relationship is explicit and stable

Body, collider and constraint declarations have nonzero stable component/slot IDs
within the owning scene object/document. A normalized body reference contains an
exact `SceneObjectId` plus body slot ID. A collider never searches ancestors for a
body, and a constraint never binds by display name, traversal order, entity slot or
runtime handle.

An authoring command may add a useful bundle, such as static body + collider or
sensor body + trigger collider, as one undoable transaction. The persisted result
is still explicit. Conversion never recreates omitted companion components.

One `RigidBodyComponent` creates one Horo body. One or more explicit colliders may
contribute to that body's immutable shape binding. When several collider slots form
one compound, offline scene/Physics cook produces or references the exact bounded
compound artifact under ADR-085; runtime activation does not cook source geometry
or assemble an unversioned native compound opportunistically.

Each constraint creates one Horo constraint only after both non-world endpoints
resolve to candidate bodies. Constraints do not create missing bodies. Cross-scene
body endpoints are unsupported in the baseline; streaming/cross-cell ownership
requires the PHY-006.9 seam rather than retaining a foreign runtime handle.

### 3. Component presence does not imply automatic render/Physics coupling

A primitive or imported render mesh is visual data. It gains collision only through
an explicit collider component/shape descriptor and explicit body reference. A
rigid body with no valid resolved collider set fails conversion; Horo does not
install an empty/native placeholder shape. A collider with no body fails rather
than creating an implicit static body.

The selected collider profile resolves through ADR-086 and physical materials
resolve through stable Horo asset/material IDs. Missing values do not fall back to
display-name conventions, renderer submeshes, the project default profile or a
built-in native material.

Trigger/sensor semantics are explicit body/collider participation, not inferred
from `Overlap` filter responses alone. `SimulationPairResponse::Overlap` can apply
to ordinary colliders; `TriggerVolumeComponent` remains a distinct authoring
workflow and event producer.

### 4. Scene Model carries typed data; Physics owns semantic conversion

Ownership is split as follows:

| Responsibility | Owner |
|---|---|
| Editable components, stable authored IDs, incomplete drafts and undo/history | Scene/prefab authoring model |
| Persistence parsing and syntax/schema migration into typed Horo values | Scene Model/editor-neutral persistence service |
| Immutable entity/component handoff and aggregate candidate transaction | RuntimeScene / Scene Runtime coordinator |
| Body graph, transform, shape/filter/material, mode and constraint semantic validation | Physics `PhysicsScenePlanBuilder` |
| Shape/filter/material artifact/lease delivery | Assets and the owning Horo domains |
| Detached native world/body/shape/constraint construction | Physics scene activation participant |
| Atomic scene + Physics publication and old-bundle retirement | Scene Runtime coordinator at the host lifecycle boundary |

Editor code maps a stable document snapshot to typed `RuntimeSceneDefinition`
component payloads and structured source locations. It does not choose Jolt types,
allocate bodies, compile filters or invoke a native cooker. The same Physics-owned
plan builder is used by editor preview, packaged runtime, headless validation and
scene cook/qualification paths.

`RuntimeSceneDefinition` is allowed to carry Physics-authored Horo value payloads
owned by a lower backend-neutral model contract. Core `RuntimeScene` stores or
forwards them but does not interpret their semantics. This preserves the target
direction: Physics may consume Runtime/RuntimeScene contracts; RuntimeScene never
includes solver headers or calls Physics through a service locator.

### 5. Physics is an explicitly injected scene activation participant

`RuntimeSceneService` retains the single public `QueuePreparation` admission path.
The host composition root injects a `PhysicsSceneActivationParticipant` through a
narrow generic scene-participant contract when Physics is part of the product
composition. There is no static registration, feature discovery from components or
second Physics activation API callable by editor panels/gameplay.

The participant receives:

- immutable `RuntimeSceneDefinition` and stable source evidence;
- the candidate's detached entity/object identity map;
- exact project collision-schema, asset-registry and package generations;
- required shape/material artifact leases and Physics capability/profile;
- cancellation, budgets and operation diagnostic context.

If Physics is omitted, a scene with no required Physics producers remains valid. A
scene containing required Physics components/capabilities fails admission with
`PhysicsCapabilityUnavailable`; RuntimeScene does not create a null world or ignore
the components.

[ADR-088](088-physics-determinism-capability-and-support-tiers.md) requires the host
to negotiate the requested determinism tier and exact fingerprint/evidence before
this participant publishes a candidate. Replacement capability mismatch fails the
candidate and preserves the active scene/world under the same rollback invariant.

### 6. Conversion produces a closed immutable Physics scene plan

`PhysicsScenePlanBuilder` performs all fallible semantic conversion before native
world construction. Its immutable result contains canonical-ID-ordered:

- body definitions keyed by authored object/body slot;
- exact cooked body-shape artifacts and stable collider/material/subshape mappings;
- resolved collision profile/layer IDs and expected filter-schema generation;
- body-local collider poses and body world poses in Horo SI/local-cluster space;
- constraint definitions with resolved body-plan indexes and local anchors;
- stable writeback/event/debug mappings to scene objects/component slots;
- required capabilities, limits, fingerprints and bounded diagnostic provenance.

The plan has no source paths, editor selection/history, mutable document pointers,
ECS component-pool pointers, native types or public runtime handles. Canonical
ordering uses stable typed IDs, not authored traversal order.

A plan is valid only for its exact scene definition revision, asset/package/filter/
shape/material generations, Physics profile and local-origin generation. Any stale
input invalidates the candidate before publication.

### 7. Transform and authority conversion is exact and finite

The plan builder resolves the authored hierarchy from a complete stable snapshot
and computes each body pose using Scene Math's right-handed, Y-up SI convention.
Collider and constraint anchors are converted to the referenced body's local frame
with checked finite transforms. Body scale is identity; collider scale follows
ADR-085's validated cook/bake policy.

Non-finite, singular, reflected/unsupported scale, shear, invalid quaternion,
hierarchy cycle or out-of-profile local-cluster pose rejects conversion. Physics
does not drop parent transforms, clamp positions or approximate shear.

Static authored transforms seed static bodies. Kinematic targets flow from scene/
gameplay during PrePhysics. Dynamic results write only to the declared body-owner
runtime transform during post-step publication. A dynamic body cannot have another
system overwrite the same authoritative transform later in the tick.

Collider child objects may contribute fixed body-local shape poses but do not
receive independent dynamic writeback from the shared body. Unsupported moving
hierarchy/compound arrangements fail until a typed articulation/constraint policy
exists.

### 8. Preparation builds a detached, closed Physics world candidate

After a plan succeeds, Physics constructs `PhysicsWorldCandidate` privately. It:

1. reserves world/body/constraint/filter/event/debug capacity and scratch budget;
2. acquires immutable filter-schema, shape, material, asset and package leases;
3. creates a closed native world using the pinned ADR-084 profile;
4. creates shapes/body records in canonical plan order without publishing handles;
5. creates constraints only after all endpoint bodies exist;
6. installs private lookup/writeback/event tables and validates native state;
7. runs candidate-safe startup validation with external callbacks still closed;
8. reports `Ready` with a complete resource/identity/fingerprint manifest.

Workers may build pure plans and prepare solver-independent tables from immutable
inputs. Native world mutation occurs only under the Physics owner-thread/qualified
job policy. No worker touches the active world or publishes a body handle.

The candidate owns all partial native objects and leases through RAII. Failure or
cancellation at any step destroys constraints, bodies, world-native state and
leases in reverse dependency order without touching active Scene/ECS/Physics state.

### 9. Aggregate scene activation is one atomic transaction

The Scene Runtime coordinator owns an aggregate `SceneActivationCandidate` with
detached core ECS storage and every required participant candidate. Before commit
it revalidates cancellation, scene/definition/project/asset/package/filter/origin
generations, capability availability and aggregate old/new overlap budgets.

Commit occurs on the owner thread only at `CommitDeferredLifecycleChanges` before
the next eligible fixed tick. All allocation, loading, native creation, callback
registration preparation and fallible startup work completed earlier. Commit is a
bounded no-fail publication that:

1. assigns the new `SceneRuntimeId` and `PhysicsWorldId` generations;
2. binds candidate body/shape/constraint slots into the new scene's private
   `PhysicsSceneBindingTable`;
3. publishes the complete ECS + resource leases + Physics world bundle;
4. opens fixed-tick/query/event admission for that same generation;
5. moves the prior bundle to retirement only after the new bundle is authoritative.

No observer sees new entities with the old Physics world or new Physics bindings
with old ECS storage. The first Physics tick occurs after publication. Activation
hooks that can reject belong to candidate preparation; post-publication observers
cannot request rollback by throwing or returning failure.

### 10. Failure preserves the prior active scene invariant

When initial load has no active scene, candidate failure leaves the service
unloaded. During replacement/reload, any definition, dependency, plan, budget,
native construction, constraint, startup, cancellation or final revalidation
failure destroys only the candidate and leaves the prior active bundle, scene
identity, structural revision, Physics tick/query admission and handles unchanged.

Failed candidates consume no public scene/world/body/shape/constraint identity or
event sequence. They publish no gameplay contact/trigger events, transform
writeback, DataBus success, editor preview revision or partially resolved binding
table. Diagnostics refer to authored IDs and the failed operation/candidate ID.

Rollback never reconstructs the old world from candidate state because the old
bundle was never mutated. If old/new overlap cannot fit the admitted budget, the
replacement fails before commit; Horo does not destroy the active world early to
make room unless an explicit destructive transition policy is separately approved.

### 11. Runtime structural Physics changes use the same invariants

Later entity/component mutation does not rescan live ECS at arbitrary times. A
typed structural command captures stable scene/object/component IDs, expected
revisions and complete Physics intent, builds a mini-candidate, then commits with
the corresponding ECS batch at a Physics/scene safe point.

Adding a body/collider/constraint cannot publish the ECS component before its
Physics binding succeeds. Removing one first closes new Physics admission, commits
the structural mapping change atomically and retires native resources after steps,
queries, events and debug snapshots drain. A batch is all-or-nothing.

Direct solver-callback mutation, component constructors/destructors that create or
delete bodies and unordered process-bus conversion are forbidden.

### 12. Reload rebuilds by default and preserves only typed state

Scene/document reload builds a new aggregate candidate from the new immutable
definition. The default is a complete new Physics world. Optional state transfer
is a typed policy keyed by stable authored body/constraint slot identity and exact
compatibility evidence; it never copies native memory, IDs, contact caches or
component bytes by name.

Transfer candidates may include validated Horo pose, linear/angular velocity,
sleep/wake state and later constraint state when their owning contracts admit it.
Shape, motion-mode, profile, material, origin, solver/profile or endpoint changes
may reject individual preservation or the complete reload according to the explicit
policy. The active world remains unchanged until the candidate commits.

Editor authoring `SceneDocument`, selection, history and dirty state remain separate
from the preview/play runtime. Stopping play destroys its Physics bundle and never
writes simulated transforms back into the document unless an explicit editor
command captures and applies selected Horo values.

### 13. Identity and lease ownership follows scene generation

The published scene bundle owns `PhysicsWorld`. The world owns body/constraint
maps, native world/filter/listener state, tick/query/event buffers and scene binding
table. Shared immutable shape/material/filter artifacts are held through leases;
ECS components hold generation-checked handles or stable binding slots only.

Every binding carries `SceneRuntimeId`, `PhysicsWorldId`, slot generation and source
`SceneObjectId`/component ID. Stale, cross-scene or candidate-only references fail
before native access. Native callbacks copy bounded evidence into world-owned
buffers and translate to stable bindings after the step under ADR-084.

Scene unload closes structural/body/query/event/debug admission, cancels and joins
candidate work, completes or aborts the owned step, drains snapshots/callbacks,
destroys constraints then bodies/world-native state, releases shared leases and
only then destroys component storage. Shutdown follows the same order and is
idempotent after every partial candidate/publication state.

### 14. Streaming and external producers use explicit future seams

The baseline ADR covers authored scene/prefab components flattened into one scene
definition. Terrain, world-cell Physics Mesh, character controller, ragdoll,
destruction and gameplay-spawned producers do not gain permission to inject native
bodies directly.

They must provide a typed Physics scene contribution/structural candidate through
their owning PHY-006 seam, with stable identity, leases, cancellation, safe-point
commit and rollback equivalent to this ADR. ADR-023 cell providers stage detached
resources and join the scene structural transaction; they cannot mutate an active
world during decode/provider callbacks.

### 15. Compatibility, errors and observability are explicit

Authored Physics component schema, runtime Physics payload, scene-plan schema,
binding-table schema, shape/filter/material generations and private solver adapter
version are separate. Durable migrations preserve authored object/component/slot
identity and report ambiguous legacy inference. Candidate/native state is derived
and rebuilt, never migrated as save authority.

ADR-008 results cover missing/duplicate/stale body/collider/constraint IDs, missing
body/endpoint/shape/profile/material/capability, illegal motion/shape/filter/sensor
combination, hierarchy/transform/scale/origin error, dependency/fingerprint mismatch,
cycle/order/limit/budget, cancellation, native construction, startup, generation
conflict, operation in progress, unload and shutdown.

Diagnostics carry bounded scene definition/revision, project/filter/artifact
generation, object/component/body/collider/constraint IDs, phase and expected/
actual counts. They do not log raw asset geometry, native pointers or unbounded
component graphs.

Metrics include candidate phase/duration/result, body/collider/constraint counts,
shape lease/cache reuse, plan/native bytes, overlap budget, rollback phase, stale
completion rejection, active/retiring generations and unload drain time. Metrics
never use display names or high-cardinality native IDs as dimensions.

### 16. CanonicalV1 preparation limits are bounded

One scene Physics candidate admits at most:

| Item | Maximum |
|---|---:|
| bodies | `262,144` |
| collider slots | `1,048,576` |
| collider slots per body | `64` |
| constraints | `262,144` |
| aggregate plan plus binding-table bytes | `512 MiB` |

ADR-085 shape/artifact limits and ADR-086 filter limits apply independently.
Hosts/projects may lower these values. Raising them requires a qualified profile
with checked arithmetic, preparation latency, native capacity, memory overlap and
unload evidence.

Validation counts before native allocation and accounts incrementally. Active,
candidate and retiring bundles remain charged until their leases truly release.
Steady fixed ticks and callbacks do not allocate for scene conversion or grow
binding tables.

### 17. Qualification is part of the contract

Required coverage includes:

- exact producer truth table: rigid body creates one body, collider contributes to
  an explicit body shape, constraint creates no body and visual components create
  no Physics object;
- rejection of implicit parent body, render-mesh collider, missing body/collider,
  name/index/handle endpoint and runtime default profile/material fallback;
- stable object/component/body/collider/constraint identity across reorder, prefab
  expansion, rename, cook, reload and package path change;
- multiple collider compound artifact mapping, material/subshape/filter bindings
  and exact shape/motion compatibility;
- hierarchy/body-local transform conversion, finite/scale/shear/origin limits and
  static/kinematic/dynamic authority/writeback;
- canonical plan equivalence under different authored input order;
- omitted Physics with an empty scene and required-Physics rejection without a null
  world;
- failure/cancellation after every plan/native body/constraint/startup stage with
  zero public identity, event, transform or active-state change;
- final generation revalidation and active/candidate/retiring budget overlap;
- no-fail atomic bundle publication, first tick after commit and binding-table
  generation correctness;
- structural add/remove batches, reload preservation compatibility and rollback;
- stale workers, query/event/debug leases, unload, project/package close and
  repeated shutdown after every partial lifecycle state;
- maximum and over-limit bodies/colliders/constraints/bytes with checked arithmetic;
- editor preview, packaged runtime and headless plan/activation semantic parity;
- fuzzing typed component graphs, references, transforms, counts and plan tables.

Golden fixtures compare Horo plan/binding semantics and stable IDs, not native Jolt
object creation order or pointer values. Fault injection proves the prior active
scene/world remains queryable and unchanged after every replacement failure before
commit.

## Consequences

Authored intent remains portable and explicit, Physics owns every semantic/native
decision, and RuntimeScene remains solver-neutral while coordinating one complete
transaction. Failed loads and reloads cannot expose half-constructed bodies or
entities, and every runtime handle has one scene/world generation owner.

The cost is a typed Physics payload/plan layer, explicit component bundles and body
references, detached old/new memory overlap and a generic participant seam in scene
activation. Authoring conveniences must materialize complete components instead of
depending on hidden runtime inference.

## Rejected Alternatives

### Let the editor build Physics bodies during document conversion

Rejected because headless/packaged conversion would diverge, editor code would gain
solver/lifecycle ownership and native failures could escape document transactions.

### Publish ECS first and scan it to create Physics objects afterward

Rejected because observers could see entities without required collision and a
failure could not preserve the old aggregate scene invariant.

### Make RuntimeScene interpret Physics component semantics directly

Rejected because the backend-neutral scene core would duplicate Physics rules and
gain an unnecessary dependency on the canonical solver boundary.

### Infer bodies from colliders, hierarchy, primitive or render meshes

Rejected because hierarchy/render/import edits would change simulation silently.
Authoring commands may create explicit bundles, but runtime conversion never
repeats that convenience inference.

### Store runtime handles in authored component values

Rejected because handles are scene/world-generation scoped and cannot survive
serialization, candidate rollback, reload or another process.

### Mutate the active Physics world while preparing a replacement

Rejected because rollback would require reconstructing lost native/contact/query
state and candidate failure could corrupt the currently playable scene.

### Destroy the old scene early to reduce candidate memory overlap

Rejected as the normal policy because it converts an ordinary reload failure into
loss of the active scene. Any destructive transition must be a separate explicit
user/product policy, never an allocation fallback.
