# ADR-011: AI Fixed-Tick Order, Authority and Simulation Budget

- **Status**: Proposed
- **Date**: 2026-08-28
- **Supersedes**: None
- **Scope**: AI simulation tick scheduling, perception-decision-navigation pipeline ordering, multiplayer host authority boundaries, simulation profiles and CPU budget allocation
- **Issue**: [#1358](https://github.com/abdullahbodur/horo-engine/issues/1358) ([GAI-005.1])
- **Normative document**: [Navigation And AI Architecture](../architecture/runtime/navigation-and-ai-architecture.md)

## Context

Horo Engine's AI and navigation subsystems encompass sensory perception, blackboard knowledge representation, behavior evaluation (behavior trees, state machines, utility AI), pathfinding, dynamic obstacle avoidance, crowd simulation, and character locomotion integration.

Early documentation drafts introduced two architectural defects that require ratification and formal correction:

1. **Conflation of AI Simulation Budgets with Graphics Renderer Tiers**: Legacy specification drafts grouped AI agent limits and perception query capabilities under rendering API tiers (`es3`, `dx11`, `dx12_vulkan`, `high_end`). In Horo Engine's architecture, graphics backends (`OpenGL`, `Metal`, `Vulkan`, `NullRenderer`) are strictly presentation adapters. They possess zero ownership over CPU worker budgets, perception query capacity, or gameplay simulation authority. Headless dedicated servers run with `NullRenderer` but must be capable of executing full-scale, high-density AI simulations.
2. **Ambiguity in Fixed-Tick Phase Ordering and Authority Boundaries**: The execution order between sensory polling, blackboard synchronization, behavior evaluation, navigation intent generation, character controller physics locomotion, animation rig updates, and render extraction was not formally sequenced. Furthermore, in networked multiplayer configurations, the boundary between authoritative server-side AI evaluation and client-side visual presentation lacked an explicit privacy and authority rule.

[GAI-005.1] resolves these architectural requirements by establishing a normative decision for fixed-tick phase ordering, network host authority, simulation modes, and typed hardware-driven gameplay AI profiles.

## Decision

**AI simulation is strictly decoupled from graphics backends and executes within fixed simulation ticks across an explicit seven-phase sequence (`PerceptionSensePoll -> BlackboardSync -> AiDecisionEvaluate -> NavIntentCommit -> CharacterControllerLocomotion -> AnimationRigUpdate -> RenderExtraction`). In networked topologies, dedicated servers retain exclusive authority over AI decisions, perception, and blackboards, while clients receive replicated transforms and animation targets for presentation-only interpolation. AI simulation budgets are governed exclusively by typed `GameplayAiProfile` specifications based on available CPU worker threads and memory constraints.**

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
5. CharacterControllerLocomotion (Physics Integration)
     | (resolved world transforms & velocities)
     v
6. AnimationRigUpdate
     | (skeletal pose evaluation)
     v
7. RenderExtraction
     | (immutable presentation snapshot)
```

Each phase operates under explicit ownership, input/output contracts, and phase invariants:

| Phase | Phase Responsibility | Inputs | Outputs | Phase Invariants & Constraints |
|---|---|---|---|---|
| `PerceptionSensePoll` | Polls sensory listeners (sight cones, hearing spheres, proximity triggers, damage events) against scene physics and spatial query accelerators. | Physics scene line-of-sight, audio event buffers, damage dispatch queue | Staged `PerceivedStimulus` buffers per agent | Read-only with respect to blackboard and world transform states. Does not mutate behavior trees or blackboards. |
| `BlackboardSync` | Ingests staged stimuli, applies memory decay, updates target references, adjusts alert levels, and synchronizes external gameplay events into agent knowledge. | Staged `PerceivedStimulus` buffers, agent configuration | Updated `AIBlackboard` state | Agent blackboard mutation is isolated to this phase; prevents mid-decision race conditions. |
| `AiDecisionEvaluate` | Evaluates behavior trees, finite state machines, or utility AI against current blackboard state to select tactical actions. | `AIBlackboard`, agent behavior definitions | High-level movement intent, combat actions, state transitions | Pure decision logic. Does not directly mutate physics bodies, move collision capsules, or perform rendering calls. |
| `NavIntentCommit` | Dispatches pathfinding requests, evaluates path-following waypoints, resolves local dynamic obstacle avoidance (RVO/crowd), and computes kinematic movement vectors. | Movement intent, NavMesh spatial queries, dynamic obstacles | Kinematic velocity & direction vectors for character controllers | Submits steering commands to the character locomotion layer; does not step physics simulation directly. |
| `CharacterControllerLocomotion` | Executes kinematic character controller updates within the physics tick. Resolves terrain contact, collisions, stepping, and slope limits. | Kinematic velocity vectors, physics collision geometry | Committed world transforms, contact normals, linear/angular velocities | Physics collision and transform updates become authoritative for the current simulation tick. |
| `AnimationRigUpdate` | Evaluates skeletal animation blend trees, locomotion state machines, and procedural IK based on committed locomotion velocity and action states. | Committed transforms, locomotion velocities, action state tags | Final bone pose transforms and animation curves | Operates on committed simulation output. Does not feed back into physics locomotion within the same tick. |
| `RenderExtraction` | Extracts immutable presentation snapshots of interpolated transforms and bone matrices for presentation rendering. | Previous and current simulation poses, interpolation $\alpha$ | Immutable `RenderSceneSnapshot` | Read-only presentation pass. Executed during variable-rate frame update, completely decoupled from fixed-tick mutation. |

### 2. Network Authority And Host Roles

AI execution and knowledge ownership vary strictly by host role:

```text
[ Dedicated Server ]
  - Authoritative PerceptionSensePoll
  - Authoritative BlackboardSync (PRIVATE)
  - Authoritative AiDecisionEvaluate (PRIVATE)
  - Authoritative NavIntentCommit & Avoidance
  - Authoritative CharacterControllerLocomotion
         |
         | (Replicated Transforms, Velocity, Public Action Tags)
         v [ Network Transport ]
[ Remote Client ]
  - Presentation-only state receipt
  - Transform Interpolation (alpha)
  - Client AnimationRigUpdate (visual blending)
  - RenderExtraction -> Viewport Presentation
  (NO perception queries, NO blackboard, NO decision evaluation)
```

#### Host Roles

- **Standalone Host (Single-Player / Local Preview)**:
  - Executes all seven phases locally in the single process.
  - The local simulation is the single authority for both gameplay decisions and presentation.
- **Dedicated Server Host (Headless Server-Authoritative Multiplayer)**:
  - Executes phases 1 through 5 (`PerceptionSensePoll` through `CharacterControllerLocomotion`) as the sole authoritative simulation owner.
  - Generates network replication snapshots containing public agent state: `NetworkId`, committed `Transform`, linear/angular velocity, active public animation state tags, and gameplay health.
  - Headless dedicated servers omit phases 6 and 7 (or run skeletal LOD only when required for authoritative hitboxes).
  - Dedicated servers operate identically regardless of whether `NullRenderer` or a mock device is attached.
- **Client Host (Connected Multiplayer Client)**:
  - Acts strictly as a presentation-only consumer for server-replicated AI agents.
  - Receives replicated network snapshots and applies buffer interpolation over network jitter buffers.
  - Executes `AnimationRigUpdate` and `RenderExtraction` for visual presentation.
  - **Forbidden on Client**: Clients NEVER execute `PerceptionSensePoll`, `BlackboardSync`, `AiDecisionEvaluate`, or `NavIntentCommit` for server-owned AI agents. Clients never invent authoritative movement or state transitions for AI.

#### Privacy and Security Boundary

Server perception data (sight frustums, hearing memory, target tracking weights) and `AIBlackboard` internals (internal state machine states, tactical threat scores, patrol index) are **server-private**.

- Network serialization protocols MUST NOT serialize `AIBlackboard` or `PerceivedStimulus` structures to clients by default.
- Only observable gameplay properties (position, rotation, locomotion speed, visible equip state, public audio triggers) are replicated.
- This prevents client-side information leakage (e.g., radar/wallhacks revealing enemy AI awareness and stealth detection states).

### 3. Decoupling from Graphics Tiers: Gameplay AI Profiles

AI agent capacities and perception workloads are governed exclusively by hardware compute capabilities (CPU worker threads, core count, cache hierarchy, and host memory budgets), completely decoupled from the active graphics backend.

```cpp
struct GameplayAiProfile {
    std::string_view profileName;
    uint32_t         maxActiveNavMeshAgents;
    uint32_t         maxDynamicObstacles;
    uint32_t         maxPerceptionQueriesPerTick;
    uint32_t         pathfindingWorkerThreads;
    uint32_t         perceptionWorkerThreads;
    bool             enableCrowdSimulation;
    bool             enableHierarchicalPathfinding;
    bool             enableTimeSlicedLOD;
    float            highFrequencyRadius;      // meters: full-rate evaluation
    float            mediumFrequencyRadius;    // meters: 1/2 rate evaluation
    float            lowFrequencyRadius;       // meters: 1/4 rate evaluation
};
```

Standard engine profiles:

| Profile | Target Hardware / Host Role | Max Active Agents | Dynamic Obstacles | Pathfinding Budget | Perception Budget | Simulation LOD Strategy |
|---|---|---|---|---|---|---|
| `LowCpu` | 2–4 CPU threads, mobile / embedded standalone | 64 | 32 | 1 Worker / Serial | 16 queries / tick | Aggressive Time-Slicing (3 Distance Tiers) |
| `MediumCpu` | 6–8 CPU threads, mainstream desktop / console | 512 | 128 | Job System Parallel | 128 queries / tick | Time-Slicing (2 Distance Tiers) |
| `HighCpu` | 12+ CPU threads, high-end desktop standalone | 2,048 | 512 | Job System Parallel | 512 queries / tick | Full-rate near; Time-sliced far |
| `DedicatedServer` | Headless Linux/Windows dedicated server instance | 4,096+ | 1,024+ | Job System Parallel | 1,024+ queries / tick | Priority-based Time-Slicing + Full Headless Budget |

**Architectural Invariant**: The active graphics backend (`OpenGL`, `Metal`, `Vulkan`, `NullRenderer`) grants **zero** AI capacity, perception query fidelity, or gameplay authority. Selecting Vulkan over OpenGL or running in headless mode never alters the agent capacity or perception limits of a project.

### 4. Deterministic vs Best-Effort Simulation Modes

The engine provides two explicit simulation scheduling modes for AI:

| Simulation Mode | Characteristics | Primary Use Cases | Allowed Host Roles |
|---|---|---|---|
| **Deterministic Fixed-Tick** | Strict lockstep execution. Every active agent evaluates perception, blackboard, decision, and navigation on every fixed tick in deterministic entity-ID order. Time-slicing skips and non-deterministic heuristics are prohibited. Fixed simulation delta is constant. | Deterministic lockstep multiplayer, replay recording and bit-identical verification playback, automated AI regression test suites. | Standalone, Dedicated Server, Headless Automated Test Runner |
| **Best-Effort Bounded Time-Slicing** | Distance- and significance-based Level of Detail (Simulation LOD). Agents within `highFrequencyRadius` update every tick; distant agents update every $N$ ticks with bounded maximum latency. Pathfinding and perception requests are amortized across job system worker queues with fixed per-tick execution budgets. | High-density open-world scenes, large scale RTS/RPG titles, single-player games exceeding per-tick compute budgets. | Standalone, Dedicated Server |

Client hosts running in networked multiplayer execute neither mode for remote AI; they perform presentation-only state interpolation.

## Consequences

- **Architecture Integrity**: Graphics rendering backends are cleanly isolated to presentation and have zero influence over gameplay simulation, perception, or agent counts.
- **Dedicated Server Parity**: Dedicated servers running headless with `NullRenderer` can utilize the full CPU budget to host massive agent populations without artificial graphics-tier constraints.
- **Deterministic Replay Safety**: Deterministic tick mode guarantees repeatable replay recording and regression test validation.
- **Multiplayer Security**: Server-private blackboards and perception data prevent client-side reverse-engineering and cheating while minimizing network bandwidth consumption.
- **Predictable Performance**: Typed `GameplayAiProfile` budgets prevent CPU starvation by explicitly capping parallel pathfinding and perception job submissions.

## Rejected Alternatives

- **Retaining Graphics-Tier Names (`es3`, `dx11`, `dx12_vulkan`, `high_end`) for AI Limits**: Rejected. Graphics APIs represent GPU rendering capabilities, not CPU core counts or server memory capacity. A headless dedicated server has no GPU backend yet requires the highest AI capacity.
- **Client-Side Authoritative AI Evaluation with Server Correction**: Rejected. Running authoritative AI on clients introduces massive desynchronization risks, increases client CPU load, and opens multiplayer games to client-side decision tampering.
- **Evaluating AI Decisions in Variable-Rate Frame Update**: Rejected. Variable update rates fluctuate with rendering performance, which destroys simulation determinism, causes variable behavior across framerates, and breaks physics locomotion synchronization.
- **Replicating Raw Perception Buffers and Blackboards over Network**: Rejected. Exposes private sensory data (e.g. stealth detection cones) to potential client cheats and saturates network bandwidth with ephemeral intermediate data.
