# ADR-061: Animation Ownership, Update Order and Clock

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: Runtime pose ownership, animation clock domains, fixed-tick ordering, and physics/render handoff
- **Issue**: [ANI-001.1](https://github.com/abdullahbodur/horo-engine/issues/454)
- **Jira**: [HORO-454](https://horo-engine.atlassian.net/browse/HORO-454)
- **Parent**: [ANI-001](https://github.com/abdullahbodur/horo-engine/issues/447)
- **Related**: [ADR-014](014-sequencer-ownership-clock-authority-and-binding-boundary.md), [ADR-077](077-runtime-ui-animation-clock-and-time-domain.md), [ADR-089](089-character-controller-ownership-implementation-and-update-order.md), [ADR-091](091-footstep-and-locomotion-event-ownership.md)
- **Normative documents**: [Animation Architecture](../architecture/runtime/animation-architecture.md), [Runtime Lifecycle](../architecture/runtime/runtime-lifecycle.md), [Character Controller Architecture](../architecture/runtime/character-controller-architecture.md)

## Context

The existing Animation Architecture says that a non-AI animation pose is sampled
once per rendered frame before zero or more fixed physics steps. Runtime Lifecycle,
however, runs all fixed ticks before variable update and render extraction. The
render-rate path can therefore feed one root-motion delta into several simulation
ticks, skip authoritative advancement when no frame is presented, or produce
different movement and event results at different render cadences.

Pose ownership is also incomplete. Animation, IK, physics overrides, character
movement, editor preview, and rendering are described as sequential consumers,
but the documents do not define who owns mutable pose storage, when physics may
replace joints, or how a frame snapshot remains valid. Pause, single-step, time
scaling, reverse playback, and large traversal intervals need one clock contract
before clip, graph, pose-pool, root-motion, and physics integration tickets can
implement compatible behavior.

This decision covers the authoritative non-AI path. ADR-022 continues to own its
AI phase graph. ADR-014 continues to own cinematic player clocks and bindings,
but any cinematic input that affects animation, movement, or physics enters the
owner seams defined here.

## Decision

### 1. Authority and ownership

This ADR is the single normative owner of skeletal/pose animation clock domains,
authoritative update order, pose mutation/publication, root-motion timing, and
animation/physics/render handoff. ADR-077 separately owns Runtime UI style/property
motion and cannot mutate skeletal animation state. Animation Architecture
summarizes this contract; Runtime Lifecycle owns host phases and fixed-tick
production; Character Controller Architecture owns
collision-aware movement resolution. None defines a second animation clock or
pose owner.

| State or operation | Owner | Boundary |
|---|---|---|
| Skeleton, clip, graph, retarget, and compression artifacts | Animation model plus Asset Pipeline publication | Immutable, versioned assets; no live pose or instance identity |
| Graph instance, player cursors, transition/event state, and pose working memory | Animation runtime | One scene/runtime generation; mutation only through admitted animation stages |
| Mutable local/model pose buffers and palette preparation | Animation runtime | Never shared for external mutation; allocated from bounded instance/frame storage |
| Gameplay parameters and movement intent | Gameplay owner | Typed per-tick snapshot committed before animation evaluation |
| Character collision-root transform and movement result | Scene-owned Horo `CharacterWorld` | Physics supplies query/platform evidence; root motion is a request and Animation never writes the authoritative root |
| Ragdoll/body transforms | Physics runtime | Published as a typed post-physics override; physics never writes animation storage |
| Render pose and joint palette view | Render extraction snapshot | Immutable, frame-owned projection of committed pose state |
| Timeline preview pose | Editor preview runtime | Separate instance/storage; cannot mutate play/runtime animation state |

The exact IDs and handle layouts belong to ANI-001.2. They must distinguish
persistent asset identity, scene animation-instance identity, committed pose
generation, and frame-local presentation views. A pose handle is process-local,
generation-checked, non-serializable identity. Copying it does not own or extend
the pose lifetime. External systems receive immutable snapshots or leases, never
mutable arrays or cached pointers into a recyclable pose pool.

### 2. Evaluation domains and clocks

Skeletal/pose animation has three explicit evaluation domains:

```cpp
enum class AnimationEvaluationDomain : std::uint8_t {
    AuthoritativeSimulation,
    Presentation,
    EditorPreview,
};
```

`AuthoritativeSimulation` advances exactly once for every attempted fixed tick
in which its scene participates. It consumes `FixedStepContext::fixedDelta`, the
committed parameter/input snapshot, immutable asset revisions, and versioned
per-instance settings. Its state, root motion, and events are staged until that
tick commits. It never reads render delta, interpolation alpha, presentation
timestamps, audio-device time, or wall time.

`Presentation` does not advance an authoritative cursor. It reads the previous
and current committed pose generations plus interpolation alpha, then may apply
explicit presentation-only overlays. It cannot emit gameplay events, produce
root motion, update colliders, write animation parameters, or feed a later fixed
tick. Rendering the same committed tick more than once does not advance animation.

`EditorPreview` owns an isolated preview instance. Play, pause, manual step, seek,
scrub, and optional clamped monotonic preview playback affect only that instance.
Seek/scrub samples values without gameplay callbacks, root motion, physics writes,
or event replay. Host suspension freezes preview advancement and resume establishes
a zero-delta baseline. Editor preview cannot borrow mutable play-session pose or
player state.

### 3. Simulation rate, player rate, pause, and reverse

Runtime owns a positive bounded rational `simulationRate`. It scales the clamped
elapsed contribution to the fixed-step accumulator; the fixed quantum supplied to
physics and animation remains unchanged. The baseline rate is `1/1`. Rate changes
commit at the owner-thread command boundary before fixed-step scheduling, preserve
their fractional remainder, carry a revision for replay evidence, and cannot be
negative. Animation and physics never multiply the host rate into `fixedDelta`
again.

Gameplay pause is a separate composed host state, not `simulationRate == 0`.
While paused, no fixed tick is attempted, so authoritative animation holds its
cursor, pose, transition timers, events, and root motion. Single-step attempts
exactly one ordinary fixed tick at the unchanged quantum, runs the complete
animation/controller/physics order, publishes one committed pose on success, and
returns to paused state. A failed step publishes no animation state or events.

Each animation player has a finite, bounded, tick-boundary `playbackRate` stored
as a checked fixed-point/rational value. Positive values advance, zero holds that
player, and negative values traverse its animation data in reverse without
reversing simulation or physics. Rate multiplication and cursor accumulation
preserve a per-instance remainder so equivalent tick/settings histories do not
drift because of render cadence or repeated float rounding.

The runtime instance owns these exact remainders alongside its cursors and graph
state. Candidate cursor and remainder updates are staged and committed together;
a failed tick cannot publish one without the other. Asset durations are intervals
(`AnimationDeltaTime`), while cursor and event positions are absolute clip-local
`AnimationTime` values. Serialized examples use normalized rational values rather
than binary floating-point seconds; ANI-001.6 owns the final widths and overflow
rules.

Reverse traversal is permitted only for clips/nodes whose contract supports it.
It samples the directed interval from the old cursor to the new cursor, emits only
events explicitly eligible for reverse, and extracts the inverse directed root
delta when root motion is enabled. Non-reversible procedural/external nodes return
a typed unsupported result before instance state changes. Reverse animation never
rewinds the physics world, undoes gameplay, or replays irreversible side effects.

### 4. Large intervals and deterministic inputs

Raw frame stalls never become one authoritative animation delta. Runtime's
clamped accumulator and catch-up limit produce zero or more ordinary fixed ticks;
animation evaluates every attempted tick separately. Whole intervals dropped by
the host are not animation intervals and do not generate cursor jumps, root motion,
or events. A render frame cannot reuse one authoritative evaluation across several
catch-up ticks.

A high player rate, seek-capable tool, or loop/ping-pong traversal can still cross
multiple segments in one evaluation. Before mutation, the animation runtime
preflights bounded limits for node visits, loop/turn crossings, event occurrences,
root-motion segments, pose work, and output bytes. Over-budget work returns a typed
capacity result and leaves the player, pose, event cursor, and root-motion output
unchanged. It never partially advances and silently drops the remainder. Preview
tools may issue an explicit silent seek after reporting the clamp; authoritative
simulation may not.

Within the declared platform/numeric contract, authoritative animation is
reproducible from:

- exact engine, animation schema, cooked asset, graph, skeleton, and retarget revisions;
- initial graph/player state and stable random seeds;
- ordered fixed-tick parameter/input commands and owner-admitted cinematic intents;
- fixed quantum, simulation-rate revision history, and player-rate changes;
- root-motion consumption policy and typed post-physics override inputs.

Wall time, render cadence, interpolation count, worker completion order, GUI state,
and pointer/container iteration order are not deterministic inputs. Cross-platform
bit identity is not claimed unless a narrower numeric qualification says so.

### 5. Fixed-tick update order

The authoritative non-AI path is a validated dependency order inside the existing
`FixedUpdate`; it does not add host phases:

```text
Apply queued owner-thread commands before FixedUpdate
For each attempted fixed tick:
  1. Input/gameplay/AI/Nav/cinematic owners stage animation parameters, movement
     intent, desired heading, stance/jump, and kinematic-platform targets
  2. Animation pre-physics evaluation stages graph state, pose, eligible IK,
     events, and the exact directed root-motion delta
  3. Physics freezes the exact Character query/support/platform-motion evidence
  4. Character applies platform carry, admits desired movement plus root motion,
     resolves collision, and stages its collision-root transform/Physics commands
  5. Physics applies staged commands and steps once at the fixed quantum
  6. Physics publishes body/contact/platform and typed pose-override results
  7. Character finalizes support/attachment state and its authoritative transform;
     it does not perform a second movement pass
  8. Animation post-physics composition applies admitted overrides and finalizes
     the candidate committed pose
  9. Tick commit publishes Physics, Character/transforms, animation instance state,
     previous/current pose generations, and bounded events atomically
After all catch-up ticks:
  10. Presentation interpolation/overlays build an immutable render pose
  11. Render extraction consumes that pose and its joint palette
```

Simulation-affecting IK that contributes to hit shapes or movement belongs in
step 2 and uses only admitted fixed-tick inputs/queries. Grounding polish or other
post-physics IK that is presentation-only may run in step 6 or presentation, but
cannot feed same-tick root motion, controller movement, or collider state. A graph
must declare which path a node uses; an impossible dependency cycle is rejected.

Animation stages its own mutable state until tick commit. Root-motion requests and
physics overrides carry scene, instance, tick, skeleton, and generation identity
so duplicates, stale results, and cross-instance use fail deterministically. If a
later participant fails after an owner has performed an irreversible in-place
mutation, the runtime follows its fatal/rollback contract; it must not commit an
animation cursor against an uncommitted simulation tick.

### 6. Root motion and transform authority

Root motion is the finite translation/rotation delta over the exact directed
player interval evaluated in step 2. Animation publishes a typed request; it does
not apply the delta to a scene transform, move a physics body, decide collision
policy, or claim that the requested delta was achieved.

Gameplay selects the admitted consumption mode and combines ordinary movement
intent with the request. The character controller owns collision-aware resolution
and the resulting kinematic transform. Ignoring or clipping root motion does not
rewind the animation cursor. The resolution may be observed on the next tick to
adjust locomotion state, but animation cannot resample the same interval after
physics merely to force visual distance to match.

[ADR-089](089-character-controller-ownership-implementation-and-update-order.md)
defines the exact translation modes and heading composition. There is no render-
frame accumulator: zero attempted ticks produce no request, catch-up produces one
distinct request per tick, and a failed tick discards the request with the staged
animation/controller state. Root pitch/roll remains visual; only the admitted twist
about Character's owned up basis can affect collision heading.

One `(scene, animation instance, simulation tick, root-motion generation)` request
can be committed as consumed at most once. Controller resolution records only a
candidate consumption marker in the current tick transaction. Abort discards that
marker and the attempt-scoped request lease; a retry of the same simulation tick
may consume the newly staged equivalent request. Successful tick commit makes the
marker durable, after which the same identity is a duplicate. A stale, duplicate-
after-commit, preview, presentation, or leaked aborted-attempt request is rejected.
Network replication owns input/state or resulting transform policy; raw
presentation time and process-local pose handles are never replication identity.

### 7. Physics override and render publication

Normal animation-driven evaluation owns the candidate local/model pose. Physics
may read an immutable pre-physics pose lease for ragdoll initialization, hit-shape
queries, or an explicitly declared kinematic target. It cannot retain the lease
past its tick or mutate its storage.

Ragdoll/procedural physics publishes a `PhysicsPoseOverride` after the physics
step. The override identifies the exact scene, animation instance, skeleton,
simulation tick, joint mapping, transforms, authority mode, and finite blend
weights. Animation validates it and composes the final committed pose. Authority
is explicit per instance/joint set:

- `AnimationDriven`: physics supplies no pose write;
- `PhysicsDriven`: admitted physics joints are authoritative;
- `Blended`: the animation-owned composition applies declared finite weights.

Mode changes occur at a fixed-tick safe point and initialize both sides from the
same committed pose. Two systems never write the same pose buffer. Missing,
stale, non-finite, incompatible-skeleton, or partial-required overrides fail with
a typed result; they are not applied by joint name guessing.

After tick commit, Animation Runtime retains previous/current committed poses.
Presentation creates a frame-owned immutable pose/palette projection with source
scene, instance, tick pair, pose generations, and interpolation alpha. Render
extraction consumes that projection only. Renderer work does not retain pointers
into animation frame storage beyond the snapshot/lease contract.

### 8. Concurrency, reload, and shutdown

Animation instance state and publication mutate on the declared scene/runtime
owner thread. Independent graph/pose work may execute in bounded jobs only when
the compiled access plan proves no conflicting writes. Jobs capture immutable
assets, owned inputs, stable identities, and generation-safe leases; they join
before their fixed-tick stage completes. The frame thread never waits on unbounded
background I/O or source import.

Reload stages new immutable artifacts and instance migration privately. Publication
occurs at the owner safe point after compatibility validation. Compatible state is
mapped by stable typed IDs, never array position or display name. Failure retains
the last good generation. Old pose generations retire after all frame/physics/
render leases release; a late job cannot publish into a replaced scene or instance.

Scene unload and shutdown stop new evaluations, cancel or join owned preparation,
discard uncommitted candidates, drain/revoke leases in dependency order, then
release animation storage before its assets and job/runtime dependencies disappear.
Repeated shutdown is safe. Preview teardown cannot affect a play instance.

### 9. Migration and verification

The current render-frame-authoritative prose is removed rather than supported as
a compatibility mode. Migration proceeds by responsibility:

1. ANI-001.2 defines distinct typed asset, instance, pose, tick, and request IDs.
2. ANI-001.3/.4 provide validated skeleton and mesh-binding inputs.
3. ANI-001.5 implements bounded pose storage, generation-safe leases, and
   previous/current publication.
4. ANI-001.6 implements fixed-point directed clip traversal, wrap/reverse rules,
   and bounded interval preflight.
5. ANI-001.7/.8 preserve this clock and ownership contract through cook artifacts.
6. ANI-001.9 qualifies malformed, skewed, reload, and lifecycle behavior.

These responsibility statements do not infer scheduling from issue numbers or
milestones; native parent/sub-issue and blocked-by relationships remain authoritative.
Until those implementations land, the architecture is a target contract and does
not claim a working skeletal animation runtime.

Required contract coverage includes:

- identical authoritative results under different render cadences and catch-up grouping;
- pause, composed pause tokens, one-tick step, host suspension, and zero-delta resume;
- simulation-rate changes without changing the fixed quantum or double scaling;
- positive, zero, and reverse player rates across Once/Loop/PingPong boundaries;
- bounded high-rate/large-interval rejection without partial cursor/event/root output;
- root-motion ignore, collision clipping, duplicate/stale request, and failed-tick paths;
- animation-driven, physics-driven, and blended pose authority transitions;
- stale pose leases, preview/play isolation, reload failure, scene replacement, and repeated shutdown;
- headless fixed-tick execution with no renderer or editor clock dependency.

## Consequences

Authoritative animation, movement, physics, and events now share the fixed-tick
history, so changing render cadence cannot change their semantic order. Pause and
single-step have one meaning, reverse playback does not pretend to reverse the
world, and large traversals fail atomically instead of dropping hidden work. Pose
storage has one mutable owner while physics and rendering use typed handoffs.

The cost is separate simulation/presentation/preview state, previous/current pose
retention, fixed-point cursor bookkeeping, staged tick commit, explicit physics
override data, and bounded traversal preflight. Catch-up frames may evaluate
animation multiple times, so animation budgets and LOD policies must be measured
against maximum admitted fixed ticks rather than average render frames.

## Rejected Alternatives

### Evaluate authoritative animation once per rendered frame

Rejected because zero or multiple fixed ticks may occur in one frame. Reusing one
pose/root delta or sampling before the tick loop makes movement, events, and
physics inputs depend on render cadence.

### Let physics or the renderer own the animation clock

Rejected because physics owns integration and renderer owns presentation, not
animation player state. The host fixed scheduler supplies authoritative ticks;
presentation only interpolates committed results.

### Use raw variable delta and clamp inside each animation player

Rejected because independent clamps create different time histories, silently
drop crossings, and do not align root motion with committed physics ticks.

### Apply root motion directly from animation

Rejected because it bypasses gameplay policy, controller collision, transform
authority, networking, and failed-tick handling.

### Share one mutable pose buffer with physics and rendering

Rejected because write ownership and lifetime become timing-dependent. Typed
immutable leases and post-physics overrides preserve owner and generation checks.

### Support negative global simulation time for reverse playback

Rejected because reversing animation data does not undo physics, gameplay side
effects, networking, or asset/runtime lifecycle. Reverse is a bounded player-local
traversal with explicit event and root-motion policy.

### Allow partial advance when an interval exceeds budget

Rejected because pose, root motion, and event cursors would describe different
fractions of the tick. Preflight and atomic failure keep them coherent.
