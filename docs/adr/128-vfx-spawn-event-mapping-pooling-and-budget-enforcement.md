# ADR-128: VFX Spawn Event Mapping, Pooling and Budget Enforcement

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: Gameplay-event binding ownership, allocation-free playback pools, aggregate VFX budget accounting, explicit overload selection, and inactive-system sleep/wake admission
- **Issue**: [VFX-007.1](https://github.com/abdullahbodur/horo-engine/issues/1755)
- **Jira**: [HORO-1712](https://horo-engine.atlassian.net/browse/HORO-1712)
- **Related**: [ADR-008](008-error-model-exception-boundary-and-registry.md), [ADR-011](011-vfx-effect-ownership-simulation-domain-and-renderer-boundary.md), [ADR-012](012-world-streaming-partition-authority-and-subsystem-boundaries.md), [ADR-034](034-gpu-memory-and-residency-ownership.md), [ADR-091](091-footstep-and-locomotion-event-ownership.md), [ADR-123](123-vfx-cpu-stage-order-determinism-and-gameplay-coupling.md), [ADR-124](124-vfx-gpu-simulation-readback-and-compute-fallback.md), [ADR-125](125-vfx-transparency-sorting-and-pass-placement.md), [ADR-126](126-vfx-graph-compilation-and-runtime-representation-convergence.md), [ADR-127](127-vfx-decal-projection-lifetime-and-rendering-path-policy.md)
- **Normative documents**: [VFX and Particles Architecture](../architecture/runtime/vfx-and-particles-architecture.md), [Gameplay Module Boundary](../architecture/extensions/gameplay-module-boundary.md)

## Context

ADR-011 gives one scene incarnation's `VfxWorld` ownership of effect instances,
preallocated storage, bounded requests and deterministic queue admission. It rejects
the incoming request when a queue or pool is full and prohibits implicit eviction of
an existing effect. ADR-123 through ADR-127 specialize simulation, GPU work, sorting,
compiled representation and decals without changing that ownership.

Two boundaries remain underspecified. First, gameplay code needs semantic events such
as a landed impact or weapon hit, while VFX assets are presentation choices. If the VFX
subsystem hardcodes gameplay meanings or gameplay code selects internal emitters, both
sides become a second owner of presentation policy. Second, count ceilings alone do
not define how CPU work, GPU work, draw calls, delayed timing evidence and retained
memory share one finite product budget.

Pooling and inactivity also need one lifecycle contract. A system that stops visible
work may still retain slots and GPU resources. Conversely, waking a dormant system
must not allocate or perform unbounded catch-up. Treating sleep as an unrelated
visibility optimization would bypass the same admission rules that protect overload,
streaming and teardown.

This decision preserves rejection of the incoming request as the baseline. It permits
oldest/farthest eviction or bounded delay only when an immutable binding explicitly
selects that policy and every affected instance is eligible cosmetic work.

## Decision

### 1. The application owns semantic event-to-effect bindings

Gameplay and application composition own a cooked, immutable
`GameplayVfxBindingTable`. A binding maps a typed semantic event identity or declared
effect tag to one or more effect assets and fixed spawn policy. `VfxWorld` validates
and executes the resolved request; it never hardcodes meanings such as Footstep,
Explosion, Hit or Weather and never queries gameplay state to infer them.

```cpp
struct GameplayVfxEvent {
    GameplayVfxEventId event;
    GameplayEventOccurrenceId occurrence;
    SceneRuntimeId scene;
    SimulationTick tick;
    ProducerId producer;
    uint32_t producerSequence;
    Transform worldTransform;
    VfxEventPayload payload;
};

struct GameplayVfxBinding {
    GameplayVfxEventId event;
    VfxEffectAssetId effect;
    VfxPayloadSchemaId payloadSchema;
    VfxSpawnPolicyId spawnPolicy;
    VfxOwnershipPolicy ownership;
};
```

The application adapter that owns a semantic event occurrence resolves it against one
captured binding-table generation and submits a bounded `VfxSpawnRequest`. A binding
may fan one occurrence out to an authored finite set of layered effects, but each
request retains the original occurrence ID and a stable layer index for deduplication.
Tags are stable typed IDs from a cooked registry, not runtime strings or substring
matching.

Bindings declare payload schema, allowed producer/principal, ownership scope, request
class, quality variants and overload policy. Load/cook rejects duplicate semantic
ownership, missing assets, invalid schemas, unbounded fan-out and a policy incompatible
with required gameplay behavior. Hot reload prepares and validates a new immutable
generation, then swaps at an owner safe point; already accepted requests retain the
generation used to resolve them.

Direct effect-asset requests remain available only to explicitly granted low-level
application/tooling capabilities. Ordinary gameplay modules receive a semantic spawn
capability scoped to their permitted event/tag set. Knowing an asset or tag ID is not
authority to spawn it.

### 2. Acceptance order and results are deterministic and bounded

The scene/application owner orders authoritative inputs by scene, tick, producer and
producer sequence before binding resolution. Fan-out layer order is cooked. Each clock
domain freezes its accepted queue prefix and drains within the existing per-tick limit;
worker arrival order, hash-map iteration, render visibility and GPU completion never
choose winners.

Binding miss, stale generation, unauthorized event, payload mismatch and duplicate
occurrence return typed results without consuming a pool slot. Queue, pool or resource
failure returns the existing `EventQueueFull`, `EffectCapacityExceeded` or
`ResourceBudgetExceeded` class with event, occurrence, binding generation, effect,
request class and failed budget dimension. No completion is delivered inline.

### 3. Playback performs zero heap allocation

Before activation, explicit load/preload or streaming preparation admits and
materializes every runtime resource needed by the selected binding generation:

- effect-instance, emitter, CPU particle and decal slots;
- GPU particle/resource slices, indirect work and frames-in-flight overlap;
- spawn, delayed-request, result, cancellation and retirement records;
- simulation scratch, parameter blocks, extraction, upload and per-view sort storage;
- diagnostics records and bounded Audio/VFX follow-up event storage.

`TryEnqueueSpawn`, queue drain, spawn, simulation, extraction, render scheduling,
sleep and wake do not allocate, grow a container, compile a graph or cook an asset.
Allocation is allowed only inside an explicit non-playing preparation transaction:
initial scene/project load, admitted cell preparation, or a policy/hot-reload resize.
Such a transaction reserves peak old-plus-new memory and publishes atomically after
validation. It is not an on-demand escape hatch from a gameplay frame.

Pool slots carry scene/owner generation and are returned only after logical readers,
jobs, snapshots and GPU fences retire. Sleeping retains its slots and memory charges;
only complete retirement can return those credits. Pools may be partitioned into
required and cosmetic reserves. Cosmetic work cannot borrow required capacity in a
way that makes a previously admitted required bound unavailable.

### 4. One VFX budget plan covers all work and retained resources

Host composition owns the finite product envelope. `VfxWorld` owns the scene VFX
ledger and produces one `VfxBudgetPlan`; Renderer validates and charges the GPU and
per-view portions against the same host envelope rather than creating a second VFX
allowance. Streaming-cell reservations are slices of this ledger.

Every quality profile defines finite hard limits for at least:

- live CPU and GPU particles, effect/emitter instances and decals;
- admitted and delayed spawn/result records;
- CPU simulation work, jobs, scratch, upload/readback bytes and pending readbacks;
- per-view packed particles, sort keys, draw batches/calls and shadow/decal/volume work;
- GPU compute, sort and render work plus qualified delayed GPU-time evidence;
- live, replacement-overlap and retirement-pending CPU/GPU memory.

The baseline configurable ceilings remain 16,384 aggregate CPU particles, 1,000,000
GPU particles and 256 decals. They are ceilings, not promises. Per-domain, per-asset,
per-owner and per-cell limits subdivide the aggregate and cannot create additional
capacity. Every count/stride/frames-in-flight product is overflow checked.

Hard work and memory counters decide current admission before work starts. CPU/GPU
time targets use measurements qualified by workload, device/profile, backend,
resolution/view count, build and policy revision. Delayed or unavailable GPU timing
is never interpreted as zero. Timing evidence may tighten a later deterministic policy
revision; it does not retroactively preempt submitted native work.

Required gameplay work has reserved count, work, output and result capacity. Failure
to admit its declared worst case is a typed owner-visible failure, never cosmetic
quality reduction. Null follows the same logical count, queue and lifecycle accounting
while recording visual GPU work as unsupported/suppressed, not completed for free.

### 5. Over-budget behavior is explicit, finite and observable

Each binding selects one cooked `VfxOverBudgetPolicy`:

```cpp
enum class VfxOverBudgetPolicy : uint8_t {
    RejectNewest,
    DelayBounded,
    EvictOldestCosmetic,
    EvictFarthestCosmetic,
    UseAuthoredReduction
};
```

`RejectNewest` is the default and preserves ADR-011: accepted work is not overwritten,
the incoming request fails, and an emitter at its particle cap drops new cosmetic
births. `UseAuthoredReduction` may select only a precompiled compatible variant whose
full costs are admitted before activation.

`DelayBounded` applies only to cosmetic requests. The binding defines maximum queued
requests, maximum ticks/age and fixed FIFO priority. Delayed requests retain their
original order and reserved result record, are retried only at declared tick boundaries
and expire with `VfxSpawnDelayExpired`. Delay never blocks a producer, spins, grows a
queue or crosses a scene/cell/owner generation.

`EvictOldestCosmetic` and `EvictFarthestCosmetic` are opt-in replacement policies, not
implicit pool behavior. Eligible victims must be cosmetic, explicitly replaceable,
inside the same declared budget partition and free enough reusable logical capacity
without waiting for GPU retirement. Required/gameplay-coupled instances, permanent or
event-driven decals, protected owner/cell effects and work with pending outputs are
never eligible.

Oldest uses admitted spawn tick, producer sequence and instance ID as stable ties.
Farthest uses a binding-declared canonical distance reference frozen for the admission
boundary; multi-view policy names the authoritative view set and aggregation rule.
Nonfinite/missing references make the candidate ineligible. Distance does not depend
on unordered view traversal or a backend visibility result. Equal candidates use the
same stable oldest/instance ties.

Eviction is a normal typed stop transition and records victim/replacement correlation.
If retirement cannot provide immediately reusable capacity, the new request is
rejected or bounded-delayed according to its single cooked fallback; it cannot cascade
through arbitrary victims. No policy changes gameplay output, collision, timing or
required capacity to preserve a visual effect.

### 6. Sleep and wake are budget-controlled lifecycle states

`VfxWorld` owns `Active`, `Sleeping`, `WakePending` and `Retiring` state transitions.
A system is sleep-eligible only when its compiled policy marks it cosmetic and
sleepable and it has no required gameplay output, pending sub-emitter/Audio event,
readback consumer, owner callback or retirement dependency. Render invisibility alone
does not make required or gameplay-coupled work sleepable.

The sleep policy declares one clock behavior: freeze logical age, advance a bounded
analytic age without simulation, or restart on wake. Entry occurs at the owner safe
point after current jobs/extraction complete. Sleeping schedules no simulation,
packing, sort or draw work, but retains and charges all occupied slots and physical
resources. It is not eviction and does not satisfy a memory/slot shortage.

Wake is a fresh work admission using already prepared storage. If count/work/draw/GPU
budget is unavailable, the system remains sleeping or follows its single declared
cosmetic fallback and reports `VfxWakeBudgetExceeded`. Analytic advance is bounded;
there is no per-missed-tick replay or unbounded catch-up. A stale scene/cell/owner
generation retires instead of waking.

### 7. Diagnostics expose decisions without becoming authority

Structured metrics report capacity and high-water marks by budget dimension, binding
generation, request class, effect and owner partition. Outcomes distinguish binding
miss, queue rejection, pool rejection, each delay/expiry, authored reduction, selected
victim, sleep reason, wake denial and physical retirement lag. Required failures carry
correlation identities and are never reduced to a rate-limited log.

Observability may recommend another authored profile but cannot mutate budgets,
bindings, priority, victim eligibility or sleep state from a telemetry callback. Policy
changes are explicit revisioned transactions at the application/host owner boundary.

## Consequences

- Gameplay semantics and presentation asset choice have one explicit owner each.
- Normal playback has a finite memory/work proof and no frame-path allocation escape.
- Default overload behavior remains compatible with ADR-011 while opt-in cosmetic
  delay or replacement has deterministic selection and bounded failure.
- Sleeping reduces active work but honestly retains memory and slot accounting.
- Budgets and diagnostics span CPU, GPU and Renderer work without duplicate ledgers.
- Content authors must provide policies, variants and bounds; load/cook may reject
  effects that previously depended on runtime growth or ambiguous dropping.
- Farthest replacement requires a declared canonical reference and may be unavailable
  for a multi-view composition that cannot define one.

## Rejected Alternatives

### Let VFX interpret gameplay events and choose effects internally

Rejected because semantic authority, asset policy and gameplay state queries would be
hidden inside a presentation subsystem and impossible to capability-scope cleanly.

### Allocate or grow a pool when a spawn arrives

Rejected because allocator latency, fragmentation and failure would enter frame-hot
paths, and a successful local allocation could bypass aggregate host/GPU admission.

### Always drop the oldest or farthest instance

Rejected because permanent, gameplay-required or still-referenced work may be chosen;
distance can also vary by view. Replacement is allowed only as an explicit cosmetic
policy with stable eligible candidates and ties.

### Use visibility as an independent automatic sleep mechanism

Rejected because visibility is view-dependent, sleeping retains resources and some
invisible effects still produce required logical outputs. Sleep/wake belongs to the
same lifecycle and budget owner as spawn admission.

### Enforce budgets from measured frame time alone

Rejected because CPU/GPU timing is noisy and GPU evidence is delayed or unavailable.
Finite work/count/memory bounds provide current safety; qualified timing guides later
policy selection and diagnostics.
