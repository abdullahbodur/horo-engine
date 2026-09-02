# ADR-117: Playback Ownership, Frame Order and Determinism

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: Sequence-player instance ownership, scene/application lifetime scopes, activation identity, same-boundary multi-player order, replay/headless guarantees, numeric determinism and random-access seek
- **Issue**: [CIN-002.1](https://github.com/abdullahbodur/horo-engine/issues/1698)
- **Jira**: [HORO-1657](https://horo-engine.atlassian.net/browse/HORO-1657)
- **Related**: [ADR-010](010-job-waiting-and-operation-store-ownership.md), [ADR-014](014-sequencer-ownership-clock-authority-and-binding-boundary.md), [ADR-061](061-animation-ownership-update-order-and-clock.md), [ADR-077](077-runtime-ui-animation-clock-and-time-domain.md), [ADR-088](088-physics-determinism-capability-and-support-tiers.md)
- **Normative documents**: [Cinematic Sequencer Architecture](../architecture/runtime/cinematic-sequencer-architecture.md), [Runtime Lifecycle](../architecture/runtime/runtime-lifecycle.md)

## Context

ADR-014 assigns clocks, bindings and bounded evaluation to Cinematic Runtime and
separates fixed-tick authoritative output from presentation sampling. The architecture
also states that aggregate evaluation visits players in priority/stable-ID order and
that value sampling is history-independent. Those constraints do not yet decide who
owns a live player, how scene-authored and application-triggered instances differ,
where activation IDs come from, which commands join a frame/tick batch, or exactly
what “deterministic” means across builds and platforms.

Storing a full player inside a scene component would couple clocks, provider requests,
pause/camera tokens, nested players and event cursors to component relocation and scene
mutation. A process-global player service would survive beyond the runtime session and
risk retaining scene bindings or external callbacks through travel and shutdown.
Assigning IDs by allocation order or visiting an unordered container would make
overlapping outputs depend on thread scheduling.

Timeline position uses checked rational/fixed-point representation, but curve values
and downstream property adapters use floating-point math. Requiring bit-identical
results across all CPUs, compilers and platforms would therefore be untruthful without
a separately qualified deterministic-math contract. At the same time, replay and
headless tests need exact rules for input capture, ordering, event identity and numeric
comparison.

This ADR refines ADR-014 without changing its clock or domain-authority decisions.

## Decision

### 1. The runtime session service owns every live player

`CinematicRuntimeService` is an application/session-owned service. It owns the player
registry, player state, clock, event cursor, binding cache, nested-player tree,
capacity reservation, asset/provider leases, domain tokens and asynchronous request
lifetimes. It is created after the runtime session and required scene services are
available and closes before those dependencies are destroyed.

Scene components do not contain or own `SequencePlayer` objects. An optional
`SequencePlaybackComponent` is inert authored configuration and a lifecycle trigger:
asset identity, stable authored activation identity, clock/settings, priority,
autoplay policy and declared lifetime scope. Runtime conversion submits a typed start
request and retains only a generation-checked handle for observation/control.
Component removal releases its request/observation relationship; the service performs
the actual stop and token/lease retirement at an owning boundary.

Application/gameplay code uses the same start/stop/seek API for non-component players.
No caller receives a mutable player reference or stores a callback into component
memory.

```cpp
struct SequencePlayerHandle {
    RuntimeSessionId session;
    SequencePlayerId id;
    uint32_t generation;
};

enum class SequencePlayerScope : uint8_t {
    SceneBound,
    ApplicationBound
};
```

A `SceneBound` player is fenced by the source `SceneRuntimeId` and stops before scene
replacement publishes. An `ApplicationBound` player may survive scene travel only
when its descriptor declares no required old-scene binding and all retained adapters
support travel; otherwise activation fails or the player stops with a typed
`SceneScopeEnded` result. Neither scope outlives the runtime session. Editor preview
uses a separate preview session/registry and cannot share players, tokens or binding
caches with PIE or packaged runtime.

### 2. Player lifetime is a generation-fenced state machine

The service owns these conceptual states:

```text
Requested -> Preparing -> Ready -> Playing <-> Held
     |           |          |         |
     +-----------+----------+---------+-> Stopping -> Stopped
                              \-------> Failed
all non-terminal states ----------------> Closing -> Stopped
```

`Stopping` is the orderly, player-requested path: it closes future evaluation and
drains already admitted occurrences according to the compiled stop policy before
publishing `Stopped`. `Closing` is the host/owner disposal path for cancellation,
scene/session loss or shutdown; it closes all admission immediately, cancels what
can be cancelled, retires outstanding leases, and cannot return to playback.

Preparation validates the asset and nested graph, compiles the evaluation plan,
reserves aggregate capacity, resolves required adapters/bindings and acquires tokens
in a rollback-safe order. Only a completely prepared player becomes visible in an
evaluation batch. Failure unwinds acquired resources and publishes one sticky result.

Stop closes new evaluation/effect admission, removes the player from a later batch,
releases only its own pause/camera/domain tokens, cancels owned provider/jobs where
possible and retains assets/payloads until queued occurrences and callbacks retire.
Late completions carry session/player generation and are ignored after retirement.
Destroying a component, closing a UI, dropping a handle or losing a requester does not
free a live player synchronously.

Nested sequence players are registry-owned children with a stable parent/track/key
path, not heap-owned objects hidden in a track. Parent stop closes child admission
depth-first, while lease destruction occurs only after already committed child effects
retire. A child cannot select an independent session, budget pool or lifetime scope
beyond its root.

### 3. Activation identity is supplied before scheduling

Every root start request contains a stable `SequenceActivationId` from the initiating
authority:

- a scene-authored component derives it from `SceneRuntimeId`, stable authored object
  identity and component-local playback slot;
- a deterministic gameplay/event request uses its recorded command/event occurrence
  identity; and
- a tool/manual start receives an application-issued operation identity that must be
  recorded when replay of that session is required.

The registry maps the activation identity to a generation-checked `SequencePlayerId`.
Duplicate active activation IDs return `AlreadyActive` or target the declared
idempotent operation; they never allocate a second player implicitly. Dynamic ID
allocation order, pointer value, hash seed, worker completion order, thread arrival
order and container iteration are not semantic identity.

A nested player's identity is the root activation ID plus the ordered stable
sub-sequence track/key path and loop/instance ordinal. Cook/activation rejects path or
identity collisions rather than using insertion order as a tie-breaker.

### 4. Each boundary evaluates one immutable ordered batch

Before a fixed-tick or presentation/service boundary, the owner drains commands up to
the boundary cutoff, validates them, publishes player state changes, snapshots the
eligible registry and constructs one immutable `SequenceEvaluationBatch`. Commands
arriving after the cutoff apply no earlier than the next corresponding boundary.
Workers may prepare bounded read-only samples, but they cannot add/remove players,
publish values or dispatch events into the current batch.

The root order key is:

1. evaluation seam/domain;
2. declared priority, highest first;
3. `SequencePlayerId`, ascending lexicographic order of its opaque canonical bytes; and
4. player generation, ascending, only to distinguish retained diagnostic/replay data.

Nested players immediately follow their parent position ordered by stable track ID,
key ID, nested instance ordinal and recursion path. Within a player, ADR-014's compiled
dependency DAG and track/key order applies. The same order is used in packaged,
editor-play, deterministic replay and headless execution.

Ordering evaluation does not by itself authorize last-writer-wins. All player
contributions retain their order key and target owner. For an exclusive property, the
owner accepts the highest-priority candidate and uses lowest stable player ID only as
the declared equal-priority tie-break. A registered blend/reducer consumes inputs in
the canonical order and defines its own numeric rule. Conflicting incompatible modes
return a typed activation/evaluation error rather than relying on visit order.

Global event occurrence order is destination seam, directed crossing time,
root/nested player order, track ID and keyframe ID. Irreversible effects remain staged
until tick commit as ADR-014 requires. Parallel evaluation merges only by preassigned
order slots; job completion time cannot affect values, errors or event order.

### 5. Attempted ticks publish all-or-none cinematic state

For `CommittedSimulation`, the batch captures the exact attempted tick ID, unchanged
fixed quantum, input/command cutoff and player set. Each player stages next cursor,
loop/traversal identity, values and occurrences. Budget, binding, authority or
evaluation failure holds/fails the affected player according to its typed policy
without publishing a partial interval. A failed overall simulation tick discards all
staged cinematic state and occurrences for that attempt.

After the owning simulation participants succeed, tick commit atomically advances the
accepted player cursors and releases committed occurrences to bounded destination
queues. Optional presentation evaluation reads the last committed snapshot and cannot
advance authoritative cursors or repair a skipped fixed batch. Zero, one or multiple
fixed ticks in a rendered frame therefore produce the same semantic history as the
same ticks spread across different render cadences.

UnscaledFixedControl, MonotonicWall and External players use their ADR-014 service/
presentation boundaries. Their batch ordering remains deterministic for supplied
inputs, but elapsed wall time and live provider arrival are not reproducible inputs by
themselves.

### 6. Determinism is defined by evidence tier

The baseline guarantees:

| Evidence | Requirement |
|---|---|
| Identical execution fingerprint and recorded inputs | Exact player/batch/event identities, state transitions and ordering; bit-identical `SequenceTime`; floating outputs repeat under the same qualified compiler/CPU/FP configuration |
| Different supported platforms/build fingerprints | Same admitted graph, value/event topology and stable order; floating values compare under documented absolute/relative/ULP tolerances, not bit identity |
| Live wall/external source without recording | No replay guarantee; only bounded monotonic/discontinuity behavior from ADR-014 |

`SequenceDeterminismFingerprint` records engine/cinematic schema revision, cooked asset
digests, evaluation-policy revision, build/compiler/CPU architecture, floating-point
mode and relevant adapter/provider versions. It contains no pointer, wall timestamp or
process-random value. A mismatch is visible to the replay/test harness and cannot be
silently described as exact replay.

Cross-platform bit-level floating-point determinism is explicitly **not required** for
the baseline. Curve interpolation, quaternion/transform operations and downstream
owner adapters use floating point and may differ across instruction sets, contraction,
library implementations and compiler modes. Sequence time, IDs, ordering, interval
membership and event occurrences remain exact; sampled floats use type/track-specific
tolerances. Products that need bit-identical cross-platform results require a separate
fixed/deterministic math profile and qualification of every receiving subsystem.

Within one fingerprint, builds disable nondeterministic fast-math/FP-environment drift
for qualified replay tests and use a specified interpolation operation order. A
floating mismatch outside tolerance, ordering mismatch, missing event, extra event or
different terminal outcome is a failure; tests do not retry until one result passes.

### 7. Replay records every nondeterministic input

A replayable sequence session records or derives:

- runtime/scene session and activation identities;
- exact ordered start/stop/pause/rate/seek commands with boundary/tick admission IDs;
- cooked asset/revision and determinism fingerprint;
- committed simulation ticks or unscaled-control quanta;
- monotonic-wall/external provider samples, epochs, acknowledgements and
  discontinuities when those sources are replayed;
- binding/authority availability revisions and typed failures that can change output;
- asynchronous effect admission/completion outcomes when their timing is observed; and
- product policy, budget profile and declared adapter versions.

Replay feeds these records through the ordinary command/batch/service boundary. It
does not patch player internals, reuse a captured render frame or invoke event effects
from a log. Missing required evidence returns `ReplayEvidenceIncomplete`.

Headless tests compose the same Cinematic Runtime service, Null/recording destination
adapters and a manual recorded clock. They create no hidden window/renderer and do not
substitute a bespoke evaluator. Golden tests compare exact identities/order/events and
typed terminal results plus tolerance-based numeric samples according to the recorded
fingerprint.

### 8. Seek is random-access for values and explicit for occurrences

`Seek(target)` never advances value evaluation incrementally from the current cursor
or replays from sequence start. It validates target/loop mapping, resolves nested local
time, performs bounded indexed key lookup and samples every affected value directly at
the target. The result depends on immutable asset/settings/bindings and target time,
not prior sample count, frame cadence, direction changes or seek path.

The service stages the complete seek result, preflights binding/capacity and publishes
all accepted player cursor/value changes at one owner boundary. Failure leaves the old
cursor and values. Nested mapping is a pure checked time transform; cycles and
unbounded depth were rejected at activation.

Events remain interval state, not value samples. Default seek resets cursor,
traversal/loop identity and presentation/provider baseline without emitting skipped
occurrences. Explicit `DispatchCrossedEvents` uses one bounded directed interval and
ordinary authority/budget/commit rules; it is not replay-from-zero. Audio, VFX and
other stateful destinations receive typed seek/resynchronize intent or `Unsupported` and
never reconstruct state by executing historical side effects.

### 9. Qualification covers ordering, lifetime and numeric scope

Required evidence includes:

- component-authored and application-started players sharing one service registry,
  with component relocation/removal, dropped handles and UI close unable to free or
  retain live state incorrectly;
- SceneBound/ApplicationBound travel, preview/PIE isolation, stop/failure/cancel/
  shutdown, nested retirement and late provider/job completion generation fencing;
- activation-ID duplicate/collision behavior and randomized map/allocation/worker
  completion order producing the same batch/output order;
- priority and stable-ID ties, nested order, exclusive-owner selection, canonical
  blends and global event occurrence order across serial/parallel evaluation;
- zero/one/many fixed ticks at 30/60/144 Hz presentation, failed tick discard and
  identical committed history independent of render cadence;
- same-fingerprint repeat runs with exact IDs/time/order/events and bit-repeatable
  floats; cross-platform fixtures using documented tolerances and explicit fingerprint
  mismatch reporting;
- recorded wall/external inputs, missing evidence, provider discontinuity and
  asynchronous effect outcomes in ordinary headless service composition; and
- forward/reverse/loop/ping-pong/random seek sequences proving direct target sampling,
  default silent occurrence reset, bounded explicit crossings and failure atomicity.

## Consequences

### Positive

- Scene authoring can trigger playback without making relocatable components lifetime
  authorities.
- Same-boundary results no longer depend on allocation, container or worker order.
- Replay and headless tests share the production service and state precise numeric
  expectations.
- Random seek remains bounded and independent of playback history.

### Costs

- The runtime service needs a generation registry, boundary command queue, immutable
  evaluation batches and retained-effect retirement.
- Deterministic replay requires explicit fingerprints and capture of wall/provider/
  asynchronous inputs.
- Cross-platform tests need semantic tolerances unless a future deterministic-math
  tier is qualified.

## Rejected Alternatives

### Store the live player in a scene component

Rejected because component relocation/removal would own clocks, tokens, nested
players, providers and queued effects whose lifetimes cross scene mutation boundaries.

### Use a process-global player manager

Rejected because players must not survive their application/runtime session or retain
scene/provider resources through shutdown and host replacement.

### Use update or allocation order as the tie-breaker

Rejected because container layout, thread arrival and worker completion are not stable
semantic inputs. Priority and preassigned stable identity define the order.

### Require cross-platform bit-identical floating results now

Rejected because the current interpolation and receiving systems use floating point
without a closed deterministic-math profile across compilers, CPUs and libraries.
Exact time/order/events plus documented numeric tolerances are truthful and testable.

### Replay from zero to implement seek

Rejected because work and side effects would depend on timeline length and prior
history. Indexed target sampling and explicit bounded event crossing define separate
contracts.
