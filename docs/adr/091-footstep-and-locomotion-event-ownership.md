# ADR-091: Footstep and Locomotion Event Ownership

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: Animation footstep timing, Character locomotion facts and surface evidence, committed-tick correlation, presentation routing, duplicate prevention, missing-marker/consumer behavior, lifecycle, limits and qualification
- **Issue**: [CHR-004.3](https://github.com/abdullahbodur/horo-engine/issues/964)
- **Jira**: [HORO-964](https://horo-engine.atlassian.net/browse/HORO-964)
- **Parent**: [CHR-004](https://github.com/abdullahbodur/horo-engine/issues/933)
- **Related**: [ADR-008](008-error-model-exception-boundary-and-registry.md), [ADR-061](061-animation-ownership-update-order-and-clock.md), [ADR-062](062-audio-runtime-ownership-and-update-order.md), [ADR-068](068-music-transport-and-cross-system-ownership.md), [ADR-089](089-character-controller-ownership-implementation-and-update-order.md)
- **Normative documents**: [Character Controller Architecture](../architecture/runtime/character-controller-architecture.md), [Animation Architecture](../architecture/runtime/animation-architecture.md), [Audio Architecture](../architecture/runtime/audio-architecture.md), [VFX And Particles Architecture](../architecture/runtime/vfx-and-particles-architecture.md)

## Context

Character Controller Architecture currently lists `Footstep` beside physical state
transitions and then says Character surface events drive Audio/VFX directly.
Animation Architecture independently defines clip `Footstep` markers. Both can emit
for one planted foot, producing duplicate sounds/effects, while controller-derived
distance/timing would make Character own animation cadence it cannot observe.

The data becomes complete at different stages. Animation crosses a marker during
the exact fixed-tick clip interval before Physics. Character only finalizes grounded
support, contact position/normal and surface material after Physics. Audio and VFX
have their own admission, scheduling, lifetime and failure policy; neither may
become an authoritative simulation event bus.

The contract must preserve the marker occurrence and committed simulation tick,
join it to the correct Character surface evidence, suppress duplicates and remain
unchanged when Audio/VFX are omitted, unavailable, reloaded or shutting down.

## Decision

### 1. Timing, physical facts and presentation have different owners

| Responsibility | Owner |
|---|---|
| Clip marker definition, cursor traversal and exact marker occurrence time | Animation |
| Collision-root movement, grounding, support/contact and material evidence | Character, using Physics query/result evidence |
| Gameplay meaning, marker-to-controller binding and effect policy | Application/Gameplay `LocomotionPresentationAdapter` |
| Voice/asset choice, sample scheduling, mixing and device fallback | Audio |
| Effect asset, spawn admission, simulation domain and rendering | VFX |

Animation is the authoritative timing source for animation-driven footsteps.
Character never advances an animation cadence, infers a foot plant from speed or
distance, or emits a `Footstep` event. Physics never interprets marker names.

Character remains authoritative for physical locomotion facts such as `Landed`,
`LeftGround`, `HitWall`, `HitCeiling`, `SurfaceChanged`, `SlideStart` and
`SlideEnd`. These facts describe committed collision state; they are not animation
timing events and do not directly start Audio/VFX.

### 2. Animation publishes a typed committed occurrence

Cooked animation data maps authoring marker names to registered typed IDs. The
baseline footstep occurrence is:

```cpp
struct FootstepMarkerOccurrence {
    AnimationEventOccurrenceId occurrence;
    SceneRuntimeId scene;
    AnimationInstanceHandle animation;
    CharacterControllerHandle character;
    SimulationTick tick;
    AnimationClipRevision clipRevision;
    AnimationMarkerId marker;
    std::uint32_t traversalOrdinal;
    std::uint32_t loopOrdinal;
    FootContactSlot foot;
    AnimationTraversalDirection direction;
};
```

The occurrence is fixed-capacity value data. It contains no strings, mutable pose,
native animation state, sound/VFX asset, Physics handle or consumer callback.
`occurrence` is stable for the exact instance generation, clip revision, marker,
directed traversal ordinal and tick.

Animation stages occurrences during ADR-061 stage 2 but publishes them only with
successful tick commit. Failed ticks, presentation sampling, editor scrub and silent
seek publish none. Footstep markers are forward-only by default; reverse eligibility
must be explicit in cooked marker policy and preserves a distinct occurrence ID.

### 3. Character publishes physical facts, not foot cadence

At stage 7 Character finalizes a bounded immutable `CharacterLocomotionSnapshot`
for the same attempted tick. It includes controller/result generations, grounded
state, support body/material, achieved velocity/displacement and canonical contact
views. It also publishes typed state-transition facts with stable per-tick ordinals.

Character does not know which animation foot crossed a marker and does not emit one
footstep per contact callback, distance threshold, velocity cycle or render frame.
Repeated support contacts without a state transition produce no new `Landed` or
`SurfaceChanged` fact.

Physics supplies contact/material evidence through Character's existing bounded
query/post-step contract. Physics does not publish presentation-ready footsteps or
select audio/VFX assets.

### 4. Correlation occurs only after atomic tick commit

After ADR-089 stage 9 publishes Animation and Character state together, the
scene/application-owned `LocomotionPresentationAdapter` consumes immutable committed
occurrences and snapshots. It joins only values with exact scene, controller and
tick generations.

For a footstep marker, surface lookup occurs at this point from the committed
Character snapshot for `occurrence.tick`:

1. validate the Character binding/generation and exact tick;
2. require a valid grounded/support state under the locomotion presentation policy;
3. prefer an admitted foot-specific contact when the Character/IK contract provides
   one for the declared `FootContactSlot`;
4. otherwise use the canonical grounded support contact;
5. use Character's already-resolved non-null `SurfaceMaterialId` and committed
   contact point/normal;
6. emit at most one semantic presentation request for the occurrence.

The adapter does not issue a new Physics raycast. A late query could observe another
world/tick, add unbounded work and make presentation availability affect simulation.
If there is no valid same-tick grounded/support evidence, the default is to suppress
the footstep and record a bounded reason; it does not use the current frame's ground
or an unrelated default surface.

### 5. Missing markers do not cause inferred footsteps

If an animation clip/graph interval contains no admitted footstep marker, no
animation-driven footstep occurrence or presentation request exists. Character does
not synthesize timing from traveled distance, velocity, contact persistence or a
timer.

Procedural locomotion may use a future explicitly selected Gameplay-owned cadence
source that emits the same semantic occurrence contract with a distinct source kind
and identity. It must be mutually exclusive with Animation timing for a controller/
tick. CanonicalV1 does not silently enable such a fallback for missing asset markers.

Missing marker binding, invalid foot slot or marker capacity is an authoring/cook or
typed runtime error according to the asset policy, never a request for Character to
guess the missing cadence.

### 6. Duplicate prevention is identity based

`LocomotionPresentationAdapter` owns a fixed-capacity deduplication window keyed by
`AnimationEventOccurrenceId` plus scene/adapter generations. Re-reading the same
committed snapshot, repeated presentation frames, consumer retries or Audio/VFX
completion cannot create another semantic request.

One occurrence may fan out into one `FootstepPresentationRequest` containing both
Audio and VFX intents, but it remains one semantic request with one correlation ID.
Each consumer independently deduplicates/adopts that stable request identity at its
admission boundary. Mapping to multiple layered sounds/effects is an explicit
presentation asset policy, not duplicate locomotion emission.

Character locomotion facts have their own stable IDs and never share the marker
occurrence ID. An application may map `Landed` to impact Audio/VFX, but it cannot
also relabel that fact as the animation footstep occurrence.

### 7. Downstream requests are immutable and non-authoritative

The adapter emits a bounded value such as:

```cpp
struct FootstepPresentationRequest {
    LocomotionPresentationRequestId id;
    AnimationEventOccurrenceId sourceOccurrence;
    SceneRuntimeId scene;
    CharacterControllerHandle character;
    SimulationTick sourceTick;
    FootContactSlot foot;
    SurfaceMaterialId surface;
    Vec3 position;
    Vec3 normal;
    Vec3 achievedVelocity;
    AudioFootstepCueId audioCue;
    VfxFootstepCueId vfxCue;
};
```

Gameplay/application mapping resolves surface/character/equipment/context policy
into stable cue IDs. It does not create voices, effect instances or backend handles.
Audio maps committed simulation timing to its current sample epoch under ADR-068;
VFX admits the effect into its scene/domain lifecycle. Consumer latency or failure
cannot revise Animation cursor, Character facts or the committed tick.

If Audio is omitted, muted, device-lost or queue-full, the VFX branch and simulation
result are unchanged. If VFX is omitted, culled or over budget, Audio and simulation
are unchanged. Missing both consumers is valid in headless execution and produces no
alternative gameplay event.

### 8. Physical locomotion facts use the same presentation boundary

`Landed`, `LeftGround`, `HitWall`, `HitCeiling`, `SurfaceChanged`, `SlideStart` and
`SlideEnd` remain bounded Character facts. Gameplay may consume them at the next
permitted simulation boundary; presentation mappings consume committed immutable
facts after the tick. Character never calls Audio or VFX.

Fact payloads carry scene/controller/tick/result generations, stable fact ID,
material/contact evidence where applicable and finite impact/achieved velocity.
Stateful start/end pairs derive from committed Character state, not raw Physics
callback count or order.

Footstep is deliberately absent from this Character fact set. A foot marker may be
correlated to surface evidence without transferring timing ownership to Character.

### 9. Ordering and capacity are bounded

Within each committed tick, occurrences sort by Character stable ID, Animation
instance ID, marker directed traversal order and stable marker ID. Character facts
sort by Character ID, fact kind and canonical ordinal. Adapter outputs sort by
source tick, Character ID and source occurrence/fact ID before consumer fan-out.

Profiles bound marker occurrences, Character facts, correlations, dedup entries and
Audio/VFX request fan-out per scene/tick. Capacity is reserved during scene/runtime
preparation. Overflow fails the attempted tick when it would make authoritative
Animation event output partial; post-commit presentation admission failure is
reported per consumer and never rewrites simulation.

Steady fixed ticks and correlation allocate no heap memory. Payloads contain stable
IDs and bounded views, not `VariantMap`, strings, asset paths or native data.

### 10. Reload and shutdown preserve generation boundaries

Animation asset/graph reload creates new clip/marker and instance generations.
Character/scene replacement creates new controller/result generations. Pending
occurrences or snapshots from old generations cannot bind to replacements even when
display names and authored IDs match.

The adapter closes new admission before scene retirement, drains or cancels its
bounded presentation requests, releases dedup state and then retires the scene
generation. Audio/VFX may finish already admitted work under their own policies,
but late completion cannot re-open routing or touch scene/Character/Animation state.

On shutdown, simulation producers stop first, then the adapter closes and drains,
then VFX/Audio producers and runtimes follow their owning shutdown contracts. No
component destructor emits a final footstep, `LeftGround` or stop effect.

### 11. Errors and observability do not affect simulation

ADR-008 results distinguish invalid marker binding/payload, stale generation,
missing same-tick snapshot/support, invalid material/contact, duplicate occurrence,
capacity overflow, consumer unavailable/closed/full, reload conflict and shutdown.

Diagnostics carry bounded occurrence/request/fact IDs, scene/controller/tick,
surface, foot slot, mapping revision and suppression/failure reason. They do not log
native handles or arbitrary marker payloads. Metrics include occurrences, correlated,
suppressed by reason, duplicates, mapped Audio/VFX intents and consumer admission
failures. Stable enums may be dimensions; Character/asset IDs may not.

### 12. Qualification proves ownership and failure paths

Required coverage includes:

- one forward marker yields one occurrence and at most one semantic footstep request;
- repeated contacts, render frames, snapshot reads and consumer retries do not
  duplicate it;
- absent markers produce no Character-inferred footstep;
- same-tick grounded support selects its material/contact; airborne or stale/missing
  evidence suppresses without a live Physics query;
- left/right marker slots, loops, transitions/blends, catch-up ticks, pause/step,
  reverse eligibility and failed-tick atomicity;
- `Landed` and other Character facts remain distinct and state-transition based;
- Audio-only, VFX-only, both, neither, queue-full/device-loss/cull cases preserve
  identical Animation/Character results;
- animation/scene/character/material reload, replacement, unload and shutdown reject
  stale work and produce no teardown events;
- canonical ordering, maximum capacity, no steady-tick allocation and deterministic
  replay under different render/callback/consumer schedules.

## Consequences

Footstep timing has one owner, physical surface state has one owner and downstream
presentation is a non-authoritative adapter. Duplicate paths disappear, surface
lookup observes the exact committed tick and headless/missing-consumer operation
cannot change simulation.

The cost is a committed-tick correlation stage, stable occurrence/fact/request
identity, bounded dedup storage and explicit asset markers. Assets without markers
are silent unless a separately approved Gameplay cadence source is selected.

## Rejected Alternatives

### Let Character emit footsteps from distance, velocity or contacts

Rejected because Character does not own animation foot-plant timing and would
duplicate clip markers or vary with collision/render history.

### Let Animation choose the surface material

Rejected because Animation does not own Physics/Character contact state. Surface
evidence is taken from the same committed Character tick.

### Raycast from Audio, VFX or the adapter after commit

Rejected because a late query may observe another world/tick and gives presentation
systems Physics ownership and unbounded work.

### Emit directly from Animation or Character into Audio/VFX

Rejected because it couples simulation owners to consumer lifecycle, capacity and
backend behavior and prevents one semantic deduplication point.

### Use the latest available Character surface when exact evidence is missing

Rejected because it can attach an old/new surface to the wrong footstep and makes
consumer delay part of semantics. Missing exact evidence suppresses the request.

### Treat `Landed` as a fallback footstep

Rejected because landing is a physical state transition, not a foot-plant cadence
occurrence. Applications may map it to a distinct impact presentation.
