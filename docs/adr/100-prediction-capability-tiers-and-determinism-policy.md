# ADR-100: Prediction Capability Tiers and Determinism Policy

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: Non-predicted baseline, local prediction and rollback/resimulation capability tiers; descriptor admission, fixed-tick input/state hooks, determinism closure, bounded histories, correction, side effects, overflow, lifecycle and qualification
- **Issue**: [NET-005.1](https://github.com/abdullahbodur/horo-engine/issues/1142)
- **Jira**: [HORO-1142](https://horo-engine.atlassian.net/browse/HORO-1142)
- **Related**: [ADR-084](084-canonical-physics-solver-units-and-tolerances.md), [ADR-088](088-physics-determinism-capability-and-support-tiers.md), [ADR-092](092-character-controller-determinism-and-state-composition.md), [ADR-098](098-protocol-session-and-trust-policy.md), [ADR-099](099-replication-ownership-authority-and-compatibility.md)
- **Normative documents**: [Multiplayer Replication Architecture](../architecture/runtime/multiplayer-replication-architecture.md), [Character Controller Architecture](../architecture/runtime/character-controller-architecture.md), [Networking Architecture](../architecture/runtime/networking-architecture.md)

## Context

The replication architecture historically described every client as predicted and
showed renderer-derived prediction levels. That makes input history, state capture,
rewind and replay appear mandatory even for games that only need authoritative
snapshots. It also conflates responsive local speculation with full historical
rollback, which have very different state-closure and determinism requirements.

ADR-099 establishes explicit autonomous/simulated roles and owner-provided schema
adapters. An autonomous client still has no server authority; prediction is a local
candidate. ADR-092 provides one example of canonical fixed-tick Character state and
aggregate restore, but it deliberately does not approve network rollback. Horo
needs a capability policy that admits rollback only when every participating state
owner provides a coherent checkpoint/restore/resimulation closure.

The policy must remain bounded under latency, loss and delayed corrections. A
history overrun cannot allocate indefinitely or replay unbounded ticks in one
frame. Prediction also must not duplicate irreversible gameplay, audio, VFX,
analytics or platform side effects during resimulation.

## Decision

### 1. Prediction is opt-in through three closed tiers

```cpp
enum class PredictionCapabilityTier : std::uint8_t {
    NonPredicted,
    LocalPrediction,
    RollbackResimulation
};
```

| Tier | Client simulation | Required retained history | Correction behavior |
|---|---|---|---|
| `NonPredicted` | No speculative canonical simulation | None; bounded presentation samples may exist separately | Apply authoritative snapshot, optionally smooth presentation |
| `LocalPrediction` | Autonomous client's declared local candidate advances from fixed-tick inputs | Bounded pending input/sequence ledger, current candidate and visual error proxy; no historical world checkpoints | Replace the current candidate, retain unacknowledged inputs for transport/future ticks and decay only the visual error; no rewind to an old tick |
| `RollbackResimulation` | Declared aggregate prediction world advances from canonical fixed-tick inputs | Bounded ordered inputs, checkpoints/deltas, hashes, occurrences and authoritative receipts | Restore a compatible checkpoint, apply authoritative correction, replay ordinary fixed-tick pipeline within budget |

`NonPredicted` is the default and complete production capability. It allocates no
prediction input journal, checkpoint ring or replay workspace and schedules no
prediction capture/restore work. Presentation interpolation is not simulation
authority and cannot be used as a checkpoint.

`LocalPrediction` improves immediate response but promises no historical rewind or
cross-host deterministic equivalence. A correction atomically replaces the current
simulation candidate at the received authoritative cut. A bounded presentation-only
error proxy preserves the pre-correction visual pose and decays toward the corrected
pose, so simulation authority changes immediately without a mandatory visible hard
snap. Acknowledged inputs leave the ledger; unacknowledged inputs remain sequenced
for retransmission and future ticks but are not replayed over past state. It cannot
retroactively validate side effects or claim rollback support.

`RollbackResimulation` is admitted only when the entire participating state closure
is qualified. No profile may advertise a tier it cannot prove.

### 2. A validated descriptor admits prediction

```cpp
struct PredictionDescriptor {
    PredictionDescriptorId id;
    PredictionDescriptorRevision revision;
    PredictionCapabilityTier tier;
    FixedTickRate fixedTickRate;
    InputSchemaId inputSchema;
    ReplicationSchemaSetId replicatedState;
    BoundedVector<CheckpointProviderId> checkpointProviders;
    PredictionHookSet hooks;
    PredictionDeterminismRequirement determinism;
    PredictionHistoryLimits history;
    PredictionReplayLimits replay;
    PredictionOverflowPolicy overflow;
};
```

The gameplay owner contributes inert descriptors during host composition. The host
selects a product profile; ADR-098 negotiation confirms the same descriptor
revision/tier and compatible schemas before prediction activation. A server grant
then identifies which autonomous objects/input schemas may use it.

Validation rejects missing/duplicate/foreign IDs, unsupported tier, zero/overflowed
limits, mismatched fixed rate, missing input/state schema, incomplete hook set,
unqualified providers, incompatible determinism fingerprint, missing/cyclic provider
dependencies and policies that can exceed configured memory/work.

`checkpointProviders` declares membership, not execution order. Each registered
provider descriptor names its required provider IDs; validation computes one stable
topological order with `CheckpointProviderId` as the tie-break for independent
nodes. Missing dependencies or cycles reject the complete descriptor, and the
resolved order is included in its semantic fingerprint.

Hooks are tier-specific:

- `NonPredicted`: no prediction hooks or history are constructed;
- `LocalPrediction`: validate/canonicalize input, advance candidate and apply
  authoritative current-state correction;
- `RollbackResimulation`: capture, encode/hash, restore, apply correction, simulate
  one ordinary fixed tick, compare evidence and reconcile presentation/occurrences.

Hooks are owned by the semantic state provider, not NetworkRuntime. NetworkRuntime
orchestrates ticks, histories and correction routing without owning gameplay state.

### 3. Server authority never changes

Prediction does not grant client write permission. The authority server validates
typed input/command sequences and computes the only canonical gameplay state. A
client candidate, local hash or replay result is diagnostic/submission evidence,
not authority.

Distributed ownership transfer, peer-authoritative objects, deterministic peer
lockstep and client-majority correction are unsupported M0/1.0 assumptions. A
listen server still uses distinct server/client worlds. Same process, loopback,
possession, local player or low latency cannot bypass ADR-099 grants.

### 4. Prediction uses one canonical fixed-tick timeline

Inputs carry session/object/authority generations, `InputSchemaId`, fixed
`SimulationTick`, monotonic producer sequence and bounded canonical fields. The
server accepts them only inside its configured early/late window and never rewinds
canonical authority merely because a client predicted another result.

The client maps authoritative receipts/corrections to the same fixed-tick domain.
Presentation frames may interpolate committed/candidate samples but never create
input ticks, advance gameplay twice or become restore identity. Time dilation,
pause, hitch and catch-up policy cannot silently change fixed tick quantum.

Inputs are immutable after admission. Duplicate, reordered, missing, conflicting or
out-of-window sequences follow explicit reject/hold/resync rules; arrival order
never becomes simulation order.

### 5. Rollback requires a complete deterministic state closure

A rollback descriptor lists every provider whose state can affect replayed output:
Scene structure/revision, Gameplay state, Physics, Character, relevant Animation/
root-motion state, random streams, clocks and other declared systems. Each provider
supplies bounded canonical capture/restore and compatibility identity at the same
post-commit tick safe point.

ADR-092's Character codec/checkpoint is reused unchanged when Character participates.
The aggregate checkpoint binds its paired Physics/world checkpoint, origin,
structural revision, determinism fingerprint and provider manifest. Network code
cannot invent a smaller Character state or restore native solver/component memory.

All providers must satisfy the descriptor's ADR-088 determinism tier for the exact
platform/solver/FP/job/algorithm tuple. Exact canonical hash mismatch is divergence;
tolerances are diagnostics only. Missing provider, mismatched fingerprint/schema,
partial capture or non-transactional restore rejects rollback activation or the
candidate correction.

Restore is aggregate and transactional. It stages/validates every provider, then
commits at the owner safe point or leaves the current world unchanged. Replay calls
the ordinary fixed-tick schedule and owner APIs; it does not use a shortcut update
path.

### 6. Histories and replay work are finite

Every descriptor fixes:

- maximum pending input count/bytes and maximum input age;
- checkpoint interval, full/delta count, aggregate bytes and oldest restorable tick;
- maximum authoritative receipts and occurrence ledger entries;
- maximum correction age, replay ticks per frame and total replay work per
  correction;
- maximum consecutive resync/degrade events and diagnostic rate.

Storage is preallocated or otherwise hard-bounded before activation. Retention
evicts by deterministic tick order while preserving declared full/delta ancestry.
No packet latency or loss may grow a vector/deque without limit.

If a correction predates retained history, a required provider/checkpoint is
missing, a delta chain is broken, replay exceeds work budget or input history
overflows, rollback stops without partial restore. The client requests/awaits a
full authoritative resync, clears incompatible candidate/history, and temporarily
uses `NonPredicted` behavior. It never guesses missing input, expands limits or
continues from a half-replayed world.

LocalPrediction overflow similarly discards the candidate/pending ledger and
returns to the latest authoritative state. Recovery to a higher tier requires a
fresh server-acknowledged generation and safe-point activation.

### 7. Correction publication is atomic and presentation is separate

For rollback, the coordinator stages aggregate restore, authoritative correction
and bounded replay through the target tick. Only the completed post-replay
candidate becomes the simulation state. Gameplay/replication readers never observe
an intermediate restored tick.

Presentation may blend from the previously displayed pose to the corrected
committed/candidate pose under a bounded visual policy. That blend is not canonical,
is excluded from hashes/checkpoints and cannot delay authority publication.

Older/duplicate corrections are ignored by tick/sequence/generation. A newer
correction cancels an uncommitted older replay candidate by generation. A world/
object/session/authority generation change invalidates all prediction work.

### 8. Replay-safe side effects require occurrence identity

Prediction-capable providers classify outputs:

- canonical state mutation: captured/restored/replayed through provider hooks;
- reversible/presentation occurrence: recorded with stable occurrence ID/tick and
  deduplicated or reconciled after replay;
- irreversible/external effect (achievement, purchase, save, analytics, remote
  command): server-authoritative commit only, never emitted from speculative replay.

Audio/VFX/UI feedback may be locally speculative only through an occurrence ledger
with bounded retention and explicit confirm/cancel/update behavior. Replay does not
double-fire events merely because a tick executes again. Generic event-bus history
is not a checkpoint or occurrence authority.

### 9. Tier lifecycle is generation-safe

Prediction activates only after active ADR-098 session, replicated spawn/object
grant, descriptor/schema agreement and required provider readiness. Tier change,
ownership transfer, scene travel/reload, module reload, determinism-profile change,
disconnect and reconnect increment prediction generation and clear incompatible
history at an owner safe point.

On shutdown, stop new inputs/corrections, invalidate generations, cancel replay,
discard uncommitted candidates, remove prediction state from dispatch, drain owned
jobs and then release histories/provider pins. Late packets/jobs cannot publish.
NonPredicted worlds have no prediction teardown work beyond presentation samples.

### 10. Qualification covers timing, loss and lifecycle

Focused automated coverage proves:

- descriptor validation and exact tier/hook/provider/determinism admission;
- NonPredicted allocation/work evidence showing no input/checkpoint/replay history;
- fixed-tick input sequence with latency, jitter, duplicate, reorder, loss and
  early/late windows;
- LocalPrediction correction, candidate discard, visual-only smoothing and bounded
  input overflow;
- rollback full/delta capture, aggregate transactional restore, exact replay/hash,
  Character/Physics composition and first-divergence diagnostics;
- correction older than history, broken delta/provider closure, replay-budget and
  byte/count overflow causing full resync/NonPredicted degrade without partial
  publication;
- occurrence deduplication/cancel/confirm and prohibition of irreversible
  speculative side effects;
- spawn/despawn, ownership transfer, tier/profile change, scene travel/reload,
  disconnect/reconnect, module unload and shutdown during capture/restore/replay;
- same-process listen server without client authority and explicit rejection of
  distributed authority/lockstep configuration.

Deterministic golden fixtures record inputs, canonical checkpoints/hashes,
corrections and expected post-replay state. Fuzz/property coverage targets input,
checkpoint/delta and correction framing within hard limits.

## Consequences

### Positive

- Games that do not need prediction pay no mandatory history or replay overhead.
- Local responsiveness is available without claiming full rollback determinism.
- Rollback reuses semantic state owners and canonical checkpoints instead of
  creating network-specific state copies.
- Latency/loss/overflow have bounded recovery rather than memory/work growth.
- Server authority and side-effect correctness remain intact during replay.

### Negative

- Rollback-capable games must implement and qualify a complete provider closure.
- Bounded history can force visible hard resync under extreme network conditions.
- Occurrence reconciliation adds explicit gameplay/audio/VFX integration work.
- Cross-platform rollback availability may differ by qualified determinism tuple.

## Rejected Alternatives

### Mandatory prediction for every client/game

This imposes histories, hooks and replay cost on products that only need snapshots.
NonPredicted is the baseline.

### Treat all prediction as rollback

Current-state local speculation does not prove historical restore/resimulation.
The tiers state different guarantees explicitly.

### Predict arbitrary component/ECS memory

Native layout is not canonical state and bypasses ADR-099 owners. Only declared
semantic providers with bounded hooks participate.

### Unbounded history sized by observed latency

Hostile or degraded networks could exhaust memory/work. Profiles set hard limits
and overflow degrades to full resync/NonPredicted behavior.

### Distributed authority or deterministic lockstep baseline

These require different trust, ownership, determinism and failure models and
conflict with the accepted server-authoritative architecture.

### Replay through a special fast simulation path

A shortcut can diverge from ordinary gameplay scheduling and side effects. Replay
uses the same fixed-tick pipeline under bounded orchestration.

### Emit every side effect again after rewind

This duplicates irreversible/external actions and presentation effects. Stable
occurrence identity and server-only irreversible commits are required.
