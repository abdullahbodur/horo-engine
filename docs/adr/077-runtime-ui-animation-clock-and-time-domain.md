# ADR-077: Runtime UI Animation Clock and Time Domain

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: Runtime UI simulation, unscaled presentation, screen-transition, editor-preview, deterministic-test and manual time domains; sampling, rates, pause/step/suspension, cancellation, screen lifecycle, accessibility, errors, compatibility, and shutdown
- **Issue**: [RUI-004.4](https://github.com/abdullahbodur/horo-engine/issues/731)
- **Jira**: [HORO-731](https://horo-engine.atlassian.net/browse/HORO-731)
- **Parent**: [RUI-004](https://github.com/abdullahbodur/horo-engine/issues/726)
- **Related**: [ADR-014](014-sequencer-ownership-clock-authority-and-binding-boundary.md), [ADR-061](061-animation-ownership-update-order-and-clock.md), [ADR-073](073-runtime-ui-ownership-scope-and-update-order.md), [ADR-076](076-runtime-ui-style-asset-token-and-inheritance.md)
- **Normative documents**: [Game UI and HUD](../architecture/runtime/game-ui-and-hud.md), [Runtime Lifecycle](../architecture/runtime/runtime-lifecycle.md), [Editor UI Design System](../architecture/editor/ui-design-system.md)

## Context

Runtime UI motion includes focus/hover feedback, progress and status animation,
gameplay-synchronized indicators, route enter/exit transitions and editor preview.
Those uses do not all follow the same clock. A pause menu must remain responsive
while fixed simulation is paused, a cooldown indicator may follow committed
simulation time, a screen exit transition must end with its exact route generation,
and a deterministic test cannot read wall time.

ADR-073 established that UI presentation normally advances in VariableUpdate and
introduced provisional `PresentationUnscaled`, `FollowGameplay` and `Manual`
policies. It did not define clock identity, integer accumulation, discontinuities,
preview/test clocks, cancellation or screen lifecycle. Leaving each animation to
read a raw variable delta would create drift, resume jumps, duplicate advancement
when multiple viewports render, and callbacks targeting destroyed screens.

ADR-061 owns skeletal/pose animation evaluation domains and fixed-tick root-motion
ordering. ADR-014 owns sequencer player clocks. Runtime UI property/style motion is
a separate semantic owner under `RuntimeUiService`; it consumes host clock evidence
but cannot advance skeletal animation, sequencer, simulation or renderer state.

This decision replaces ADR-073's provisional `UiTimePolicy` enumeration with a
typed Runtime UI domain/sample/policy contract. RUI-004.5 will define property
interpolation and retargeting on top of it; it cannot add another clock source or
change pause/lifecycle semantics.

## Decision

### 1. RuntimeUiService owns UI timelines, not host clocks

The ownership split is:

| Responsibility | Owner |
|---|---|
| Monotonic presentation evidence, fixed simulation commits, pause/rate/step and host suspension | Host Runtime |
| Runtime UI domain selection, timeline instances, accumulation, playback policy, lifecycle and immutable time snapshots | `RuntimeUiService` |
| Skeletal animation and sequencer player time | ADR-061 and ADR-014 owners |
| Preview/test/manual clock commands | Editor preview/test harness/application capability respectively |
| Property interpolation/transition graphs | RUI-004.5 consuming this clock contract |
| Drawing presented values | Renderer consuming immutable UI snapshots |

The host publishes typed clock evidence; Runtime UI never calls a platform timer,
reads `std::chrono`/SDL/native timestamps directly, mutates the fixed-step
accumulator or owns gameplay pause tokens. Renderer/extraction never advances a UI
timeline. Rendering one snapshot in multiple views or more than once cannot change
animation state.

Every time/timeline/animation identity is scoped to the exact game runtime, ADR-073
owner scope, optional screen/route and generation. Handles are generation checked,
process local and non-serializable.

### 2. UI time uses closed domains and integer samples

The Runtime UI domains are:

```cpp
enum class UiTimeDomain : std::uint8_t {
    Simulation,
    PresentationUnscaled,
    ScreenTransition,
    EditorPreview,
    DeterministicTest,
    Manual,
};
```

A `UiClockSample` contains domain and clock generations, monotonic sample sequence,
signed 64-bit elapsed/delta nanoseconds, source revisions and a typed continuity
status. Deltas are finite and non-negative for forward clock advance. Reverse
property playback uses animation-local direction/rate over a forward domain; it
does not make host/simulation time run backward.

Conversions from rational fixed ticks, presentation timestamps and playback rates
use checked integer/rational arithmetic with a carried remainder. Equivalent source
sample/policy histories produce identical cursors without repeated floating-point
rounding drift. Overflow, sequence regression, unknown generation or a negative
source delta is a typed clock failure, not a clamp to an arbitrary duration.

Serialized data stores domain/policy/duration/rate semantics, not elapsed host time,
native timestamp frequency, editor frame count or a live clock handle.

### 3. Simulation domain follows committed simulation evidence

`Simulation` advances only from successfully committed fixed simulation ticks in
which the owning game runtime participates. Runtime UI samples the committed
simulation tick/time revision during VariableUpdate after fixed work. Zero fixed
ticks produce zero delta; multiple committed catch-up ticks produce their exact
combined rational duration.

The host simulation rate is already reflected in tick production. UI does not
multiply `fixedDelta` by the global rate again. Gameplay pause produces no committed
ticks and freezes this domain. Single-step commits exactly one ordinary fixed tick
and therefore one domain quantum on success; a failed/rolled-back step contributes
nothing.

Simulation UI animations may present cooldowns or world/gameplay-synchronized
effects, but they do not emit gameplay events, change authoritative simulation or
pretend reverse UI playback rewinds gameplay. They read the same committed evidence
as other presentation consumers.

### 4. PresentationUnscaled is the default interactive UI clock

`PresentationUnscaled` advances once per admitted host presentation frame from the
host's clamped monotonic presentation evidence. It ignores gameplay pause and global
simulation time scale. Menus, focus/hover/pressed feedback, accessibility feedback,
loading UI and ordinary presentation motion default to this domain.

The host admits one sample per frame before Runtime UI VariableUpdate. All UI
instances/attachments in that game runtime consume the same sample; multiple
viewports do not each advance it. A configured maximum presentation delta is a host
admission policy with an explicit discontinuity diagnostic, not an animation-local
silent clamp.

Window focus loss does not automatically stop this domain; product focus/background
policy may explicitly hold presentation and publishes that reason. Host suspension
freezes the domain. Resume creates a new clock generation/baseline and emits zero
delta for the first admitted frame, so suspended wall time never catches up.

### 5. ScreenTransition is lifecycle-gated presentation time

`ScreenTransition` is a child timeline of `PresentationUnscaled`, scoped to one
exact screen/route/layer generation. It ignores gameplay pause/time scale but only
advances while its declared route lifecycle gate is open:

- enter timelines may begin when a prepared screen enters `Activating`;
- active-state transitions advance while that route remains admitted/visible by
  policy;
- exit timelines begin with the exact screen's `Deactivating` transition;
- completion, cancellation, replacement or retirement permanently closes that
  timeline generation.

A required enter transition may delay `Active`, and a required exit transition may
delay `Retiring`, only within declared finite duration/deadline budgets. Optional
transitions never block lifecycle completion. Owner destruction, host shutdown,
required dependency failure or deadline expiry cancels/finishes according to the
declared lifecycle fallback; no animation can keep a dead route alive indefinitely.

Replacement screens receive new route/timeline generations. A completion from the
old route cannot activate/retire the replacement. Transition time is not one
process-global menu clock and is never inferred from current visibility alone.

### 6. EditorPreview is isolated and controllable

`EditorPreview` exists only in an isolated editor preview runtime. The preview
owner supplies play, pause, bounded step, seek and scrub commands at preview safe
points. Optional live playback consumes clamped editor-preview presentation evidence,
not the game runtime's mutable UI clock.

Seek/scrub evaluates presentation values without UI/gameplay callbacks, route
commands, audio, pause requests or mutation of a play session. Preview state,
cursor, selected timeline, loop range and speed are editor session data and never
serialized into runtime style/UI assets.

Host suspension freezes live preview playback and resume establishes a zero-delta
baseline. Closing the preview/tab/project cancels and retires its clock/timelines
without affecting packaged/play runtime generations.

### 7. DeterministicTest is driven only by explicit test steps

`DeterministicTest` has no wall/presentation/simulation input. A test harness creates
one clock generation and submits an ordered sequence of exact integer/rational step,
seek and reset commands. No automatic time passes between commands, regardless of
test process duration or renderer cadence.

The domain is available in headless `ModelOnly` compositions and can drive the same
style/property/lifecycle evaluation as runtime domains. Tests record the complete
step sequence, clock/policy revisions and resulting immutable UI generations.
Randomized/property tests must provide a seed as explicit input.

Test-only clock handles cannot be admitted to a shipping runtime composition and
cannot publish gameplay/route side effects except through an explicitly modeled
test host capability.

### 8. Manual timelines advance through owner commands

`Manual` advances or seeks only through an admitted typed application/owner command
at ADR-073's owner-thread cutoff. Each command names expected game/scope/screen/
timeline generations, target time or delta, sequence and behavior for crossed
markers. Stale, duplicate, out-of-order or unauthorized commands fail without
partial advancement.

Manual time is useful for authored gauges, scripted UI sequences and controlled
replay presentation. It is not a backdoor to platform wall time, skeletal animation,
sequencer or simulation. Commands arriving after the current frame cutoff apply to
the next candidate generation.

### 9. Animation-local playback policy is separate from domain time

An animation/timeline descriptor declares:

```cpp
struct UiAnimationTimePolicy {
    UiTimeDomain domain;
    UiDuration delay;
    UiDuration duration;
    UiPlaybackRate rate;
    UiPlaybackDirection direction;
    UiLoopPolicy loop;
    UiLifecyclePolicy lifecycle;
};
```

Durations are finite non-negative integer/rational values. Playback rate is a
bounded checked rational: positive advances, zero holds when explicitly allowed,
and negative is represented by `direction` rather than a negative domain delta.
Rate changes commit at the owner-thread boundary and preserve the per-instance
fractional remainder.

Delay, finite iteration count, ping-pong/reverse traversal and fill behavior are
explicit. Infinite looping is allowed only for non-lifecycle-blocking presentation
motion and ends on owner/screen cancellation. A zero-duration animation resolves
its declared final value and completion in the same VariableUpdate without divide-
by-zero or a one-frame hidden delay.

Simulation rate, gameplay pause, UI-local playback rate, lifecycle hold and
accessibility motion policy are distinct inputs. No code guesses one from another.

### 10. Pause, focus, suspension and reduced motion are orthogonal

The canonical behavior is:

| Condition | Simulation | PresentationUnscaled | ScreenTransition | Preview | Test/Manual |
|---|---|---|---|---|---|
| Gameplay pause | Hold | Advance | Advance while lifecycle gate open | Independent | Command driven |
| Global simulation rate | Reflected by committed ticks | Ignore | Ignore | Independent | Command driven |
| Window focus loss | Product policy | Product policy | Follows presentation plus lifecycle | Preview policy | No implicit effect |
| Host suspension | Hold | Hold; zero-delta resume | Hold; preserve lifecycle generation | Hold live playback | No implicit advance |
| Single-step | One committed quantum | One ordinary presentation sample | Ordinary sample if open | Independent | Explicit command only |

Accessibility/reduced-motion policy resolves before an animation instance is
published. It may set duration/delay to zero, replace motion with an approved
non-motion state change, limit repetition or disable decorative loops. The resolved
policy/revision is captured in the immutable animation/style snapshot. Required
screen lifecycle completion still occurs in the same VariableUpdate; reduced motion
never leaves a route waiting for an animation that was removed.

### 11. Evaluation occurs once in VariableUpdate

At the start of Runtime UI VariableUpdate, the service freezes one
`UiClockSnapshot` containing every admitted domain sample and source revision. It
then:

1. applies owner-thread committed clock/policy/timeline commands;
2. reconciles input/state and lifecycle evidence;
3. evaluates each active timeline once in stable runtime instance/animation order;
4. emits typed UI-local completion/cancellation evidence and computed property
   values;
5. resolves styles/layout/focus and publishes one immutable generation.

Crossed markers/completions are enumerated over the directed old-to-new interval in
stable authored order and bounded by policy. Required work exceeding the marker/
iteration budget fails the candidate rather than partially advancing and dropping
the remainder. UI-local evidence may participate in the same bounded UI transaction;
gameplay/route/asset/renderer mutations remain typed commands for their owners.

RenderExtraction and RenderExecution consume the published values only. They never
sample time, interpolate an authoritative target, fire markers or complete a screen.

### 12. Timeline lifecycle and cancellation are explicit

Each animation instance follows:

```text
Created -> Waiting -> Running <-> Held -> Completed -> Retiring -> Destroyed
                       |             |
                       +-> Cancelled -+
```

Completion and cancellation are terminal, mutually exclusive and published at most
once for one instance generation. Cancellation reasons include explicit owner
command, element removal, target/property incompatibility, screen replacement/
retirement, scope/viewport destruction, style/document reload policy, dependency
failure, lifecycle deadline, host shutdown and accessibility replacement.

Cancellation releases captures/leases owned by the animation but does not invoke
arbitrary callbacks or directly mutate gameplay/route/renderer state. Typed evidence
names the expected owner/target/timeline revisions; stale consumers reject it.

Reload may preserve an instance only when stable animation/property/target IDs,
domain policy, normalized duration/rate and interpolation compatibility all match.
Otherwise the descriptor's explicit restart, snap-to-new, complete-old or cancel
policy applies transactionally. No heuristic cursor carryover is allowed.

### 13. Errors, limits and compatibility are typed

Errors follow ADR-008 with stable reason codes for unknown/unavailable domain,
invalid duration/rate/loop/lifecycle policy, numeric overflow, clock regression/
discontinuity, stale generation/command, non-reversible operation, target mismatch,
marker/iteration/deadline/capacity exhaustion, cancellation, incompatible reload and
unsupported composition. Context contains bounded clock/timeline/element/screen/
scope/source revisions without user text or native timestamps.

Limits cover timelines/animations, duration/rate, loops, markers crossed per update,
commands, preview/test steps, retained generations, lifecycle blockers, work per
frame and diagnostics. Required overflow/backlog returns failure/backpressure; it
does not silently skip time or drop a completion.

Cooked style/UI animation descriptors use versioned domain/policy enums, explicit
integer widths/endianness and semantic fingerprints. Unknown required domains or
policies reject and request recook. Runtime profiles declare supported domains;
packaged runtime cannot silently map Preview/Test to Presentation, and headless
ModelOnly does not invent a presentation clock unless the host provides one.

### 14. Verification is part of the contract

Required coverage includes:

- each domain's source, generation, integer/rational accumulation, carried remainder
  and overflow/regression behavior;
- zero/one/multiple fixed ticks, simulation-rate changes, composed gameplay pause,
  successful/failed single-step and no double scaling;
- variable frame cadence, clamped/discontinuous presentation evidence, focus policy,
  suspension and zero-delta resume;
- enter/active/exit transition gates, required/optional blockers, replacement,
  deadline and owner destruction;
- preview play/pause/step/seek/scrub/close isolation and deterministic test step
  histories in headless mode;
- manual stale/duplicate/out-of-order/late commands and safe-point publication;
- delay/duration/rate/direction/loop/fill, zero duration, stable crossed markers,
  per-frame budget and no partial advance;
- reduced-motion zero/simplified/repetition policy with route completion;
- reload preserve/restart/snap/complete/cancel policies and old snapshot leases;
- element/screen/scope/viewport removal, dependency failure, shutdown and exactly-
  once terminal evidence;
- identical published values at 30/60/144 Hz where source histories are equivalent,
  repeated/multi-view rendering and no Renderer/platform/editor-native clock use;
- malformed/version-skewed descriptors, unsupported domains, capacity pressure and
  bounded/redacted diagnostics.

Golden/property tests compare integer cursors, marker/completion evidence and
immutable UI generations. Screenshot/video timing may qualify presentation later,
but cannot replace semantic clock tests.

## Consequences

Menus remain responsive during gameplay pause, gameplay-bound UI follows committed
ticks, route transitions cannot outlive their screen, preview/test clocks are
isolated, and render cadence/backend choice cannot double-advance animation. Pause,
time scale, lifecycle and accessibility policy are explicit independent inputs.

The cost is domain-specific clock generations, checked integer/rational accumulation,
carried remainders, timeline lifecycle/cancellation, screen blockers/deadlines and
versioned policy data. Callers cannot read raw wall/variable delta opportunistically.

## Rejected Alternatives

### Use one global variable-delta UI clock

Rejected because gameplay-bound, transition, preview and deterministic-test motion
have different pause/lifecycle/input contracts and would drift with frame cadence.

### Let every animation call a platform monotonic clock

Rejected because sampling becomes inconsistent within a frame, suspension leaks
wall time and headless tests lose determinism. The host supplies one typed sample.

### Reuse the skeletal animation or sequencer clock owner

Rejected because UI style/property state has different scope, lifecycle and side-
effect rules. The domains share host evidence but retain separate authorities.

### Advance UI animation during RenderExecution

Rejected because multiple views/retries would double advance and input/layout would
observe different values. Evaluation occurs once in VariableUpdate.

### Encode gameplay pause as time scale zero

Rejected because pause ownership/tokens, simulation rate and UI domain policy are
distinct. Simulation holds due to no committed ticks; presentation UI can continue.

### Catch up suspended wall time on resume

Rejected because it would skip transitions, overflow marker budgets and target dead
screens. Resume creates a zero-delta baseline.

### Keep screen transitions alive after route replacement

Rejected because completion could target reused state and unbounded exit motion
could block lifecycle. Timeline identity is tied to the exact route generation.

### Store preview cursor or native timestamps in runtime assets

Rejected because transient editor/host state is not portable semantic content.
Assets store typed policies and durations only.
