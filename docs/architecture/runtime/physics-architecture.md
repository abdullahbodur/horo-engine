# Physics Architecture

## Purpose

This document defines physics-world ownership, fixed-step simulation, scene
synchronization, collision events, queries, determinism, threading, and
debugging for Horo Engine.

## Core Decisions

- Each active runtime scene owns or explicitly references one physics world.
- Physics advances only during fixed simulation ticks.
- Runtime components store typed body or shape handles, not owning pointers.
- Transform synchronization has one declared authority per body mode.
- Structural physics changes are deferred to safe points.
- Collision events are bounded tick results, not unrestricted process-bus
  traffic.
- Physics queries use explicit snapshots or world affinity.
- Backend-independent engine contracts do not expose solver implementation
  details.

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
- filter layers and masks
- whether triggers are included
- maximum result count
- ordering guarantee

Immediate queries execute on the physics owner thread outside a step. Parallel
or asynchronous queries use a read-only broadphase snapshot with documented
staleness.

## Layers And Filtering

Collision layers are stable project configuration. The physics world resolves
them into efficient masks at scene activation.

Unknown layers, asymmetric invalid matrices, and unsupported runtime changes
produce diagnostics. Layer display names are not serialized as runtime identity.

## Determinism

The engine guarantees deterministic behavior only for a declared combination
of:

- engine and physics format version
- platform and architecture class
- fixed timestep and solver configuration
- initial scene definition
- ordered input frames

Iteration order, constraint order, random seeds, and floating-point policies are
kept stable within that contract. Cross-platform bit-identical simulation is
not claimed unless separately verified.

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

## Related Documents

- [Physics Debugger UI Reference](./physics-debugger.html): collision layers, contact pairs, rigidbody inspection, and solver diagnostics panel.

- [Runtime Lifecycle](./runtime-lifecycle.md)
- [Scene Runtime](./scene-runtime.md)
- [Input Architecture](./input-architecture.md)
- [Built-In Scene Primitives](./built-in-scene-primitives.md)
- [Ownership And Resource Lifetime](../foundation/ownership-and-resource-lifetime.md)
- [Observability Metrics And Profiling](../observability/observability-performance.md)
