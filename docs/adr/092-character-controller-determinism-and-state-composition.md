# ADR-092: Character Controller Determinism and State Composition

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: Canonical Character state fields and encoding, Physics/world checkpoint composition, exact hash and diagnostic tolerance policy, bounded history, restore/resimulation, save/replay/future-network reuse, lifecycle, errors, limits and qualification
- **Issue**: [CHR-006.1](https://github.com/abdullahbodur/horo-engine/issues/980)
- **Jira**: [HORO-980](https://horo-engine.atlassian.net/browse/HORO-980)
- **Parent**: [CHR-006](https://github.com/abdullahbodur/horo-engine/issues/935)
- **Related**: [ADR-008](008-error-model-exception-boundary-and-registry.md), [ADR-026](026-large-world-precision-and-floating-origin-strategy.md), [ADR-061](061-animation-ownership-update-order-and-clock.md), [ADR-084](084-canonical-physics-solver-units-and-tolerances.md), [ADR-087](087-scene-to-physics-ownership-and-conversion.md), [ADR-088](088-physics-determinism-capability-and-support-tiers.md), [ADR-089](089-character-controller-ownership-implementation-and-update-order.md), [ADR-090](090-character-dynamic-body-visibility-push-and-proxy-policy.md)
- **Normative documents**: [Character Controller Architecture](../architecture/runtime/character-controller-architecture.md), [Physics Architecture](../architecture/runtime/physics-architecture.md), [Runtime Lifecycle](../architecture/runtime/runtime-lifecycle.md), [Scene Runtime](../architecture/runtime/scene-runtime.md), [Save Game And Persistence](../architecture/runtime/save-game-and-persistence.md), [Networking Architecture](../architecture/runtime/networking-architecture.md)

## Context

ADR-089 defines a scene-owned `CharacterWorld`, tick-addressed commands and one
committed controller state, while ADR-088 requires exact canonical Physics hashes
for qualified determinism tiers. The current Character text names several state
categories but does not say which values are sufficient to resume the next tick,
which transient values must be excluded, or how a Character checkpoint composes
with the Physics/world snapshot it references.

Without one canonical schema, save code may persist a transform, replay code may
retain native query state and a future network path may invent a smaller packet
model. Restoring any one of those could lose platform attachment, stance progress,
root-motion consumption or pending dynamic reaction and then diverge on the first
tick. Serializing Jolt bodies, proxy handles, query caches or raw C++ structs would
instead freeze backend/transient layout as durable Horo state.

Exact repeatability and tolerance-based diagnostics are also different. An epsilon
can help locate numeric drift but cannot declare two different state histories
equivalent after a hash mismatch. History and resimulation additionally require
bounded checkpoints and commands without making Character own network transport,
packet authority or every producer's input history.

This ADR defines the complete semantic Character checkpoint, its canonical bytes,
hash/tolerance policy and aggregate restore contract. It does not approve rollback
networking, choose replication authority, define packet formats or make a native
solver snapshot durable.

## Decision

### 1. One canonical state schema is the source of truth

`CanonicalCharacterStateV1` is the only Horo semantic state that can resume a
`CharacterWorld` at a committed tick. Save, replay, diagnostics and future network
consumers use the same typed model and `CharacterStateCodecV1`; none defines a
parallel authoritative field set.

The schema has three layers:

1. a world checkpoint header that proves compatibility and composition;
2. one controller record per active stable authored controller identity;
3. optional bounded continuation records explicitly declared by the semantic
   algorithm, such as pending dynamic reaction.

Runtime handles may index live storage but are never canonical identity. Records
are ordered by stable scene object ID and authored Character component/slot ID.
Duplicate, missing, zero or out-of-order identities reject decode/restore.

That strict rule governs deterministic checkpoint/replay/session restore. Durable
Save Game restoration may first run an explicitly versioned content migration that
tombstones removed controller identities and materializes newly required controllers
from the target scene's authored defaults. Migration produces a complete canonical
payload before this codec runs; the codec itself never skips unknown records or
invents defaults, and Tier 2/3 restore never admits the permissive migration path.

### 2. The checkpoint header binds the complete simulation cut

Every Character checkpoint contains:

```cpp
struct CharacterCheckpointHeaderV1 {
    CharacterStateSchemaVersion schema;
    CharacterSemanticVersion semanticVersion;
    SimulationTick committedTick;
    SceneDefinitionRevision sceneRevision;
    SceneStructuralRevision structuralRevision;
    PhysicsCheckpointId physicsCheckpoint;
    PhysicsDeterminismFingerprint determinismFingerprint;
    PhysicsWorldIdStable worldIdentity;
    WorldOriginState origin;
    CollisionSchemaRevision collisionSchema;
    CharacterProfileRevision profileRevision;
    CharacterCommandProtocolVersion commandProtocol;
    std::uint32_t controllerCount;
    Digest256 controllerTableDigest;
    Digest256 worldDigest;
};
```

`PhysicsWorldIdStable` is checkpoint identity, not the process-local handle from a
live world. `WorldOriginState` contains ADR-026's exact canonical origin/cell state,
not only an ephemeral generation number. The fixed quantum, solver/FP/job tuple,
shape/material/filter versions and algorithm versions remain bound through the
determinism fingerprint and referenced aggregate manifest; they are not copied as
unverified per-controller fields.

Character capture is valid only when the header tick, scene/structural revision,
origin and fingerprint exactly match the paired Physics/world checkpoint. A
standalone Character blob is inspectable but cannot be restored.

### 3. Every controller record contains exactly resume-required state

The following fields are canonical for each active controller, in this order of
semantic groups:

#### Identity and compatibility

- stable scene object ID and Character component/slot ID;
- descriptor, controller profile and interaction-profile revision/digest;
- controller semantic state generation, never a live slot address/handle value;
- committed source tick, state sequence and active lifecycle state.

#### Collision root and motion continuation

- ADR-026 canonical global collision-root position;
- finite unit capsule up basis and canonical heading twist/quaternion;
- achieved collision-root linear velocity for the committed tick;
- Character-owned gravity/free-fall velocity accumulator;
- optional pending support-transfer velocity plus its source tick and unconsumed
  presence bit;
- movement/grounding mode and only profile-declared hysteresis/remainder counters
  that affect the next tick.

Gameplay locomotion mode, camera state, visual lean/aim and animation pose are not
Character fields. Gameplay/Animation checkpoint providers own them.

#### Stance and geometry continuation

- current and target stable stance IDs;
- stance transition phase, integer step/progress and fixed-point remainder;
- current capsule semantic generation;
- pending stance result/state only when it is already committed and changes the
  next tick's continuation.

Capsule radius/height, slope limit and other immutable values are derived from the
exact descriptor/profile revision and transition state. They are validated during
restore but are not duplicated as another authority.

#### Ground and moving-support continuation

- grounded/airborne/sliding state and previous committed state required for the
  next transition;
- canonical ground normal, signed support distance/classification and resolved
  surface material ID when grounded;
- optional stable Physics body/collider/subshape binding for support;
- support-local attachment point/heading basis and inheritance flags;
- last committed support transform/point linear-angular velocity evidence required
  by ADR-089's next-tick carry prediction;
- pending detach/transfer reason only when it has committed continuation semantics.

The stable support binding uses an authored, persisted collider/subshape ID carried
through the Physics cook mapping; it is never a native index or a hash of mutable
topology. Generated source subparts require a persisted source-subresource ID, and a
recook that cannot preserve that mapping is explicitly incompatible. The binding
resolves against the paired Physics checkpoint. A native body ID or pointer is never
stored. An unresolved required support reference rejects the aggregate candidate
rather than silently grounding against the world.

#### Command and occurrence watermarks

- last committed Character command tick and canonical per-producer sequence;
- last consumed root-motion occurrence/request identity and tick;
- last applied teleport, stance/up-basis and interaction-policy command sequence
  where replay protection needs it;
- next Character locomotion fact/event sequence and last committed result sequence.

These watermarks prevent a restored checkpoint from applying a retained command or
root-motion request twice. Future commands are not embedded in state; they belong to
the bounded command journal owned by the resimulation coordinator.

#### Dynamic interaction continuation

- effective ADR-090 interaction mode/profile revision;
- optional committed `CharacterDynamicReaction` source tick, net impulse, canonical
  normal/point velocity, deterministic saturation flag and any bounded contributor
  identity required by the reaction algorithm;
- proxy semantic generation only when required to reject stale commands.

The proxy body, native contact cache/manifold and callback order are derived or
transient. Restore creates a fresh private proxy from descriptor/state and does not
copy native state.

#### Algorithm-owned remainder and random state

- every named fixed-point accumulator/remainder whose value affects the next tick;
- every admitted Character-owned named random stream's algorithm/version/state and
  consumption count.

CanonicalV1 Character uses no random stream. A future algorithm cannot begin using
randomness without adding a versioned named field and fingerprint change.

### 4. Derivable, published and backend-transient values are excluded

Canonical state does not contain:

- process-local scene/world/controller/body/shape/proxy handles or slot indexes;
- Jolt objects, `BodyID`, `SubShapeID`, `CharacterVirtual`, `StateRecorder`, native
  broadphase/contact/cache/manifold/collector state or allocator bytes;
- query hits, sweep iterations, scratch arrays, temporary candidate state, staged
  impulses/proxy targets or worker/job completion order;
- immutable descriptor fields derivable from pinned revisions;
- current-frame presentation interpolation, visual pose, IK overlay, camera,
  renderer, Audio or VFX state;
- debug geometry, logs, diagnostics, metrics, timestamps or profiling counters;
- already published contact/result/fact payloads except the sequence/watermark and
  continuation values required to prevent replay or advance the next tick;
- future/unadmitted commands, input snapshots, Animation graph/player state,
  Gameplay state or Network packet/session state.

Canonical tick output is a separate immutable evidence schema. It may include
contacts, movement result and locomotion facts for replay comparison, but restoring
those outputs cannot replace state and does not re-emit side effects.

### 5. Encoding is explicit and portable within the qualified tuple

`CharacterStateCodecV1` emits a bounded canonical binary stream:

- fixed ASCII domain/magic and unsigned little-endian schema/length/count fields;
- explicit field tags/order and length-delimited records; no native struct dumps,
  padding, RTTI, container capacity or host `size_t`;
- stable IDs in their specified canonical byte order;
- IEEE-754 binary32/64 only where the owning Horo value contract selects it;
- all floats finite, signed zero normalized to positive zero, quaternion/unit-vector
  normalization and quaternion sign canonicalized before commit;
- enum values range checked and unknown required fields/versions rejected;
- checked aggregate and per-record byte/count limits before allocation.

The codec is shared implementation, not shared serialized bytes across every use.
A save archive wraps the canonical payload in its durable chunk/integrity envelope;
a replay may frame checkpoints/deltas; a future network protocol may fragment,
compress or exact-delta the canonical bytes. Those wrappers cannot reinterpret,
quantize, omit or add authoritative Character fields.

### 6. Exact hashes are authoritative; tolerances are diagnostic

Each controller digest is SHA-256 over the ASCII domain
`HORO.CHARACTER.STATE.CONTROLLER.V1`, a zero separator and the exact canonical
controller bytes. The world digest is SHA-256 over
`HORO.CHARACTER.STATE.WORLD.V1`, a zero separator, the canonical header excluding
the two stored digest fields, and every ordered controller record length plus
digest. `controllerTableDigest` covers that ordered length/digest table;
`worldDigest` stores the resulting world digest.

Equivalent decoded state must re-encode byte-for-byte. Incremental and batch hashing
must produce the same digest. Digests are state identity/evidence, not authentication;
save/network trust uses its owning integrity/authentication contract.

ADR-088 Tier 2/3 comparison is exact. A different canonical byte or digest fails at
the first differing tick. No epsilon, hash bucket, rounding after capture or
"visually equivalent" result may turn that failure into a pass.

`CharacterStateDiagnosticToleranceV1` may additionally report drift:

| Value | Absolute tolerance | Relative tolerance |
|---|---:|---:|
| local position/support distance | `1.0e-4 m` | `1.0e-6` |
| linear/gravity/support velocity | `1.0e-5 m/s` | `1.0e-6` |
| unit-vector/quaternion component | `1.0e-6` | `1.0e-6` |
| angular value/velocity | `1.0e-6 rad` / `1.0e-6 rad/s` | `1.0e-6` |

Stable IDs, ticks, sequences, modes, enums, presence bits, fixed-point remainders,
global cell/integer coordinates and schema/fingerprint values compare exactly.
Tolerance reports identify drift only and never authorize restore, replay
compatibility or determinism success.

### 7. Capture is an atomic post-commit view

Character capture occurs at the Runtime lifecycle safe point from one fully
committed stage-9 tick, after Physics/Character/Animation publication and before
the next command cutoff. There is no candidate tick, staged command or half-applied
structural change in the snapshot.

The aggregate checkpoint coordinator pins the exact Scene structure, Physics state,
Character state, origin and required provider roots for one tick. Character writes
owned immutable bytes/pages within an admitted budget and releases live storage;
workers hash/compress/store only the detached checkpoint. It never freezes the
simulation while durable I/O completes.

If any required provider cannot expose the same cut, capture defers or fails. It
does not combine Character tick N with Physics tick N-1 or a newer origin/filter
generation.

### 8. Restore is aggregate, validated and all-or-nothing

Restore prepares a detached scene/Physics/Character candidate:

1. bound/check envelope sizes, schema and canonical encoding;
2. validate fingerprint, scene/structural/profile/filter/origin compatibility or
   run an explicitly registered durable migration before candidate construction;
3. build the detached Scene and Physics checkpoint candidate;
4. create Character slots/proxies from stable descriptors in canonical order;
5. decode every controller record and resolve all required support/body/material
   references against the candidate Physics binding table;
6. validate finite/canonical values, invariants, capacities and command/occurrence
   watermarks;
7. reconstruct only derived caches/query/proxy/native state and run a no-step
   validation pass;
8. publish the complete aggregate at `CommitDeferredLifecycleChanges` with no
   remaining fallible work.

Any missing/extra/duplicate controller, incompatible field, unresolved support,
invalid reaction, capacity failure or cancellation destroys only the candidate.
The active Scene/Physics/Character bundle and public generations remain unchanged.
Restore emits no contacts, locomotion facts, root motion, Audio/VFX or Gameplay
side effects for the checkpoint tick.

For durable Save Game migration only, an unresolved support binding may apply a
declared `ClearSupportAndResumeAirborne` migration: it clears attachment/carry
continuation, marks the controller airborne, and records the migration in archive
evidence before canonical validation. Deterministic replay/session restore and
unregistered migrations still reject the complete aggregate.

Partial controller restore into a live world is forbidden. A future structural
subset operation must be a separately versioned gameplay/scene command with explicit
reference and authority semantics; it cannot call the checkpoint restore path.

### 9. History and resimulation are bounded host capabilities

The host may compose a `CharacterCheckpointHistory` with finite checkpoint count,
byte budget, full-checkpoint interval and bounded exact deltas. Admission reserves
the worst-case full fallback; a delta chain cannot grow without a full checkpoint
or exceed maximum restore depth.

Character history stores canonical state/output bytes and digests. The simulation/
replay coordinator separately retains the ordered tick-addressed Character command,
Animation root-motion, Gameplay/input, Physics structural and other required
provider histories. Character does not copy or own foreign producer state.

Resimulation restores one aggregate checkpoint, suppresses/reconciles external side
effects under the owning contracts, then replays exact command frames through the
ordinary fixed-tick pipeline. It never edits state fields directly or calls a
special controller update with different semantics.

History exhaustion, missing command/provider history or fingerprint change ends the
available rewind horizon with a typed result. It does not fetch network packets,
block for storage or silently continue from partial state.

### 10. Save, replay and networking reuse without sharing authority

Runtime Save contributes a stable required `horo.character.state.v1` provider chunk
using the canonical codec and aggregate capture epoch. Durable migrations convert
old supported schemas into a complete current candidate before publication; they
never fabricate missing resume fields without an explicit migration rule.

Replay records canonical full/delta checkpoints, exact command frames, fingerprints
and output hashes. A replay requiring exact Tier 2/3 evidence rejects an incompatible
build/profile/content tuple rather than tolerance-restoring it.

A future Network runtime may reference/delta/compress canonical state after its own
authority, interest, baseline, acknowledgement and security decisions. Transport
packet order, connection/session IDs, sequence/ack numbers, prediction policy and
loss recovery never enter `CanonicalCharacterStateV1`. A lossy presentation snapshot
is allowed only as a distinct non-authoritative schema and cannot restore or hash as
canonical Character state.

Thus the canonical typed model/codec is shared, while capture cadence, retention,
transport, trust and authority remain owned by Save, Replay host and Network.

### 11. Structural and content changes close compatibility explicitly

Controller add/remove, descriptor/stance/profile/filter/material changes, Physics
body/support replacement, origin rebase and scene reload are ordered structural or
semantic commands. A checkpoint after their commit records the new structural
revision/fingerprint and complete controller set.

History cannot cross an unrecorded structural boundary. Exact resimulation crosses
only when the coordinator retains the versioned command plus both sides' immutable
artifacts and the effective determinism capability qualifies it. Hot reload without
that evidence starts a new checkpoint/session epoch.

Durable save migration may intentionally cross engine/schema revisions, but its
output is a new canonical candidate with a new digest and recorded migration chain;
it is not proof that old/new builds replay identically.

### 12. Concurrency, lifecycle and retention are explicit

`CharacterWorld` is the only mutable state owner. Capture borrows committed state on
the owner thread and publishes immutable leases. Hash/compression/diff workers never
retain live controller pointers. Restore workers prepare owned candidate values and
the owner thread alone publishes them.

Scene replacement/unload closes new capture/history/restore/query/command admission,
cancels and joins candidate work, drains immutable leases and retires history before
destroying controller storage. Late captures/restores with retired scene/world/state
generations are rejected. Shutdown is idempotent after active, partial capture,
partial candidate, failed restore and already-stopped states.

History/save/replay/network consumers use separate bounded leases. One slow consumer
cannot force unbounded state retention; it receives backpressure, a dropped optional
checkpoint or a typed required-capture failure under its declared policy.

### 13. Errors and observability preserve first cause

ADR-008 results distinguish malformed/noncanonical encoding, unsupported schema,
fingerprint/scene/structure/origin/profile mismatch, missing/extra/duplicate record,
invalid finite/unit/enum/state combination, unresolved support/body/material,
stale command/reaction/watermark, digest mismatch, history/delta/capacity overflow,
missing resimulation input, cancellation, operation conflict, unload and shutdown.

Diagnostics carry bounded checkpoint/tick, stable controller ID, schema/fingerprint,
expected/actual digest, first differing field tag/index, provider phase, counts and
limits. They never dump native state, full save/network payloads or arbitrary
Gameplay data.

Metrics include capture/encode/hash/restore counts and time, full/delta bytes,
history depth/high water, migration outcome, mismatch field class and stale/overflow
rejections. Stable result/schema/field enums may be dimensions; controller IDs and
digests may not.

### 14. Qualification proves state completeness and boundaries

Required coverage includes:

- golden canonical bytes and SHA-256 for empty, single and maximum ordered worlds;
- every required field toggled independently changes canonical bytes/digest and a
  capture/restore/next-tick oracle proves it is necessary;
- every excluded transient/native/debug field changes without changing canonical
  state when semantic outputs remain equal;
- signed zero, quaternion sign, non-finite/denormal policy, enum/range, duplicate/
  order, truncation/trailing bytes and checked-size fuzz cases;
- exact-hash mismatch remains failure while each diagnostic tolerance boundary
  reports pass/fail without changing the exact outcome;
- grounded/airborne/sliding, static/kinematic/dynamic support, stance transition,
  transfer velocity, root-motion watermark and pending proxy reaction restore;
- capture at structural/origin/profile/reload boundaries and rejection of mixed-tick
  or unresolved Physics/world references;
- failed/cancelled restore at every phase preserves the active aggregate and emits
  no tick facts or presentation side effects;
- full plus bounded-delta history eviction, maximum restore depth, missing command
  history and ordinary-pipeline resimulation;
- save round trip/migration, replay checkpoint/hash and future network exact-delta
  fixtures decode through the same canonical codec;
- render cadence, catch-up grouping, worker/callback order and repeated shutdown;
- maximum controllers/state bytes/history budget with zero steady-tick heap growth.

## Consequences

Character has one complete semantic state and one exact encoding/hash contract.
Save, replay and future networking cannot drift into competing field authorities,
and restore cannot pair Character with the wrong Physics/world cut or partially
mutate a live scene. Native/transient data remains private and rebuildable.

The cost is explicit versioned state maintenance, aggregate checkpoint coordination,
stable support/watermark/reaction fields, bounded history and migration work. Exact
determinism rejects any bit-level canonical divergence; tolerance remains useful
only for diagnosis.

## Rejected Alternatives

### Save only the collision-root transform and velocity

Rejected because stance, support carry, gravity continuation, command/root-motion
watermarks and dynamic reaction can change the first resumed tick.

### Serialize the live `CharacterWorld` or Jolt/native state

Rejected because layout, pointers, handles, caches and solver-version details are
backend-transient and cannot be a durable Horo contract.

### Let save, replay and networking define separate authoritative schemas

Rejected because field drift would make equivalent checkpoints resume differently.
They share the canonical model/codec and retain separate envelopes/authority.

### Restore Character independently from Physics and Scene

Rejected because support bodies, origin, structure, filters and dynamic reaction
are meaningful only against the exact paired aggregate checkpoint.

### Accept tolerance-equal state after an exact hash mismatch

Rejected because small numeric drift can cross a discrete collision/grounding branch
later. Tolerance identifies drift but cannot restore determinism evidence.

### Store every tick as an unbounded full snapshot

Rejected because memory/time grow without a declared product budget. History uses
finite full/delta bounds and reports horizon exhaustion.

### Put packet/session/prediction state in Character state

Rejected because Network transport and replication authority are optional consumers.
Canonical Character state must remain usable by offline save/replay/headless hosts.
