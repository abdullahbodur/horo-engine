# ADR-089: Character Controller Ownership, Implementation and Update Order

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: Horo-owned kinematic query-controller strategy, scene ownership, private Jolt query boundary, fixed-tick command/root-motion cadence, movement/platform/orientation composition, transform authority/publication, backend replacement, lifecycle, migration and qualification
- **Issue**: [CHR-001.1](https://github.com/abdullahbodur/horo-engine/issues/937)
- **Jira**: [HORO-937](https://horo-engine.atlassian.net/browse/HORO-937)
- **Parent**: [CHR-001](https://github.com/abdullahbodur/horo-engine/issues/930)
- **Related**: [ADR-008](008-error-model-exception-boundary-and-registry.md), [ADR-022](022-ai-fixed-tick-order-authority-and-simulation-budget.md), [ADR-026](026-large-world-precision-and-floating-origin-strategy.md), [ADR-061](061-animation-ownership-update-order-and-clock.md), [ADR-084](084-canonical-physics-solver-units-and-tolerances.md), [ADR-086](086-collision-layer-profile-and-query-channel-policy.md), [ADR-087](087-scene-to-physics-ownership-and-conversion.md), [ADR-088](088-physics-determinism-capability-and-support-tiers.md)
- **Normative documents**: [Character Controller Architecture](../architecture/runtime/character-controller-architecture.md), [Animation Architecture](../architecture/runtime/animation-architecture.md), [Physics Architecture](../architecture/runtime/physics-architecture.md), [Scene Runtime](../architecture/runtime/scene-runtime.md), [Runtime Lifecycle](../architecture/runtime/runtime-lifecycle.md)
- **Upstream references**: [Jolt v5.6.0 character controllers](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Docs/Architecture.md#character-controllers), [Jolt v5.6.0 CharacterVirtual](https://jrouwe.github.io/JoltPhysicsDocs/5.6.0/class_character_virtual.html)

## Context

Character Controller Architecture describes a kinematic capsule that performs
sweeps, slopes, steps, grounding and moving-platform behavior. ADR-061 already
moved authoritative animation/root motion to every attempted fixed tick, but the
remaining text still says the transform is read/written "each frame", platform
carry occurs on the "next frame", requests carry caller-provided delta time and
contacts allocate a `std::vector`. Ownership of the sweep algorithm and rotations
is also incomplete.

Jolt v5.6.0 offers `Character` and query-based `CharacterVirtual`. The latter uses
narrowphase queries, supports wall sliding, stairs and moving platforms, but owns
native state/callback/timing and is updated separately from rigid bodies. Wrapping
it behind a public replaceable controller ABI would freeze Jolt semantics and make
another solver reproduce native edge cases. Reimplementing every low-level collision
query outside Physics would duplicate broadphase/filter/material/origin policy.

Horo instead needs one engine-owned semantic algorithm that can be replayed,
bounded, inspected and qualified with stable Horo queries/results. The canonical
Jolt adapter can provide private collision casts and body evidence without becoming
the controller contract. There is no current multi-solver Physics ABI, so a public
`ICharacterBackend` would be premature for the same reasons rejected by ADR-084.

The fixed-tick sequence must include gameplay intent, animation sampling, root
motion, kinematic-platform targets, current dynamic-platform motion, character
query resolution, staged impulses/kinematic presence, the Physics step, support
finalization, transform publication and post-Physics animation. A render-frame
accumulator cannot bridge these phases because one frame may contain zero or many
attempted ticks.

Finally, a capsule's collision up axis, gameplay desired heading, animation root-
motion rotation, platform angular carry and rendered visual orientation are
different authorities. Treating them all as one transform lets platform tilt rotate
the capsule, visual lean change collision or animation overwrite collision results.

## Decision

### 1. Horo owns the controller semantics and sweep solver

The accepted strategy is a Horo-owned kinematic query controller. Horo specifies
the complete movement pipeline, iteration/limits, contact reduction, slope/step/
ground/platform policy, state transitions, errors and deterministic ordering.

The implementation consumes a narrow Horo `CharacterPhysicsQueryContext` borrowed
from the exact scene `PhysicsWorld` generation. The canonical adapter maps its
capsule casts, overlaps, body point velocity, material/filter and impulse commands
to private Jolt narrowphase APIs. Public Character/Scene/Gameplay contracts never
expose `JPH::Character`, `JPH::CharacterVirtual`, collectors, body/subshape IDs,
listeners, allocators or native settings.

Horo does not use `CharacterVirtual` as the semantic/state owner in CanonicalV1.
Upstream algorithms/tests may inform implementation and qualification, but native
update behavior is not forwarded wholesale. There is no public or extension
`ICharacterControllerBackend`, provider registry or runtime backend selector.

### 2. The initial Character domain lives behind the Physics target

The first implementation lives inside `HoroEngine::Physics` as a distinct
`Horo::Character` domain because Physics is currently one canonical target. Its
public surface contains only Horo descriptors, commands, results, snapshots and
generation-checked handles. Native queries remain private.

If a later architecture splits Character into API/runtime targets, Character
Runtime may depend on a backend-neutral Horo Physics query capability. Physics must
not depend back on Character. Such a split cannot change Character semantics or
introduce hot-path virtual dispatch without migration and qualification.

Replacing Jolt or the private query adapter is therefore an implementation change,
not a controller-backend selection. It must reproduce the Horo golden behavior and
requalify queries, contacts, platforms, determinism and performance before shipping.

### 3. Each active scene generation owns one Character world

ADR-087's aggregate scene candidate owns exactly one `CharacterWorld` when the
scene requires Character capability. It is paired with one exact `SceneRuntimeId`,
`PhysicsWorldId`, collision-schema generation and origin generation.

`CharacterWorld` owns:

- controller slots/handle generations and stable authored bindings;
- committed and candidate controller state;
- tick-indexed command/root-motion admission;
- per-tick fixed-capacity query/contact/impulse/event scratch;
- support/platform attachments and previous/current publication state;
- bounded debug/metrics snapshots.

Scene components store stable authored controller identity and runtime binding
handles, not controller objects. Gameplay borrows typed capabilities. Animation
submits a root-motion request. Physics supplies one immutable query view and accepts
staged commands. No subsystem caches pointers into Character storage.

Character candidate preparation follows scene activation: validate descriptors and
bindings, reserve all capacity, create controller slots against the detached Physics
candidate, then publish both in the same no-fail aggregate commit. Failure leaves
the prior scene/Physics/Character bundle untouched.

### 4. Controller identity and state are generation checked

`CharacterControllerHandle` contains scene, Character-world and slot generations.
The binding table maps it to stable `SceneObjectId` and authored controller component
ID. A handle is process-local/nonserializable and never aliases a replacement world
or reused slot.

The committed controller state includes finite collision-root position, velocity,
up basis, heading, capsule/stance generation, grounded/support state, platform
attachment, movement flags and source tick. Candidate state for an attempted tick
is private until tick commit. Failed ticks preserve committed state and consume no
event/publication generation.

Exact field schemas, descriptor/request/result/error types are owned by CHR-001.2;
this ADR fixes their ownership, clocks and relationships.

### 5. Authoritative Character work occurs only per attempted fixed tick

Gameplay submits one tick-addressed `CharacterMovementCommand` before the Character
admission boundary. It contains desired movement/facing, jump/stance/teleport and
root-motion consumption policy; it does not contain a caller-selected `deltaTime`.
Character reads `FixedStepContext::fixedDelta` exactly once.

Commands produced at variable/render rate are latched as input intent and assigned
to an exact simulation tick by the gameplay/input owner. They are not integrated or
summed by frame delta. A command may be replaced before its tick closes under a
versioned policy; after closure, late/duplicate commands fail or target a future
tick explicitly.

Zero attempted ticks produce no Character movement. Catch-up executes a complete
Character update for every attempted tick. Pause freezes state; single-step runs one
ordinary update. Simulation-rate changes affect tick production, not the fixed
quantum passed to Character.

### 6. Root motion is generated and consumed once per tick

ADR-061 Animation evaluates the exact directed player interval for one attempted
tick and stages one finite `RootMotionRequest` carrying scene, animation instance,
controller binding, tick and generation. Presentation/editor-preview sampling never
enters Character.

There is no render-frame root-motion accumulator. When one frame produces several
ticks, Animation produces a distinct delta for each. When it produces none, no
authoritative delta exists. A failed tick discards the staged request and does not
advance the committed animation cursor or controller.

The movement command chooses one explicit translation mode:

- `GameplayOnly`: ignore root translation;
- `RootMotionSuggest`: use root translation only when the command explicitly has no
  gameplay translation intent;
- `RootMotionAdditive`: sum gameplay and root translation before collision;
- `RootMotionOverride`: root translation replaces gameplay translation.

An explicit presence bit, not a velocity epsilon, distinguishes "no gameplay
translation". Root rotation has corresponding ignore/additive/override policy in
the heading composition below. One request is consumed at most once.

### 7. The fixed-tick order is exact

The authoritative non-AI/AI locomotion paths converge on this dependency order
inside one existing `FixedUpdate` attempt:

```text
0. Apply owner-thread commands before FixedUpdate; freeze scene/world generations
1. Input, Gameplay, AI/Nav and admitted Cinematic owners stage movement/facing,
   animation parameters, jump/stance and kinematic-platform targets
2. Animation pre-Physics evaluates the candidate pose, simulation IK, events and
   one root-motion delta for this tick
3. Physics freezes CharacterPhysicsQueryContext from committed bodies plus admitted
   kinematic targets and support point-velocity evidence
4. Character applies support/platform carry, composes intent/root motion, runs the
   bounded Horo query solver and stages controller pose, impulses/presence targets
5. Physics applies staged platform/Character commands and steps exactly once
6. Physics publishes candidate rigid-body/contact/platform results
7. Character finalizes grounded/support/attachment state, bounded events and the
   authoritative collision-root transform; it does not perform a second move
8. Animation applies admitted Physics pose overrides and finalizes candidate pose
9. Tick commit atomically publishes Physics, Character, transforms, animation state
   and bounded events
10. Variable presentation interpolates committed roots/poses and applies visual-only
    overlays before render extraction
```

Stages 1–9 are one transaction. A failure prevents the attempted tick from
committing under Runtime Lifecycle policy. No process DataBus event establishes
their order. Catch-up repeats all stages; presentation never runs between them.

Simulation IK that affects movement/query geometry completes in stage 2. IK based
on stage-6 contacts is post-Physics/presentation and cannot feed the same tick's
movement. AI/Nav produces stage-1 intent; it does not run a separate controller or
move transforms before Character.

### 8. Movement displacement composes in one declared order

At stage 4, Character constructs requested displacement in this order:

1. derive support/platform carry from the stage-3 snapshot;
2. establish the carried collision root and carried heading;
3. resolve an explicit gameplay desired heading if present;
4. rotate the tick's local-space root translation by that pre-root heading;
5. combine gameplay translation and root translation using the selected mode;
6. integrate Character-owned vertical velocity/gravity/jump for the fixed quantum;
7. run the Horo capsule query pipeline over carry plus requested movement;
8. apply the admitted root/gameplay heading result to the collision root;
9. stage bounded impulses/presence targets for Physics.

Platform carry is never added again as desired gameplay velocity. Root-motion
translation is the animation root's local directed delta; it is not divided by
render time or resampled after collision clipping. Movement result records requested,
carried, achieved and rejected/clipped deltas so Gameplay/Animation can react next
tick without rewriting this one.

### 9. Up basis, heading and visual orientation have different owners

The collision root is a position, one finite unit `up` basis and one normalized
heading twist about that basis. CanonicalV1 uses project/world `+Y` by default.

- `CharacterWorld` owns the capsule up basis. Gravity direction or platform normal
  does not rotate it implicitly. A typed up-basis change commits at a safe point,
  revalidates capsule clearance/support and may fail.
- Gameplay owns optional absolute desired heading about the current up basis.
  Character validates/projects it; pitch/roll are not capsule heading.
- Animation owns local root-motion rotation for the tick. Character extracts the
  admitted twist about up for collision heading. Residual pitch/roll remain visual
  pose motion and never tilt the capsule.
- Physics owns the platform transform/angular velocity sample. Full platform
  rotation moves the attachment point; only its twist about Character up affects
  heading when `inheritPlatformHeading` is enabled.
- Animation/Presentation owns visual orientation/lean/aim relative to the committed
  collision root. It cannot feed collision until a future typed fixed-tick command.

Heading composition is deterministic:

1. start from previous committed heading;
2. pre-apply admitted platform twist when inheritance is enabled;
3. replace with explicit Gameplay desired heading when present;
4. for root `Additive`, post-apply root twist to the current base;
5. for root `Override`, apply root twist to the carried pre-Gameplay heading and
   ignore Gameplay desired heading for this tick;
6. normalize/canonicalize sign and reject non-finite/degenerate results.

Root translation uses the heading after steps 1–3 and before root twist. Capsule
collision is rotationally symmetric around up; heading still owns gameplay/visual
forward and local-root translation.

### 10. Moving-platform carry uses explicit tick evidence

The support attachment stores stable Physics body identity/generation, local point,
last committed support transform/evidence and policy flags. It never retains a
native pointer.

Stage 3 supplies one immutable `PlatformMotionSample`:

- for a kinematic platform, its admitted current-tick target transform/point
  velocity;
- for a dynamic platform, its committed pose plus point linear/angular velocity at
  the attachment point, integrated for the fixed quantum under the Character
  profile;
- for static ground, zero carry.

Character applies carry before its own displacement and sweeps it through collision
like other movement. Full rotation transports the local attachment point. Optional
heading inheritance uses only platform twist as defined above; platform tilt never
changes capsule up in CanonicalV1.

After Physics steps, Character uses the final body/contact evidence only to finalize
support identity/local attachment and next-tick state. It does not silently move a
second time after Physics. Dynamic-platform prediction error is observed and
resolved by the next fixed tick's bounded support/ground/depenetration policy; it is
not corrected from variable presentation.

Detachment occurs on support loss, jump/teleport, stale body generation, rejected
carry or explicit policy. Transfer velocity is captured as a typed tick result and
applied exactly once under the movement profile.

### 11. The Horo query solver is bounded and snapshot based

The stage-3 `CharacterPhysicsQueryContext` is read-only and world/tick/generation
affine. The baseline pipeline is:

1. bounded spawn/continuing-overlap recovery;
2. support/ground classification;
3. platform carry sweep;
4. requested capsule sweep with iterative slide;
5. guarded step-up/forward/down attempt;
6. vertical/gravity/jump sweep;
7. bounded step-down/stick-to-ground;
8. contact/material/support canonicalization.

Exact sweep inflation, tolerances, maximum iterations, contact capacity, slope/step
tests and failure behavior are Horo profile fields, not upstream defaults. Queries
use stable ADR-086 channel/profile/filter identities and ADR-088 canonical result
ordering. A query callback only records bounded evidence; gameplay/contact policy
does not execute inside native collectors.

The solver stages impulses to dynamic bodies and any optional Character presence
target through Physics commands. It never mutates bodies while iterating query hits.
Controller-to-controller collision is unsupported in the initial profile until a
CharacterWorld snapshot/order policy is specified; it is not delegated implicitly
to `CharacterVirtual`.

### 12. Character owns collision-root transform publication

During an attempted tick, Scene Transform is not an independent writer for a bound
controller. Character stages the collision root at stage 4, finalizes it at stage 7
and publishes it with the tick at stage 9. Physics owns rigid-body transforms;
Animation owns poses; no later Gameplay/Animation system overwrites Character root.

Teleport, spawn, stance/size and up-basis changes are typed Character commands with
their own clearance/recovery/failure policy. Direct transform writes while a
controller is active are rejected or converted by an explicit host command before
the tick closes.

Renderer interpolation consumes previous/current committed collision roots. Visual
root/lean/aim offsets are presentation/pose state and cannot become authoritative
through extraction. Failed ticks publish neither controller transform nor visual
pose generation.

### 13. Determinism and concurrency follow Physics tiers

Character ordering/profile/command/root/platform/query/contact/state versions and
capacities participate in ADR-088's Physics determinism fingerprint. Controllers
iterate by stable Horo ID; commands, contacts, impulses and events use canonical
tie-breaks. Pointer, native callback, worker completion and render order are not
inputs.

CanonicalV1 updates Character serially on the Physics owner thread using preallocated
scratch. Future parallel controller batches require immutable shared query evidence,
deterministic controller/body impulse reduction, per-controller FPU state and a new
qualified tuple. Parallelism cannot change public results or let controllers observe
partially updated peers.

Character uses fixed Horo/Jolt math admitted by the effective tier. Debug/presentation
queries and visual ground polish cannot feed simulation. Overflow produces the same
typed failure at the canonical item; it never drops arrival-order contacts.

### 14. Activation, reload, unload and origin shift are transactional

Character descriptors/bindings validate and reserve all world/controller/scratch
capacity during ADR-087 scene candidate preparation. Required Character capability
without Physics/query/filter/material support fails activation; no null controller
or collision-free fallback is published.

Reload builds a candidate `CharacterWorld`. Typed preservation may transfer Horo
position/velocity/heading/stance/support only when stable controller IDs, descriptor,
Physics/filter/origin/profile and platform evidence are compatible. Native state or
contacts are never copied. Failure preserves the active bundle.

ADR-026 origin shift translates committed/candidate controller positions, contact
points/query bounds and attachment platform evidence by the same safe-point delta;
velocities, up, heading and local attachment do not change. No tick/query overlaps
the shift.

Unload closes commands/queries/events, cancels candidate work, drains tick/debug/
snapshot readers, destroys controller slots/scratch and releases Physics/filter/
material leases before Scene component storage. Shutdown is reverse dependency,
idempotent and never calls native Character callbacks after Physics teardown.

### 15. Errors, limits and observability are Horo-owned

ADR-008 results cover stale/cross-world handles, missing/duplicate command or root
motion, invalid descriptor/capsule/up/heading/transform, unsupported profile/query/
controller collision, slope/step/stance/platform policy, overlap recovery, query/
contact/iteration/scratch/impulse/event limit, non-finite result, generation/origin
mismatch, tick phase, cancellation, unload and native query invariant.

Velocities/displacements are validated against declared profile limits. They are not
silently clamped. A profile may define an explicit saturating gameplay policy whose
applied value/result flag is deterministic and observable; safety rejection remains
distinct.

Diagnostics carry bounded scene/Character/Physics/controller/tick/command/root/
platform IDs, phase, counts and expected/actual generation. They do not log native
pointers, raw geometry or unbounded contacts. Metrics include controller states,
query/sweep/iteration/contact counts, step/ground/platform outcomes, clipped/rejected
movement, scratch high water, overflow and update time. Debug draw uses immutable
bounded snapshots outside the owner hot path.

### 16. Migration from the existing frame-oriented document is explicit

This decision revises affected architecture text as follows:

- "reads and writes transform each frame" becomes Character-owned fixed-tick
  candidate/final publication;
- caller-provided `MovementRequest::deltaTime` is removed; Character consumes
  `FixedStepContext::fixedDelta`;
- "next frame" moving-platform carry becomes stage-3/4 evidence for the next
  attempted fixed tick;
- root motion is never accumulated from presentation frames and uses one request
  per attempted tick;
- contacts/results use bounded world-owned storage/views, not per-tick
  `std::vector` allocation;
- serialized layer/masks become stable collision profile/query-channel IDs;
- native/default query-controller behavior becomes the versioned Horo solver
  profile;
- AI's old Character-then-Animation phase text is replaced by Gameplay/AI intent,
  Animation pre-Physics/root motion, Character, Physics, post-Physics Animation.

Existing callers that update a controller during variable/presentation update must
submit a typed future-tick command. Direct transform writers, arbitrary delta-time
callers and renderer-driven root motion fail migration validation rather than being
kept as a parallel compatibility path.

### 17. Qualification is part of the contract

Required coverage includes:

- Horo solver golden fixtures independent from native `CharacterVirtual` state;
- zero/one/multiple catch-up ticks, pause/single-step and 30/60/144 Hz presentation
  with identical committed Character results;
- root translation modes, one-shot generation, failed-tick discard, local-space
  conversion and root/gameplay/platform heading composition;
- capsule up changes, desired heading projection, root pitch/roll visual isolation,
  platform full-point rotation versus optional twist inheritance;
- ground/slope/slide, step-up/down, corners, ceilings, narrow gaps, depenetration,
  stance resize, teleport and failure budgets;
- static/kinematic/dynamic support, platform point velocity, jump/detach/transfer,
  stale body and dynamic prediction error across ticks;
- stable query/contact/material/filter canonicalization, staged impulses and no body
  mutation from collectors;
- exact stage ordering and transform/pose visibility at every phase;
- scene activation/rollback, reload preservation, origin shift, unload and shutdown
  after every partial state;
- maximum controllers, queries, contacts, iterations, scratch, impulses/events and
  no fixed-tick heap growth;
- same-build/platform determinism fingerprint/replay evidence and explicit downgrade
  for unqualified features;
- private adapter replacement parity across every Horo golden fixture;
- fuzz/property tests for finite descriptors/commands/hit sets/order and checked
  arithmetic.

Performance evidence records controller count, scene/query complexity, fixed rate,
platform/CPU/build, p50/p95/max stage cost, scratch/contact high water and native
query counts. Claims do not extrapolate from empty scenes or average render frames.

## Consequences

Horo gains one stable, testable Character behavior independent from native controller
classes while reusing the canonical Physics collision world. Every simulation input
and orientation has one owner, root motion cannot depend on render cadence, and
scene/Physics/Character publication remains transactional.

The cost is owning and qualifying a sophisticated bounded sweep/step/platform
algorithm. Upstream `CharacterVirtual` improvements are not inherited automatically;
they must be deliberately mapped into Horo semantics. Dynamic-platform carry has an
explicit velocity-derived pre-step model and next-tick reconciliation instead of an
unspoken after-step correction.

## Rejected Alternatives

### Expose `JPH::CharacterVirtual` or wrap it as the public controller

Rejected because native state, callbacks, filters, update timing and version drift
would become public semantics and prevent backend-neutral Horo qualification.

### Add a replaceable `ICharacterControllerBackend` now

Rejected because Horo has one canonical Physics solver and no evidence for a useful
multi-backend ABI. Replacement occurs below the Horo query/behavior contract.

### Implement Character collision without the Physics query boundary

Rejected because it would duplicate broadphase, shapes, filters, materials, origin
and native lifetime policy outside their owner.

### Update Character once per render frame

Rejected because zero/multiple fixed ticks, pause, catch-up and headless execution
would produce different collision, root motion and events.

### Accumulate presentation root motion until the next Physics tick

Rejected because sampling cadence and failed/dropped frames would change the
authoritative delta. Animation evaluates one exact interval per attempted tick.

### Let Animation or Gameplay write the final Character transform

Rejected because collision resolution would lose authority and later writes could
move through walls. They submit intent; Character publishes the collision root.

### Rotate capsule up/heading directly with platform or visual orientation

Rejected because platform tilt and animation lean/aim would alter collision
implicitly. Full platform rotation transports the attachment point; up and heading
follow explicit policies.

### Move the Character again after Physics to match the final platform pose

Rejected because other bodies already stepped against the staged pre-Physics pose
and a second hidden query/move would not participate in the same Physics solve.
Post-Physics finalization updates support evidence for the next tick only.
