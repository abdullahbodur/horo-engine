# ADR-011: Environment Query Ownership, Item and Scoring Model

- **Status**: Proposed
- **Date**: 2026-08-28
- **Supersedes**: None
- **Scope**: Environment Query System (EQS), query templates/plans, item types, contexts, generators, tests, score normalization, subsystem provider boundaries, time-budgeted execution, caching, determinism, and cancellation
- **Issue**: [#1346](https://github.com/abdullahbodur/horo-engine/issues/1346) ([GAI-004.1])
- **JIRA**: HORO-1785
- **Normative document**: [Navigation And AI Architecture](../architecture/runtime/navigation-and-ai-architecture.md)

## Context

Tactical AI agents require spatial reasoning to select positions in the world (such as finding cover, establishing flanking angles, maintaining line of sight to a target, or choosing patrol vantage points). Without a formal tactical query framework, gameplay code tends to embed ad hoc physics raycasts, NavMesh traversability checks, perception scans, and scoring heuristics directly inside behavior trees, state machines, or tick callbacks. This causes:

1. **Distributed and competing ownership**: Navigation, Physics, and Perception systems are polled ad hoc without unified scheduling or lifecycle tracking.
2. **Fixed-tick stalls and unbounded frame overhead**: Synchronous world queries (e.g. dozens of line-of-sight raycasts and NavMesh path computations) stall the simulation thread when multiple agents make tactical decisions simultaneously.
3. **Inconsistent and non-deterministic scoring**: Ad hoc scoring formulas produce non-normalized values, suffer from arithmetic anomalies (NaN/Inf), and lack deterministic tie-breaking.
4. **Lifecycle and memory leaks**: When an agent or target entity is destroyed during an in-flight query, callbacks access dangling pointers or leave background work orphaned.
5. **Lack of caching and deduplication**: Identical tactical queries (e.g. multiple squad members searching for cover near the same anchor point) duplicate expensive geometric tests across ticks.

[GAI-004.1] requires an authoritative architecture decision freezing the tactical query model, defining query templates, items, contexts, generators, tests, score normalization, provider separation, asynchronous time-budgeted execution, deterministic replay, caching, and lifecycle cancellation before subsequent GAI-004 implementation tickets proceed.

## Decision

**The Gameplay AI subsystem (`HoroAI` / `EnvironmentQueryManager`) is the sole authority owning query orchestration, stage scheduling, test evaluation, score normalization, result aggregation, and terminal outcomes. Navigation, Physics, and Perception systems act strictly as read-only snapshot providers. Behavior trees and decision graphs consume query results asynchronously via typed handles without blocking fixed-tick simulation. All scoring is normalized to `[0.0, 1.0]` with strict deterministic tie-breaking cascades, and queries execute under per-tick time budgets with cooperative cancellation on entity or scene destruction.**

### 1. Subsystem Ownership and Separation of Concerns

| Subsystem / Authority | Role in Environment Queries | Forbidden Actions |
|---|---|---|
| **`EnvironmentQueryManager` (GAI)** | **Single authority** owning query lifecycle, query plans, stage scheduling, candidate generation, test execution, scoring normalization, cache management, and terminal outcome delivery. | Does not own collision geometry, NavMesh geometry, or sensory stimulus stores. |
| **`NavigationSystem`** | **Read-only provider** for NavMesh surface projection, polygon containment, traversability, and pathfinding distance/cost calculation. | Does not execute query plans, score candidates, or manage tactical decision state. |
| **`PhysicsSystem`** | **Read-only provider** for geometric raycasts, shape sweeps, line-of-sight checks, and scene collision overlap queries. | Does not orchestrate multi-stage AI queries or rank items. |
| **`PerceptionSystem`** | **Read-only provider** for perceived entities, stimulus locations, sensory memory records, and visibility states. | Does not generate spatial grids or evaluate navigation costs. |
| **Decision Graphs / Behavior Trees** | **Consumers** requesting query execution via asynchronous task nodes and reading immutable `QueryResult` snapshots. | Never synchronously wait on queries or mutate query execution state during fixed ticks. |

### 2. Query Asset, Template, and Item Model

1. **Query Template (`EnvironmentQueryTemplate` / `EnvironmentQueryPlan`)**:
   - Immutable, versioned compiled data assets.
   - Declares primary item type, input context requirements, generator stages, ordered test/filter stages, normalization rules, and time budgets.
   - Stages possess stable unique `StageId`s independent of editor ordering.
2. **Item Types (`EnvQueryItemType`)**:
   - `Point` (`WorldCoordinate`): 3D spatial points (cover positions, tactical vantage points, ambush locations).
   - `Actor` / `Entity` (`EntityId`): Scene entities (potential targets, allies, cover obstacles, interactive items).
   - `DirectionalRay` (`Ray` / `Direction`): Vectors or bearings (flanking trajectories, escape angles, aim vectors).
   - `Custom`: Extensible payload with explicit schema for game-specific tactical constructs.
3. **Query Contexts (`EnvQueryContext`)**:
   - Built-in contexts: `Querier` (requesting agent), `Target` (primary focus/adversary), `QuerierLocation`, `TargetLocation`, `WorldOrigin`.
   - Pluggable custom contexts: resolved by registered context providers (e.g. `SquadLeaderContext`, `CombatAnchorContext`, `LastKnownEnemyLocationContext`).
   - Context values are snapshotted into an immutable `QueryContextSnapshot` at query start. Contexts never expose raw mutable scene pointers.

### 3. Generators and Tests

1. **Generators (`EnvQueryGenerator`)**:
   - Produce candidate items bounded by explicit configuration parameters.
   - Built-in generators:
     - `GridGenerator`: 2D/3D regular lattice around context with defined radius, density/spacing, and layer masks.
     - `DonutRingGenerator`: Concentric radial rings around context with inner radius, outer radius, and radial step counts.
     - `ConeGenerator`: Wedge/cone oriented along context forward vector with radius, angle, and angular steps.
     - `NavMeshProjectionGenerator`: Projects spatial candidate points onto valid NavMesh surfaces, filtering unreachable geometry.
     - `PerceivedEntitiesGenerator`: Populates candidate items from agent perception memory matching filter criteria.
   - All generators enforce strict maximum item limits (`maxItems`) to prevent unbounded memory allocation and CPU spikes.
2. **Tests and Filters (`EnvQueryTest`)**:
   - `LineOfSightTest`: Raycast or shape sweep between candidate item and target/querier context using Physics traces.
   - `DistanceTest`: Euclidean, Chebyshev, Manhattan, or NavMesh path distance between item and context.
   - `DotProductTest`: Directional alignment (facing angle, field of view, flanking angle).
   - `PathfindingCostTest`: NavMesh path length, travel cost, or reachability.
   - `CoverExposureTest`: Obstacle height, stance clearance, and exposure angle relative to threat positions.
   - Test Modes:
     - `FilterOnly`: Discards items failing a boolean threshold (e.g. must have line of sight).
     - `ScoreOnly`: Assigns normalized continuous score `[0.0, 1.0]` without discarding.
     - `FilterAndScore`: Discards non-qualifying items and scores survivors.

### 4. Scoring Normalization and Deterministic Tie-Breaking

1. **Score Normalization**:
   - Every scoring test normalizes raw values to `[0.0, 1.0]` using explicit scoring functions:
     - `Linear`: `(val - min) / (max - min)`
     - `InverseLinear`: `1.0 - (val - min) / (max - min)`
     - `Sigmoid` / `SCurve`: Smooth progression emphasizing mid-ranges.
     - `ThresholdStep`: Step function returning `0.0` or `1.0`.
   - Arithmetic Safety: Non-finite scores (`NaN`, `+Inf`, `-Inf`) and division-by-zero are trapped; offending items are assigned `0.0` with diagnostic reporting and cannot rank as winning candidates.
2. **Total Score Aggregation**:
   - Weighted linear combination:
     $$\text{TotalScore} = \frac{\sum_{i=1}^{N} w_i \cdot s_i}{\sum_{i=1}^{N} w_i}$$
     where $w_i \ge 0$ is the stage weight and $s_i \in [0.0, 1.0]$ is the normalized test score.
3. **Deterministic Tie-Breaking Cascade**:
   - Candidate ranking must be 100% deterministic and independent of worker thread scheduling or memory address order.
   - Ranking order:
     1. Primary: Highest `TotalScore`.
     2. Secondary: Configured tie-breaker test score (e.g. closest distance to Querier).
     3. Tertiary: Stable item generation sequence index (`item_index`), ensuring zero jitter across replay runs.

### 5. Time-Budgeted Asynchronous Execution and Lifecycles

```text
+-------------------------------------------------------------------------+
|                       EnvironmentQueryManager                           |
|  +-------------------------------------------------------------------+  |
|  | Tick (Time-Sliced Budget: e.g. 1.5ms / max N items per frame)      |  |
|  +-------------------------------------------------------------------+  |
|         |                     |                      |                  |
|  +--------------+     +----------------+     +------------------+       |
|  | Query State: |     | Query State:   |     | Query State:     |       |
|  | Resolving    | --> | Generating /   | --> | Scoring /        |       |
|  | Context      |     | Nav Projection |     | Physics Traces   |       |
|  +--------------+     +----------------+     +------------------+       |
|                                                      |                  |
|                                              +------------------+       |
|                                              | Query State:     |       |
|                                              | Completed /      |       |
|                                              | PartialSuccess   |       |
|                                              +------------------+       |
+-------------------------------------------------------------------------+
```

1. **Asynchronous Multi-Frame Staging**:
   - Queries execute across multiple frames using cooperative time-slicing under a configured global/per-tick time budget (e.g. `1.5ms` per simulation tick).
   - High-load queries yield at stage boundaries or item batch boundaries and resume on subsequent ticks.
2. **Terminal States**:
   - `Completed`: Query evaluated all stages and returned ranked items.
   - `PartialSuccess`: Budget exhausted or early exit requested; returns best-ranked valid candidate evaluated so far.
   - `Cancelled`: Query cancelled cooperatively by caller token or owning task.
   - `TimedOut`: Query exceeded maximum allowed elapsed frames without finding a valid candidate.
   - `Aborted`: Querier, target entity, or scene was destroyed during in-flight evaluation.
3. **Lifecycle Safety and Cancellation**:
   - Queries hold weak `EntityId` handles and scene generation counters.
   - On entity destruction or scene transition, in-flight queries transition to `Aborted` immediately; outstanding asynchronous jobs, raycasts, and pathfinding requests are cancelled.
   - Result callbacks and decision nodes observe typed failure rather than executing against invalid entities.
4. **Caching and Invalidation**:
   - Immutable query results may be cached using a composite key:
     $$\text{CacheKey} = (\text{QueryTemplateId}, \text{ContextSnapshotHash}, \text{NavMeshRevision}, \text{PhysicsRevision})$$
   - Bounded LRU cache with time-to-live (TTL).
   - Cache eviction never invalidates caller-owned `QueryResult` snapshots.

## Ratify-or-Revise Outcomes

| Area | Current Baseline | Outcome |
|---|---|---|
| Query orchestration authority | Ad hoc raycasts and navigation queries scattered in gameplay code | **Revised.** `EnvironmentQueryManager` is established as the sole orchestration authority. |
| Subsystem boundaries | Direct synchronous physics/navmesh calls | **Ratified separation.** Navigation, Physics, and Perception provide read-only data snapshots; GAI executes queries. |
| Item types | Implicit spatial coordinates only | **Revised.** Explicit typed items (`Point`, `Actor`/`Entity`, `DirectionalRay`, `Custom`). |
| Scoring normalization | Unbounded raw values, potential division by zero | **Revised.** Mandatory normalization to `[0.0, 1.0]`, NaN/Inf traps, and deterministic tie-breaking cascades. |
| Execution threading | Synchronous single-tick queries risking frame drops | **Revised.** Asynchronous time-budgeted multi-frame staging with cooperative cancellation. |
| Lifecycle safety | Unchecked raw entity handles | **Revised.** Weak entity tracking, scene generation counters, and automatic abortion on destruction. |

## Consequences

- AI tactical decision making is decoupled from low-level physics and navigation details.
- Frame rates remain stable under heavy AI load due to strict time budgeting and asynchronous staging.
- Tactical queries produce 100% deterministic results across replay runs.
- Behavior trees and decision graphs interact through clean, non-blocking async task nodes.
- Memory and entity lifetimes are protected against dangling references and orphaned background jobs.

## Rejected Alternatives

- **Let Physics or Navigation subsystem own tactical queries.** Rejected: violates single responsibility and couples world geometry to gameplay decision logic.
- **Run all queries synchronously in a single frame.** Rejected: causes catastrophic simulation frame drops when multiple agents query simultaneously.
- **Use unbounded floating-point scores without normalization.** Rejected: leads to weighted combination imbalances, arithmetic overflow, and non-deterministic behavior.
- **Rely on memory address or hash map iteration order for candidate ranking.** Rejected: creates non-deterministic simulation divergence across different platforms and runs.
- **Allow queries to hold direct scene pointers.** Rejected: leads to use-after-free crashes when entities or components are removed during query execution.
