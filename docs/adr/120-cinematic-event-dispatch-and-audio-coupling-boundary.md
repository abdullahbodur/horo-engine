# ADR-120: Cinematic Event Dispatch and Audio Coupling Boundary

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: EventTrack binding, typed payloads, committed occurrence dispatch, gameplay adapter ownership, AudioTrack request coupling, missing binding/media behavior, lifecycle and diagnostics
- **Issue**: [CIN-004.1](https://github.com/abdullahbodur/horo-engine/issues/1701)
- **Jira**: [HORO-1660](https://horo-engine.atlassian.net/browse/HORO-1660)
- **Related**: [ADR-014](014-sequencer-ownership-clock-authority-and-binding-boundary.md), [ADR-017](017-prefab-role-ownership-and-capability-tiers.md), [ADR-062](062-audio-runtime-ownership-and-update-order.md), [ADR-064](064-audio-asset-and-cook-boundary.md), [ADR-068](068-music-transport-and-cross-system-ownership.md), [ADR-117](117-playback-ownership-frame-order-and-determinism.md)
- **Normative documents**: [Cinematic Sequencer Architecture](../architecture/runtime/cinematic-sequencer-architecture.md), [Audio Architecture](../architecture/runtime/audio-architecture.md), [Runtime Lifecycle](../architecture/runtime/runtime-lifecycle.md), [Engine Data Bus](../architecture/foundation/engine-data-bus.md)

## Context

ADR-014 gives Cinematic Runtime ownership of directed interval crossing and stable
event occurrences, while domain owners retain authority over the effects those
occurrences request. ADR-117 fixes player/order identity, commit behavior and
late-completion fencing. The cinematic architecture already states that event and
Audio tracks use typed adapters rather than direct state writes.

The dispatch contract remains incomplete. Event names could be treated as generic
`EngineDataBus` notifications, resolved into arbitrary callbacks during playback, or
compiled into narrow application-owned handlers. Those choices have different
ordering, failure, payload, lifetime and authority semantics. A timeline event that
requests quest progress or a prefab spawn cannot be silently dropped like an
ordinary notification, and it must not invoke gameplay reentrantly while Sequencer
is advancing its cursor.

Audio tracks also need a strict cross-family seam. Sequencer owns time and directed
traversal, but Audio owns media readiness, sample-clock conversion, callback command
publication and voice lifetime. Repeating those policies in the CIN family would
create a second Audio contract and let the documents drift.

This ADR selects one event-delivery path, defines payload and failure behavior, and
records the AUD-family decisions that remain authoritative for Audio coupling.

## Decision

### 1. One application dispatcher owns cinematic event delivery

The application composition root owns one `CinematicEventDispatcher` per runtime or
PIE session. It binds cooked EventTrack identities to narrow, injected
`ICinematicEventHandler` adapters supplied by authorized gameplay/application
modules. Cinematic Runtime produces occurrences; the dispatcher owns admission,
destination lookup, safe-point invocation, result publication and handler-lifetime
fencing. The gameplay adapter owns the domain effect.

```text
EventTrack interval crossing
    -> immutable CinematicEventOccurrence
    -> session CinematicEventDispatcher queue
    -> destination owner boundary
    -> registered typed gameplay/application adapter
    -> CinematicEventDispatchResult
```

This is the only gameplay EventTrack delivery path. Sequencer does not publish
gameplay occurrences to `EngineDataBus`, call game-module functions directly from
evaluation, use reflection by function name, or mutate scene/gameplay state. An
adapter may submit a normal domain command or operation, but the dispatcher does not
become that domain's state authority.

Editor preview owns an isolated preview dispatcher whose handlers are explicitly
preview-safe. It cannot invoke runtime gameplay adapters or commit authoritative
effects. Headless hosts use the same production dispatcher with registered headless
adapters; missing presentation capabilities do not define a second path.

### 2. Bindings and payloads are typed and cooked

Authoring may display a stable qualified event name, but cook resolves it through a
host/package event descriptor registry into an `EventBindingId` and immutable
`EventPayloadSchemaId`/version. A descriptor declares:

- stable module-qualified event identity and compatibility version;
- destination domain and owner boundary;
- fixed, bounded payload schema and maximum encoded size;
- permitted runtime, PIE, preview, headless and packaged-build contexts;
- required capability/authority and handler result schema; and
- whether the effect is required or optional for player activation.

Payloads are schema-validated during authoring/cook and stored as bounded immutable
cooked values or handles. Runtime occurrences carry binding/schema identity,
payload reference, session/scene/player/track/keyframe generations, traversal/loop/
direction identity, source clock evidence and committed tick/boundary identity.
They do not contain JSON/maps, script function names, raw object pointers, native
handles or unbounded strings.

The registry detects duplicate names, ID collisions, incompatible schema versions
and forbidden build contexts before descriptors activate. A module may retain a
stable binding identity while changing its C++ implementation; changing payload
meaning requires a versioned schema/migration rather than reinterpretation.

### 3. Occurrences dispatch only after their source commits

EventTrack evaluation stages the complete directed interval under ADR-014/117.
Authoritative gameplay occurrences become eligible only after the originating fixed
tick commits. Failed attempted ticks, presentation interpolation, editor scrubbing
and default seek never dispatch gameplay events.

The dispatcher drains a bounded immutable batch at the destination owner's next
permitted boundary. Within a destination it preserves Sequencer's canonical order:
directed crossing time, player order, track ID and keyframe ID. Handlers cannot
reenter the active Sequencer evaluation or change the already-committed occurrence
batch. Commands produced by a handler follow that domain's ordinary later commit
rules; same-tick effect is not implied.

`CinematicEventOccurrenceId` is the idempotency identity. It includes the player
generation plus traversal/loop/direction, track and key identity. The dispatcher
records either a bounded terminal result or a retryable-attempt record per live
generation. `Accepted`, `OperationStarted` and non-retryable failures are terminal;
a policy-admitted retryable failure records attempt count/backoff but leaves the
same occurrence eligible. A retried queue submission cannot invoke a handler after
a terminal result. It does not deduplicate by event name, payload or timestamp.

Queue capacity is atomically reserved for the exact occurrence batch before the
player cursor/value state commits. The reservation is generation-bound and is
consumed by commit or released on rollback/cancellation; concurrent players cannot
spend it between preflight and enqueue. Required occurrences are never silently
dropped. Reservation exhaustion returns
`EventDispatchCapacityExceeded` and holds/fails the player according to its compiled
policy without partial interval publication. Optional presentation-only signals may
use a separately declared coalescing policy, but they are not gameplay EventTrack
occurrences.

### 4. EngineDataBus remains a notification plane

`EngineDataBus` may publish a small coalesced revision/availability notification
after the dispatcher records results, so diagnostics or editor surfaces can re-query
their owning store. That notification is not the event occurrence, does not carry the
gameplay payload, does not select/invoke the handler and cannot establish timeline
ordering or success.

Generic bus dead-event and backpressure policies are therefore irrelevant to
gameplay EventTrack correctness. No subscriber appearing or disappearing changes
whether a required cinematic event is considered delivered. EventTrack uses the
session dispatcher and explicit adapter lifetime tokens.

### 5. Handler outcomes and lifecycle are explicit

A handler returns one bounded typed outcome such as:

| Outcome | Meaning |
|---|---|
| `Accepted` | Destination owner admitted the command; state mutation occurs at the owner's next commit phase |
| `OperationStarted` | Destination returned a stable application-owned `OperationId`; completion is asynchronous |
| `SuppressedByAuthority` | Current gameplay/network/cinematic authority rejects the requested effect |
| `CapabilityUnavailable` | Required destination capability is absent in this context/build |
| `InvalidTarget` | Generation-checked scene/gameplay target cannot be resolved |
| `Backpressured` | Destination's bounded queue cannot admit the request |
| `HandlerFailed` | Adapter contained an exception or returned a domain failure |

`Accepted` means accepted by the receiving owner, not that an asynchronous effect
completed. Long-running operations use the application `OperationStore` under
ADR-010; the dispatcher does not create a competing job/result store or block the
simulation thread.

Player stop/cancel closes new occurrence admission, cancels only operations whose
domain contract supports cancellation and fences all later results by session/scene/
player/handler generation. Effects already committed are not generally reversible.
Module unload first revokes handler admission, drains or cancels owned work, and
retires the adapter after in-flight calls finish. It cannot leave a callable raw
function pointer in a compiled sequence.

Default required-event failure stops future effects from that track and reports the
typed result; it does not roll back a tick whose other owner effects already
committed. Retry is allowed only when the descriptor declares bounded idempotent
retry, the outcome is classified retryable, and the same occurrence ID and reserved
semantic payload are reused. Optional-event failure disables or skips
according to its explicit compiled policy and remains observable.

### 6. Unknown events fail before playback whenever possible

An authored event name absent from the target registry is
`UnknownCinematicEventName` and fails cook with asset path, track/key location and
requested qualified name. A schema mismatch is `CinematicEventSchemaMismatch` and
also fails cook. Cook never hashes an unknown string into a binding that can appear
valid at runtime.

At activation, a cooked binding whose module/handler/capability is unavailable or
whose registry generation is incompatible returns `CinematicEventBindingUnavailable`.
A required binding prevents player activation and unwinds acquired resources; an
optional binding disables only its declared track with a deduplicated diagnostic.

After activation, unload/reload, scene travel or authority change invalidates the
handler generation. Pending occurrences return `StaleCinematicEventBinding` or the
specific authority result. The dispatcher never searches again by string, calls a
different same-named handler or reports success because the generic bus had a
subscriber.

### 7. AudioTrack submits intent only through AudioFrontend

AudioTrack evaluation emits a bounded generation-tagged schedule/cancel/seek/preroll
bundle through the application Audio/Cinematic adapter into `AudioFrontend`.
Sequencer owns sequence time, interval traversal, loop/direction identity, scrub/seek
intent and stable ordering. Audio owns media leases, source-to-sample correlation,
sample targets, queue admission, voice/transport state, callback publication and
actual playback observations.

The bundle contains stable session/player/track/key/occurrence identities, resolved
Audio asset/cue identity and revision, `SequenceTime` plus source clock generation,
intent, late/pause/seek policy and cancellation token. It contains no device sample
index, callback buffer, voice handle, decoder pointer, middleware ID or backend type.
Render-frame interpolation cannot emit an Audio command.

The cross-family ownership dependencies are normative and are not re-decided here:

| AUD decision | Authority consumed by this CIN boundary |
|---|---|
| [AUD-001.1 #525](https://github.com/abdullahbodur/horo-engine/issues/525) / ADR-062 | Audio runtime/control/callback ownership, command publication phases, generation and teardown order |
| [AUD-002.1 #537](https://github.com/abdullahbodur/horo-engine/issues/537) / ADR-064 | Audio-domain cook metadata, runtime media payload/readiness and asset lease boundary |
| [AUD-008.1 #607](https://github.com/abdullahbodur/horo-engine/issues/607) / ADR-068 | Sequence-to-sample correlation, schedule horizon, music transport, seek/scrub/preroll, late policy and acknowledgements |

Future AUD-family decisions may refine Audio-internal formats, mixers, providers or
middleware. CIN consumes their published Horo-owned AudioFrontend contract; it does
not duplicate those outcomes or branch on the selected backend.

### 8. Missing Audio media is typed and policy-driven

Cooked AudioTrack keys reference stable `AssetId`/cue identity and expected cooked
revision. An unknown source reference, invalid Audio payload or incompatible target
profile fails cook under AUD-002.1 as `AudioTrackAssetInvalid`. Cook does not preserve
an unresolved path for runtime lookup.

Activation requests Audio preparation and retains the admitted media lease for the
track's required horizon. Outcomes are explicit:

| Condition | Outcome and policy |
|---|---|
| Required media absent, unpublished, corrupt or incompatible | `AudioTrackAssetUnavailable`; player activation fails and unwinds |
| Optional media absent under an authored silent/skip policy | Track is disabled/skipped, diagnostic emitted, non-Audio tracks may continue |
| Media not yet resident but load is permitted | `AudioTrackPreparing` with bounded operation/deadline; playback does not start until acknowledged |
| Preparation deadline or queue horizon missed | AUD-008.1 late policy yields typed reject/next-boundary/immediate-with-ramp behavior |
| Asset revision replaced or lease invalidated during playback | Old leased generation finishes if policy permits, otherwise typed cancel/reprepare; never a stale pointer |
| Audio device/backend unavailable | Audio runtime's typed suspended/unavailable result; an authored no-Audio product policy decides whether the sequence continues |

Missing media never triggers synchronous I/O on the evaluation or callback thread,
direct decoder construction, filename fallback, an arbitrary replacement clip or an
implicit successful silent voice. A deliberately silent cinematic outcome must be an
authored optional/no-Audio policy and remains visible in diagnostics.

### 9. Audio acknowledgements are observations, not gameplay events

Audio publishes admitted, actual-start, stop, marker, rejection and completion
observations through its bounded control-side channel using the original request/
occurrence identity. The application adapter may translate a declared observation
into a later gameplay/domain command through the same typed event dispatcher, but the
Audio callback never invokes gameplay and `EngineDataBus` delivery never becomes an
exact sample-boundary action.

Captions, dialogue semantics and narrative progression remain their own domain
authorities under AUD-008.1. Muting, virtualization, missing hardware or Audio failure
cannot silently erase a required semantic event; products fan out semantic intent to
Audio and non-Audio destinations independently.

### 10. Diagnostics preserve the failed boundary

Diagnostics include stable session/scene/player/track/key/occurrence or Audio request
identity, binding/schema/asset revision, source clock/tick, destination domain,
outcome and bounded causal context. They exclude raw payload secrets, unbounded asset
paths, native handles and backend/vendor strings from metric dimensions.

Unknown name/schema is reported at authoring/cook. Missing handler/capability appears
at activation. Queue/authority/target/handler outcomes appear at destination drain.
Audio preparation/scheduling/callback observations retain their Audio provenance.
The dispatcher does not collapse these into one `EventFailed` boolean.

### 11. Qualification proves ordering, isolation and failure behavior

Required implementation evidence includes:

- cook-time known/unknown names, duplicate/colliding identities, compatible/
  incompatible payload versions, size limits and packaged-context capability checks;
- fixed-tick commit/failure, forward/reverse/loop/seek semantics and no dispatch from
  presentation sampling or editor scrub;
- stable ordering and exactly-once occurrence identity under randomized player,
  allocation, queue-arrival and worker-completion order;
- required/optional handler absence, authority suppression, invalid target,
  backpressure, contained handler failure, bounded idempotent retry and async
  operation completion;
- runtime/PIE/editor-preview/headless dispatcher isolation, module reload/unload,
  scene travel, stop/cancel and late generation-fenced results;
- proof that generic `EngineDataBus` subscriber/dead-event/backpressure changes do not
  affect gameplay EventTrack delivery or ordering;
- Audio preparation and schedule admission for required/optional missing, corrupt,
  unloaded, replaced and stale media plus device/backend loss;
- continuous playback, pause, seek, scrub, loop, preroll, missed horizon and external
  discontinuity through AUD-008.1 acknowledgements; and
- native, middleware and Null/recording Audio adapters receiving the same Horo-owned
  bundles without Sequencer computing sample indices or storing voice/native handles.

## Consequences

### Positive

- Gameplay events have one ordered, typed, observable path instead of generic bus
  delivery or reentrant arbitrary callbacks.
- Payload and handler incompatibility fail at cook/activation wherever possible.
- Audio keeps sole ownership of readiness, sample scheduling and playback lifetime.
- Unknown events and missing media have stable outcomes rather than silent loss.

### Costs

- Application composition must register versioned event descriptors and narrow
  handler adapters for each supported runtime context.
- The dispatcher needs bounded occurrence/result storage, safe-point draining and
  module-lifetime fencing.
- Audio tracks require preparation/lease planning and explicit optional/no-Audio
  policy instead of best-effort playback.

## Rejected Alternatives

### Publish gameplay EventTrack payloads on EngineDataBus

Rejected because the bus is a notification plane with subscriber-dependent delivery
and configurable backpressure. It cannot own required effect admission, directed
timeline ordering, exactly-once occurrence results or domain authority.

### Call gameplay callbacks directly while Sequencer evaluates

Rejected because arbitrary reentrancy can mutate the scene/player mid-interval,
bypass owner safe points and leave no bounded admission/result/lifetime contract.
Only the session dispatcher invokes registered adapters at destination boundaries.

### Resolve event function names and payload maps at runtime

Rejected because string/reflection lookup and dynamic maps are unbounded, difficult
to version and can bind differently after module reload. Cook produces stable typed
binding and schema identities.

### Let Sequencer start voices or calculate device sample positions

Rejected because sequence time is not the Audio callback clock. AUD-001.1 and
AUD-008.1 keep command publication and sample conversion in Audio.

### Treat missing Audio as successful silence

Rejected because required content loss would be invisible and narrative timing could
diverge. Silence/skip is permitted only as an explicit optional product policy with a
typed result and diagnostic.

### Duplicate Audio scheduling rules in the CIN family

Rejected because Audio owns runtime phases, media payloads and transport semantics.
This decision names AUD-family dependencies and constrains only the Cinematic-facing
request boundary.
