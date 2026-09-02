# ADR-090: Character Dynamic-Body Visibility, Push and Proxy Policy

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: Character/dynamic-body visibility modes, one-way push, optional kinematic presence proxy, transform and momentum authority, capability negotiation, filtering, fixed-tick ordering, lifecycle, failure, limits and qualification
- **Issue**: [CHR-003.7](https://github.com/abdullahbodur/horo-engine/issues/959)
- **Jira**: [HORO-959](https://horo-engine.atlassian.net/browse/HORO-959)
- **Parent**: [CHR-003](https://github.com/abdullahbodur/horo-engine/issues/932)
- **Related**: [ADR-008](008-error-model-exception-boundary-and-registry.md), [ADR-084](084-canonical-physics-solver-units-and-tolerances.md), [ADR-086](086-collision-layer-profile-and-query-channel-policy.md), [ADR-087](087-scene-to-physics-ownership-and-conversion.md), [ADR-088](088-physics-determinism-capability-and-support-tiers.md), [ADR-089](089-character-controller-ownership-implementation-and-update-order.md)
- **Normative documents**: [Character Controller Architecture](../architecture/runtime/character-controller-architecture.md), [Physics Architecture](../architecture/runtime/physics-architecture.md), [Scene Runtime](../architecture/runtime/scene-runtime.md), [Runtime Lifecycle](../architecture/runtime/runtime-lifecycle.md)
- **Upstream references**: [Jolt v5.6.0 character controllers](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Docs/Architecture.md#character-controllers), [Jolt v5.6.0 CharacterVirtual](https://jrouwe.github.io/JoltPhysicsDocs/5.6.0/class_character_virtual.html)

## Context

ADR-089 makes Character a Horo-owned kinematic query controller. Queries let the
controller see selected dynamic bodies, but a query-only controller is absent from
the rigid-body contact graph. A dynamic body therefore cannot collide with or push
the controller merely because Character queried it. Conversely, mutating a dynamic
body from a sweep collector would couple gameplay policy to native callback order
and violate the staged Physics command boundary.

Games need deliberately different behavior. A ghost may ignore dynamics. A player
may treat crates as obstacles without moving them. Another controller may push light
bodies, while a high-interaction character must be visible during the Physics solve
and react to contact impulse. One boolean such as `enablePhysicsInteraction` cannot
state who sees whom, who owns the Character root or whether an unavailable backend
feature may degrade.

A dynamic character body would make Physics the transform authority and replace
ADR-089's bounded sweep/step/ground semantics. A solid kinematic proxy can make
dynamics see the resolved controller pose, but it cannot be allowed to write the
Character root from native state. It also risks applying both a Horo staged push and
a solver contact impulse unless ownership is exclusive.

The policy must separate query visibility, Character-to-body push and body-to-
Character reaction. It must preserve the one-move-before-Physics order, use stable
Horo identities and collision policy, bound all evidence and commands, and make
unsupported interaction observable rather than a backend-selected fallback.

## Decision

### 1. Character remains transform authority in every mode

`CharacterWorld` owns the committed and candidate collision root, velocity,
grounding and support state. Physics owns dynamic bodies and may own a private
kinematic proxy, but that proxy is a projection of Character's candidate pose, not
a second Character state.

No mode turns the controller into a dynamic rigid body. Native contact resolution,
proxy pose, accumulated impulse or body velocity never writes the Character root
directly. Dynamic-body influence becomes bounded Horo reaction evidence for the
next attempted fixed tick.

This one-tick causal boundary is deliberate. Character resolves and stages its pose
before ADR-089's single Physics step. Moving it again afterward would create an
unsolved transform and divergent visibility for other bodies.

### 2. Four explicit interaction modes cover CanonicalV1

| Mode | Character queries selected dynamics | Dynamic blocks Character | Character pushes dynamic | Dynamic sees Character in contacts | Dynamic reaction affects Character |
|---|---|---|---|---|---|
| `Disabled` | No | No | No | No | No |
| `ObstacleOnly` | Yes | Yes | No | No | Support/carry policy only |
| `OneWayPush` | Yes | Yes | Horo staged impulse | No | Support/carry policy only |
| `BidirectionalProxy` | Yes | Yes | Physics contact against proxy | Yes | Bounded next-tick reaction |

`Disabled` disables only dynamic-body interaction; static/kinematic environment
queries remain governed by the normal controller channel. `ObstacleOnly` does not
pretend the body can see Character. `OneWayPush` is an explicit asymmetry.
`BidirectionalProxy` supplies visibility/reaction without surrendering root
authority.

Dynamic-platform support remains ADR-089's separate attachment/carry contract. A
body may be admitted as support independently from push mode. Proxy contact alone
never establishes grounding.

### 3. Requested and effective policy are immutable and fail closed

The authored descriptor carries a complete policy:

```cpp
enum class CharacterDynamicInteractionMode : std::uint8_t {
    Disabled,
    ObstacleOnly,
    OneWayPush,
    BidirectionalProxy,
};

struct CharacterDynamicInteractionPolicy {
    CharacterDynamicInteractionMode requestedMode;
    std::optional<CharacterDynamicInteractionMode> explicitFallback;
    PhysicsQueryChannelId queryChannel;
    CollisionProfileId proxyProfile;
    CharacterInteractionProfileId interactionProfile;
};
```

Scene preparation resolves it against Physics capabilities and publishes an
immutable effective record with requested/effective modes, capability, filter and
profile generations, plus a mandatory typed resolution reason (`ExactRequested`,
the admitted fallback reason, or the rejection reason). A rejected candidate still
returns that reason in diagnostics and never publishes a partial effective record.

Fallback is allowed only when the descriptor names the exact lower mode. Missing
capability with no admitted fallback rejects the candidate. Horo never silently
changes bidirectional interaction to one-way, obstacle-only or disabled because a
backend, platform, server build or budget lacks support.

### 4. Horo capabilities describe semantic support

Physics reports these capabilities before activation:

- `CharacterDynamicQuery`: generation-affine dynamic capsule-query evidence;
- `CharacterStagedImpulse`: canonical bounded impulses before the step;
- `CharacterKinematicPresenceProxy`: private kinematic capsule participation in
  the dynamic contact graph;
- `CharacterProxyContactReaction`: bounded post-step reaction evidence mapped to
  stable Horo identities;
- `CharacterInteractionDeterminismTier`: qualified ADR-088 tier for the exact
  mode/profile tuple.

`Disabled` requires none of these dynamic capabilities. `ObstacleOnly` requires
dynamic queries, `OneWayPush` also requires staged impulses, and
`BidirectionalProxy` requires query, proxy and reaction support. A native class,
callback or body mode does not itself satisfy a capability; the adapter must meet
Horo ordering, filtering, bounds, lifecycle and qualification semantics.

### 5. Query and simulation visibility are separate dimensions

The Character query channel controls which colliders Character sees as `Ignore`,
`Overlap` or `Block` under ADR-086. The proxy collision profile independently
controls which dynamics see the proxy. Both are stable project identities resolved
through the exact scene collision-schema generation.

CanonicalV1 proxy participation is narrow:

- the proxy collides only with explicitly admitted dynamic-body profiles;
- static/kinematic environment bodies ignore it because queries resolve them;
- Character proxies ignore other Character proxies until a peer policy exists;
- sensors/triggers derive no gameplay events from proxy pairs;
- the proxy cannot be selected as ground or a moving platform;
- evidence maps through Horo IDs, never native layer indexes or pointers.

Activation rejects a contradictory policy, such as `BidirectionalProxy` with no
dynamic profile allowed to collide. It never repairs the schema by choosing a
default profile.

### 6. One-way push is a staged Horo command

In `OneWayPush`, the query solver classifies a blocking dynamic hit with the
immutable interaction profile. It records evidence during collection and later
reduces that evidence into canonical `CharacterBodyImpulseCommand` entries. Native
collectors never mutate bodies or call Gameplay.

The profile supplies finite Horo values for virtual Character mass, maximum impulse
per contact/tick, maximum affected bodies, admitted body-mass range, normal/tangent
transfer and wake policy. Commands carry stable Character/body/subshape identity,
tick/world generations, contact point/normal, canonical reason and linear impulse.
Physics derives angular response from the point impulse.

Commands sort by Character ID, body ID, subshape ID and contact ordinal. Per-body
and aggregate budgets are checked before mutation. Overflow, stale identity or
non-finite reduction fails the attempted tick; Horo never drops the last callback
or clamps by arrival order.

The contact ordinal is scoped to the exact `(tick, Character ID, body ID, subshape
ID)` tuple. It is assigned after canonical contact reduction by contact point,
normal and feature identity, starting at zero, so callback order cannot change the
sort key.

The exact equation belongs to implementation, but it is a versioned Horo algorithm
over committed evidence and the fixed tick. It cannot forward an upstream callback
side effect as public semantics.

### 7. Bidirectional mode uses one private kinematic presence proxy

Each active `BidirectionalProxy` controller owns one private proxy slot in the same
scene/world generation. It uses the admitted capsule geometry with a versioned
finite inset/contact profile. It is kinematic, gravity-free, non-sleeping while
active and invisible outside the private Character/Physics binding table.

At ADR-089 stage 4, Character completes query movement and stages one generation-
checked proxy target equal to the candidate root. At stage 5, Physics applies the
target before collision detection and solves admitted dynamics against it. Because
dynamics receive ordinary solver impulses, Character emits no `OneWayPush` impulse
for proxy-visible pairs in that tick. This exclusive ownership prevents double
pushing.

The proxy is not a public `PhysicsBodyHandle`, authored `RigidBodyComponent`, save/
network identity, gameplay query result or constraint endpoint. Debug output uses
stable Character/proxy generations, never native body IDs.

### 8. Proxy reaction becomes bounded next-tick evidence

After Physics, the private adapter canonicalizes proxy contacts into a fixed-
capacity result:

```cpp
struct CharacterDynamicReaction {
    SimulationTick sourceTick;
    CharacterControllerHandle character;
    Vec3 netLinearImpulse;
    Vec3 weightedContactNormal;
    Vec3 sourcePointVelocity;
    BoundedBodyContributionView contributors;
    bool saturated;
};
```

It carries stable body/subshape identities and exact world/proxy generations. The
reduction order is independent of callback/worker order. Capacity overflow fails
the tick unless the exact profile admits a deterministic, fingerprinted saturation
rule whose result is observable.

Stage 7 stores reaction with the committed result. Stage 4 of the next attempted
tick converts it through the versioned profile into external velocity/displacement
intent and resolves that through ordinary sweeps. Teleport, detach, world/profile/
proxy generation change or explicit reset invalidates pending evidence.

Presentation never applies reaction. A body cannot push Character through a wall
because the next-tick response is swept. This kinematic, delayed model does not
claim exact dynamic two-body momentum conservation; the effective capability makes
that limitation explicit.

### 9. Fixed-tick ordering extends ADR-089 without a second move

```text
3. Physics freezes committed dynamic query/support evidence
4. Character query-resolves once, reduces optional one-way impulses and stages an
   optional BidirectionalProxy target
5. Physics applies either one-way impulses or the proxy target, then steps once
6. Physics publishes dynamic results and bounded proxy contact evidence
7. Character finalizes support and stores reaction for the next tick; it does not
   alter the current candidate root
9. Tick commit atomically publishes Physics, Character and reaction evidence
```

Failed ticks expose no body impulse, proxy target, reaction or Character result.
Catch-up repeats the whole sequence. Paused/zero-tick frames apply neither pushes
nor reaction, and presentation cadence is never an input.

### 10. Geometry and teleports are safe-point operations

Spawn, stance/capsule resize, up-basis change and teleport prepare Character state
and proxy replacement together. Changed proxy geometry is built detached, checked
against the exact filter/profile generation and swapped with the Character
candidate. Old proxy geometry never represents a new capsule.

Teleport closes old contact/reaction generations, clears pending reaction and stages
the new root/proxy target. Failure preserves both old Character and proxy. Direct
native proxy shape/transform mutation is forbidden.

### 11. Scene activation derives proxies transactionally

The proxy is runtime-derived state, not another authored Physics component producer.
`PhysicsScenePlanBuilder` validates policy and reserves private slots/capacity. The
detached `CharacterWorld` candidate creates bindings against the detached
`PhysicsWorldCandidate` in stable Character order.

Aggregate commit publishes Scene, Physics, Character and proxy bindings together.
Proxy allocation, shape/profile/capability or startup failure destroys only the
candidate. Failed private construction consumes no public body identity.

Reload rebuilds proxies. Typed preservation may carry committed Horo state/reaction
only when controller identity, capsule, mode/profile/filter, world and determinism
fingerprint are compatible. Native body/contact/cache state is never copied.

### 12. Removal and shutdown close every admission direction

Removal first closes movement, query, impulse, proxy-target and reaction admission.
Physics completes or aborts the owned step, drains callback evidence/snapshots,
unbinds the proxy and retires native resources before slot reuse with a new
generation. Scene replacement/unload and shutdown follow the same order.

Late results, commands, callbacks or reactions with retired generations are
rejected. No callback retains a `CharacterWorld`, scene object or proxy pointer.
Origin shift rebases roots, bodies and proxy targets in one owner-thread transaction
and cannot appear as gameplay impulse or movement.

### 13. Errors and observability expose effective behavior

ADR-008 results distinguish invalid mode/profile, missing query/proxy profile,
incompatible filter responses, unsupported capability/tier, proxy capacity or
construction failure, stale identity, illegal pair, non-finite impulse/reaction,
overflow, cancellation, unload and shutdown.

Diagnostics carry stable Character/body/profile/channel IDs, requested/effective
mode, capability/filter/profile generations, tick, phase, counts, limits and
fallback reason. They expose no native IDs, pointers or unbounded manifolds.

Metrics include counts by effective mode, active proxies, query/push/contact counts,
impulse/reaction magnitude buckets, saturation/fallback/failure counts, scratch
high-water and stage time. Character/body IDs are not metric dimensions.

### 14. Limits and determinism are mode-specific

Profiles bound dynamic hits, distinct pushed bodies, impulse commands, proxy
contacts/contributors, active proxies, native/storage bytes, scratch and pending
reaction age. Exact numbers belong to implementation and cannot exceed Physics
capacities. Steady ticks allocate no heap memory or grow buffers.

Effective mode, algorithm/profile versions, query/proxy profiles, filter generation,
capsule/proxy inset, capacities, tie-break/reduction rules, impulse/reaction equations
and native solver tuple enter ADR-088's fingerprint.

`OneWayPush` qualification hashes canonical commands and resulting body/Character
state. `BidirectionalProxy` also hashes targets, reduced contacts, reaction and the
next-tick result. Native callback order/manifold bytes are not public evidence. An
unqualified mode fails or uses only its explicit fallback; it never silently
downgrades the scene tier.

### 15. Qualification covers principal and failure boundaries

Required regression evidence includes:

- four-mode visibility/push/reaction truth tables for light/heavy, awake/sleeping,
  supported/unsupported and filtered dynamics;
- absent interaction in `Disabled`, Character-only obstruction in `ObstacleOnly`,
  exactly one staged push in `OneWayPush` and one proxy collision in
  `BidirectionalProxy`;
- no double impulse and no baseline proxy collision with static, kinematic, sensor
  or Character pairs;
- root authority, one-tick reaction latency, no second move and wall-swept reaction;
- mass/profile/impulse limits, canonical multi-contact reduction, wake policy and
  saturation/overflow behavior;
- missing capabilities, explicit fallback, disabled mode and unqualified tiers in
  interactive/headless compositions;
- spawn/resize/stance/up/teleport, replace/reload, body/proxy removal, origin shift,
  failed activation, unload and shutdown at every partial phase;
- stale/reused generations and late callback/reaction rejection;
- render-cadence/catch-up replay, callback/job-order permutations and private
  adapter parity against Horo golden fixtures;
- maximum capacity, zero steady-tick allocation and measured fixed-stage cost.

Performance reports name mode, controller/dynamic/proxy counts, contact density,
fixed rate, solver/build/platform, p50/p95/max stage cost and buffer high water.

## Consequences

Character keeps one root authority while projects explicitly choose no dynamic
interaction, query-only obstruction, one-way push or solver-visible bidirectional
interaction. Fallback is inspectable, proxy contacts cannot double-apply pushes and
reaction is collision-resolved next tick instead of moving Character after Physics.

The cost is a private proxy lifecycle, separate query/simulation filtering, mode-
specific qualification and a one-tick-delayed kinematic reaction model rather than
exact dynamic two-body momentum. Authors must choose a mode and explicit fallback
instead of relying on a universal checkbox.

## Rejected Alternatives

### Make every Character a dynamic rigid body

Rejected because Physics would own the root and native contact/constraint behavior
would replace ADR-089's bounded sweep, step, slope and grounding semantics.

### Keep every controller query-only

Rejected because dynamics cannot see a query-only controller during the Physics
solve, so body-to-Character collision and reaction claims would be false.

### Install a proxy for every controller

Rejected because it adds bodies/contact work and changes simulation even for ghosts,
obstacle-only controllers and products that intentionally disable interaction.

### Copy the post-step proxy pose to Character

Rejected because a kinematic target is not a second root authority. Post-step copy
would bypass Horo collision resolution and create a hidden second movement pass.

### Apply query pushes and proxy impulses together

Rejected because the same pair could receive two order-dependent impulses. Each
mode assigns push authority to exactly one path.

### Apply proxy reaction immediately after Physics

Rejected because bodies have already stepped. Moving Character then creates an
unsolved overlap and an unqualified second query pass.

### Treat sensors as physical presence

Rejected because overlap evidence supplies no blocking impulse and cannot justify a
claim that dynamics physically collide with or push Character.

### Silently degrade unavailable proxy support

Rejected because obstacle-only, one-way and bidirectional behavior are observably
different contracts. Only an authored exact fallback may change effective mode.
