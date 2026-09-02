# ADR-101: Interest, Priority and Network Budget Model

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: Renderer-independent network project profiles, immutable relevancy inputs, per-connection scheduling, fairness, bandwidth/work/queue units and bounds, boundary hysteresis, overload, lifecycle, observability and qualification
- **Issue**: [NET-006.1](https://github.com/abdullahbodur/horo-engine/issues/1152)
- **Jira**: [HORO-1152](https://horo-engine.atlassian.net/browse/HORO-1152)
- **Related**: [ADR-097](097-default-real-time-transport-backend.md), [ADR-098](098-protocol-session-and-trust-policy.md), [ADR-099](099-replication-ownership-authority-and-compatibility.md), [ADR-100](100-prediction-capability-tiers-and-determinism-policy.md)
- **Normative documents**: [Multiplayer Replication Architecture](../architecture/runtime/multiplayer-replication-architecture.md), [Networking Architecture](../architecture/runtime/networking-architecture.md)

## Context

The multiplayer document derived player, object and bandwidth numbers from `es3`,
DirectX/Vulkan and `high_end` renderer tiers. Rendering capability does not describe
server session count, network tick rate, interest density, transport throughput or
replication work. A headless server has no renderer tier at all.

ADR-099 assigns interest, baselines and serialization to NetworkRuntime but does not
choose scheduling/fairness. Unbounded candidate evaluation, dirty queues or reliable
backlogs can turn a crowded boundary or slow peer into frame stalls and memory
growth. Pure priority sorting can also starve low-priority state indefinitely.

The project must choose explicit network limits with units. Runtime scheduling then
uses immutable per-tick facts, finite ledgers and deterministic overload behavior;
it cannot mutate Scene to make work disappear or silently raise limits.

## Decision

### 1. One immutable network project profile owns every limit

The project/host resolves and validates a `NetworkProjectProfileV1` before opening
the authority world/listeners:

```cpp
struct NetworkProjectProfileV1 {
    NetworkProjectProfileId id;
    NetworkProjectProfileRevision revision;
    Hertz networkTickRate;

    CountLimit maxActiveConnections;
    CountLimit maxNetworkObjects;
    CountLimit maxCandidatesPerConnectionPerTick;
    CountLimit maxRelevantObjectsPerConnection;
    CountLimit maxInterestTransitionsPerConnectionPerTick;

    CountLimit maxCapturedObjectsPerTick;
    CountLimit maxSerializedFieldsPerConnectionPerTick;
    Microseconds maxInterestWorkPerTick;
    Microseconds maxCaptureWorkPerTick;
    Microseconds maxSchedulingWorkPerTick;

    BytesPerSecond targetBytesPerConnection;
    ByteCount burstBytesPerConnection;
    CountLimit maxMessagesPerConnectionPerTick;
    ByteCount maxReliableQueuedBytesPerConnection;
    ByteCount maxUnreliableQueuedBytesPerConnection;

    TickCount enterDwellTicks;
    TickCount exitDwellTicks;
    TickCount maxEligibleStarvationTicks;
    TickCount saturationGraceTicks;
};
```

Every wrapper validates a finite non-zero value and its documented unit. The
profile additionally declares bounded priority classes, reservations/weights,
interest-provider limits, distance quantization, hysteresis margins, coalescing and
disconnect policy. Missing fields have no ambient hardware-derived default.

Profiles are network assets/configuration selected by product role, match/game mode
and qualification evidence. Renderer backend/tier, screen resolution, render LOD,
GPU memory, frame rate and graphics settings cannot select or modify them. A profile
change creates a new server scheduling generation and is admitted at a network-tick
safe point; it is not mutated piecemeal.

Validation rejects arithmetic overflow, zero tick rate, relevant/candidate counts
above world limits, reservations exceeding rate/burst/work/queue capacity,
unreachable starvation promises, invalid hysteresis, or queue limits smaller than
one admitted required record. Repository architecture defines the schema and safe
ceilings; product performance targets require measured qualified profile values.

### 2. ReplicationScheduler is the sole scheduling authority

One `ReplicationScheduler` owned by NetworkRuntime schedules an authority world.
Gameplay, Scene, interest providers, transport telemetry and editor tools contribute
read-only facts; none enqueue packets, alter deficits/tokens or force priority.

At one immutable network-tick cut, inputs include:

- active ADR-098 session/principal and ADR-099 role/object generations;
- per-connection viewer/subscription/group facts and profile ID/revision;
- per-object stable network ID, spatial bounds/group, declared priority/update
  class, dirty age, initial/despawn requirement and last-sent tick;
- validated baseline/ack age and bounded previous encode-size/work estimates;
- transport queue depth/backpressure normalized into Horo units.

Inputs contain no mutable component pointers, renderer visibility/LOD, camera draw
lists, native transport handles or wall-clock arrival ordering. Interest and
priority affect routing/scheduling only; they never grant authority, change field
values, mutate Scene or change compatibility.

### 3. Interest is finite, read-only and hysteretic

Registered interest providers emit bounded candidate facts from immutable Scene/
Gameplay snapshots. Baseline providers are spatial cell/distance, explicit
subscription/group and authority-required lifecycle. Provider output is unioned by
stable object ID, then filtered by session permissions and profile limits.

Spatial comparisons use the profile's canonical world-coordinate distance
quantization. Enter and exit thresholds plus dwell ticks prevent boundary chatter.
`maxInterestTransitionsPerConnectionPerTick` bounds churn; excess transitions are
deferred in stable object-ID order and revalidated next tick. A despawn/permission
revocation bypasses ordinary relevance deferral through the reserved lifecycle
class.

Providers cannot create/destroy objects, mark fields dirty, retain live views,
allocate without bound or call transport. Candidate/relevant overflow produces
typed saturation evidence and deterministic truncation/defer policy; it cannot
silently expand storage.

### 4. Scheduling uses rate tokens plus weighted deficit fairness

Each connection owns fixed-capacity ledgers:

- a byte token bucket accrued from `targetBytesPerConnection` at network tick rate,
  capped by `burstBytesPerConnection`;
- message, field and measured work budgets for the current tick;
- per-priority-class weighted deficit counters capped by the profile;
- per-object last-eligible/last-sent ticks and coalesced snapshot slot.

Required control/session traffic belongs to ADR-098/transport reservations and
cannot be consumed by replication. Replication orders lifecycle/initial state,
gameplay-critical reliable, gameplay state and cosmetic/replaceable classes using
profile reservations and weighted deficit round robin. Within equal eligibility,
selection is deterministic by class, accumulated deficit/age and stable
`NetworkObjectId`.

Age increases eligibility only up to a bounded cap. `maxEligibleStarvationTicks`
is enforced for an object that remains relevant, fits an admitted record and has a
feasible class reservation. Profile validation cannot promise service when the
minimum admitted set exceeds capacity; runtime reports the unsatisfied invariant
instead of falsifying fairness.

Serialization/capture is admitted from estimates, charged using actual bounded
bytes/fields/work, and cannot exceed the remaining ledger. Underestimation debt is
bounded and repaid before later work; repeated estimator error is diagnostic/profile
failure, not permission to overspend.

### 5. Overload preserves bounds and required lifecycle

Overload policy is ordered:

1. stop admitting new sessions/objects when world/profile capacity is reached;
2. preserve reserved control, disconnect, despawn and required initial-state work;
3. coalesce replaceable unreliable state to the newest candidate per object/schema;
4. defer lower classes while deficit/age fairness remains within feasible bounds;
5. reject reliable submissions that exceed their bounded queue with a typed error;
6. after `saturationGraceTicks`, degrade the offending connection's optional
   frequency/classes and then close it when required progress remains impossible.

Reliable lifecycle/state is never silently dropped. Unreliable superseded snapshots
may be replaced only by explicit schema/profile policy. There is no unbounded
backlog, global allocation, sleep/block on the simulation thread, renderer-quality
change or mutation of authoritative state to reduce load.

One slow/hostile connection has independent ledgers and cannot consume another's
reservation. World-level capture/interest budgets use rotating deterministic
connection start order plus accumulated deficit so a fixed low ID is not always
favored.

### 6. Lifecycle and observability are generation-safe

Connect initializes empty ledgers under the pinned profile. Spawn, interest enter/
exit, ownership change, profile replacement, scene travel, disconnect and reconnect
are tick-boundary transitions carrying scheduling generation. Late candidates,
estimates or transport feedback from another generation are discarded.

Disconnect removes the connection from scheduling before transport close and frees
bounded queues/ledgers after callbacks drain. Shutdown stops admission, invalidates
generations, discards replaceable work, resolves/fails required queued work,
cancels providers and then releases profile/snapshot pins. No scheduling result may
publish afterward.

Metrics expose profile/generation, candidate/relevant/transition counts, bytes/
messages/fields/work budget and use, token/deficit, age/starvation, coalesce/defer/
drop/reject, queue depth, saturation duration and disconnect cause. Payloads and
credentials are excluded.

### 7. Qualification covers boundaries, fairness and saturation

Focused automated coverage proves:

- every profile field's unit, zero/max/overflow validation and infeasible
  reservation/starvation rejection;
- no renderer setting/backend/tier/frame-rate input changes a network profile or
  scheduling result;
- deterministic interest enter/exit with exact-boundary movement, dwell,
  hysteresis, teleports, group/permission changes and transition caps;
- candidate/relevant/world/connection limits and stable truncation/defer order;
- token accrual/fractional carry/burst cap, byte/message/field/work charging and
  estimator debt;
- weighted-deficit fairness, rotating connection start, starvation bounds and no
  cross-connection budget theft;
- reliable queue rejection, unreliable coalescing, sustained saturation degrade/
  disconnect and preserved lifecycle/control progress;
- spawn/despawn, ownership/profile generation, scene travel, disconnect/reconnect,
  cancellation and shutdown with queued/provider work.

Property tests vary movement, dirty patterns, peer speeds and budgets while proving
hard memory/work bounds and deterministic results. Performance claims require
measured qualified project profiles, not renderer-tier inference.

## Consequences

### Positive

- Headless, desktop and low/high graphics products use the same explicit network
  policy model.
- Every connection has isolated finite bandwidth, work and queue accounting.
- Boundary churn and saturation cannot grow memory/work without limit.
- Weighted deficit plus aging prevents feasible low-priority starvation.
- Required lifecycle traffic is distinct from replaceable state.

### Negative

- Projects must choose and qualify network profile values explicitly.
- Fair scheduling requires per-connection/class/object ledger state.
- Hard overload behavior may visibly reduce updates or disconnect a persistently
  slow peer.
- Accurate encode/work estimation requires instrumentation and tuning.

## Rejected Alternatives

### Derive limits from renderer tiers, LOD or frame rate

Graphics capability is unrelated to network/server topology and absent headlessly.
Network profiles are independent typed inputs.

### Let gameplay/providers enqueue or force-send packets

Multiple scheduling authorities defeat budgets/fairness. Providers contribute
immutable facts; `ReplicationScheduler` alone admits work.

### Sort only by instantaneous priority

Pure priority starves lower classes. Weighted deficit, reservations and bounded age
are required.

### Keep reliable backlog until it eventually drains

Unbounded reliable queues exhaust memory and retain stale work. Admission rejects
overflow and sustained inability to progress closes the connection explicitly.

### Drop arbitrary records under overload

Lifecycle/required reliable state cannot be silently lost. Only explicitly
replaceable unreliable snapshots may coalesce/drop by policy.

### Mutate relevance or authoritative state to reduce bandwidth

Interest is a read-only routing decision, not gameplay authority. Overload changes
scheduling/admission, never canonical state.
