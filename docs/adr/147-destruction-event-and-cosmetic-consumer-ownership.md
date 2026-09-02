# ADR-147: Destruction Event and Cosmetic Consumer Ownership

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: Committed destruction facts, event identity and lifetime, application dispatch, delivery safe points, deduplication, VFX/Decal/Audio/gameplay consumer ownership, failure, replacement and shutdown
- **Issue**: [DFR-004.1](https://github.com/abdullahbodur/horo-engine/issues/2021)
- **Jira**: [HORO-1975](https://horo-engine.atlassian.net/browse/HORO-1975)
- **Related**: [ADR-008](008-error-model-exception-boundary-and-registry.md), [ADR-010](010-job-waiting-and-operation-store-ownership.md), [ADR-011](011-vfx-effect-ownership-simulation-domain-and-renderer-boundary.md), [ADR-018](018-command-registration-permissions-threading-and-packaged-build-policy.md), [ADR-027](027-renderer-resource-identity-and-descriptors.md), [ADR-062](062-audio-runtime-ownership-and-update-order.md), [ADR-091](091-footstep-and-locomotion-event-ownership.md), [ADR-120](120-cinematic-event-dispatch-and-audio-coupling-boundary.md), [ADR-123](123-vfx-cpu-stage-order-determinism-and-gameplay-coupling.md), [ADR-127](127-vfx-decal-projection-lifetime-and-rendering-path-policy.md), [ADR-128](128-vfx-spawn-event-mapping-pooling-and-budget-enforcement.md), [ADR-144](144-destruction-ownership-authority-state-and-runtime-geometry-boundary.md), [ADR-146](146-destruction-runtime-activation-physics-cleanup-and-rollback.md)
- **Normative documents**: [Destruction and Fracture Architecture](../architecture/runtime/destruction-and-fracture-architecture.md), [VFX and Particles Architecture](../architecture/runtime/vfx-and-particles-architecture.md), [Decal System Architecture](../architecture/runtime/decal-system-architecture.md), [Audio Architecture](../architecture/runtime/audio-architecture.md), [Engine Data Bus](../architecture/foundation/engine-data-bus.md)

## Context

ADR-144 makes `DestructionWorld` the authority for canonical destruction state, and
ADR-146 publishes a new semantic revision only after the complete Scene/Physics/Render
transition commits. The current architecture says that gameplay, VFX, Decal and Audio
consume the resulting event, but it does not distinguish the canonical fact from a
presentation request or define delivery, lifetime and duplicate behavior.

If DFR selects effect assets or starts voices directly, it absorbs application mapping,
consumer budgets and device lifecycle. If consumers subscribe to a generic lossy bus,
required gameplay meaning can disappear when a queue is full or a subscriber reloads.
Conversely, treating a missing cosmetic effect as failure of an already committed
fracture would make presentation capability a semantic authority.

One destruction transition may also produce several related facts and several layers
per consumer. Retries, save restoration, replication correction, scene replacement and
adapter reload must not replay the same sound/decal/gameplay effect accidentally. An
event therefore needs occurrence identity and source revision, while every consumer
needs its own request identity and lifetime fence.

This ADR fixes those ownership and delivery rules. DFR-004.2 owns the exact event-stream
API and application adapters. Later tickets own concrete effect libraries, diagnostics,
replication and persistence encodings.

## Decision

### 1. Facts, mappings and effects have different authorities

| Responsibility | Authority |
|---|---|
| Canonical destruction fact, source order and committed occurrence identity | Scene-scoped `DestructionWorld` |
| Bounded fact journal, cursor and source-generation lifetime | Destruction Runtime |
| Product meaning, destination selection and semantic-to-consumer mapping | Application composition |
| Gameplay effect and commands caused by a fact | Registered gameplay domain adapter |
| Effect asset, spawn admission, simulation, pooling and Render extraction | `VfxWorld` |
| Decal asset, projection target, attachment, lifetime, pool and render descriptor | Decal/VFX presentation owner under ADR-127 |
| Cue/media resolution, schedule admission, voice/mixer/device and sample clock | Audio |
| Caption/accessibility semantic presentation | Accessibility/UI adapter, independently of Audio playback |
| Optional observability notification | `EngineDataBus`, never authoritative delivery |

DFR publishes immutable facts. It never chooses a particle graph, decal material, audio
clip/bank, voice, bus, caption string, camera shake or gameplay callback. Consumer
owners cannot mutate destruction health, chunk membership or revision and cannot report
their local playback/simulation success as fracture success.

The application owns one `DestructionEventDispatcher` per runtime or PIE session. The
composition root injects narrow destination adapters and an immutable cooked binding
table. Descriptor construction is inert: it does not subscribe globally, load assets,
create voices/effects or mutate a world.

### 2. DFR publishes bounded typed committed facts

The baseline envelope is equivalent to:

```cpp
enum class DestructionFactKind : uint8_t {
    Damaged,
    ChunksActivated,
    SupportLost,
    ChunksDormant,
    ChunksReactivated,
    AvailabilityChanged
};

struct DestructionFact {
    DestructionEventOccurrenceId occurrence;
    DestructionWorldId world;
    DestructibleId destructible;
    DestructionGeneration generation;
    DestructionStateRevision stateRevision;
    DestructionTransitionTicket transition;
    FixedTickId committedTick;
    uint32_t revisionOrdinal;
    DestructionFactKind kind;
    BoundedDestructionFactPayload payload;
};
```

`occurrence` is stable for the exact world/destructible generation, committed state
revision, fact kind and revision ordinal. It is not random and is never reused after
unload/replacement. The bounded payload may carry stable chunk IDs or a leased bounded
chunk-set value, world-space point/normal, quantized strength/scale, typed material or
surface semantics and the cause command/evidence identity where policy permits.

Payloads contain finite validated values only. They contain no native Physics/Render/
Audio handle, ECS pointer, mutable span, source path, asset-selected presentation ID,
runtime string, arbitrary map or consumer callback. A binding schema declares which
fields a destination may consume; absence remains explicit.

The fact states what committed, not what should be rendered or played. For example,
`ChunksActivated` can carry the committed chunk set and impact facts, but does not mean
“spawn dust asset X” or “play explosion clip Y.” `SupportLost` is distinct from a direct
damage fact even when both share one transition.

### 3. Publication occurs after aggregate commit into a queryable journal

ADR-146 transition planning computes the exact fact batch and reserves required journal
and dispatcher capacity before aggregate commit. Failed/cancelled candidate transitions
publish no facts. Immediately after RuntimeScene commits the aggregate root,
`DestructionWorld` atomically appends the immutable batch in ascending
`(committedTick, destructible, stateRevision, revisionOrdinal)` order.

The journal is the source-delivery authority for the bounded live window. It publishes a
monotonic `DestructionEventCursor`; registered dispatchers read committed batches by
cursor at the Destruction/application owner boundary. A temporary adapter delay or
lossy notification cannot erase required facts. If a consumer cursor falls behind the
retained window, it receives `DestructionEventGap` and re-queries the canonical
Destruction snapshot/revision instead of guessing or replaying unbounded history.

The canonical snapshot remains the state authority. The journal is not a save archive,
replication log or unlimited audit store. Product limits bound retained revisions,
records, payload bytes, outstanding cursors and required acknowledgements. Required
adapter reservations and retention are charged before transition commit. Cosmetic-only
adapters cannot extend canonical journal lifetime indefinitely.

`EngineDataBus` may announce a coalesced
`DestructionEventJournalRevisionChanged` after append. That notification carries only
query identity/revision. Its loss, duplication, merge or subscriber order cannot alter
fact order, required delivery or consumer success.

### 4. The application dispatcher owns safe-point fan-out

The session dispatcher captures one validated `DestructionEventBindingTableGeneration`
and converts each source fact at the destination owners' next permitted boundary. It
never invokes arbitrary callbacks while Destruction commits, Physics steps, Render
extracts or the Audio callback runs.

Bindings declare:

- stable fact kind plus payload-schema revision and permitted source principals;
- optional material/surface/cause predicates over fixed typed fields;
- a finite ordered set of gameplay, VFX, Decal, Audio and accessibility destinations;
- each destination's request schema, required/cosmetic class and capability policy;
- mapping generation, finite fan-out, dedup scope and overload policy; and
- runtime, PIE, isolated editor-preview, headless and packaged-build eligibility.

Cook/activation rejects duplicate ownership, unknown destination IDs, incompatible
schemas, unbounded fan-out, unavailable required adapters and cosmetic policy attached
to a required gameplay result. Runtime mapping performs no asset search, reflection,
JSON parsing, string matching, filesystem access or consumer discovery.

For one occurrence, fan-out follows cooked destination order. The dispatcher copies
bounded values into destination-owned requests and records one typed result per layer.
An adapter may submit a normal domain command or operation; that work follows the
destination domain's own later commit rules and cannot reenter or rewrite the source
fact.

### 5. Occurrence and request identities prevent duplicate effects

Every destination request identity is derived from
`(DestructionEventOccurrenceId, binding generation, destination binding ID, layer
ordinal)`. A consumer deduplicates by this identity, not timestamp, asset, position,
destructible ID or approximate payload equality. The same fact can legitimately map to
several ordered effect layers; retries of one layer cannot create another instance.

Dispatcher and adapters keep bounded terminal/admitted result records for the live
source and binding generations. A duplicate returns `AlreadyDispatched` or
`AlreadyAdmitted` with the original result identity. A retry after the dedup window has
expired returns `OccurrenceExpired`; it does not become a new effect. Durable or remote
replay requiring a new presentation occurrence must be an explicit policy with a new
qualified request identity.

Hot-reloaded bindings apply only after an atomic safe-point swap. Facts already admitted
retain the captured binding generation; unconsumed retained facts use the source
generation policy explicitly selected for reload. They are never partly fanned through
old and new tables. Editor preview uses an isolated dispatcher namespace so scrubbed or
preview occurrences cannot collide with runtime occurrence IDs.

### 6. Each consumer retains its native ownership and failure policy

The application VFX adapter maps a fact to bounded `VfxSpawnRequest` values under
ADR-128. `VfxWorld` owns effect assets, queue/pool admission, CPU/GPU simulation,
quality/overload policy and Render extraction. DFR does not query particle visibility or
GPU completion. Gameplay-required logical output cannot depend on GPU particles or a
Null renderer.

The Decal adapter resolves cooked decal mapping and submits a bounded projection request
under ADR-127. The Decal/VFX owner validates target surface evidence, attachment policy,
lifetime and capacity, then owns render-resource retirement. A missing surface/material
or full cosmetic pool is a typed decal outcome. DFR never stores a decal handle or keeps
a chunk alive merely because a cosmetic decal references presentation data; attachment
uses an explicit generation-scoped owner fence.

The Audio adapter resolves semantic cue/media mapping and submits generation-tagged
intent to `AudioFrontend`. Audio owns readiness, sample-clock conversion, late policy,
voice/mixer/device state, callbacks and acknowledgements under ADR-062. Neither DFR nor
the dispatcher calculates sample positions or treats silent/virtualized playback as
semantic failure. Audio callbacks never invoke DFR or gameplay.

Gameplay adapters are narrow typed handlers registered by the application. They may
translate a committed fact into a later domain command, but cannot synchronously mutate
the source transition. Gameplay state that must be authoritative cannot depend solely
on a cosmetic request or Audio/VFX observation. Accessibility/caption intent fans out
from the same semantic fact independently, so mute, missing media/device or effect
suppression cannot erase required accessible output.

### 7. Required semantic delivery and optional cosmetics fail differently

Required gameplay/accessibility adapter availability and output capacity are validated
during session/scene activation and reserved for each transition before commit. If that
preflight cannot guarantee the declared bounded batch, transition preparation returns a
typed failure with no semantic publication. A post-commit violation of a successful
required reservation is a host/session fault: the fact remains committed and queryable,
and recovery resumes from its cursor. It is never silently dropped or used to roll back
the destruction revision.

Cosmetic destinations use separately budgeted fallible admission. Missing optional
binding/capability, stale target, queue or pool exhaustion, culled quality tier, device
loss or shutdown returns a typed destination result such as `CosmeticSuppressed`,
`ConsumerUnavailable` or `ConsumerCapacityExceeded`. The dispatcher continues other
independent destinations in cooked order. It does not substitute a default asset,
evict unrelated required work or retry every frame.

A required presentation policy, if a product declares one, must define its capability
and reservation at activation/transition preflight. It cannot turn an already committed
fact into an in-place rollback if device playback later fails. Headless profiles retain
canonical facts and required gameplay adapters while explicitly resolving VFX/Decal/
Audio destinations as unavailable or omitted according to cooked policy.

### 8. Prediction, restoration and replication cannot impersonate commit

Only authoritative committed DFR revisions publish `DestructionFact`. Client prediction
may issue separately typed `PredictedDestructionCue` presentation requests in an
isolated namespace with prediction key and reconciliation policy. A later authoritative
occurrence can confirm/suppress/replace that cosmetic cue through its consumer owner; it
does not reuse the predicted occurrence ID or claim the prediction was canonical.

Save restore installs canonical destruction state through the owning restore/activation
transaction. It does not automatically replay historical impact sounds, decals or VFX.
An authored load presentation is a new explicit occurrence class. Replication captures
canonical state/deltas through its typed Destruction adapter, not by serializing the
generic event bus or native consumer requests.

Dormancy/reactivation facts may map to presentation only when binding policy explicitly
permits it. Streaming unload and ordinary scene teardown emit lifecycle results, not
fake destruction impacts. Late join reconstructs state first; optional catch-up
presentation is bounded and explicitly classified, never inferred from every broken
chunk.

### 9. Replacement, cancellation and shutdown fence every generation

Replacing the destruction world, scene, binding table or destination adapter creates a
new generation beside the old one. New facts stop routing to a closing generation.
Already admitted destination requests retain copied values, source occurrence, binding
generation and explicit Scene/cell/module owner fences until they complete or cancel.
They never retain a raw DFR journal record or mutable world pointer.

Cancellation before ADR-146 aggregate commit produces no facts. Cancellation after
commit may suppress only destination work whose policy permits cancellation; it cannot
delete the committed occurrence. Consumer completion or cancellation observations are
typed results owned by the dispatcher/consumer, not new DFR facts unless an explicit
later gameplay command changes canonical state.

Shutdown closes source admission, commits no unpublished candidates, freezes a final
journal prefix, drains/cancels dispatch according to required/cosmetic policy, revokes
adapter registration, retires consumer requests at their owner boundaries and finally
releases journal/binding generations after cursors and readers drain. The Audio callback,
GPU completion and worker threads never run teardown callbacks into DFR. A deadline
reports incomplete shutdown and does not force-free referenced resources.

### 10. Results and observability preserve boundary provenance

Results distinguish `InvalidFactPayload`, `StaleSourceGeneration`,
`DestructionEventGap`, `BindingUnavailable`, `BindingSchemaMismatch`,
`DestinationUnauthorized`, `RequiredReservationDenied`, `AlreadyDispatched`,
`OccurrenceExpired`, `ConsumerUnavailable`, `ConsumerCapacityExceeded`,
`CosmeticSuppressed`, `AdapterReloaded`, `Cancelled` and `ShutdownInProgress`.
They retain occurrence, source revision, binding/destination generation and bounded
consumer provenance without flattening native failures into a generic boolean.

Metrics count committed facts by kind, retained journal records/bytes, cursor lag/gaps,
mapped layers, duplicates, required reservation failures and destination outcomes.
Dimensions use finite enums and product profiles, not destructible/chunk/asset IDs,
paths, payload strings or native handles. Traces may correlate occurrence and request IDs
under the observability privacy policy. Diagnostics cannot retry, mutate or select a
fallback consumer.

## Compatibility And Follow-Ups

This decision replaces any implied direct DFR-to-VFX/Decal/Audio call or generic-bus
gameplay delivery with a committed fact journal and application-owned dispatcher.
Existing presentation integrations must move asset/cue selection into cooked application
bindings and accept copied typed requests at owner boundaries. There is no compatibility
shim that publishes both direct and dispatcher paths because it would duplicate effects.

[DFR-004.2](https://horo-engine.atlassian.net/browse/HORO-1973) defines the exact
fact stream, binding schemas and adapter interfaces. DFR-001.2/DFR-001.3 freeze concrete
identity encodings and numeric limits. Focused follow-ups own consumer-specific
destruction mapping libraries, prediction reconciliation, persistence/network state
encoding and production diagnostics.

## Consequences

### Positive

- DFR publishes one authoritative fact regardless of which presentation systems exist.
- Gameplay and accessibility delivery do not rely on a lossy generic notification bus.
- VFX, Decal and Audio retain complete admission, resource, thread and native ownership.
- Stable occurrence/layer identities prevent duplicate effects across retries and reload.
- Headless, missing-device and cosmetic-overload behavior cannot change destruction.

### Costs

- The application must cook and activate explicit binding tables and destination
  adapters.
- Required delivery needs bounded journal/result storage and pre-commit reservation.
- Every consumer needs generation-fenced dedup and terminal-result retention.

### Rejected Alternatives

- **Publish destruction payloads only on `EngineDataBus`**: its notification delivery
  and backpressure cannot guarantee gameplay meaning or exact order.
- **Let DFR choose assets and call consumers directly**: couples semantic state to
  presentation policy, device lifecycle and native ownership.
- **Invoke handlers during destruction commit**: permits reentrancy and partial fan-out
  while the aggregate root is changing.
- **Deduplicate by timestamp, position or asset**: merges legitimate layered events and
  fails under retry, reload or quantization.
- **Roll back a fracture when a cosmetic consumer fails**: makes optional presentation
  an authority over committed gameplay state.
- **Replay all fracture events after restore or late join**: creates unbounded bursts and
  confuses reconstructed state with new occurrences.

## Verification

Required contract and integration coverage includes:

- no fact before aggregate commit and one canonically ordered batch after commit;
- stable occurrence IDs across retry and unique IDs across unload/replacement;
- journal capacity reservation, cursor lag/gap, snapshot recovery and lossy bus
  notification independence;
- cook/activation rejection for unknown schemas, unbounded fan-out, duplicate bindings,
  missing required adapters and invalid runtime/headless contexts;
- deterministic fan-out and per-layer dedup under duplicate delivery, adapter reload and
  delayed consumer schedules;
- Audio-only, VFX-only, Decal-only, every consumer, no presentation consumer and
  headless profiles with identical DFR state/revisions;
- queue/pool exhaustion, missing asset/media/surface, device loss, culling and shutdown
  as typed consumer outcomes without semantic rollback;
- required gameplay/accessibility reservation and recovery from the committed cursor;
- predicted cue confirmation/suppression, restore without historical replay and bounded
  late-join presentation;
- no native handles, mutable pointers, arbitrary maps or runtime strings in public facts
  and requests; and
- cancellation/replacement/shutdown with journal readers, adapter work, Audio callbacks,
  workers and GPU frames in flight.
