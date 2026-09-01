# ADR-014: Sequencer Ownership, Clock Authority and Binding Boundary Decision

- **Status**: Proposed
- **Date**: 2026-08-28
- **Supersedes**: None
- **Scope**: Cinematic clock sources and policies, sampling/event phases, domain authority, bindings, origin rebasing and evaluation budgets
- **Issue**: [CIN-001.1](https://github.com/abdullahbodur/horo-engine/issues/1697)
- **Jira**: [HORO-1656](https://horo-engine.atlassian.net/browse/HORO-1656)
- **Normative document**: [Cinematic Sequencer Architecture](../architecture/runtime/cinematic-sequencer-architecture.md)

## Context

Cinematic assets coordinate values, camera selection and event occurrences across
simulation and presentation. The previous proposal conflated fixed simulation time
with render interpolation, allowed a paused simulation clock to keep advancing,
and called event dispatch stateless. It also exposed component byte offsets and
implied unlimited cinematic authority over physics, cameras and gameplay pause.
These ambiguities must be resolved before independent implementations converge.

## Decision

**CinematicRuntime owns per-player clocks, binding caches and bounded evaluation.
Clock source, pause following and dilation policy are separate validated settings.
Value sampling is history-independent; events use stateful interval crossing.
Authoritative simulation writes use their domain's fixed-tick seam, while optional
presentation samples never mutate gameplay or fire authoritative events. Those
presentation samples occupy the runtime lifecycle window after `RenderExtraction`
and before `Present` (phases 8–10: `RenderExecution`, `RenderGui`, `Present` in
[Runtime Lifecycle](../architecture/runtime/runtime-lifecycle.md#frame-phases);
[ADR-033](033-presentation-and-display-ownership.md) owns that split). A coarser
“render execution and GUI presentation” grouping in this ADR is that same window,
not a second phase contract. SceneModel owns bindings for scene/component-authored
properties only. Physics, camera and pause authorities admit typed requests and
retain final control.**

### Ownership And Dependencies

| Contract | Owner | Boundary |
|---|---|---|
| SequenceAsset, tracks, keys and clock settings | CinematicModel | Immutable cooked data; depends on Foundation, SceneModel and Assets |
| SequencePlaybackClock, SequenceBindingAuthority, SequenceEvaluationSystem | CinematicRuntime | Uses CinematicModel, RuntimeScene and Runtime; no EditorServices, Gui or ImGui dependencies |
| PropertyBindingRegistry | SceneModel | Scene/component metadata and validating accessors; shared downward by runtime and Inspector |
| Fixed control clock and gameplay pause tokens | Host Runtime | Separate from committed simulation tick count; no player-owned scheduler booleans |
| Final movement/collision state | Physics / character controller | Explicit cinematic request modes only; direct property writes forbidden |
| Final active camera | Camera subsystem | Resolves gameplay proposals and admitted cinematic override tokens |
| Timeline/Inspector authoring | EditorServices / Gui | Uses model and application use cases; runtime never depends on editor UI |

Host composition injects typed domain adapters. SceneModel neither discovers global
services nor becomes a generic engine-wide property reflection registry.

### Clock Sources And Policies

The earlier SequenceClockPolicy enum is replaced by three independent settings:

```cpp
enum class SequenceClockSource : uint8_t {
    CommittedSimulation,
    UnscaledFixedControl,
    MonotonicWall,
    External
};
enum class SequencePausePolicy : uint8_t { FollowGameplay, PlayerOnly };
enum class SequenceDilationPolicy : uint8_t { SourceNative, ApplyGameplayScale };
```

- **CommittedSimulation** consumes `FixedStepContext::fixedDelta * playbackSpeed`
  once per committed tick. Host simulation rate is already represented by fixed-
  tick production under [ADR-061](061-animation-ownership-update-order-and-clock.md)
  and is not multiplied into `fixedDelta` again.
  Cursor, values and events are staged for the attempted tick and discarded if it
  fails. No render-interpolated delta advances this authoritative clock.
- **UnscaledFixedControl** consumes host-owned unscaled fixed service quanta even
  while simulation is paused; these are explicitly not committed simulation ticks.
  Its bounded clock/catch-up policy is defined in Runtime Lifecycle. Repeatability
  requires identical recorded control-tick/input histories.
- **MonotonicWall** consumes clamped monotonic elapsed time for UI/intro playback.
  It does not promise deterministic fixed-tick reproduction.
- **External** follows accepted timestamped media-provider samples. It guarantees
  bounded synchronization and explicit loss/discontinuity handling, not simulation
  determinism. Smoothing can affect presentation, never authoritative event crossing.

FollowGameplay honors the gameplay pause authority; PlayerOnly ignores that pause
but still honors explicit player pause and host suspension. CommittedSimulation
requires FollowGameplay and SourceNative: a policy cannot create missing simulation
ticks. ApplyGameplayScale is valid only for the two unscaled local sources and
multiplies their raw delta once before playback speed. External rate/seek/reverse
requires provider capabilities and acknowledgement, never local multiplication.

### Pause And External-Sync Behavior

| Condition | CommittedSimulation | UnscaledFixedControl / MonotonicWall | External |
|---|---|---|---|
| Gameplay pause | Frozen: no committed ticks | FollowGameplay freezes; PlayerOnly continues | FollowGameplay holds output; PlayerOnly follows master |
| Player requests pauseGameplay | Invalid combination | Requires PlayerOnly; presentation-only effects | Requires PlayerOnly; presentation-only effects |
| Explicit player pause | Hold cursor/values | Hold cursor/values | Hold local output; request provider pause only if supported |
| Gameplay dilation | Already included upstream | SourceNative ignores; ApplyGameplayScale multiplies once | Provider owns rate; ignore gameplay dilation |
| Host suspend/resume | Freeze / resume ticks normally | Freeze / zero-delta resume baseline | Freeze / reacquire provider epoch and baseline |
| Negative speed | Reverse interval advance | Reverse interval advance | Requires provider reverse/rate capability |
| Seek or editor scrub | Values sample immediately; skipped events suppressed | Same | Provider seek request or typed Unsupported |

A pauseGameplay request acquires a scoped host-owned token at a safe point before
the next simulation tick. It pauses AI, gameplay scripts, controller movement,
physics and simulation-driven animation together, not just selected behavior
callbacks. UI, services and presentation continue. Multiple tokens compose; stop
or failure releases only the player's own token, never a menu/debugger/network pause.
Denied requests unwind activation. Gameplay-mutating event bindings are rejected
for pauseGameplay players rather than accumulating a backlog against frozen physics.

External seek/rate requests hold output until acknowledgement; timeout/provider
loss yields ClockUnavailable. On resume, accepted seek or discontinuity, adopt the
provider's `(epoch, time)` baseline, clear drift history and suppress skipped events.
Small continuous drift may be corrected within configured offset/rate bounds; a
jump opposite the negotiated playback direction, epoch change or excessive drift must not cause a catch-up event burst.

### Values, Events And Numeric Guarantees

`Sample(T)` is independent of prior samples for value tracks. Canonical timeline
times use checked rational/fixed-point advancement; float interpolation results
follow documented numeric tolerances, not an unsupported cross-platform bit-identity
promise. Random access is independent of playback history; binary key lookup is
O(log N), with bounded interpolation work per segment, not generic O(1).

Event dispatch retains a cursor and traversal identity. Forward intervals are
`(previous, current]`; reverse intervals are `[current, previous)` and only eligible
reverse keys fire. Stable tie-breaking uses TrackId and KeyframeId. Loop/turn rules
and per-occurrence IDs prevent duplicate boundary emission. Seek/scrub is silent by
default; explicit DispatchCrossedEvents seek requires a capable runtime adapter and
obeys the same budgets/authority rules. Rebind and external discontinuities never
replay missed markers. Event preflight rejects oversized intervals before advancing
the cursor or applying values, so no partial authoritative interval is emitted.

### Evaluation Phases And Domain Authority

The normative [phase contract](../architecture/runtime/cinematic-sequencer-architecture.md#frame-evaluation-phase)
separates fixed-tick authoring effects from presentation. Cinematic intents are
resolved after relevant gameplay proposals and before their owning subsystem's
consumption phase. Descriptor-declared dependencies form a validated DAG; hierarchy
order applies only within transform application, not to every property/event track.
Post-physics-owned properties use their owner's later seam. Incompatible phase
requirements are rejected instead of creating a second scheduler.

For AI entities, movement is an admitted NavIntentCommit intent, followed by
CharacterControllerLocomotion and AnimationRigUpdate under ADR-022. Cinematics do
not insert another AI mutation phase or let clients control server-owned actors.
The non-AI animation/root-motion path follows ADR-061's authoritative fixed-tick
ordering. Sequencer inputs that can affect movement are staged through that owner
seam; optional presentation samples remain render-only.
Optional interpolated values target immutable presentation/preview output only.

Physics/controller transforms, velocity and collider state cannot be directly
keyframed. A component may expose an explicit kinematic/blend request with owner
admission, collision behavior and safe token release; otherwise binding fails.
CameraCutTrack submits typed selection/blend requests through a camera override
token. Camera owns final selection and restores current gameplay proposals on
release, not stale pointers. Concurrent players resolve conflicts by explicit
priority and stable identity or an owner-defined blend rule.

Camera/event delivery uses bounded typed domain queues, not DataBus scheduling.
ADR-015's single approved accessibility event does not reserve the entire bus;
its notification-plane principle still rules out using it as a cinematic data path.
Irreversible events are released only after tick commit and execute at the target's
next permitted boundary, never reentrantly during sampling.

### Binding And Property Contract

Authored tracks store StableObjectId, never EntityId or pointers. Each apply checks
SceneRuntimeId, entity generation, authored identity and component/schema revision.
No cached component address survives relocation. Missing targets enter PendingTarget;
incompatible types enter Incompatible until a relevant revision changes. Lifecycle
notifications and index revision checks schedule bounded rebind after the authored
index update at CommitDeferredLifecycleChanges, including same-scene respawn.
Rebind samples current values without replaying events.

PropertyBindingDescriptor exposes stable IDs, component/property type, validating
read/write functions, ranges and owner policy. Byte offsets are private optimization
metadata inside component implementations, never stable public/cooked contracts.
The registry covers scene/component-authored properties only; global audio/material
or gameplay services use separate adapters. Host composition validates inert
descriptors and seals the registry before activation. Accessors cannot bypass owner
validation, component lifetime checks or physics authority.

### Origin Rebase And Capacity

TransformTrack always stores parent/anchor-relative curves. Root tracks require a
durable canonical WorldCoordinate64 sequence anchor; current rebased root floats
are not stable authoring coordinates. Scene spatial ownership resolves anchor plus
local offset against the current origin epoch once. CameraCutTrack stores camera
identity only; camera paths use anchored TransformTracks. There is no implicit
world-space camera-track exception or mutation of keyframe data on rebase.

The normative [evaluation budget](../architecture/runtime/cinematic-sequencer-architecture.md#evaluation-capacity-and-admission)
sets explicit aggregate player/track, key, nesting, event, loop, rebind and retained
byte limits. Host-selected CPU/memory profiles replace graphics API eligibility.
Nested players share aggregate budgets; cycles and over-budget activation are
rejected before acquiring tokens. Authoritative sampling cannot be skipped based
on wall-clock profiling. Over-budget advancement holds the player with a typed
outcome rather than partially applying values or silently dropping events.

### Async Effects And Delivery Boundaries

Prefab spawn adapters follow ADR-017: AssetNotLoaded is an explicit outcome, not
permission to synchronously load in evaluation. Tick-exact spawns preload assets.
Other asynchronous effects return typed requests, retain payload lifetime and use
occurrence identity for idempotency. ADR-010/018 governs JobId and application-owned
OperationStore tracking for exposed user operations. Completion returns to the
owner at a later boundary; no sampling/worker path blocks or creates its own store.
Cancellation retires owned requests and rejects stale-generation completions.

[Delivery Boundaries](../architecture/runtime/cinematic-sequencer-architecture.md#delivery-boundaries)
links the existing tickets, including CIN-003.1/#1700 and CIN-003.2/#1723 for camera
authority/cuts, and CIN-004.1/#1701 and CIN-004.2/#1727 for event authority/dispatch.
CIN-002.1/#1698 and CIN-002.2/#1699 own detailed playback and cross-domain authority
integration; CIN-002.7/#1720 and CIN-002.8/#1721 own pause and concurrent budgets.
CIN-001.2–001.7 retain identities, schema, sampling, transform/property and foundation
qualification ownership. This decision establishes constraints, not completed
runtime implementation or amended issue scope by implication.

## Consequences

- Tick-driven semantic values and event ordering are independent of render cadence
  for identical committed input histories; wall/external sources have narrower guarantees.
- Pausing gameplay no longer asks a stopped simulation clock to advance itself.
- Explicit owner seams prevent cinematic writes from fighting physics or cameras.
- Binding lifetime, coordinate space and overload behavior are implementable and testable.
- Required conformance tests are listed in the normative document as follow-up work.

## Rejected Alternatives

- **One stateless deterministic clock/evaluator for all tracks**: Conflates clock
  inputs, presentation sampling and stateful event occurrence rules.
- **Raw variable delta for authoritative gameplay**: Couples event timing to render
  cadence instead of committed fixed ticks.
- **Scheduler pause booleans or direct physics/camera writes**: Breaks composed
  authority and allows one player to overwrite another subsystem's current state.
- **Public raw offsets or string property lookup**: Exposes storage layout or
  performs dynamic resolution on hot paths instead of validated stable bindings.
- **Replay from zero on seek**: Makes work depend on playback history; bounded
  indexed sampling and explicit event intervals provide a predictable alternative.
