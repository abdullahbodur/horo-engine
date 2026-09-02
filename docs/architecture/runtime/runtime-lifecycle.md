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
  +-- Runtime UI Service (optional model-only or rendered composition)
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
  -> Running <-> Suspended
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
5. construct platform, jobs, data bus, and asset services
6. initialize the requested omitted/null/device audio composition through ADR-062
7. initialize requested renderer and window capabilities
8. initialize the selected omitted/model-only/rendered Runtime UI composition
9. construct application services
10. load project and initial scene through application use cases
11. start optional runtime console, MCP, and GUI adapters
12. enter `Ready`, then `Running`

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
5. FixedUpdate zero or more times
6. VariableUpdate
7. RenderExtraction
8. RenderExecution
9. RenderGui
10. Present
11. CommitDeferredLifecycleChanges
12. EndFrame
```

Frame phases are profiler scopes. A subsystem cannot mutate another owner's
state merely because both execute in the same frame.

`RuntimePhase` exposes this complete ordering to both graphical and headless
hosts. Headless compositions register successful no-op participants for omitted
capabilities rather than defining a second phase contract. Presentation is
admitted only after `RenderExtraction`, `RenderExecution`, and `RenderGui` have
all completed successfully in the same frame. Failure or cancellation skips all
later normal phases, including `EndFrame`; owned frame scopes perform cleanup by
abort/RAII instead.

Main-thread job continuations are pumped with phase-specific budgets. State
required by fixed simulation is committed before fixed steps begin. Destructive
scene and renderer lifecycle changes are committed only at their documented
safe points.

[ADR-033](../../adr/033-presentation-and-display-ownership.md) specializes these
safe points for presentation: apply host/window intent and publish a revisioned
pending output candidate through owner-thread commands before layout/extraction.
Realize that candidate and commit active surface state at `RenderExecution`
entry before native frame acquisition; mismatched or unrealized candidates skip
that output. Commit final destruction through deferred lifecycle work after
retirement. Minimized/zero-pixel output suspends presentation, not the simulation
clock. No additional runtime phase is introduced.

[ADR-062](../../adr/062-audio-runtime-ownership-and-update-order.md) specializes
audio participation without adding phases. Owner-thread commands commit audio
lifecycle/focus/device/scene requests; `VariableUpdate` drains producer and
callback queues and publishes bounded callback work; deferred lifecycle commit
retires acknowledged scene-context barriers; `EndFrame` publishes bounded
control snapshots. The hardware callback advances on its independent sample
clock and never calls runtime phases.

[ADR-073](../../adr/073-runtime-ui-ownership-scope-and-update-order.md) specializes
Runtime UI without adding phases. Owner commands commit scope/document/resource
lifecycle work. FixedUpdate publishes gameplay state but never advances UI.
VariableUpdate routes input against the last successfully presented interaction
snapshot, resolves committed bindings and publishes layout/focus/hit-test state.
RenderExtraction emits immutable per-view UI snapshots and RenderExecution composes
them. Runtime game UI is not `RenderGui`; that phase remains host/editor/development
GUI composition. Presentation success adopts the next interaction revision and
deferred lifecycle commit retires old scope/render generations.

## Time Model

The host tracks:

- monotonic real time
- clamped presentation delta
- positive rational simulation rate and its revision
- fixed simulation delta
- accumulator
- interpolation alpha
- simulation tick
- frame number

```cpp
accumulator.AddScaled(Clamp(realDelta, RealDuration::Zero(), maxFrameDelta),
                      simulationRate, simulationScaleRemainder);

while (accumulator >= fixedDelta && steps < maxCatchUpSteps) {
    FixedUpdate(fixedDelta);
    accumulator -= fixedDelta;
}

alpha = accumulator / fixedDelta;
Render(alpha);
```

`simulationRate` changes how much clamped elapsed time enters the accumulator;
it does not change `fixedDelta`. Scaling uses checked rational/fixed-point math
and the host clock owns `simulationScaleRemainder` across frames. `AddScaled`
preflights overflow and commits the accumulator plus remainder atomically; it
does not round through floating-point seconds or lose an unrepresented fraction.
The baseline rate is `1/1`. A rate change commits through the owner-thread command
boundary before fixed-step scheduling, preserves that remainder, and records a
revision for replay evidence. Negative rates are invalid. Gameplay
pause is separate composed state and contributes no accumulator time; it is not
represented by a zero rate.

Long stalls do not create an unbounded simulation spiral. When
`maxCatchUpSteps` is reached, the host records dropped simulation time according
to the configured policy.

The baseline Phase 1 policy uses a 16,666,667 ns fixed step, at most five
catch-up ticks, and a 250 ms maximum real delta. Negative samples normalize to
zero. After catch-up saturation, whole fixed intervals are discarded while the
fractional remainder is preserved, keeping interpolation alpha in `[0, 1)`.
The first frame and first frame after resume establish a zero-delta baseline.

`FixedStepContext::simulationTick` is the one-based tick currently being
attempted. It becomes committed only after every fixed-step participant
succeeds. `FrameContext::completedSimulationTick` is the count of committed
ticks and is therefore zero before the first successful tick. Variable update
and render extraction observe that committed count together with interpolation
alpha.

[ADR-061](../../adr/061-animation-ownership-update-order-and-clock.md) applies
this policy to animation. Animation consumes the unchanged fixed quantum once
per attempted tick. Host simulation rate is already represented by tick cadence;
animation and physics do not multiply it into their delta again.

The scheduler retains allocation-free cumulative counters for committed ticks,
dropped time and steps, catch-up saturation, negative samples, and maximum-delta
clamps. Hosts query snapshots without locks or logging in the hot path;
observability adapters may publish them at their own bounded cadence.

### Cinematic Fixed Control Clock

ADR-014 defines an optional Runtime-owned unscaled fixed control clock for
presentation-only cutscenes that continue while gameplay is paused. It is a service
clock, not FixedUpdate: it advances no simulationTick/completedSimulationTick count
and never runs physics, AI, controller movement or gameplay callbacks during pause.
The host services its bounded quanta in VariableUpdate before presentation sampling.

Its validated configuration explicitly supplies a positive quantum and catch-up
limit. Baseline defaults reuse 16,666,667 ns, five quanta and the 250 ms elapsed-time
clamp above; excess whole quanta are dropped with counters, keeping the remainder.
The first frame/resume establishes zero delta. Host suspension freezes the clock.
Identical recorded control-quanta and setting histories reproduce cinematic
sampling; elapsed wall time or a different dropped-time history alone does not.
Gameplay pause tokens compose at a safe point before the next simulation tick;
releasing a cinematic token cannot unpause another owner's reason. See
[Cinematic Sequencer Architecture](./cinematic-sequencer-architecture.md).

## Error And Allocation Contract

Expected participant failures use `Result<void>` and stable error codes.
Exceptions are forbidden as participant control flow, but the host contains an
unexpected exception at the participant boundary, aborts owned frame state, and
translates it to a typed fatal runtime error.

The allocation-free guarantee applies to the successful steady-state scheduler
path: clock math, context construction, participant traversal, and phase
dispatch. Error construction, diagnostic formatting, exception containment,
and shutdown failure handling may allocate. GUI and concrete renderer internals
retain their own separately measured allocation contracts.

Variable-rate update is not used for deterministic physics integration.

## Simulation Tick

One fixed tick executes:

1. consume the input command state assigned to the tick
2. run ordered pre-physics systems, including gameplay animation parameters,
   animation pose/root-motion staging, and character-controller movement
3. step physics
4. publish physics results into scene transforms
5. run post-physics and behavior systems, including typed animation pose
   overrides and candidate pose finalization
6. commit deferred entity/component changes
7. atomically publish committed subsystem state, events, and previous/current
   state for interpolation

System ordering is declared by the scene runtime and validated before execution.
The data bus is not used to establish per-tick system order.

## Variable Update

Variable update is used for:

- editor camera and presentation behavior
- interpolation of committed animation poses and isolated editor preview playback
- Runtime UI input, binding, presentation animation and layout under ADR-073
- job progress and query refresh
- audio presentation updates where supported
- streaming and resource coordination

Variable update must not introduce simulation behavior that changes with frame
rate. Animation presentation/preview cannot emit gameplay events, root motion,
or physics/controller writes; its authoritative order is defined by ADR-061.

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
returns to paused state. It uses the ordinary unchanged fixed quantum and complete
subsystem order; it does not consume accumulated wall time. Authoritative animation
therefore holds during pause and advances once during a successful step. Isolated
editor preview may advance only under its separate preview controls.

Runtime UI continues its ordinary VariableUpdate/input/layout/render path while
gameplay is paused. Menus/navigation/accessibility feedback use declared unscaled
presentation time by default; `FollowGameplay` UI animation freezes and `Manual`
time advances only by owner command. A UI action requests pause through the
application pause capability and never owns scheduler tokens directly. After
single-step, UI observes the newly committed tick in one normal VariableUpdate.

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

Explicit host suspension is distinct from gameplay pause. While suspended, the
host runs only `BeginFrame`, `PollPlatformEvents`,
`ApplyQueuedOwnerThreadCommands`, and `EndFrame`. Fixed/variable simulation,
render extraction, execution, GUI rendering, and presentation are skipped.
Resume resets the clock baseline so suspended wall time never becomes catch-up
work. ADR-062 keeps the bounded audio lifecycle/control subset serviceable on
the remaining owner-thread boundaries; the callback follows only committed audio
suspend policy.

Runtime UI performs no VariableUpdate, input dispatch, extraction or render work
while suspended; interactive capture is released and resume revalidates viewport
attachments with a zero-delta baseline. Suspended wall time is not UI catch-up.

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
7. close Runtime UI command/input admission, retire every scope/attachment, join
   UI work, and release UI render/resource leases
8. release GUI and editor sessions
9. destroy scene/project/application services after closing their renderer and
   audio producer ports
10. wait for renderer idle as required and destroy GPU resources
11. quiesce/detach the audio callback, reconcile terminal outcomes,
   and release device-owned resources under ADR-062
12. destroy remaining asset, data-bus, job, and platform services
13. flush observability and write clean-shutdown marker
14. transition to `Stopped`, unless a subsystem's documented fatal-retention path
    requires immediate process termination without normal reclamation

Shutdown may be requested more than once but executes its transitions once.

## Headless Runtime

Headless commands use the same initialization graph with omitted capabilities.
A command that requires rendering explicitly requests the null or a real
renderer. Headless execution does not create a hidden window merely to satisfy
an accidental dependency.

Runtime UI composition is independently `Omitted`, `ModelOnly`, or `Rendered`.
Model-only headless tools may validate documents, bindings, layout and Null
extraction without a viewport/input/GPU. A request requiring visible or interactive
UI fails capability preflight instead of creating a hidden window or editor GUI.

## Testing

Required tests cover:

- startup success and partial-initialization failure unwind
- fixed-step accumulator and catch-up bounds
- fixed-state determinism under 30, 60, and 144 Hz variable frame cadences
- interpolation alpha at equivalent accumulator positions
- presentation gating after extraction, execution, and GUI failures
- dropped-time and clamp counter observability
- allocation-free successful steady-state scheduler dispatch
- mid-frame cancellation and exception containment
- explicit suspend pump phases and resume baseline reset
- pause and one-tick step
- editor play isolation and stop restoration
- stale asynchronous scene transition rejection
- minimized and focus-loss policies
- repeated shutdown requests
- shutdown with running jobs and renderer resources
- headless lifecycle without GUI or graphics
- Runtime UI phase order across zero/multiple fixed ticks, gameplay pause/step,
  suspension, failed presentation, scene/viewport teardown and shutdown

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
