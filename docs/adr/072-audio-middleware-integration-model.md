# ADR-072: Audio Middleware Integration Model

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: Event-bridge and backend-replacement models, normalized frontend frames, stable IDs, device/mixer ownership, native coexistence, voice budgets, capabilities, bank activation, profiling, package lifecycle, and distribution
- **Issue**: [AUD-016.1](https://github.com/abdullahbodur/horo-engine/issues/682)
- **Jira**: [HORO-682](https://horo-engine.atlassian.net/browse/HORO-682)
- **Parent**: [AUD-016](https://github.com/abdullahbodur/horo-engine/issues/681)
- **Related**: [ADR-054](054-extension-and-package-authority-boundary.md), [ADR-062](062-audio-runtime-ownership-and-update-order.md), [ADR-063](063-audio-sample-format-and-channel-layout.md), [ADR-064](064-audio-asset-and-cook-boundary.md), [ADR-065](065-mixer-topology-and-constrained-dag.md), [ADR-067](067-platform-audio-backend-strategy.md), [ADR-069](069-audio-extension-capability-and-abi.md)
- **Normative documents**: [Audio Architecture](../architecture/runtime/audio-architecture.md), [Horo Package System](../architecture/packages/package-system.md), [Release and Distribution](../architecture/release/release.md), [Observability Metrics and Profiling](../architecture/observability/observability-performance.md), [Extension System](../architecture/extensions/plugin-system.md)

## Context

Wwise/FMOD-style integrations can either translate Horo events into a vendor
runtime while Horo keeps its native mixer, or replace Horo's final mixer and
device path. Those shapes have different device, clock, voice, routing, failure,
bank, profiling, and distribution ownership. Treating them as one generic plugin
callback would permit two final-output owners, silently bypass Horo voice limits,
or make vendor strings and handles part of gameplay contracts.

Horo also needs native clips, generators, middleware events, editor preview,
package lifecycle, and observability to coexist predictably. The integration model
must therefore be selected before runtime construction, consume normalized Horo
intent, preflight exact capabilities and content, and remain transactional across
startup, bank reload, device loss, package update, and shutdown.

## Decision

### 1. Exactly two integration models are supported

One `AudioMiddlewareModel` selects one model for an `AudioRuntime`:

```cpp
enum class AudioMiddlewareModel : std::uint8_t {
    EventBridge,
    BackendReplacement,
};
```

The selected model and exact adapter contribution are resolved by the application
composition root before runtime, mixer, or device construction. Model identity is
fixed for that runtime generation. Switching model or adapter requires ordered
AudioRuntime teardown and replacement; a callback, package, bank, scene, editor,
or device-loss handler cannot switch it.

One runtime has exactly one final-output owner. Event bridge keeps Horo as final
mixer/device owner. Backend replacement makes the middleware adapter the final
mixer/device owner. Two independent output devices, OS-mixed Horo/vendor streams,
two final Master buses, or event bridge plus backend replacement in one runtime
are not admitted. The baseline selects at most one middleware integration; multiple
simultaneous vendor runtimes require a later decision.

### 2. Horo frontend values are the public boundary

Scene, gameplay, cinematic, animation, editor preview, and application services
submit Horo-owned values through `AudioFrontend`. A revisioned immutable
`AudioFrontendFrame` contains bounded typed operations such as source/event create,
start/stop, transforms/listeners, parameters, routing intent, timing, owner tokens,
and cancellation. It names scene/runtime/content generations and stable request or
occurrence IDs.

Ordinary gameplay APIs expose only Horo types:

- `StableEventId`, `StableParameterId`, `AudioEventInstanceId` and `AudioRequestId`;
- typed `AudioParameterValue`, timing, fade, spatial, priority and result values;
- Horo asset, bus, emitter/listener and capability IDs;
- typed acceptance, completion, virtualization, failure and cancellation outcomes.

They never expose vendor event/RTPC names, GUID structs, numeric hashes, bank
indexes, playing IDs, object handles, callbacks, result enums, allocator objects,
native device handles or SDK headers. Adapter entry points are internal and may
translate to private vendor values only after Horo validation.

### 3. Authoring names compile to stable Horo bindings

Authoring adapters may display or import vendor event, parameter, switch, state,
bus and bank names. The integration's AudioCook contribution converts those values
into a `CookedMiddlewareBindingManifest` keyed by stable Horo IDs. Each binding
records the exact adapter/schema version, vendor project/bank revision, private
vendor identity bytes, typed parameter schema, event capabilities, voice cost,
dependencies and compatibility fingerprint.

Gameplay and scene data store the stable Horo ID. The private vendor string/GUID/
hash remains in the integration authoring asset and cooked binding manifest. It is
not copied into `AudioSourceComponent`, frontend frames, save data, telemetry
dimensions, or ordinary diagnostics. Rename/reimport produces an explicit binding
migration or a missing-binding error; runtime never guesses by display string.

Registries freeze at cook/activation. String-to-ID registration or vendor lookup
does not occur in the callback or ordinary gameplay path. Duplicate stable IDs,
ambiguous vendor bindings, schema mismatch, missing bank content, or an unknown
parameter type rejects the candidate before publication.

### 4. Event bridge keeps Horo's final mixer and device

Event bridge admits `AudioSourceKind::MiddlewareEvent` alongside Horo clips,
streams, generators, and DSP. The Horo runtime owns the final ADR-065 mixer, Master
bus, ADR-067 output backend/device, sample epoch, source/listener extraction,
global voice policy, command admission, and lifecycle state.

Before posting an event, Audio control reserves a generation-checked proxy instance
and the event's declared voice-cost tokens. It passes stable event/parameter IDs,
emitter/listener snapshots, bus/stem destination, timing and correlation IDs to
the adapter. Private vendor playing IDs remain behind the adapter and are correlated
to `AudioEventInstanceId` for stop, parameter and observation operations.

The vendor engine renders one or more declared prevalidated stems into Horo-owned
ADR-063-compatible source blocks or a bounded adapter ring. Those stems enter
ordinary Horo buses with explicit gain/layout/latency/tail policy. The adapter does
not open a second final output device, publish a hidden Master route, call a native
backend directly, or rely on the OS to mix independent outputs.

An adapter may be driven from the Horo callback only if its exact integration ABI
declares bounded allocation-free render entry points. Otherwise its own qualified
real-time worker fills preallocated bounded rings; the Horo callback consumes only
complete generation-tagged blocks and applies the declared underrun/discontinuity
policy. Neither path may block the Horo callback or call gameplay/editor code.

Before the first render and after any Horo device reconfiguration, Audio control
passes the event bridge an immutable output-epoch configuration containing the
sample/device epoch, effective sample rate and layout, maximum block frames,
adapter-ring/latency contract, and transition deadline. The bridge prepares a
private candidate off-callback and acknowledges readiness; Audio publishes the new
device and bridge epochs together at the callback boundary. Failure keeps the prior
epoch active when recoverable or fails the parent device transaction. `PostEvent`
cannot run against an unacknowledged epoch, and neither side may silently resample
or retain the prior block contract after publication.

### 5. Backend replacement owns the final middleware path

Backend replacement is an equal internal backend peer at the ADR-067 composition
boundary. The selected middleware adapter owns vendor initialization, native device
and callback, final mixer/voices, vendor buses, output negotiation, device events,
and SDK shutdown. Horo control still owns the `AudioRuntime` state machine,
normalized frontend admission, scene-context barriers, product capability policy,
stable results/diagnostics, and ordered teardown.

The adapter consumes validated `AudioFrontendFrame` values and publishes bounded
Horo observations. It cannot ask gameplay for vendor objects or require gameplay
to call a vendor API. Vendor callbacks emit only adapter-owned bounded facts; Horo
control reconciles them into the ADR-062 state and completion model.

Backend replacement does not automatically preserve Horo-native clips, generators,
buses, effects, spatial providers, or sample-accurate semantics. The adapter must
either map each required Horo feature to a declared equivalent or expose an exact
`HoroSubmixInput` capability. With that capability, a prepared Horo mixer renders
one or more bounded stems into the vendor final mixer while the vendor remains the
only device/Master owner. Without it, a project using native sources/buses fails
preflight rather than silently dropping or separately playing them.

### 6. Coexistence is a preflighted product profile

The effective integration profile declares which source kinds, routing semantics,
layouts, spatial modes, timing precision, parameters, buses/stems, effects,
virtualization, tails, capture/monitoring, offline rendering and profiler signals
are required. Adapter-reported support is intersected with project policy, target,
SDK/runtime, cooked content and device facts into one immutable effective profile.

Required absence is activation failure. Optional fallback is named, bounded and
observable. Registration order, first successful probe, bank order, vendor default,
or a similarly named capability never selects a behavior. Event bridge cannot
silently become replacement; replacement cannot open the Horo native backend as a
fallback; neither can fall back to Null or another adapter after activation.

Event bridge is the baseline coexistence model for Horo-native and vendor events.
Replacement coexistence is supported only through the effective feature mapping or
`HoroSubmixInput`. A project that requires incompatible Horo and vendor semantics
is rejected before the first audible frame.

### 7. Voice and resource budgets remain globally enforceable

Each cooked event binding declares a conservative bounded cost: event-instance
slots, maximum physical/vendor voices, virtual voices, object/listener slots,
parameter updates, stem channels, callback work, state/scratch, bank residency,
latency and tail. Dynamic/unbounded vendor spawning is not admitted. A vendor event
whose worst case is unknown reserves an explicit product maximum or fails cook/
activation.

Event bridge reserves Horo proxy/cost tokens before the adapter may post. The
adapter cannot create more admitted vendor work than its reservation and reports
actual/virtualized counts against the same instance generation. Rejection,
replacement and virtualization follow deterministic Horo policy, with adapter
capability proving how stop, tail and virtual advancement preserve semantics.

Backend replacement owns physical allocation but must consume an immutable Horo
budget snapshot, apply the selected policy, and report every admission/steal/
virtualization decision through stable Horo observations. Hidden vendor voices,
unbounded event fan-out, or stats-only enforcement does not satisfy the contract.
Budget exhaustion is a typed result, never silent drop or ordinary log parsing.

### 8. Capabilities and activation are exact and transactional

ADR-069 gains two closed versioned Audio capability families:
`audio.middleware.event_bridge` and `audio.middleware.backend`. A contribution
declares exact model, adapter/SDK/runtime/ABI versions, supported frontend and
binding schemas, platform/architecture, device role, layouts/rates/blocks, clocks/
scheduling, source kinds, buses/stems, spatial/parameter/voice semantics, profiler
signals, bank formats, limits, permissions, license/redistribution facts and
restart/live-reload policy.

EXT/PKG remains discovery, signature, trust, dependency, module and lease authority.
Audio validates the Audio-specific candidate. Project configuration cannot grant
native trust, reinterpret a generic callback as middleware, or lower permissions.
Descriptor validation is inert and does not initialize an SDK, open a device, load
a bank, start a thread or register ambient services.

Activation prepares the adapter, complete binding manifest, all required bank
generations, memory/voice reservations, effective capability profile, observation
queues and device/render candidate privately. Publication is one transaction. Any
missing/mismatched bank, SDK, mapping, target, license gate, budget, capability,
trust, permission or callback readiness failure unwinds the candidate and preserves
the previous runtime/last-good generation. Partial event sets do not activate.

### 9. Banks are package-bound cooked content

Vendor projects and source banks enter through the integration's AudioCook/package
contribution under ADR-064. A cooked bank record includes stable `MiddlewareBankId`,
content digest, platform/architecture, adapter/SDK/bank-format version, dependency
set, locale/variant role, binding-manifest digest, memory budget, license class and
integrity metadata. Runtime code does not scan vendor folders or load arbitrary
paths.

AST/PKG owns stable package/asset identity, verified files, cache, staging,
publication, rollback and install records. The adapter owns vendor bank parsing,
compatibility and private handles. Bank load prepares a complete generation; a
new bank or mapping never mutates the active generation in place.

Locale/content replacement, package update, rollback and hot reload publish a
complete compatible bank/binding/adapter generation. Existing instances either
finish on their pinned generation, migrate through an explicitly compatible
off-callback contract, or stop by product policy. Old banks and module code remain
leased until instances, tails, callbacks, queues, captures and profiler views
retire.

### 10. Profiling is normalized, bounded and correlated

Every adapter publishes one bounded `AudioMiddlewareStatsSnapshot` with adapter,
model, runtime/device/content generations and provenance. The common schema covers
at least admitted/active/physical/virtual voices, event instances, command queue
depth and lag, render/callback load, underruns/overruns, bank resident bytes,
stream/decode pressure, dropped observations, faults and last sample timestamp when
available. Unsupported and unknown remain distinct from zero.

Metrics use bounded dimensions such as selected adapter/model/generation; raw
event/parameter strings, playing IDs, bank paths and per-instance IDs are not
metric dimensions. Detailed per-event timelines require an explicit bounded
profiler capture correlated by Horo stable IDs. Vendor-specific counters and
authoring profiler sessions may appear in a vendor integration panel, labeled with
their provenance, but do not become gameplay APIs or rewrite common metrics.

Callback/vendor-real-time paths update only preallocated counters or bounded event
records. Control paths snapshot and format them. Diagnostics redact vendor handles,
memory addresses, proprietary state, license keys, project paths and user content.

### 11. Threading and failure ownership stay explicit

Horo producer threads never call vendor APIs. Audio control validates frames and
submits them through the integration's declared owner thread/queue. Borrowed frame,
bank, string and asset views cannot be retained. The adapter copies or translates
into its preallocated/private ownership before the invocation ends.

Vendor callbacks cannot commit Horo runtime/device state, allocate through Horo
general heaps, log, call UI/ECS/gameplay, publish packages, select fallback, or free
Horo-visible generations. They emit bounded facts. Adapter-owned threads, vendor
allocators, callback registration, bank objects, output objects and SDK handles are
private and released on their documented affinity after quiescence.

Device loss in event bridge is handled by Horo's selected ADR-067 backend; the
adapter follows the resulting pause/reset/sample-epoch transaction. Device loss in
replacement is reported by the adapter, but Horo control still commits ADR-062
states and product recovery policy. A vendor SDK must not restart a device behind
Horo's generation model or silently select a new endpoint.

### 12. Disable, update and shutdown are lease gated

Disable, update, trust revocation, bank replacement and shutdown first close new
admission. Event bridge stops new posts, publishes/removes stems, drains/cancels
events and tails, quiesces adapter render work, releases Horo proxy tokens, then
retires banks and module leases. Backend replacement stops frontend submission,
quiesces vendor callbacks/device, reconciles terminal observations, drains/cancels
voices/tails, destroys vendor objects, then releases banks/modules.

No adapter or bank unloads until every callback epoch, event/voice, command,
observation, stem/ring, Horo submix, capture/profiler view and module/content lease
reaches zero. Timeout, unknown tail, lost device acknowledgement, stuck vendor work
or surviving handle leaves the integration loaded and reports pending restart/
failure. Force-unload is forbidden; shutdown is idempotent after partial startup.

Retirement has a bounded control-path deadline and publishes a normalized
`AudioMiddlewareRetirementBlockers` snapshot containing the adapter/generation,
retirement stage, deadline outcome, counts by lease class, oldest owning generation,
and last bounded progress timestamp. It never exposes vendor handles. On timeout,
the editor stops waiting, marks the integration `PendingRestart`, retains the old
code/content generation, and rejects additional live activation for that adapter/
package so repeated reloads cannot accumulate pinned generations. Final process
shutdown follows ADR-062's bounded fatal-retention policy if vendor callback work
cannot quiesce; it never frees or unloads executable state that may still run.

### 13. Release distribution is a hard gate

The package and release profile names exact integration packages, native runtime
libraries, cooked banks, platform/architecture variants, version constraints,
license/notice files, redistribution class, signing requirements and optional
vendor prerequisites. A local SDK installation or successful editor run is not
permission to redistribute it.

Release validation fails before signing/publication when required binaries/banks/
notices are missing, hashes or variants mismatch, redistribution is prohibited or
unknown, a runtime dependency is undeclared, or the selected integration/profile
was not qualified for the target. Horo records evidence and required notices; it
does not procure vendor licenses or embed secrets/license keys in project metadata,
logs, diagnostics or release history.

### 14. Migration and verification

Audio Architecture replaces its informal middleware sketch with this ownership
model. Package, Release and Observability documents project bank/package,
distribution and normalized-stat rules. AUD-016.2 and later children define exact
event/backend ABIs, bank tooling, editor surfaces, reference adapters and
qualification without adding a third model or vendor-facing gameplay API.

Required contract coverage includes:

- event-bridge and backend-replacement selection before construction, fixed model
  identity, and rejection of dual devices/Masters or simultaneous models;
- compile-time proof that ordinary gameplay/public Horo headers contain no vendor
  SDK type, handle, enum, callback, string-name contract or include dependency;
- stable event/parameter binding across rename/serialization, duplicate/missing/
  ambiguous binding rejection and no runtime string lookup;
- complete normalized frontend ordering, stale generation/revision, duplicate
  request, timing/late/cancel and bounded queue behavior;
- event proxy reservation, worst-case fan-out, global voice pressure, vendor
  rejection/steal/virtualization/tail reconciliation and hidden-voice detection;
- event-bridge stems through Horo mixer/device with no second endpoint, layout/
  latency/ring underrun and callback-bound render fixtures, including initial and
  reconfigured output-epoch preparation/acknowledgement before event admission;
- replacement device lifecycle, required-feature preflight, native-source failure
  without `HoroSubmixInput`, and qualified coexistence when it is present;
- capability/profile intersection independent of registration/probe/bank order,
  exact required-feature failure and observable optional fallback;
- complete bank/binding/adapter activation, version/target/digest/dependency/budget/
  trust/license failure rollback, last-good retention and generation reload;
- normalized metrics with unknown/unsupported semantics, bounded dimensions,
  correlation, vendor provenance, callback-safe counters and redaction;
- event-bridge native device loss, replacement device loss, stale callbacks,
  restart attempts, partial initialization, repeated shutdown and no silent
  native/Null/other-adapter fallback;
- package disable/update/trust revocation with active events/tails/profiler capture,
  lease-gated retirement, bounded blocker diagnostics, one-retained-generation
  limit, pending-restart behavior and fatal process-shutdown retention;
- release matrix, clean-machine package restore, native library/bank inclusion,
  notices, prohibited/unknown redistribution, signing and reference qualification.

## Consequences

Middleware can integrate deeply without making gameplay vendor-specific. Event
bridge provides explicit native/vendor coexistence under Horo's final mixer and
device; backend replacement provides a full vendor output path while retaining
Horo frontend, lifecycle, capability, diagnostics and package contracts.

The cost is strict preflight, cooked ID mappings, conservative voice/resource
costs, transactional banks, normalized observations, single-endpoint composition,
and target-specific license/qualification evidence. These costs prevent partial
activation and hidden ownership conflicts.

## Rejected Alternatives

### Expose vendor event names and handles to gameplay

Rejected because content renames, SDK changes and replacement adapters would make
gameplay vendor-dependent. Cooked mappings keep private identities at the adapter.

### Let Horo and middleware open independent output devices

Rejected because clocks, focus, latency, volume, suspend, device loss and final
mix ownership would diverge, and global budgets/routing could not be enforced.

### Treat event bridge and backend replacement as runtime-toggleable modes

Rejected because device, mixer, callback, banks, capabilities and voice state have
different owners. Switching requires complete runtime teardown and reconstruction.

### Let backend replacement claim implicit feature parity

Rejected because vendor mixers do not automatically preserve Horo buses, DSP,
spatial, timing or native-source semantics. Required capabilities are preflighted.

### Count vendor voices only after they are created

Rejected because dynamic fan-out could exceed callback and memory budgets before
Horo reacts. Conservative cost tokens are reserved before admission.

### Load whichever banks are available

Rejected because partial event sets and mismatched mappings produce silent content
loss. Adapter, bindings and all required banks activate as one generation.

### Parse vendor profiler logs into common metrics

Rejected because log text is unstable, unbounded and ambiguous. Adapters publish
typed bounded snapshots with explicit provenance.
