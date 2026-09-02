# ADR-022: AI Fixed-Tick Order, Authority and Simulation Budget

- **Status**: Proposed
- **Date**: 2026-08-28
- **Supersedes**: None
- **Scope**: AI simulation tick scheduling, perception-decision-navigation pipeline ordering, multiplayer host authority boundaries, simulation profiles and CPU budget allocation
- **Issue**: [GAI-005.1](https://github.com/abdullahbodur/horo-engine/issues/1358)
- **Jira**: [HORO-1358](https://horo-engine.atlassian.net/browse/HORO-1358)
- **Related**: [ADR-109](109-avoidance-crowd-and-renderer-independent-budget.md)
- **Normative documents**:
  - [Navigation And AI Architecture](../architecture/runtime/navigation-and-ai-architecture.md)
  - [ADR-024: Perception Ownership, Sense Policy and Budget](024-perception-ownership-sense-policy-and-budget.md)
  - [ADR-025: AI Decision Assets and Shared Gameplay Behavior Boundary](025-ai-decision-assets-and-gameplay-behavior-boundary.md)

## Context

Horo Engine's AI and navigation subsystems encompass sensory perception, blackboard knowledge representation, behavior evaluation (behavior trees, state machines, utility AI), pathfinding, dynamic obstacle avoidance, crowd simulation, and character locomotion integration.

Early documentation drafts introduced two architectural defects that require ratification and formal correction:

1. **Conflation of AI Simulation Budgets with Graphics Renderer Tiers**: Legacy specification drafts grouped AI agent limits and perception query capabilities under rendering API tiers (`es3`, `dx11`, `dx12_vulkan`, `high_end`). In Horo Engine's architecture, graphics backends (`OpenGL`, `Metal`, `Vulkan`, `NullRenderer`) are strictly presentation adapters. They possess zero ownership over CPU worker budgets, perception query capacity, or gameplay simulation authority. Headless dedicated servers run with `NullRenderer` but must be capable of executing full-scale, high-density AI simulations.
2. **Ambiguity in Fixed-Tick Phase Ordering and Authority Boundaries**: The execution order between sensory polling, blackboard synchronization, behavior evaluation, navigation intent generation, character controller physics locomotion, animation rig updates, and render extraction was not formally sequenced. Furthermore, in networked multiplayer configurations, the boundary between authoritative server-side AI evaluation and client-side visual presentation lacked an explicit privacy and authority rule.

[GAI-005.1] resolves these architectural requirements by establishing a normative decision for fixed-tick phase ordering, network host authority, simulation modes, and typed hardware-driven gameplay AI profiles.

## Decision

**AI simulation is strictly decoupled from graphics backends. Its four intent phases (`PerceptionSensePoll -> BlackboardSync -> AiDecisionEvaluate -> NavIntentCommit`) feed the common fixed-tick locomotion order (`AnimationPrePhysics -> CharacterControllerLocomotion -> PhysicsStep -> CharacterFinalize -> AnimationPostPhysics`). After tick commit, variable-rate `RenderExtraction` consumes immutable snapshots. In networked topologies, dedicated servers retain exclusive authority over AI decisions, perception and blackboards, while clients receive replicated transforms and animation targets for presentation-only interpolation. AI budgets are governed exclusively by typed `GameplayAiProfile` CPU/memory specifications.**

### 1. Simulation Tick Phase Order

Fixed-tick AI simulation executes in a strict, deterministic sequence within the fixed simulation step defined by [Runtime Lifecycle Architecture](../architecture/runtime/runtime-lifecycle.md):

```text
1. PerceptionSensePoll
     | (sensed raw stimuli)
     v
2. BlackboardSync
     | (synchronized knowledge & alerts)
     v
3. AiDecisionEvaluate
     | (behavior actions & target intent)
     v
4. NavIntentCommit
     | (pathfinding queries, steering & avoidance velocity)
     v
5. AnimationPrePhysics
     | (candidate pose, simulation IK & exact tick root motion)
     v
6. CharacterControllerLocomotion
     | (platform carry, bounded query movement & staged Physics commands)
     v
7. PhysicsStep
     | (rigid-body/contact/platform results)
     v
8. CharacterFinalize
     | (support, attachment & authoritative collision-root transform)
     v
9. AnimationPostPhysics
     | (typed pose overrides & final candidate pose)
     v
Presentation bridge (variable-rate): RenderExtraction
     | (immutable presentation snapshot)
```

Each fixed-tick phase and the following presentation bridge operate under explicit
ownership, input/output contracts, and invariants:

| Stage | Responsibility | Inputs | Outputs | Invariants & Constraints |
|---|---|---|---|---|
| Fixed phase 1 — `PerceptionSensePoll` | Polls sensory listeners defined by ADR-024 against event queues and spatial-query seams. | Sight line-of-sight candidates, hearing events, damage dispatches, touch/proximity overlaps, and team/affiliation events or bounded relay inputs | Staged `PerceivedStimulus` buffers per agent | Read-only with respect to blackboard and world transform states. Does not mutate behavior trees or blackboards. |
| Fixed phase 2 — `BlackboardSync` | Ingests staged stimuli, applies memory decay, updates target references, adjusts alert levels, and synchronizes external gameplay events into agent knowledge. | Staged `PerceivedStimulus` buffers, agent configuration | Updated `AIBlackboard` state | Agent blackboard mutation is isolated to this phase; prevents mid-decision race conditions. |
| Fixed phase 3 — `AiDecisionEvaluate` | Evaluates behavior trees, state machines, or utility AI against current blackboard state under ADR-025. | `AIBlackboard`, agent behavior definitions | High-level movement intent, combat actions, state transitions | Pure decision logic. Does not directly mutate physics bodies, move collision capsules, or perform rendering calls. |
| Fixed phase 4 — `NavIntentCommit` | Dispatches pathfinding requests, evaluates path-following waypoints, resolves the selected local safe-velocity capability, and computes kinematic movement vectors. | Movement intent, NavMesh spatial queries, dynamic obstacles | Kinematic velocity & direction vectors for character controllers | Submits steering commands to the character locomotion layer; does not step physics simulation directly. |
| Common phase 5 — `AnimationPrePhysics` | Evaluates authoritative animation graphs, simulation-affecting IK and exact-tick root motion from staged AI/gameplay parameters. | Action/locomotion intent, committed animation state, fixed delta | Candidate pose, events and one root-motion request | Runs once per attempted fixed tick under ADR-061; presentation sampling never feeds locomotion. |
| Common phase 6 — `CharacterControllerLocomotion` | Applies platform evidence and resolves Nav/Gameplay intent plus root motion with the bounded Horo query controller. | Movement/facing command, root motion, immutable Physics query/support snapshot | Candidate collision root, contacts and staged Physics commands | ADR-089 owns movement/orientation composition. It stages rather than commits transforms and never steps Physics directly. |
| Common phase 7 — `PhysicsStep` | Applies staged platform/Character commands and advances rigid-body simulation once. | Candidate Character/kinematic commands, fixed Physics world | Body/contact/platform and pose-override results | One fixed quantum; Character does not move a second time after this step. |
| Common phase 8 — `CharacterFinalize` | Finalizes support/platform attachment, bounded Character events and authoritative collision-root transform. | Candidate Character movement plus Physics step results | Final Character state/transform for tick commit | Updates next-tick platform evidence only; no post-Physics movement sweep. |
| Common phase 9 — `AnimationPostPhysics` | Applies typed Physics pose overrides and finalizes the candidate skeletal pose. | Candidate pose, committed Character root, Physics overrides | Final bone pose transforms and animation curves | Does not feed back into same-tick Character/Physics movement. All common state publishes atomically at tick commit. |
| Variable-rate bridge — `RenderExtraction` | Extracts immutable presentation snapshots of interpolated transforms and bone matrices for presentation rendering. | Previous and current committed simulation poses, interpolation $\alpha$ | Immutable `RenderSceneSnapshot` | Not a fixed-tick phase. Read-only and completely decoupled from fixed-tick mutation. |

### 2. Network Authority And Host Roles

AI execution and knowledge ownership vary strictly by host role:

```text
[ Dedicated Server ]
  - Authoritative PerceptionSensePoll
  - Authoritative BlackboardSync (PRIVATE)
  - Authoritative AiDecisionEvaluate (PRIVATE)
  - Authoritative NavIntentCommit & Avoidance
  - Authoritative AnimationPrePhysics root-motion/IK when configured
  - Authoritative CharacterControllerLocomotion
  - Authoritative PhysicsStep, CharacterFinalize & AnimationPostPhysics
         |
         | (Replicated Transforms, Velocity, Public Action Tags)
         v [ Network Transport ]
[ Remote Client ]
  - Presentation-only state receipt
  - Transform Interpolation (alpha)
  - Client presentation Animation sampling/blending
  - RenderExtraction -> Viewport Presentation
  (NO perception queries, NO blackboard, NO decision evaluation)
```

#### Host Roles

- **Standalone Host (Single-Player / Local Preview)**:
  - Executes all four AI intent phases and all five common authoritative locomotion phases locally, followed by the variable-rate `RenderExtraction` bridge in the same process.
  - The local simulation is the single authority for both gameplay decisions and presentation.
- **Dedicated Server Host (Headless Server-Authoritative Multiplayer)**:
  - Executes phases 1 through 9 as the sole authoritative simulation owner; authoritative Animation stages remain required when root motion, simulation IK, hitboxes or pose-driven Physics participate.
  - Generates network replication snapshots containing public agent state: `NetworkId`, committed `Transform`, linear/angular velocity, active public animation state tags, and gameplay health.
  - Headless dedicated servers omit only the presentation bridge and purely visual pose work.
  - Dedicated servers operate identically regardless of whether `NullRenderer` or a mock device is attached.
- **Client Host (Connected Multiplayer Client)**:
  - Acts strictly as a presentation-only consumer for server-replicated AI agents.
  - Receives replicated network snapshots and applies buffer interpolation over network jitter buffers.
  - Executes presentation-only Animation sampling/blending and the variable-rate `RenderExtraction` bridge; neither grants authority over server-owned AI.
  - **Forbidden on Client**: Clients NEVER execute `PerceptionSensePoll`, `BlackboardSync`, `AiDecisionEvaluate`, or `NavIntentCommit` for server-owned AI agents. Clients never invent authoritative movement or state transitions for AI.

#### Privacy and Security Boundary

Server perception data (sight frustums, hearing memory, target tracking weights) and `AIBlackboard` internals (internal state machine states, tactical threat scores, patrol index) are **server-private**.

- Network replication protocols MUST NOT serialize `AIBlackboard` or `PerceivedStimulus` structures to clients. Authenticated developer diagnostics may expose a separately redacted projection, but that tooling channel is not gameplay replication and is unavailable in shipping clients.
- Only observable gameplay properties (position, rotation, locomotion speed, visible equip state, public audio triggers) are replicated.
- This prevents client-side information leakage (e.g., radar/wallhacks revealing enemy AI awareness and stealth detection states).

### 3. Decoupling from Graphics Tiers: Gameplay AI Profiles

Gameplay decision cadence and perception workloads are governed by validated
project/host-role configuration, completely decoupled from the active graphics
backend. ADR-109 separately owns navigation crowd scale, quality, execution mode
and no-avoidance policy; ADR-108 owns dynamic-obstacle policy. This profile does not
duplicate either navigation authority.

```cpp
enum class AiLodSchedulingPolicy : uint8_t {
    FullRate,
    DistanceBands,
    PriorityAged,
};

struct GameplayAiProfile {
    std::string_view profileName;
    uint32_t         maxPerceptionQueriesPerTick;
    uint32_t         perceptionWorkerThreads;
    AiLodSchedulingPolicy lodSchedulingPolicy;
    float            highFrequencyRadius;      // meters: full-rate evaluation
    float            mediumFrequencyRadius;    // meters: 1/2 rate evaluation
    float            lowFrequencyRadius;       // meters: 1/4 rate evaluation
};
```

Standard engine profiles:

| Field | `LowCpu` | `MediumCpu` | `HighCpu` | `DedicatedServer` |
|---|---:|---:|---:|---:|
| Target CPU threads | 2–4 | 6–8 | 12+ | 16+ |
| `maxPerceptionQueriesPerTick` | 16 | 128 | 512 | 1,024 |
| `perceptionWorkerThreads` | 1 | 2 | 4 | 4 |
| `lodSchedulingPolicy` | `DistanceBands` | `DistanceBands` | `DistanceBands` | `PriorityAged` |
| `highFrequencyRadius` | 15 m | 25 m | 35 m | 0 (unused) |
| `mediumFrequencyRadius` | 35 m | 60 m | 90 m | 0 (unused) |
| `lowFrequencyRadius` | 60 m | 120 m | 180 m | 0 (unused) |

The four built-in profiles are exact defaults, not open-ended ranges; each column
name is its `profileName`. Projects may derive validated custom profiles for
larger hosts. `FullRate` disables time-slicing and ignores zero-valued radii.
`DistanceBands` requires strictly increasing positive radii. `PriorityAged`
requires all three radii to be zero and uses ADR-024's weighted fair queue with
deadline aging so low-priority agents cannot starve.

`maxPerceptionQueriesPerTick` is the aggregate admission cap for costly spatial
or physics queries issued by `PerceptionSensePoll`. ADR-024's
`maxSightRaycastsPerTick` is the sight-line-of-sight subset and must be less than
or equal to this aggregate cap. Event delivery itself does not consume a query;
any follow-up LOS, overlap, or spatial lookup does. This distinction keeps Sight,
Hearing, Damage, Touch/Proximity, and Team/Affiliation on one bounded contract.

**Architectural Invariant**: The active graphics backend (`OpenGL`, `Metal`, `Vulkan`, `NullRenderer`) grants **zero** AI capacity, perception query fidelity, or gameplay authority. Selecting Vulkan over OpenGL or running in headless mode never alters the agent capacity or perception limits of a project.

### 4. Deterministic vs Best-Effort Simulation Modes

The engine provides two explicit simulation scheduling modes for AI:

| Simulation Mode | Characteristics | Primary Use Cases | Allowed Host Roles |
|---|---|---|---|
| **Deterministic Fixed-Tick** | Strict lockstep execution. Every active agent evaluates perception, blackboard, decision, and navigation on every fixed tick in deterministic entity-ID order. Time-slicing skips and non-deterministic heuristics are prohibited. Fixed simulation delta is constant. | Deterministic lockstep multiplayer, replay recording and bit-identical verification playback, automated AI regression test suites. | Standalone, Dedicated Server, Headless Automated Test Runner |
| **Best-Effort Bounded Time-Slicing** | Distance- and significance-based Level of Detail (Simulation LOD). Agents within `highFrequencyRadius` update every tick; distant agents update every $N$ ticks with bounded maximum latency. Pathfinding and perception requests are amortized across job system worker queues with fixed per-tick execution budgets. | High-density open-world scenes, large scale RTS/RPG titles, single-player games exceeding per-tick compute budgets. | Standalone, Dedicated Server |

Client hosts running in networked multiplayer execute neither mode for remote AI; they perform presentation-only state interpolation.

## Verification Requirements

- Lifecycle tests assert exactly six ordered fixed-tick phases and prove that
  `RenderExtraction` runs only after commit without mutating simulation state.
- Host-role tests assert that dedicated servers never require extraction and that
  clients cannot submit perception, blackboard, decision, or navigation authority
  for server-owned agents.
- Profile fixtures initialize every field to the table values and reject invalid
  perception worker counts, non-canonical radius/policy combinations, and
  perception budgets where
  `maxSightRaycastsPerTick > maxPerceptionQueriesPerTick`. ADR-109 fixtures own
  navigation crowd scale/quality/mode/fallback validation.
- Replication schema tests reject `AIBlackboard` and `PerceivedStimulus` fields in
  shipping client payloads.

## Consequences

- **Architecture Integrity**: Graphics rendering backends are cleanly isolated to presentation and have zero influence over gameplay simulation, perception, or agent counts.
- **Dedicated Server Parity**: Dedicated servers running headless with `NullRenderer` can utilize the full CPU budget to host massive agent populations without artificial graphics-tier constraints.
- **Deterministic Replay Safety**: Deterministic tick mode guarantees repeatable replay recording and regression test validation.
- **Multiplayer Security**: Server-private blackboards and perception data prevent client-side reverse-engineering and cheating while minimizing network bandwidth consumption.
- **Predictable Performance**: Typed `GameplayAiProfile` and independent ADR-109
  navigation profiles prevent CPU starvation by explicitly bounding admitted work.

## Rejected Alternatives

- **Retaining Graphics-Tier Names (`es3`, `dx11`, `dx12_vulkan`, `high_end`) for AI Limits**: Rejected. Graphics APIs represent GPU rendering capabilities, not CPU core counts or server memory capacity. A headless dedicated server has no GPU backend yet requires the highest AI capacity.
- **Client-Side Authoritative AI Evaluation with Server Correction**: Rejected. Running authoritative AI on clients introduces massive desynchronization risks, increases client CPU load, and opens multiplayer games to client-side decision tampering.
- **Evaluating AI Decisions in Variable-Rate Frame Update**: Rejected. Variable update rates fluctuate with rendering performance, which destroys simulation determinism, causes variable behavior across framerates, and breaks physics locomotion synchronization.
- **Replicating Raw Perception Buffers and Blackboards over Network**: Rejected. Exposes private sensory data (e.g. stealth detection cones) to potential client cheats and saturates network bandwidth with ephemeral intermediate data.
