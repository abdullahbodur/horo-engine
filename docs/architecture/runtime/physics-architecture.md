# Physics Architecture

## Purpose

This document defines physics-world ownership, fixed-step simulation, scene
synchronization, collision events, queries, determinism, threading, and
debugging for Horo Engine.

## Core Decisions

- Each active runtime scene owns or explicitly references one physics world.
- Physics advances only during fixed simulation ticks.
- Physics simulation operates in local cluster coordinates relative to the active floating origin.
- Origin shifts translate spatial proxies without altering velocities, momentum, contact caches, or waking sleeping bodies.
- Runtime components store typed body or shape handles, not owning pointers.
- Transform synchronization has one declared authority per body mode.
- Structural physics changes are deferred to safe points.
- Collision events are bounded tick results, not unrestricted process-bus
  traffic.
- Physics queries use explicit snapshots or world affinity.
- Backend-independent engine contracts do not expose solver implementation
  details.
- [ADR-084](../../adr/084-canonical-physics-solver-units-and-tolerances.md)
  selects pinned Jolt v5.6.0 as the one private initial solver; it does not create
  a runtime multi-solver ABI.
- [ADR-086](../../adr/086-collision-layer-profile-and-query-channel-policy.md)
  separates stable project collision layers, reusable profiles and query channels
  from generation-scoped packed/native filter representations.

## Canonical Solver, Units And Tolerance Profile

CanonicalV1 pins Jolt `v5.6.0` at commit
`e77f175595e64cb44218cc9d9d56fc365ad0e36a`, float precision and a serial Horo-
owned job profile. Jolt types, handles, listeners, locks, allocators, job objects
and binary state stay private to `HoroEngine::Physics`; public/scene/gameplay
contracts expose only Horo values and generation-checked handles.

Physics uses SI units: meters, seconds, kilograms, radians, newtons and derived SI
units. It shares Scene Math's right-handed, `+Y`-up, `-Z`-forward, column-vector
convention without a hidden axis/unit conversion. Default gravity is
`(0, -9.81, 0) m/s²`. Foreign content normalizes at import; Physics never guesses
centimeters or per-body scale.

The ADR-026 local float cluster has a hard half-extent of `8192 m` and a qualified
high-fidelity dynamic-contact radius of `4096 m`. Canonical ordinary dynamic
objects are `0.1..10 m`, static objects `0.1..2000 m`, and speeds `0..500 m/s`.
Out-of-profile data is diagnosed/rejected, not silently clamped.

`PhysicsToleranceProfileId::CanonicalV1` pins collision tolerance `1e-4 m`,
manifold tolerance `1e-3 m`, speculative distance and penetration slop `0.02 m`,
maximum correction `0.2 m`, warm-contact distance `0.01 m`, sleep velocity
`0.03 m/s`, sleep time `0.5 s`, and solver iterations `10/2`. Exact identities,
events, layers and serialization never use these as a generic epsilon.

Initial Horo qualification covers macOS 14+ arm64/x86_64, Windows 11 x86_64 and
Ubuntu 24.04 x86_64, including headless compositions. Android, iOS, WebAssembly,
other architectures and consoles remain unqualified/unsupported for shipped Horo
Physics until their explicit matrix exists. Upstream build support alone is not a
Horo support claim.

Jolt is distributed under MIT with pinned license/notice/SBOM inputs. Solver
upgrades are dedicated compatibility changes reviewing release/API/default drift,
compile definitions, derived-cache invalidation, platform qualification,
performance and rollback. Horo never persists Jolt binary state as project/save
authority.

## Ownership

```text
SceneRuntime
  +-- PhysicsWorld
        +-- Bodies
        +-- Shapes
        +-- Constraints
        +-- Broadphase
        +-- Solver
        +-- Tick Event Buffer
```

The physics world owns bodies, shapes, constraints, broadphase proxies, and
solver state. ECS components hold generation-checked handles.

Scene unload disables physics updates, drains scene-scoped queries/jobs, removes
bindings, and destroys the world before component storage disappears.

## Handles

```cpp
using BodyHandle = Handle<PhysicsBodyTag>;
using ShapeHandle = Handle<PhysicsShapeTag>;
using ConstraintHandle = Handle<PhysicsConstraintTag>;
```

Stale handles return typed errors or empty query results according to the API
contract. They never alias newly created physics objects.

## Shape Authoring, Cook And Runtime Boundary

[ADR-085](../../adr/085-physics-shape-authoring-cook-and-runtime-boundary.md)
separates typed authored collider descriptors, Physics-normalized cook input,
target-keyed Horo cook artifacts and immutable runtime shape leases. Scene and
gameplay data retain stable Horo asset/material/subshape IDs; source paths, native
Jolt types, pointers and serialized solver state do not cross the boundary.

Boxes, spheres, capsules and static planes use analytic descriptors. Convex hulls,
triangle meshes, height fields and compounds use bounded canonical geometry and
stable child/material mappings. Dynamic bodies accept primitives, convex hulls and
convex compounds; triangle meshes, height fields and static planes remain static.
Scale is validated and baked before cook rather than applied to runtime shapes.

Cook identity binds semantic source and dependency digests to Horo shape/cooker
schemas, the canonical tolerance profile, target platform and the exact private
solver build fingerprint. Runtime activation consumes only validated artifacts;
it never imports or cooks source and never silently substitutes a primitive.
Shape replacement builds a candidate lease first and swaps body references only
at the Physics pre-step safe point, retaining old leases until all readers and
frames that can reference them have drained.

## Scene Conversion And Activation Ownership

[ADR-087](../../adr/087-scene-to-physics-ownership-and-conversion.md) makes
authored rigid-body components explicit body producers, collider components
explicit contributors to one named body shape and constraint components explicit
links between named body slots. Colliders, render meshes, primitives and hierarchy
never create implicit bodies; authoring conveniences persist complete component
bundles.

Physics owns `PhysicsScenePlanBuilder`, which consumes immutable typed scene data
and resolves body graphs, transforms, shape/material/filter dependencies,
constraints and stable writeback mappings without editor or native types. The host
injects Physics as a scene activation participant. It builds a closed detached
world candidate; neither workers nor editor code mutate the active world or publish
handles.

`RuntimeSceneService` owns the aggregate candidate and sole activation path.
`CommitDeferredLifecycleChanges` atomically publishes ECS storage, resource leases,
the Physics world and its generation-scoped binding table only after all fallible
work and final generation/budget validation succeed. Replacement failure destroys
only the candidate and leaves the prior scene/world/query state unchanged. The
first Physics tick occurs after the complete bundle is authoritative.

## Character Query Boundary

[ADR-089](../../adr/089-character-controller-ownership-implementation-and-update-order.md)
keeps the Horo Character domain behind the initial `HoroEngine::Physics` target but
does not expose Jolt Character classes or add a replaceable backend ABI. One scene-
owned `CharacterWorld` implements the bounded Horo overlap/support/carry/sweep/
slide/step/ground algorithm against a read-only, tick/world-generation-affine
`CharacterPhysicsQueryContext`.

The private adapter maps Horo capsule queries, filter/material evidence and staged
impulses/presence targets to Jolt narrowphase/body commands. Native collectors do
not run gameplay or mutate bodies. A private adapter/solver replacement must
reproduce Horo golden semantics and requalify determinism/performance.

Character movement stages before the Physics step from committed bodies, admitted
kinematic targets and support point-velocity evidence. Physics then applies staged
commands and steps once. Post-Physics Character work finalizes support/attachment
and publishes the collision root with tick commit; it never performs a second move.

[ADR-090](../../adr/090-character-dynamic-body-visibility-push-and-proxy-policy.md)
separates dynamic query visibility, one-way staged impulses and optional solver
visibility. In `BidirectionalProxy`, Physics privately owns one derived kinematic
capsule proxy per admitted Character. The proxy collides only with selected dynamics,
is not a public/authored body and never becomes transform authority. Physics reduces
post-step proxy contacts into bounded stable Horo reaction evidence; Character may
consume committed evidence on the next tick through its ordinary sweep solver.

One-way impulses and proxy solver impulses are mutually exclusive for a pair. Proxy
creation, target updates, filter/schema generations, contact evidence and retirement
are part of the same scene/world/Character lifecycle and fail closed when the exact
capability or qualified determinism tier is unavailable.

[ADR-092](../../adr/092-character-controller-determinism-and-state-composition.md)
requires every Character checkpoint to bind one exact committed Physics/world
checkpoint, tick, origin, structure and determinism fingerprint. Stable support-body
bindings resolve against that detached Physics candidate during restore. Character
cannot restore independently or persist Jolt bodies, proxy state, manifolds, query
caches or native IDs. Aggregate failure leaves the active Scene/Physics/Character
bundle unchanged.

## Fixed-Step Pipeline

One physics tick:

1. apply deferred body, shape, and constraint changes
2. copy kinematic targets from scene transforms
3. apply forces, impulses, and controller commands
4. run broadphase
5. generate and update contact manifolds
6. solve constraints
7. integrate dynamic bodies
8. write dynamic results to runtime transform state
9. produce bounded collision and trigger events
10. record metrics and debug snapshot

Physics uses the fixed delta provided by
[Runtime Lifecycle](./runtime-lifecycle.md). It does not measure wall time
internally.

## Body Modes

| Mode | Transform authority |
|---|---|
| Static | Scene definition; changes rebuild or update the physics representation |
| Kinematic | Scene/gameplay target drives physics |
| Dynamic | Physics result drives runtime transform |

Conflicting writes are rejected or ordered by an explicit controller contract.
A dynamic body cannot also be silently overwritten by an arbitrary transform
system after the physics step.

## Structural Changes

Creating or removing bodies, changing shapes, and modifying constraints while
the world is stepping are deferred through a physics command buffer.

Commands carry scene and generation identity. Late commands targeting an
unloaded scene or stale entity are discarded with diagnostics.

## Collision And Trigger Events

Tick events include:

- contact began
- contact persisted where requested
- contact ended
- trigger entered
- trigger exited

```cpp
struct PhysicsContactEvent {
    SimulationTick tick;
    EntityId first;
    EntityId second;
    ContactEventKind kind;
    ContactSummary contact;
};
```

Events are stored in a world-owned bounded buffer and consumed during the
declared post-physics phase. They are not individually published to the
process-wide data bus.

If overflow occurs, the world records a metric and emits one diagnostic summary.

## Queries

Supported queries include ray, shape cast, overlap, and point tests.

Queries declare:

- target world and scene generation
- one stable project query channel plus explicit typed selectors where applicable
- whether triggers are included
- maximum result count
- ordering guarantee

Immediate queries execute on the physics owner thread outside a step. Parallel
or asynchronous queries use a read-only broadphase snapshot with documented
staleness.

## Layers And Filtering

[ADR-086](../../adr/086-collision-layer-profile-and-query-channel-policy.md)
defines separate opaque 128-bit `CollisionLayerId`, `CollisionProfileId` and
`PhysicsQueryChannelId` values. They are project-stable identities; display names,
list positions, serialized bitmasks and Jolt object-layer values are not identity.

The committed project collision schema owns one complete symmetric layer-pair
matrix. `Ignore` rejects simulation, `Overlap` detects without solver response and
`Block` admits contact solving. Every complete reusable profile selects exactly one
simulation layer and an explicit `Ignore`/`Overlap`/`Block` response for every
query channel. Simulation and query responses are distinct enum types and cannot
be substituted for one another.

Scene preparation validates exact profile/channel references and acquires an
immutable schema generation. The world deterministically compiles dense indices,
bitsets, broadphase partitions and native adapters as private state. Immediate
queries use the active generation; snapshot queries lease the captured generation.
Unknown IDs, missing/asymmetric entries and unsupported runtime changes fail with
typed diagnostics rather than using the project's authoring default.

Semantic schema changes build complete candidate tables and publish atomically at
the Physics pre-step safe point. Bodies retain typed IDs plus schema generation,
and every debug/event/query projection translates private indices back to Horo IDs.

## Determinism

[ADR-088](../../adr/088-physics-determinism-capability-and-support-tiers.md)
defines four fail-closed capability tiers: `Unspecified`, development-only
`SameMachineDiagnostic`, the 1.0 target `SameBuildSamePlatform`, and future
`CrossPlatformQualified`. The current architecture decision does not itself mark a
shipping tuple qualified; PHY-007.2/.3/.5/.8 ordering, state, replay and evidence
gates must pass first.

Compatibility is an exact versioned fingerprint over shipped participating module
bytes, pinned solver source/defines, compiler/ABI/ISA/FP/job profile, Horo Physics
algorithms/schemas, fixed delta, content/filter/shape/material/package manifests,
initial state and ordered command protocol. Tier 2 requires identical fingerprints
and a qualified platform class. Tier 3 uses separate member fingerprints plus one
explicit pairwise-evidenced compatibility group; enabling Jolt's cross-platform
define locally creates no claim.

Initial state and every mutation use stable Horo identity/order/seed and tick-indexed
command frames. Included checkpoints, body/constraint state, events, writeback,
origin and command results compare exact canonical encodings. Raw callback, native
enumeration and broadphase query order are excluded; an authoritative Horo query
must revalidate/canonicalize/sort results before it can influence simulation.

CanonicalV1 remains serial and uses the normal non-cross-platform build. Parallel
stepping or a future `CrossPlatformDeterministicV1` profile needs a new fingerprint,
performance/support matrix and qualification. Determinism alone does not authorize
rollback or lockstep; complete Horo snapshots, structural resimulation, history,
side-effect and Network authority contracts remain separate.

## Threading

The initial canonical model is one physics owner thread within the fixed tick.
Internal parallelism may be added behind the world interface when:

- dependency order remains deterministic within the declared contract
- component storage is not accessed concurrently without snapshots
- task completion is joined before results are published
- shutdown and scene unload cancel or join all physics tasks

Gameplay code does not retain references into solver-owned temporary memory.

## Reload And Play Mode

Entering editor play creates a play-session physics world from the runtime scene
definition. Stopping play destroys it without modifying authoring transforms.

Reload rebuilds physics state by default. Preservation of velocity or sleep
state requires a typed policy keyed by stable object ID.

## Floating Origin Rebasing

The active `PhysicsWorld` executes in local rebased cluster coordinates relative to the dynamic floating origin:

- **Local Half-Extent**: Simulation is bounded by $[-R_{\text{physics}}, +R_{\text{physics}}]$ (default $8192\,\text{m}$). This is independent of the rebase trigger $R_{\text{threshold}}$ (default $1000\,\text{m}$). See [Coordinate Precision And Origin Rebasing](./coordinate-precision-and-origin-rebasing.md).
- **Two-Phase Protocol**: As a registered `IOriginRebaseParticipant`, the physics adapter validates solver lock state during `PrepareRebase` and shifts spatial data during `CommitRebase`.
- **Position Updates**: Bodies, colliders, broadphase bounding volumes, and raycast caches have $\Delta_{\text{origin}}$ subtracted:
  $$\vec{x}_{\text{new}} = \vec{x}_{\text{old}} - \Delta_{\text{origin}}$$
- **Velocity Invariance**: Because origin shifting is an instantaneous coordinate re-indexing, linear velocity $\vec{v}$, angular velocity $\vec{\omega}$, and applied forces $\vec{F}$ remain strictly unchanged ($\Delta \vec{v} = 0, \Delta \vec{\omega} = 0$).
- **Solver State Continuity**: Contact manifolds retain relative contact points and penetration normals. Sleeping rigid bodies and deactivated islands remain asleep without triggering wake-up spikes or momentum shocks.
- **Timing Safe Point**: Origin shifts are forbidden while `PhysicsWorld::Step` is executing. Shifts execute only at the declared pre-render frame synchronization safe point.

See [Coordinate Precision And Origin Rebasing](./coordinate-precision-and-origin-rebasing.md) and [ADR-026](../../adr/026-large-world-precision-and-floating-origin-strategy.md).

## Debugging And Metrics

Physics exposes:

- step, broadphase, narrowphase, and solver time
- active and sleeping body counts
- shape and constraint counts
- broadphase pair and contact counts
- command and event buffer depth
- dropped event count

Debug draw data is extracted into a bounded render snapshot. The renderer does
not access live physics storage.

## Error Handling

Invalid user or scene data returns diagnostics. Internal solver invariant
violations use assertions in development and preserve safety checks in release.

NaN or non-finite body state is detected at owned boundaries, associated with
body/entity identity, and quarantined or treated as fatal according to the
configured runtime policy.

## Testing

Required tests cover:

- fixed-step independence from render frame rate
- body and shape handle generation
- static, kinematic, and dynamic transform authority
- deferred mutation during simulation
- deterministic ordering fixtures
- collision begin/end and trigger semantics
- bounded event overflow
- query filtering and stable ordering
- scene unload and stale command rejection
- reload preservation policy
- non-finite state detection
- core collider shape primitives resolve correctly from the primitive catalog
- origin shift position translation without velocity or momentum alterations
- sleeping island preservation across origin rebasing transactions

## Related Documents

- [Physics Debugger UI Reference](./physics-debugger.html): collision layers, contact pairs, rigidbody inspection, and solver diagnostics panel.

- [Coordinate Precision And Origin Rebasing](./coordinate-precision-and-origin-rebasing.md)
- [ADR-026: Large-World Precision and Floating Origin Strategy](../../adr/026-large-world-precision-and-floating-origin-strategy.md)
- [Runtime Lifecycle](./runtime-lifecycle.md)
- [Scene Runtime](./scene-runtime.md)
- [World Streaming Architecture](./world-streaming-architecture.md)
- [Input Architecture](./input-architecture.md)
- [Built-In Scene Primitives](./built-in-scene-primitives.md)
- [Ownership And Resource Lifetime](../foundation/ownership-and-resource-lifetime.md)
- [Observability Metrics And Profiling](../observability/observability-performance.md)
