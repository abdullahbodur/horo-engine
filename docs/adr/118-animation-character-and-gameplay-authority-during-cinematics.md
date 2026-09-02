# ADR-118: Animation, Character and Gameplay Authority During Cinematics

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: Cinematic skeletal-pose composition, per-joint authority, Character movement/root ownership, gameplay control arbitration, pause behavior, typed conflicts, lifecycle and backend-neutral integration seams
- **Issue**: [CIN-002.2](https://github.com/abdullahbodur/horo-engine/issues/1699)
- **Jira**: [HORO-1658](https://horo-engine.atlassian.net/browse/HORO-1658)
- **Related**: [ADR-014](014-sequencer-ownership-clock-authority-and-binding-boundary.md), [ADR-061](061-animation-ownership-update-order-and-clock.md), [ADR-089](089-character-controller-ownership-implementation-and-update-order.md), [ADR-090](090-character-dynamic-body-visibility-push-and-proxy-policy.md), [ADR-117](117-playback-ownership-frame-order-and-determinism.md)
- **Normative documents**: [Cinematic Sequencer Architecture](../architecture/runtime/cinematic-sequencer-architecture.md), [Animation Architecture](../architecture/runtime/animation-architecture.md), [Character Controller Architecture](../architecture/runtime/character-controller-architecture.md), [Runtime Lifecycle](../architecture/runtime/runtime-lifecycle.md)

## Context

ADR-014 requires Cinematic Runtime to submit typed owner requests instead of writing
Physics, Character, camera or gameplay state directly. ADR-061 makes Animation
Runtime the only mutable pose-buffer owner and defines per-joint AnimationDriven,
PhysicsDriven and Blended composition. ADR-089 makes Character the final collision-
root and movement authority. ADR-117 owns sequence-player lifetime and stable
multi-player ordering.

Those decisions do not yet say what happens when cinematic and gameplay animation
both target an actor, whether the graph is suspended, which joints can be overridden,
how a cinematic movement path interacts with Character, or what result a gameplay
write receives while cinematic authority is active. A broad `cinematicActive` boolean
would spread policy checks through gameplay, Animation and Character, while direct
transform/pose writes would create two owners.

Gameplay pause is another distinct concern. ADR-014's pause token stops the complete
simulation domain. An unscaled cinematic may continue presentation while paused, but
there are no Character or Physics ticks in which to move a collision root. Products
often want a cutscene to keep deterministic character movement running while player
input and AI control are suppressed; using whole-game pause for that case is
architecturally wrong.

This ADR decides the owner and arbitration mode for skeletal pose, Character movement
and gameplay control. Camera selection, event dispatch and Audio scheduling remain
owned by their focused CIN-003/CIN-004 decisions.

## Decision

### 1. Cinematic Runtime proposes; domain owners decide and publish

The responsibility split is:

| State or decision | Final owner | Cinematic boundary |
|---|---|---|
| Sequence time, player order and sampled track values | `CinematicRuntimeService` | Produces stable generation/tick-addressed proposals under ADR-117 |
| Mutable graph instance, joint poses and final committed pose | Animation Runtime | Validates and composes cinematic contributions; Cinematic never receives a mutable pose |
| Collision root, desired displacement resolution, stance/jump and final movement | `CharacterWorld` | Admits a cinematic movement command under its ordinary fixed-tick solver |
| Gameplay/AI/player control channel | Gameplay/application authority | Grants scoped cinematic-exclusive leases or keeps gameplay ownership |
| Fixed simulation pause | Host Runtime pause authority | Grants a composed pause token; player never sets scheduler state |
| Bodies, contacts, kinematic targets and ragdoll joint override | Physics | Existing typed Character/Animation seams; no cinematic native calls |
| Presentation-only pose overlay | Animation presentation owner | Consumes committed state plus cinematic sample; cannot feed simulation |

Cinematic Runtime depends only on Horo-owned model/runtime contracts and injected
adapters. It does not include or expose Jolt, OpenGL, Metal, Vulkan, D3D12, platform
input, native animation middleware or gameplay-module implementation types.

### 2. Activation acquires one atomic authority plan

A compiled sequence declares required claims for every actor/channel it may drive:

```cpp
enum class CinematicControlChannel : uint8_t {
    SkeletalPose,
    AnimationParameter,
    CharacterTranslation,
    CharacterHeading,
    CharacterStance,
    CharacterJump,
    CharacterTeleport,
    GameplayAction
};

enum class CinematicClaimMode : uint8_t {
    ObserveOnly,
    Exclusive,
    Blend,
    PresentationOverlay
};
```

`PresentationOverlay` is valid only for `SkeletalPose`; activation records that it
owns no simulation channel and rejects any root-motion, collision, event or later-
tick feedback claim. Jump and teleport remain distinct Character capabilities so a
translation lease cannot implicitly authorize either discontinuous action.

Claims include stable actor/channel/property/joint-mask identity, required/optional
status, player priority/order identity, admitted evaluation seam, blend contract and
handoff policy. Before a player enters Ready, the application requests one aggregate
authority plan from Gameplay, Animation and Character owners. Required denial,
incompatible mode, overlapping equal-authority claim, server/client mismatch or
impossible phase fails activation and unwinds all granted claims. Optional denial
disables that declared track with a typed diagnostic.

Claims are generation-scoped leases owned by the ADR-117 player. Stop, failure,
binding loss, scene replacement, network-authority change and shutdown close new
proposals, remove the player from a later batch, then release each claim at its owner's
safe point. Closing UI or losing a component does not mutate authority immediately.

There is no process-global or component boolean that other systems poll. Each owner
resolves proposals and ordinary gameplay commands against its immutable authority
snapshot for the exact tick/boundary.

### 3. Animation always owns pose state and composition

A cinematic skeletal track submits immutable `CinematicPoseContribution` data with
scene, player/generation, animation-instance/skeleton, tick or presentation boundary,
stable order key, joint mask, mode, finite weight and sampled local transforms.
Animation validates skeleton/joint identities and copies/composes into Animation-owned
working storage. Cinematic never suspends an Animation thread, swaps pose pointers or
writes a palette.

The admitted pose modes are:

| Mode | Meaning |
|---|---|
| `Override` | Cinematic contribution has weight 1 for the declared AnimationDriven joints; graph contribution for those output joints is masked during composition |
| `Blend` | Animation combines graph/base and cinematic local transforms by a finite authored per-joint weight/curve in canonical joint order |
| `PresentationOverlay` | Applies only to the immutable presentation pose after committed simulation; excluded from root motion, hit shapes, colliders, events and later ticks |

There is no baseline `SuspendAnimationRuntime` mode. While fixed simulation runs, the
underlying graph instance continues to evaluate once per attempted tick even when
some of its output joints are overridden. Its state machines, cursors and permitted
events therefore resume from the same tick history rather than from hidden elapsed
time. When gameplay is paused, no authoritative Animation tick occurs and the graph
holds with the rest of simulation; permitted presentation overlays may still sample
an unscaled/external cinematic clock.

Enter/exit can be `Cut` or a bounded `Blend(duration, curve)` selected in the compiled
claim. Animation initializes a handoff from the last committed pose at the owner safe
point and advances it only in the declared domain. A stopped player cannot leave an
unowned frozen pose or restore a stale pre-cinematic snapshot.

### 4. Per-joint Animation/Physics authority remains final

Cinematic composition is part of the AnimationDriven side of ADR-061's existing
per-joint authority model:

1. Animation evaluates the underlying graph/base pose.
2. Animation applies admitted cinematic Override/Blend contributions in ADR-117
   player/order-key order.
3. Simulation-affecting IK evaluates over that candidate.
4. Character consumes the one admitted root-motion/movement request.
5. Physics steps and publishes any `PhysicsPoseOverride`.
6. Animation applies PhysicsDriven/Blended joint authority and finalizes the pose.

A PhysicsDriven joint cannot be reclaimed by a cinematic track. A required cinematic
claim that overlaps such a joint fails `ConflictsWithPhysicsAuthority` unless the
Physics/Animation owner first performs an explicit safe-point mode transition.
For ADR-061 `Blended` joints, Physics' declared final weight composes over the already
combined graph/cinematic Animation-side candidate. Cinematic priority cannot bypass
ragdoll/body authority.

The skeleton root's visual joint is not the Character collision root. Cinematic pose
translation/rotation affects visual skeleton composition only. Authoritative world
movement must use the Character seam below; presentation overlay never produces
consumable root motion.

### 5. Character always owns collision-root movement

For each actor and tick, Character admits exactly one effective control source:

| Character control state | Accepted translation/heading/stance source |
|---|---|
| `GameplayControlled` | Ordinary gameplay/AI command plus admitted Animation root-motion policy |
| `CinematicControlled` | One ADR-117-selected cinematic command; ordinary gameplay commands for claimed channels are suppressed |

`CinematicControlled` is exclusive for each claimed Character channel. The baseline
does not blend arbitrary gameplay and cinematic world displacement because collision,
path timing and authored staging would no longer have one predictable authority.
A sequence that wants gameplay locomotion leaves Character channels unclaimed and may
use pose-only Blend. Products needing cooperative locomotion must define a separate
typed owner-side reducer rather than summing vectors in Cinematic Runtime.

The cinematic command carries desired world/local displacement intent, heading,
stance/jump policy where admitted, root-motion composition mode and exact scene/
controller/player/tick/generation/order identity. It carries no caller delta and no
native body handle. Character applies platform carry, validates the command, resolves
ordinary Horo sweeps/collision/dynamic interaction, stages Physics commands and
publishes achieved/rejected movement exactly once under ADR-089. Cinematic cannot
teleport or set the transform unless a distinct typed Character teleport capability
was declared and admitted at a safe point.

If multiple players claim the same Character channel, the owner uses the ADR-117
priority/stable-player order fixed at activation. Required equal-priority incompatible
claims fail aggregate activation rather than alternating winners per frame. Optional
losers remain disabled and visible. Runtime arrival or evaluation order never steals a
lease.

### 6. Gameplay writes receive typed arbitration outcomes

Gameplay, player input and AI continue to submit ordinary owner commands; they never
write pose buffers or Character transforms directly. The receiving owner classifies
each channel against the tick's authority snapshot:

| Situation | Outcome |
|---|---|
| Channel is not claimed by Cinematic | `AcceptedGameplay` |
| Exclusive cinematic claim owns the same parameter/movement/heading/stance/action | `SuppressedByCinematic` |
| Declared Animation pose/parameter Blend accepts both contributions | `AcceptedForOwnerBlend` |
| Command targets a different unclaimed channel | `AcceptedGameplay` |
| Claim/command identity, generation, tick or network authority is stale/wrong | `StaleAuthority` / `AuthorityDenied` |
| Required owner cannot honor the compiled mode | `UnsupportedAuthorityMode` and player activation/evaluation failure |

Suppressed commands are not queued for replay after the cinematic, converted to zero
values or reported as success. Their bounded typed result is available to the
submitting gameplay system and diagnostics. Edge-triggered actions such as jump,
attack or interact are consumed/suppressed according to product input policy for that
tick; they do not burst on lease release. Persistent gameplay state unrelated to the
claimed channel continues normally when simulation is running.

Animation parameters may be claimed individually. A cinematic aim parameter does not
silence unrelated locomotion parameters; a full-pose Override does not grant Gameplay
action or Character movement authority. Joint masks and property/channel identities
are validated at cook/activation, not discovered by string comparisons in the hot
path.

### 7. Whole-game pause and cinematic control are different tools

When a cinematic owns a host gameplay-pause token:

- no fixed simulation tick is attempted;
- Gameplay/AI scripts, authoritative Animation, Character and Physics hold their
  committed state;
- no gameplay command, root-motion request, Character movement, collision query,
  physics step or gameplay-mutating cinematic event is accumulated for later;
- unscaled/wall/external playback may update permitted camera, Audio, Runtime UI and
  presentation-only skeletal overlays at their service/presentation boundaries; and
- one host single-step runs the complete ordinary fixed-tick authority pipeline once,
  then returns to pause.

Therefore a cinematic cannot move an authoritative Character while whole-game pause
is active. A cutscene requiring collision-aware movement keeps fixed simulation
running, acquires CinematicControlled leases for the selected actors/channels and
suppresses player/AI control through the Gameplay authority. This is control transfer,
not scheduler pause.

Pause token release resumes the next ordinary fixed tick. It does not apply elapsed
presentation motion to Character, catch up graph state, replay suppressed gameplay
commands or synthesize a large root-motion interval.

### 8. Network/world authority cannot be widened by playback

The application resolves actor/world authority before granting claims. A client-side
sequence may present cosmetic pose overlays for locally permitted views but cannot
acquire Character, Physics, server-owned Animation parameter or gameplay-action
authority for a server-owned actor. It submits a normal network/gameplay request where
the product protocol permits one; local cinematic priority is not network authority.

Server-authoritative cinematic commands join the same fixed-tick input/command frame
and can be replicated as semantic control state or resulting authoritative state.
Process-local player handles, mutable poses, wall clock and native controller objects
are never replication identity. Authority transfer or disconnect invalidates affected
leases by world/session generation before later player proposals can apply.

### 9. Failure, conflict and lifecycle are explicit

Stable outcomes include AuthorityPlanDenied, RequiredBindingMissing,
ConflictsWithPhysicsAuthority, ConflictingCinematicClaim,
UnsupportedAuthorityMode, SuppressedByCinematic, InvalidJointMask,
InvalidBlendWeight, StaleAuthority, NetworkAuthorityDenied,
EvaluationBudgetExceeded and OwnerUnavailable.

A required claim/binding/owner failure before activation leaves the player unstarted.
During playback, loss of a required actor/owner/authority holds or stops the player
according to its declared failure policy; it cannot fall back to direct writes or
silently give gameplay and cinematic simultaneous exclusive ownership. Optional
tracks may disable with a bounded deduplicated diagnostic.

All contributions and commands are staged for an attempted tick. Tick failure
publishes no pose/graph cursor, Character movement or irreversible gameplay effect.
Stop/cancel closes admission before releasing leases. Late animation/character/
physics/provider results carry scene/session/player/owner generation and cannot
restore old authority or pose state.

### 10. Qualification proves every authority mode

Required evidence includes:

- pose-only Override, per-joint Blend and PresentationOverlay against a continuing
  graph, with Cut/blended entry/exit and no stale-pose restoration;
- AnimationDriven, PhysicsDriven and Blended joint masks, required overlap rejection,
  safe-point mode transition and Physics final authority after cinematic composition;
- visual root versus collision root separation, presentation overlay producing no root
  motion and CinematicControlled movement resolving through ordinary Character sweeps;
- GameplayControlled/CinematicControlled transitions, mid-cinematic parameter,
  movement, heading, stance and action results for claimed/unclaimed channels;
- multiple players with priority/stable-ID claim resolution, optional loser behavior,
  required conflict failure and independence from worker/container order;
- whole-game pause, single-step and resume proving no Character/Physics movement,
  gameplay backlog, graph catch-up or presentation-to-simulation feedback;
- running-simulation cutscene control transfer with player/AI suppression but ordinary
  Physics, Character, Animation and unrelated gameplay still ticking;
- scene travel, binding loss, component removal, stop/failure/cancel, network authority
  change and shutdown with every lease/token/result generation safely retired; and
- Null/recording Animation, Character, Gameplay and Physics adapters in headless tests,
  with no concrete backend or editor dependency in Cinematic Runtime.

## Consequences

### Positive

- Animation, Character, Gameplay and Physics retain one final authority each.
- Pose override/blend is per joint and owner-composed rather than a mutable-buffer
  handoff.
- Gameplay writes have observable outcomes instead of silently fighting cinematic
  values.
- A cutscene can suppress actor control without stopping the complete simulation.

### Costs

- Player activation needs atomic multi-owner claim planning and rollback.
- Animation requires stable joint-mask contribution plans and bounded handoff state.
- Gameplay and Character command producers must consume typed suppression/denial
  results and avoid edge-trigger backlogs.

## Rejected Alternatives

### Suspend Animation Runtime whenever a cinematic targets an actor

Rejected because graph state, events, root motion, physics overrides and unrelated
joints would lose their normal tick semantics. Animation continues owning/evaluating
the graph and masks/blends only declared joints.

### Let Cinematic Runtime write pose arrays or scene transforms

Rejected because it would create a second mutable pose/collision-root owner and bypass
generation, Character collision and Physics authority.

### Sum gameplay and cinematic movement vectors

Rejected because collision path, staging time and authored arrival become ambiguous.
Character uses one exclusive control source unless a future owner-side reducer is
separately specified.

### Pause gameplay to suppress player input while characters keep moving

Rejected because host gameplay pause intentionally stops all fixed simulation. Actor
movement during a cutscene uses scoped control leases while simulation continues.

### Queue suppressed gameplay commands until the cinematic ends

Rejected because edge-triggered actions and stale movement would burst under a new
world/authority state. Suppression is a typed terminal result for that command/tick.

### Use concrete Animation or Physics backend APIs

Rejected because cinematic policy must remain portable and testable with Null/
recording adapters across all renderer, physics and platform compositions.
