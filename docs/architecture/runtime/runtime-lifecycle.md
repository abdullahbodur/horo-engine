# Runtime Lifecycle Architecture

## Purpose

This document defines process startup, host composition, frame execution,
fixed-step simulation, play state, suspension, scene transitions, and shutdown
for editor and game runtimes.

## Core Decisions

- Hosts own runtime composition and lifecycle.
- Simulation advances with a fixed timestep; presentation uses variable frame
  time and interpolation.
- Frame phases and mutation boundaries are explicit.
- Editor mode and play mode use the same runtime services but separate
  authoritative scene state.
- Scene transitions occur through state machines, never by replacing live state
  from arbitrary callbacks.
- Shutdown is ordered, idempotent, and stops producers before dependencies.
- Headless execution follows the same lifecycle without window, input, GUI, or
  graphics capabilities unless requested.

## Runtime Owners

```text
Process Host
  +-- Platform Services
  +-- Observability Runtime
  +-- Configuration Service
  +-- Job System
  +-- Engine Data Bus
  +-- Asset Services
  +-- Scene Runtime
  +-- Physics Runtime
  +-- Audio Runtime (optional)
  +-- Network Runtime (optional)
  +-- Renderer (optional)
  +-- Application Services
  +-- Runtime Console Service (optional)
  +-- MCP Service (optional)
  +-- GUI / Editor Session (HoroEditor only)
```

The composition root constructs dependencies in order and shuts them down in
reverse dependency order. No subsystem starts a hidden process-global runtime.

## Process State Machine

```text
Created
  -> Initializing
  -> Ready
  -> Running
  -> Stopping
  -> Stopped

Initializing -> Failed
Running      -> Failed
```

Every transition is observable and happens once. A failed initialization
unwinds only resources that were successfully initialized.

## Startup

Startup order:

1. parse launch arguments without starting subsystems
2. initialize emergency diagnostics and platform directories
3. start observability and emit build/system identity
4. resolve configuration
5. construct platform, jobs, data bus, and application services
6. initialize requested renderer and window capabilities
7. load project and initial scene through application use cases
8. start optional runtime console, MCP, and GUI adapters
9. enter `Ready`, then `Running`

Arguments, environment, and persisted settings are resolved through
[Configuration System](../foundation/configuration-system.md). Startup failures return
typed errors and retain enough diagnostics for CLI or GUI presentation.

## Frame Phases

The canonical interactive frame is:

```text
1. BeginFrame
2. PollPlatformEvents
3. BuildInputSnapshot
4. ApplyQueuedOwnerThreadCommands
5. AdvanceFixedSimulation zero or more times
6. UpdateVariableRateServices
7. BuildRenderSnapshot
8. RenderWorld
9. RenderGui
10. Present
11. CommitDeferredLifecycleChanges
12. EndFrame
```

Frame phases are profiler scopes. A subsystem cannot mutate another owner's
state merely because both execute in the same frame.

Main-thread job continuations are pumped with phase-specific budgets. State
required by fixed simulation is committed before fixed steps begin. Destructive
scene and renderer lifecycle changes are committed only at their documented
safe points.

## Time Model

The host tracks:

- monotonic real time
- clamped presentation delta
- fixed simulation delta
- accumulator
- interpolation alpha
- simulation tick
- frame number

```cpp
accumulator += Clamp(realDelta, 0, maxFrameDelta);

while (accumulator >= fixedDelta && steps < maxCatchUpSteps) {
    FixedUpdate(fixedDelta);
    accumulator -= fixedDelta;
}

alpha = accumulator / fixedDelta;
Render(alpha);
```

Long stalls do not create an unbounded simulation spiral. When
`maxCatchUpSteps` is reached, the host records dropped simulation time according
to the configured policy.

Variable-rate update is not used for deterministic physics integration.

## Simulation Tick

One fixed tick executes:

1. consume the input command state assigned to the tick
2. run pre-physics gameplay systems
3. step physics
4. publish physics results into scene transforms
5. run post-physics and behavior systems
6. commit deferred entity/component changes
7. preserve previous/current state for interpolation

System ordering is declared by the scene runtime and validated before execution.
The data bus is not used to establish per-tick system order.

## Variable Update

Variable update is used for:

- editor camera and presentation behavior
- non-simulation UI state
- job progress and query refresh
- audio presentation updates where supported
- streaming and resource coordination

Variable update must not introduce simulation behavior that changes with frame
rate.

## Render Snapshot

The scene runtime builds an immutable or frame-owned render snapshot after
simulation updates. The renderer does not traverse mutable editor documents or
partially updated ECS storage.

Interpolation reads previous and current simulation transforms using `alpha`.
It does not modify authoritative simulation state.

## Runtime Modes

```cpp
enum class RuntimeMode {
    Headless,
    Game,
    EditorIdle,
    EditorPlay,
    EditorPaused
};
```

Capabilities vary by mode, but lifecycle rules remain the same.

### Editor Idle

The editor document is authoritative. A preview runtime may render a converted
snapshot, but runtime mutation does not silently modify the document.

### Editor Play

Entering play mode:

1. validate the editor document
2. build a runtime scene definition
3. create a new play-session scene runtime
4. load required resources
5. activate simulation at tick zero

The play runtime owns its ECS and physics state. Stopping play destroys this
state and returns to the editor document. Applying runtime changes back to the
document requires an explicit editor command.

### Pause And Step

Pause stops simulation ticks but keeps event processing, GUI, rendering, and
required service updates alive. Single-step advances exactly one fixed tick and
returns to paused state.

### Runtime Console And Development Overlays

The runtime console and development overlays are lifecycle participants when the
selected product profile enables them. They may accept input and query logs,
metrics, profiler state, and runtime inspection services while the game is
running or paused. Mutable commands are queued to the owning runtime safe point;
the console UI never mutates scene, renderer, physics, asset, or networking state
directly.

See [Runtime Debug Console And Development Overlays](./debug-console-and-overlays.md).

## Scene Transitions

Scene load, reload, unload, and replacement use the state model in
[Scene Runtime](./scene-runtime.md). Worker preparation may occur
asynchronously; final activation happens at a runtime safe point.

A stale completed load cannot replace a newer request. Transition requests
carry runtime session and generation identities.

## Suspension And Focus

Window focus loss does not automatically pause all products. The active host
policy decides whether to:

- continue normally
- throttle rendering
- pause game simulation
- suppress gameplay input

Minimized or occluded windows avoid unnecessary presentation work while still
servicing required jobs, transports, and shutdown requests.

## Fatal Failure

A fatal runtime failure:

1. stops accepting new mutation requests
2. records the typed error and emergency diagnostics
3. attempts bounded cleanup that is known to be safe
4. presents or returns the failure through the active host
5. terminates rather than continuing with violated invariants

Recoverable subsystem failures, such as an asset import error, do not enter this
path.

## Shutdown

Canonical shutdown:

1. transition host to `Stopping`
2. stop external request acceptance and close modal workflows
3. stop play simulation and unload active scenes
4. cancel and join host-scoped jobs
5. stop MCP, networking, and other transports
6. drain owner-thread continuations
7. release GUI and editor sessions
8. wait for renderer idle as required and destroy GPU resources
9. stop audio and release device-owned resources
10. destroy application and engine services
11. stop job and platform services
12. flush observability and write clean-shutdown marker
13. transition to `Stopped`

Shutdown may be requested more than once but executes its transitions once.

## Headless Runtime

Headless commands use the same initialization graph with omitted capabilities.
A command that requires rendering explicitly requests the null or a real
renderer. Headless execution does not create a hidden window merely to satisfy
an accidental dependency.

## Testing

Required tests cover:

- startup success and partial-initialization failure unwind
- fixed-step accumulator and catch-up bounds
- deterministic simulation under different presentation frame rates
- pause and one-tick step
- editor play isolation and stop restoration
- stale asynchronous scene transition rejection
- minimized and focus-loss policies
- repeated shutdown requests
- shutdown with running jobs and renderer resources
- headless lifecycle without GUI or graphics

## Related Documents

- [Scene Runtime](./scene-runtime.md)
- [Rendering Architecture](./rendering-architecture.md)
- [Input Architecture](./input-architecture.md)
- [Physics Architecture](./physics-architecture.md)
- [Audio Architecture](./audio-architecture.md)
- [Networking Architecture](./networking-architecture.md)
- [Asset Pipeline](./asset-pipeline.md)
- [Runtime Debug Console And Development Overlays](./debug-console-and-overlays.md)
- [Concurrency And Job System](../foundation/concurrency-and-jobs.md)

