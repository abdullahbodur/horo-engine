# ADR-146: Destruction Runtime Activation, Physics, Cleanup and Rollback

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: Destruction command safe points, intact/chunk authority, deterministic support loss, pre-cooked Physics body preparation, aggregate publication, cleanup, rollback, replacement, cancellation and shutdown
- **Issue**: [DFR-003.1](https://github.com/abdullahbodur/horo-engine/issues/2011)
- **Jira**: [HORO-1965](https://horo-engine.atlassian.net/browse/HORO-1965)
- **Related**: [ADR-008](008-error-model-exception-boundary-and-registry.md), [ADR-010](010-job-waiting-and-operation-store-ownership.md), [ADR-012](012-world-streaming-partition-authority-and-subsystem-boundaries.md), [ADR-023](023-world-index-and-cell-format-architecture-decision.md), [ADR-027](027-renderer-resource-identity-and-descriptors.md), [ADR-085](085-physics-shape-authoring-cook-and-runtime-boundary.md), [ADR-087](087-scene-to-physics-ownership-and-conversion.md), [ADR-099](099-replication-ownership-authority-and-compatibility.md), [ADR-114](114-canonical-runtime-world-persistence-boundary.md), [ADR-144](144-destruction-ownership-authority-state-and-runtime-geometry-boundary.md), [ADR-145](145-destruction-source-chunk-geometry-collision-and-cook-ownership.md)
- **Normative documents**: [Destruction and Fracture Architecture](../architecture/runtime/destruction-and-fracture-architecture.md), [Physics Architecture](../architecture/runtime/physics-architecture.md), [Scene Runtime](../architecture/runtime/scene-runtime.md), [Save Game and Persistence](../architecture/runtime/save-game-and-persistence.md)

## Context

ADR-144 assigns canonical destruction semantics to a scene-scoped
`DestructionWorld` and requires Scene, Physics and Render changes to publish through
one aggregate root. ADR-145 makes chunk geometry, connectivity and solver-neutral
collision inputs immutable cooked data while Physics owns its separately keyed native
shape artifacts. Neither decision fixes the runtime transaction that turns an intact
object into active chunks.

Without that transaction, a Physics contact callback could mutate health, hide the
intact render object and create bodies while the solver is stepping. A failure after
one of those operations would expose a half-fractured object, leak native resources or
require reconstructing the old world from state that no longer exists. Treating body
sleep as cleanup permission would also erase gameplay-authoritative state without a
durable handoff.

Core 1.0 additionally needs one deliberately simple support-loss rule. It must consume
the pre-cooked graph, terminate within declared limits and remain distinct from damage
accumulation and threshold policy owned by DFR-003.2. Runtime stress solving, topology
generation and solver-specific fracture decisions are outside this ticket.

This ADR defines the command-to-commit protocol, owner safe points, body preparation,
cleanup classes and rollback boundary. It does not define damage magnitudes,
thresholds, fracture-authoring algorithms, rendering effects, save encoding,
replication encoding or exact product limits.

## Decision

### 1. Semantic, native and aggregate state retain separate authorities

| State or action | Authority |
|---|---|
| Health, semantic phase, broken/supported/dormant chunk sets, transition revisions and canonical events | Scene-scoped `DestructionWorld` |
| Permission to issue damage/fracture/restore commands | Product gameplay or replication authority |
| Body, shape, constraint, island, sleep, contact and Physics-world lifetime | Physics |
| Entity/component lifetime and the complete visible scene root | RuntimeScene |
| Intact/chunk draw objects, visibility, materials, GPU resources and retirement | Render |
| Cell demand, peak reservations and eviction admission | World Streaming |
| Durable destruction state and dormancy handoff | Runtime Save/Persistent World |
| Cosmetic debris, audio, decals and camera effects | Their presentation owners |

`DestructionWorld` never owns a native body, shape, solver world or render handle.
Physics cannot decide that a contact fractured a chunk, and Render cannot make a chunk
canonical because it became visible. RuntimeScene coordinates publication but does not
interpret damage, support graphs, mass properties or solver state.

Each destructible has a stable `DestructibleId`, runtime generation, exact content
generation and monotonically increasing semantic revision. Each chunk is addressed by
the cooked `DestructionChunkId`, never by a vector index, ECS entity, native body ID or
render object ID. The active RuntimeScene root holds generation-scoped private mappings
from those identities to consumer-owned objects.

### 2. Commands and Physics evidence enter at bounded owner safe points

Gameplay and replication submit typed commands to the Destruction command queue. A
command includes issuer/authority, target destructible generation, expected semantic
revision, idempotency key, command kind, bounded payload and eligible fixed tick. Queue
admission validates size and structural shape but does not mutate the world.

Physics contact callbacks may write only bounded immutable contact evidence into a
pre-reserved Physics-owned buffer. Evidence contains Horo body/chunk bindings, world and
binding generations, fixed-tick ID, canonical pair ordering and finite impulse/contact
facts. It contains no borrowed solver pointer. Overflow is an explicit tick result and
cannot invoke gameplay or allocate an unbounded spill buffer.

The fixed-tick order is:

1. Destruction accepts admitted gameplay/network commands at its pre-tick safe point
   and rejects stale authority, generation or revision.
2. Physics applies previously committed commands and steps exactly once. Callbacks only
   collect evidence.
3. Physics freezes and publishes the completed tick's evidence after the step.
4. Destruction consumes eligible evidence in canonical
   `(tick, destructible, chunk, pair, sequence)` order and may admit a later typed
   damage/fracture command.
5. Destruction plans transitions and asks consumer owners to prepare candidates.
6. Prepared consumer changes publish privately at their safe points and remain hidden
   behind an activation ticket.
7. RuntimeScene commits the complete aggregate root at
   `CommitDeferredLifecycleChanges`; canonical events become observable afterward.

A contact observed during tick `N` cannot change solver topology or public destruction
state inside tick `N` callbacks. It can first contribute to a legal later transition.
DFR-003.2 defines how evidence and gameplay damage produce threshold decisions; this ADR
defines only the safe transport and commit order.

### 3. One immutable transition plan closes direct and unsupported chunks

After command validation, `DestructionWorld` builds an immutable candidate equivalent
to:

```cpp
struct DestructionTransitionPlan {
    DestructionTransitionTicket ticket;
    DestructibleId destructible;
    DestructionGeneration generation;
    DestructionStateRevision expectedRevision;
    DestructionStateRevision candidateRevision;
    FractureArtifactIdentity artifact;
    DestructionPolicyRevision policy;
    FixedTickId evidenceThrough;
    BoundedChunkSet directlyDetached;
    BoundedChunkSet supportDetached;
    BoundedChunkSet remainsSupported;
    DestructionPeakReservation peak;
};
```

The plan owns values and immutable artifact leases until it reaches a terminal state.
It names every Scene entity mutation, Physics body/shape request, Render object change,
event and old/new resource overlap before preparation begins. Checked arithmetic proves
the complete peak reservation; no consumer may discover extra chunks during publication.

Core 1.0 support loss is graph reachability, not a runtime stress solver. Starting from
the cooked support/connectivity graph, the planner removes the command-selected chunk
set, then traverses remaining eligible edges from the immutable anchor set. Every
remaining chunk not reachable from an anchor is unsupported and joins the detach set.
Traversal, component enumeration and output use ascending stable chunk ID with fixed
neighbor ordering. A missing anchor/edge reference, cycle-policy violation,
non-canonical graph, stale artifact or work/output limit failure rejects the whole plan.

This rule does not accumulate damage, invent a threshold, propagate impulse through
edges, split geometry, reparent chunks or ask Physics which island should fracture.
Richer structural stress, staged fracture and runtime topology are separate qualified
post-1.0 capabilities. When the exact closed set exceeds the active tier, the result is
`DestructionTransitionLimitExceeded`; partial detachment is forbidden.

### 4. Physics prepares exact pre-cooked chunk bodies privately

For each chunk in the closed detach set, Destruction lends Physics the exact stable
chunk identity, DFR artifact/subshape revision, transform, density/material/filter
intent and bounded initial-motion descriptor. Physics resolves only the ADR-145
solver/profile/platform-private immutable shape artifact and validates dynamic-convex
eligibility. It never imports geometry, runs convex decomposition, substitutes a
box/triangle mesh, changes chunk membership or creates a fallback body.

Physics computes finite mass, center of mass and inertia from validated cooked inputs
and policy. Zero/negative/non-finite mass data, an unavailable shape artifact,
unsupported motion/filter policy or capacity denial fails preparation. Initial linear
and angular impulses are staged commands applied once at the Physics pre-step publish
safe point; Destruction does not write body velocity directly.

Physics returns generation-scoped receipts equivalent to:

```cpp
enum class DestructionPhysicsStage : uint8_t {
    Preparing,
    Prepared,
    PrivatelyPublished,
    Public,
    Retiring,
    Retired,
    Failed,
    Cancelled
};

struct DestructionPhysicsReceipt {
    DestructionTransitionTicket ticket;
    PhysicsWorldGeneration world;
    PhysicsBindingGeneration bindings;
    PhysicsShapeArtifactIdentity shapeArtifact;
    DestructionPhysicsStage stage;
    DestructionPhysicsResult result;
};
```

`Prepared` means resources exist but no active-world query can find them.
`PrivatelyPublished` means Physics installed them at its safe point in ticket-scoped
routing; ordinary queries still resolve the old aggregate root. Only RuntimeScene
aggregate commit makes the receipt `Public`. Receipts contain Horo identities and typed
results, never native handles.

### 5. Aggregate commit is the only intact-to-chunk visibility boundary

RuntimeScene owns one candidate containing the Destruction semantic snapshot, exact ECS
batch, Physics receipt, Render receipt and required leases/reservations. Preparation may
run off-thread against immutable inputs; every owner validates its exact generation
again at its publish safe point. The final commit performs no allocation, cooking,
graph traversal, waiting, user callback or fallible native creation.

Immediately before commit, the old root remains complete: intact/previous semantics,
Scene representation, body/render visibility and queries agree. Immediately after
commit, the new semantic revision, chunk entities, exact required bodies/render objects
and intact retirement routing agree. No ordinary observer can see a prepared chunk body
or hidden intact object early.

Required Render or Physics absence rejects the candidate. A headless product may omit
Render only when its validated profile declares Render optional while preserving the
same Destruction/Physics transaction. There is no visual-only, collision-only,
Null-backend or previous-body fallback for required capability.

Canonical destruction, gameplay, replication and save events publish after commit and
carry the committed revision/ticket. Optional VFX, Audio and Decal requests consume the
event and cannot decide success. A dropped optional request does not undo canonical
state.

### 6. Cleanup is policy-driven; sleep and visibility are not permission

Every activated chunk is classified by validated policy as gameplay-authoritative,
durable-world or cosmetic presentation. Core chunk bodies created by this transaction
are gameplay-authoritative or durable-world unless an explicit future capability says
otherwise; VFX debris remains separate cosmetic state.

Physics sleep is an observation only. Distance, age, off-screen status, memory pressure
or missing Render cannot silently remove a canonical chunk. A cleanup request names the
eligible chunk set, expected revision, reason, stable priority/tie order and complete
peak/retirement cost. Destruction validates it and creates another aggregate transition.

Gameplay-authoritative chunks may become dormant only through explicit gameplay policy.
Durable-world chunks additionally require Runtime Save/Persistent World to acknowledge
the exact state revision and durable handoff before World Streaming permits eviction.
Dormancy removes live Scene/Physics/Render representations through aggregate commit but
retains canonical chunk identity, phase and dormant membership. Reactivation uses a new
ticket and the same artifact/generation checks, not body resurrection by native ID.

Cosmetic VFX debris uses its separately bounded pool/lifetime and is never added to the
canonical chunk set, save state or replication hash. Capacity pressure may trim that
pool according to declared policy without granting permission to retire canonical
chunks.

### 7. Rollback ends at aggregate commit

Before aggregate commit, cancellation, stale revision, authority loss, content/world
replacement, budget denial, consumer failure or shutdown marks the candidate terminal
and preserves the old public root. Each consumer removes only its ticket-scoped private
routing at its safe point. Candidate resources and old-generation leases retire after
all jobs, steps, queries, command buffers and frames that can reference them drain.
Logical rollback never means immediate native free.

After aggregate commit, the transaction is history and is never rolled back in place.
Restore, repair, dormancy or replacement requires a new authorized command, expected
revision and aggregate transaction. If Physics or Render loses a required capability
after commit, RuntimeScene exposes typed `Suspended`/`Failed` availability and begins
the owner's recovery policy; it does not pretend the destructible is intact or rewind
canonical state without a compensating transition.

Content replacement prepares a new artifact and dependent consumer products beside the
old generation. Preservation is allowed only when a versioned compatibility plan maps
every active/broken/support/dormant stable chunk identity and validates semantic, shape
and material compatibility. Positional index or name matching is forbidden. Mismatch
rejects replacement and leaves the old generation live; reset is a separate destructive
command.

### 8. Capacity, errors, observability and shutdown are bounded

Active policy declares finite limits for commands, contact evidence, chunks per
transition, support edges visited, candidate bytes, active/dormant bodies, events,
simultaneous tickets and old/new overlap. Reservations charge Destruction, Physics,
Render, RuntimeScene and World Streaming before work. A runtime limit cannot grow from
content, retry recursively or borrow from a different world without admission.

Typed results distinguish at least `StaleGeneration`, `StaleRevision`,
`AuthorityDenied`, `InvalidEvidence`, `InvalidSupportGraph`,
`DestructionTransitionLimitExceeded`, `ShapeArtifactUnavailable`,
`PhysicsCapabilityUnavailable`, `PhysicsPreparationFailed`,
`RenderPreparationFailed`, `PeakReservationDenied`, `CancelledBeforeCommit`,
`ReplacementIncompatible` and `ShutdownInProgress`. Failures retain the ticket, owner,
stage, relevant generations and bounded offending counts/IDs. Native error codes remain
private evidence attached by their adapter.

Metrics use bounded dimensions such as result, stage, feature tier and product profile;
they never use asset paths, chunk IDs or native handles as dimensions. Traces may carry
the ticket and stable Horo identities under observability privacy policy. Counters cover
admitted/rejected commands, stale evidence, planned direct/support chunks,
prepare/commit/retire latency, candidate bytes, active/dormant bodies and cleanup
outcomes. Diagnostics cannot mutate or retry a transition.

Shutdown closes new command/evidence admission, freezes publication, cancels candidate
task groups, invalidates unpublished tickets and requests reverse-dependency retirement:
presentation consumers, RuntimeScene mappings, Physics/Render objects, then artifact
leases and Destruction storage. Owners retain modules, queues, receipts and resources
until exact-generation readers acknowledge release. A deadline reports incomplete
shutdown and never force-frees potentially referenced native state.

## Compatibility And Follow-Ups

This decision narrows the broad “runtime fracture” description to activation of
validated pre-cooked chunks. Future APIs expose stable Horo identities, revisions,
tickets, receipts and typed results rather than mutable component fields or native
handles. There is no supported legacy path that fractures directly from a Physics
callback or treats an individually created body as publication.

[DFR-003.2](https://horo-engine.atlassian.net/browse/HORO-1966) owns damage
accumulation, threshold evaluation and their semantic state transitions. DFR-001.2 and
DFR-001.3 own final identity schemas and numeric limits. Later decisions own Render
realization, persistence, replication, diagnostics and any qualified post-1.0 stress
solver or runtime geometry capability.

## Consequences

### Positive

- Intact and chunk representations cannot become publicly inconsistent during failed
  activation.
- Physics keeps exclusive solver-resource ownership without gaining destruction
  authority.
- Core support loss is deterministic, bounded and testable from the cooked graph.
- Cleanup cannot silently erase authoritative or unsaved world state.
- Pre-commit failure preserves a known-good root while native lifetime drains safely.

### Costs

- Activation needs ticket-scoped private routing and receipts in every required
  consumer.
- Old and candidate resources overlap until commit and retirement, increasing peak
  budgets.
- Post-commit recovery uses compensating transactions rather than local rewinds.

### Rejected Alternatives

- **Fracture directly in a contact callback**: violates solver safe points, command
  authority and bounded callback work.
- **Let Physics choose detached islands**: makes solver state semantic authority and
  breaks cross-provider/save/network identity.
- **Publish bodies, visibility and semantics independently**: exposes partial worlds
  and makes rollback ambiguous.
- **Treat sleeping, old or invisible chunks as disposable**: permits silent canonical
  data loss and nondeterministic cleanup.
- **Restore old native handles after commit**: ignores in-flight readers and cannot
  restore an atomic world revision.
- **Cook or substitute collision at activation**: violates ADR-145 and makes the core
  path unbounded and platform-dependent.

## Verification

Required contract and integration coverage includes:

- command authority, idempotency, generation/revision and fixed-tick ordering;
- contact callback non-mutation, evidence overflow, stale evidence and canonical sort;
- direct-detach plus anchor-reachability golden graphs, malformed graphs, stable output
  and full rejection when closure exceeds limits;
- exact artifact/chunk/subshape Physics binding, finite mass properties, staged initial
  impulse and proof that runtime shape cook/fallback never occurs;
- prepared/private/public receipt visibility and adversarial owner safe-point order;
- aggregate atomicity under every Destruction, Scene, Physics, Render and reservation
  failure point;
- cancellation before commit, asynchronous retirement and no premature free;
- sleep/distance/visibility pressure without cleanup permission, durable dormancy
  handoff, reactivation and cosmetic-debris separation;
- compatible/incompatible replacement, stale receipts and compensating post-commit
  recovery;
- headless optional-Render and required-Physics profiles with no semantic drift; and
- repeated shutdown with jobs, steps, queries and frames in flight, including deadline
  reporting without forced release.
