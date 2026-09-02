# ADR-123: VFX CPU Stage Order, Determinism and Gameplay Coupling

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: Gameplay-coupled CPU particle step stages, stage authority/access, deterministic particle identity and random streams, parallel scheduling/reductions, typed gameplay payloads, commit/failure semantics and CPU/GPU equivalence baseline
- **Issue**: [VFX-002.1](https://github.com/abdullahbodur/horo-engine/issues/1750)
- **Jira**: [HORO-1707](https://horo-engine.atlassian.net/browse/HORO-1707)
- **Related**: [ADR-008](008-error-model-exception-boundary-and-registry.md), [ADR-011](011-vfx-effect-ownership-simulation-domain-and-renderer-boundary.md), [ADR-022](022-ai-fixed-tick-order-authority-and-simulation-budget.md), [ADR-026](026-large-world-precision-and-floating-origin-strategy.md), [ADR-088](088-physics-determinism-capability-and-support-tiers.md)
- **Normative documents**: [VFX and Particles Architecture](../architecture/runtime/vfx-and-particles-architecture.md), [Runtime Lifecycle](../architecture/runtime/runtime-lifecycle.md), [Gameplay Module Boundary](../architecture/extensions/gameplay-module-boundary.md)

## Context

ADR-011 assigns logical VFX/CPU state to scene-owned `VfxWorld`, requires real CPU
simulation for gameplay-coupled/headless emitters and forbids authoritative GPU
readback. It establishes fixed versus presentation clock domains, bounded queue
admission and immutable extraction, but not the exact CPU kernel order or which stage
may read/write each attribute.

The current VFX graph groups spawn, update and output contexts. Independent compiler
or runtime implementations could therefore integrate before applying forces, kill
before collision events, compact in worker-completion order or extract a partially
failed step. A mutable emitter-wide random generator would also make results depend
on job partitioning, branch/call count and particle death/reuse.

Gameplay coupling needs a narrower boundary than mutable custom particle storage.
Allowing gameplay code to inspect or write the CPU SoA during a step would violate
VfxWorld ownership, make parallel scheduling unsafe and give headless/GPU paths
different authority. This ADR fixes the CPU baseline used by gameplay and future
CPU/GPU equivalence qualification.

## Decision

### 1. One attempted CPU step has seven ordered stages

Every gameplay-coupled CPU emitter executes exactly this logical order for one
admitted fixed tick:

```text
Spawn -> Initialize -> Forces -> Integrate -> Collide -> Kill -> Extract
```

`Extract` follows successful simulation commit and is read-only; it is included in
the pipeline order but is not a mutable particle stage. Cosmetic CPU emitters use the
same stage semantics in their selected presentation clock. One emitter unit is never
advanced on both clocks, and render view count never repeats a stage.

The compiled kernel plan declares stage reads/writes, required inputs, bounded output
counts and scratch. Cook validates every dependency before publication. Runtime may
fuse adjacent kernels or vectorize/parallelize ranges only when the observable result
is identical to this stage barrier order under the asset's qualified numeric contract.

### 2. Stage authority and access are fixed

| Stage | Final owner and permitted mutation | Inputs | Published/staged output |
|---|---|---|---|
| `Spawn` | `VfxWorld` queue/admission and emitter allocator | Frozen accepted request prefix, rate/burst state, committed emitter age/tick, capacity | Deterministic birth count, monotonic spawn ordinals and reserved inactive slots; no partially live particles |
| `Initialize` | `CpuParticleSimulator` compiled initialization kernels | Emitter descriptor, typed parameter snapshot, spawn ordinal/particle ID, transform/origin snapshot, semantic RNG channels | Every required attribute for each new particle initialized exactly once and marked live in staging |
| `Forces` | `CpuParticleSimulator` force kernels | Previous committed/new initialized attributes, immutable field/input snapshots, fixed delta | Staged acceleration/velocity contributions only; no position, collision, death or gameplay publication |
| `Integrate` | `CpuParticleSimulator` integration kernel | Initialized attributes plus completed force result and fixed delta | Staged velocity, position, age, rotation/size/curve state; no environment query |
| `Collide` | `CpuParticleSimulator` with injected immutable query adapter | Integrated candidate, captured Physics/scene query snapshot and origin epoch | Deterministically ordered collision facts and staged response state; no Physics/ECS mutation |
| `Kill` | `CpuParticleSimulator` lifecycle/compaction owner | Post-collision state, age/lifetime, explicit kill flags and admitted capacity | Stable survivor set, death/collision/sub-emitter occurrences and canonical packed order |
| `Extract` | `VfxRenderExtractor` | Successfully committed post-kill state and matching scene/step/origin generation | Immutable frame-owned render data and typed committed gameplay event/aggregate snapshots; no SoA mutation |

Requests or sub-emitter births produced during Initialize/Collide/Kill enter the
appropriate bounded queue for the next eligible tick; they never reenter `Spawn` in
the active step. Audio/VFX/gameplay outputs are occurrence records staged until
commit, not callbacks from a particle kernel.

### 3. The whole simulation portion commits atomically

Spawn through Kill operate on an exclusive candidate/staging generation. The
previous committed generation remains readable until every required stage succeeds.
At tick commit, VfxWorld atomically publishes the new particle generation, emitter
cursors/spawn ordinal, RNG evidence, survivor order and external occurrence batch.
`Extract` reads only that committed generation.

If an attempted fixed tick fails/cancels, no new live particle, cursor/age, position,
collision/death occurrence, gameplay result or extracted generation becomes visible.
The host follows its normal failed-tick policy; VFX does not advance independently or
retry with a larger delta. A cosmetic presentation emitter may apply only an authored
drop/hold/reset policy, with a typed diagnostic; it cannot partially publish stages.

Capacity and required scratch are admitted before the step. A required gameplay
emitter cannot drop births, collisions or outputs mid-step. Insufficient capacity is
`VfxStepCapacityExceeded` and preserves the previous committed generation.

### 4. Stable particle identity is independent of storage

Each admitted emitter activation has stable asset/emitter/activation identity and a
monotonic 64-bit spawn ordinal. A `ParticleSimulationId` is derived from those stable
identities plus the ordinal; it is not a SoA index, pointer, freelist slot, thread ID
or renderer instance. Killed slots may be reused only after retirement, but reuse
never reuses particle identity or RNG sequence. Checked ordinal exhaustion terminates
the emitter with a typed failure rather than wrapping.

Simulation iteration and final compaction use ascending `ParticleSimulationId` unless
a compiled stage declares another stable semantic key with particle ID as final
tie-break. Worker partition, SIMD width, slot allocation, unordered containers and
completion order cannot change identity or survivor/event ordering.

### 5. Randomness uses versioned counter-based per-particle streams

The baseline is not one mutable per-emitter PRNG. Cook assigns stable semantic
`VfxRngChannelId` values to every stochastic property/node. `VfxRngV1` is a specified
counter-based integer generator whose input tuple is:

```text
project/effect seed
+ effect asset and compiled emitter identity
+ stable activation identity
+ ParticleSimulationId (or emitter step/spawn lane before identity exists)
+ RngChannelId
+ sample ordinal
+ algorithm version
```

The tuple produces the same unsigned integer bits across supported CPU platforms,
builds, runs, job counts and SIMD widths. Uniform integer/range mapping and integer-
to-`[0,1)` float conversion are specified by the cooked runtime version; they do not
use standard-library distributions, platform entropy, wall time, pointers or native
floating random functions.

Each property requests a semantic channel/sample ordinal, so adding a random sample
to an unrelated node does not shift existing streams. Repeated samples within one
node use increasing local sample ordinals. Emitter-level spawn decisions use a
separate counter tuple keyed by activation, committed step, trigger/request identity,
channel and sample ordinal; they cannot consume particle streams.

The cooked effect records RNG algorithm/version, channel map, project/effect seed
policy and determinism fingerprint. Changing any of them is a compatibility change
requiring recook/migration. Save/replay stores stable activation, committed step,
spawn ordinal and semantic state; it never serializes a mutable engine PRNG object.

### 6. Determinism guarantees are qualified precisely

With identical cooked effect digest, RNG version/channel map, stable activation,
ordered inputs, committed fixed ticks/deltas, query snapshots, numeric mode and
capacity policy:

- particle IDs, RNG integer outputs, stage/event order, births, deaths and typed
  control decisions are exact across supported CPU platforms and runs;
- integer and explicitly quantized payload fields are exact;
- floating particle attributes and collision math are bit-exact only within a
  separately qualified deterministic-math/Physics fingerprint; otherwise supported
  platform fingerprints compare using declared absolute/relative/ULP tolerances; and
- wall time, render cadence, view count and worker completion never affect results.

This is the CPU reference contract for future CPU/GPU equivalence. GPU kernels must
match identity, RNG integer/channel mapping, stage barriers and observable event/
aggregate outputs exactly where the asset claims equivalence; float attributes use
the declared tolerance/fingerprint tier. A visual-only GPU effect may declare no
authoritative equivalence, but it cannot feed gameplay.

### 7. Parallel execution preserves semantic order

Jobs receive disjoint candidate slices and immutable snapshots. They cannot append to
a shared variable-order event list, allocate identities, mutate live VfxWorld/ECS or
call Physics/gameplay. Spawn IDs and output slots are preassigned in semantic order.
Each job writes its fixed range and bounded local occurrence slots.

Stage barriers join through the host scheduler before the next stage consumes data.
Reductions use a compiler-declared stable tree/range order and specified accumulator/
numeric policy. Collision candidates are normalized by particle ID, stable collider/
shape identity and feature index before response selection. Equal-time/equal-distance
ties never use native query or worker return order.

Required job failure cancels the remaining candidate work and fails the step. The
owner never publishes completed partitions while another partition failed or timed
out. There is no nested wait from a worker or unbounded fallback to serial replay.

### 8. Gameplay input is typed, bounded and pre-step

Gameplay submits effect spawn/parameter commands through `VfxEventQueue` and declared
VFX parameter capabilities. At the tick cutoff VfxWorld copies admitted values into
an immutable `VfxGameplayInputSnapshot` keyed by request/emitter/schema revision.
Only compiled channels marked `GameplayInput` are writable by gameplay, with typed
range/size/update-frequency and owner-phase validation.

Gameplay cannot receive mutable particle spans, SoA indices or stage callbacks. It
cannot set position/velocity/lifetime/custom flags during a step, inject arbitrary
force/collision code, choose job partitions or call Spawn/Initialize/Forces/
Integrate/Collide/Kill/Extract. A desired behavior must be a registered compiled node
or typed parameter consumed at its declared stage.

Late commands become eligible next tick. Stale scene/emitter/schema generations,
wrong authority, invalid/nonfinite values and capacity failure return typed results
without altering the active input snapshot.

### 9. Gameplay output is post-commit and schema-limited

Custom payload channels are classified at cook:

| Class | Visibility and authority |
|---|---|
| `SimulationInternal` | Private CPU candidate/committed state; VFX kernels only |
| `RenderOnly` | Copied to immutable extraction for materials/sort/render; never gameplay input/output |
| `GameplayInput` | Immutable typed tick snapshot from admitted gameplay commands; read only by declared stages |
| `GameplayOutput` | Bounded typed collision/death/threshold/aggregate occurrence produced by CPU simulation and published only after commit |

GameplayOutput descriptors define schema/version, maximum occurrences per emitter/
tick, source stage, stable order, target owner boundary and required/cosmetic policy.
They expose semantic values such as particle/event identity, stable hit target,
quantized/qualified position/normal and authored payload fields—not arbitrary SoA,
renderer data, pointers or native Physics handles.

After commit, an application adapter drains the immutable batch at the gameplay
owner's next safe point. Delivery cannot reenter the current VFX step or retroactively
change it. Required output capacity is reserved at activation/step preflight;
overflow fails the candidate rather than dropping gameplay events. No authoritative
GameplayOutput is produced by GPU readback, RenderOnly channels, extraction or Null
simulation.

Gameplay may query only declared immutable effect-level aggregate snapshots from the
last committed generation. Per-particle queries are not baseline gameplay API; a
feature requiring them must define bounded snapshot/query capability and CPU-
mandatory cost explicitly.

### 10. Contract violations fail at the earliest boundary

Cook rejects stage cycles/backward dependencies, undeclared attribute access,
uninitialized reads, multiple conflicting writers, gameplay access to private/render
channels, GPU authoritative output, unbounded occurrences, unknown RNG channels and
missing CPU kernels. The invalid effect is not published.

Runtime admission rejects incompatible schema/capability/domain/determinism/budget
with no instance or partial reservation. A gameplay module request against a private,
wrong-stage, stale or unauthorized channel returns `VfxGameplayAccessDenied`,
`VfxPayloadSchemaMismatch`, `StaleVfxGeneration` or `VfxStageContractViolation` and
has zero effect. Public capabilities do not expose unsafe memory APIs, so native and
script modules follow the same enforcement path.

An internal compiled-kernel violation detected during execution contains the fault,
discards the candidate and reports effect/emitter/stage/kernel/generation identity.
Development builds may assert after recording the typed fault; Shipping returns the
declared required/cosmetic failure policy without continuing corrupt state. It never
silently skips the node, reorders stages, clamps required gameplay output or exposes a
half-updated buffer.

### 11. Qualification proves order, RNG and isolation

Required implementation evidence includes:

- stage sentinels proving Spawn -> Initialize -> Forces -> Integrate -> Collide ->
  Kill -> commit -> Extract and no same-tick reentrant sub-emitter/event spawn;
- failure/cancel/capacity injection at every stage with unchanged previous generation,
  no extracted candidate and no gameplay/Audio/VFX occurrence leakage;
- stable particle identity across slot reuse, deaths, compaction, SIMD widths, worker
  counts, allocation patterns and randomized completion order;
- golden `VfxRngV1` integer/range/float-conversion vectors on every supported CPU,
  independent semantic channels and recook/migration on version/channel changes;
- identical recorded tick/input/query histories across 30/60/144 Hz presentation,
  graphical/headless and supported CPU platforms with exact/tolerant fields matched
  to the declared fingerprint;
- stable parallel reductions/collision ties and contained worker failure;
- every payload class, valid/invalid/stale gameplay input, bounded required/cosmetic
  output, next-owner-boundary delivery and no mutable/per-particle private access;
- cook/runtime rejection for every stage/schema/domain/RNG violation with zero partial
  publication; and
- CPU/GPU reference fixtures proving claimed identity/RNG/stage/event equivalence while
  GPU/Null/RenderOnly paths cannot drive authoritative gameplay.

## Consequences

### Positive

- CPU VFX has one testable stage order and one owner for every mutation.
- Counter-based per-particle randomness is independent of jobs, deaths and call order.
- Gameplay coupling is typed and post-commit without exposing particle storage.
- The CPU reference gives later GPU equivalence work precise exact/tolerant fields.

### Costs

- Compiled graphs need stage access metadata, semantic RNG channel IDs and bounded
  payload schemas.
- Atomic candidate state and deterministic parallel merge/reduction require admitted
  scratch and may limit some aggressive in-place optimizations.
- Gameplay-visible emitters must reserve worst-case output capacity and real CPU work.

## Rejected Alternatives

### Use one mutable PRNG per emitter

Rejected because branching, deaths, job partitioning and unrelated node samples would
shift all later values. Versioned counter-based semantic streams are order-independent.

### Let graph topology choose arbitrary stage order

Rejected because force/integration/collision/death meaning and gameplay output would
differ across compilers. Graph dependencies must fit the fixed stage DAG.

### Let gameplay read or mutate the CPU SoA

Rejected because it creates two owners, defeats parallel/lifetime safety and cannot
match GPU/headless policy. Gameplay uses typed snapshots and post-commit occurrences.

### Publish completed worker partitions incrementally

Rejected because a later failure would expose a half-step and completion order would
change compaction/events. The candidate commits atomically.

### Generate gameplay events from GPU readback

Rejected by ADR-011 because asynchronous renderer/device timing cannot be the
authoritative gameplay clock. Gameplay-output units are CPU-mandatory.

### Promise bit-identical floating simulation on every platform

Rejected until deterministic math and Physics fingerprints prove it. IDs, RNG bits,
order and integer/quantized fields are exact; floats use declared qualification
tolerances outside a bit-exact profile.
