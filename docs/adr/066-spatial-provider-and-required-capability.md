# ADR-066: Spatial Provider and Required Capability

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: Spatial provider identity, capability requirements, profile resolution, activation preflight, optional fallback, runtime failure, and observability
- **Issue**: [AUD-005.1](https://github.com/abdullahbodur/horo-engine/issues/577)
- **Jira**: [HORO-577](https://horo-engine.atlassian.net/browse/HORO-577)
- **Parent**: [AUD-005](https://github.com/abdullahbodur/horo-engine/issues/576)
- **Related**: [ADR-062](062-audio-runtime-ownership-and-update-order.md), [ADR-063](063-audio-sample-format-and-channel-layout.md), [ADR-065](065-mixer-topology-and-constrained-dag.md)
- **Normative document**: [Audio Architecture](../architecture/runtime/audio-architecture.md)

## Context

Audio Architecture provides a deterministic stereo panner and allows packages to
register HRTF, Ambisonic, platform-native, or middleware spatializers. It currently
says that an unavailable provider falls back to the core stereo panner. That is
safe only when spatial enhancement is optional. A project authored and qualified
for required binaural, Ambisonic, object-audio, or provider-specific behavior must
not launch with a materially different mix merely because installation, device,
backend, license, capacity, or initialization requirements are missing.

Installation and registration also do not prove that one provider can process the
active output profile. Provider selection must occur before callback-visible state
is published and must account for Horo ABI, real-time guarantees, channel/layout
support, device/backend capabilities, content requirements, limits, and package
lifetime. Runtime failure and device replacement need the same required-versus-
optional distinction without letting the callback choose policy or log failures.

## Decision

### 1. Core stereo is an explicit baseline provider

Horo core contains one always-linked backend-neutral provider with a versioned
stable identity such as `horo.spatial.core_stereo.v1`. It provides deterministic
equal-power stereo panning over ADR-063 planar blocks, consumes prevalidated
source/listener parameters, and requires no platform-native spatial API. Distance
attenuation and optional Doppler inputs are prepared by Audio control/frontend
policy and enter it as typed values. Two-dimensional voices bypass spatial
positioning and route normally.

The core provider is a real selection result, not an unnamed emergency branch.
Its declared capabilities do not include HRTF, Ambisonic rendering, object output,
height/speaker rendering beyond its explicit layout conversion, room propagation,
occlusion queries, or a middleware-specific feature. It cannot be reported as
satisfying those capabilities simply because it can produce audible stereo.

The Null audio composition uses a deterministic validation provider and clock. It
can prove command, graph, layout, and lifecycle contracts, but it does not prove
hardware or native spatial capability. A test may install a simulated provider;
Null mode never converts an unproved production requirement into satisfied state.

### 2. Capabilities and providers have typed stable identity

Every provider descriptor carries a stable `SpatialProviderId`, semantic version,
Audio/provider ABI, implementation owner/package identity, supported operation
and output-layout capabilities, limits, required backend/device features, prepared
state/scratch/latency/tail declarations, and real-time processing guarantees.
Capabilities use Horo-owned `SpatialCapabilityId` values, for example:

- core stereo panning;
- binaural HRTF with declared source/order/latency limits;
- Ambisonic encode, rotate, or decode for admitted ACN/SN3D orders;
- named speaker-layout rendering;
- platform object-audio output with declared object capacity;
- provider-specific room or spatial metadata behind a typed extension contract.

A capability ID is not a marketing string. Its versioned contract defines inputs,
outputs, coordinate convention, ADR-063 layouts, required metadata, quality/limit
fields, and compatibility. A descriptor may advertise only operations its current
implementation can initialize and process under the real-time contract. Manifest
hints and registration are catalog evidence, not active capability.

Native device, SDK, middleware, or platform types remain private to the provider
and backend adapters. Public, project, asset, graph, and diagnostic contracts use
Horo IDs, limits, layouts, and results only.

### 3. SpatialRenderProfile declares optional or required behavior

Project/build policy selects a versioned `SpatialRenderProfile` for each admitted
audio output/listener context. A profile contains:

- a stable profile ID and schema version;
- a requirement strength: `Optional` or `Required`;
- the complete required capability set and minimum limits;
- an exact provider ID or an explicit ordered candidate list when identity matters;
- output layout, sample-rate, quality, latency, source/object, scratch, and memory
  constraints;
- for `Optional` only, an explicit `AllowCoreStereoFallback` flag;
- compatibility and cook/runtime requirement versions.

One profile resolves to exactly one primary spatial provider for its output graph.
Individual voices and buses select provider-supported modes through compiled typed
descriptors; they do not discover or switch providers. Additional reachable audio
content or package descriptors may add minimum capabilities, but cannot weaken the
project/build requirement. Preflight evaluates the union of profile and admitted
content requirements.

`Required` means every declared capability and limit must be satisfied by one
admitted provider. It never permits core fallback unless the core provider itself
is the explicitly required provider and its capability set is sufficient.
`Optional` permits fallback only when the profile explicitly enables it and the
content/output contract remains valid under core stereo. An omitted fallback flag,
unknown strength, empty required candidate, or contradictory limit fails profile
validation.

### 4. Resolution is deterministic and registration-order independent

The Audio control owner resolves a provider from an immutable catalog, profile,
content-requirement, backend/device, and product-policy snapshot. Candidate order
is:

1. the exact provider requested by the profile, when present;
2. the profile's explicit ordered candidate IDs;
3. for an optional automatic profile only, compatible candidates ordered by the
   host's versioned recommendation policy and then canonical provider ID;
4. core stereo only when optional fallback is explicitly allowed and compatible.

The first candidate that passes complete preflight is selected. Registration,
module load, filesystem discovery, hash-map, job-completion, or UI display order is
never a tie breaker. Diagnostics retain every attempted candidate and rejection
reason. Automatic recommendation is a policy result for the exact host snapshot,
not an architectural ranking or permission to rewrite the saved profile.

Provider-specific cooked data names the exact provider/schema compatibility it
requires. Generic cooked spatial data names capabilities and Horo schemas. A
runtime provider cannot reinterpret another provider's opaque data merely because
both advertise HRTF or object output.

### 5. Activation preflight happens before callback publication

Selection and preflight run outside the callback before AudioRuntime becomes
`Active` and before a new device/graph epoch is published. Preflight verifies:

- installed, enabled, trusted, verified, host-compatible package and owner lease;
- exact Audio/provider ABI, descriptor/schema, and cooked-data compatibility;
- selected backend and negotiated device/output support without native-type leak;
- every required operation, layout, order, source/object count, quality, latency,
  scratch, state, memory, and per-block work limit;
- real-time declarations, bounded preparation, fault reporting, reset, and
  shutdown behavior;
- prepared provider instance and immutable ADR-065 render-plan integration;
- absence of undeclared scene, physics, file, network, registry, or callback
  dependencies.

Preflight is transactional. It pins the candidate/catalog/device/profile/content
revisions, prepares all provider and graph state privately, and publishes only a
complete validated generation. Cancellation, stale revision, preparation failure,
or budget failure destroys the candidate and retains the last good compatible
generation. Startup with no prior generation returns a typed activation failure.

Editor preview, play, packaged game, CLI validation, and tests call the same
application preflight operation with explicit composition/profile inputs. A UI or
command adapter cannot mark a provider available based only on installation or a
previous device probe.

### 6. Optional fallback and required failure are different outcomes

When an optional preferred candidate is missing, incompatible, or fails bounded
preparation, control may resolve core stereo only if the saved profile permits it
and core satisfies all remaining hard layout/content limits. The activation result
is `ActiveFallback`, not ordinary `Active`, and records the requested provider or
capabilities, selected core provider, reason, and affected profile/output. Product
policy may still reject degraded activation.

When a required candidate or capability is unavailable, preflight returns
`RequiredSpatialCapabilityUnavailable` and AudioRuntime does not activate that
output. It does not create voices, start the callback with core stereo, drop the
required spatial metadata, reinterpret layouts, or wait for a later provider while
reporting success. The host presents install/enable/repair/change-profile actions
where available.

An optional profile also fails instead of falling back when core cannot satisfy a
hard output layout, content requirement, memory/latency budget, or authored mode.
"Optional provider" does not mean required content becomes optional.

### 7. Runtime provider failure never triggers callback policy

The callback may emit a bounded generation-tagged provider fault record and render
preallocated silence for the affected block/voices according to the compiled fault
contract. It cannot instantiate core stereo, rebuild the graph, change layouts,
select a provider, allocate, log, unload code, or commit runtime state.

Audio control consumes the fault and applies the resolved profile policy:

- for `Required`, stop ordinary admission for the affected output, enter ADR-062
  recovery or terminal failure policy, and restore audio only after the required
  provider passes a new transactional preflight;
- for `Optional` with admitted fallback, prepare a complete core-stereo provider
  and graph generation off-thread, publish it at a buffer boundary, and report
  `ActiveFallback` after matching acknowledgement;
- for `Optional` without a valid fallback, use the same unavailable/recovery path
  as required behavior rather than inventing a substitute.

No already-visible provider or graph state is freed until callback quiescence/swap
acknowledgement and provider owner-lease retirement. Repeated faults are bounded
and coalesced by generation/reason outside the callback.

### 8. Device, profile, content, and package changes re-run preflight

Device open/reconfigure/recovery, output-layout or sample-rate change, profile
change, package enable/disable/update, provider catalog revision, relevant asset or
graph hot reload, and newly admitted content requirements invalidate the resolved
spatial plan. Control prepares and validates a replacement before publication.

A device that loses a required object/layout capability cannot silently keep the
old plan or downmix to stereo. A package cannot unload while an active/candidate/
retiring generation holds its provider lease. A newly installed provider does not
replace an active provider until an explicit policy-resolved transaction succeeds.

### 9. Observability is part of the selection contract

Control-owned snapshots and bounded events expose:

- requested profile, strength, candidates, capabilities, and minimum limits;
- catalog/device/content revisions used by preflight;
- attempted and selected provider ID/version/owner plus active generation;
- normal, fallback, recovering, unavailable, or failed state;
- stable rejection/fallback/fault category and safe remediation action;
- source/object capacity, layout/order, latency, memory/scratch, and budget usage;
- fallback and provider-fault counters tagged by stable IDs, not display names.

Editor, CLI, MCP, diagnostics, and profiler surfaces project the same snapshot.
Fallback is visible at activation and remains visible for the lifetime of the
degraded generation; it is not only a transient log line. The callback formats no
logs and emits no provider-native handles, proprietary payload, source/listener
positions, or user audio.

### 10. Provider processing boundary

Providers consume immutable prepared source/listener/spatial parameters and
borrowed ADR-063 processing blocks through the compiled ADR-065 plan. Scene and
physics extraction owns world queries; providers do not query ECS, physics,
editor, configuration, package discovery, files, or network during processing.
`Prepare` and destruction run on declared non-callback owner threads; `Process`
runs with fixed memory and bounded work on the real-time path.

AUD-005.5 and AUD-011.4 define the concrete processing and registration APIs, but
they must preserve this selection and failure model. A middleware backend that
owns spatialization satisfies the same typed capability/preflight contract through
its adapter; middleware presence alone is not proof of a capability.

### 11. Migration and verification

Audio Architecture replaces its unconditional stereo fallback statement with this
required-versus-optional policy. Existing projects with no spatial profile migrate
explicitly to the core-stereo profile; they do not acquire an ambient "best
available" provider. Existing custom-provider settings migrate to an optional or
required profile through a user-visible rule, never by guessing intent.

Required contract coverage includes:

- core-stereo and Null capability truth without overclaiming HRTF/Ambisonic/object
  support;
- exact provider, ordered candidate, optional automatic, and explicit core
  selection independent of registration/load/map/job order;
- profile/content requirement union, unknown/duplicate/contradictory capability,
  ABI/schema/cooked-data/layout/order/limit/device/backend rejection;
- required missing, disabled, untrusted, incompatible, unavailable, prepare-failed,
  budget-exceeded, and provider-specific-data failures with no core activation;
- optional fallback allowed/forbidden/incompatible outcomes and persistent
  observable requested-versus-selected evidence;
- cancellation/stale preflight, last-good retention, package update/removal, new
  content admission, profile change, device reset/loss, and output-format change;
- callback fault silence/record behavior, required recovery, optional prepared
  fallback swap, generation acknowledgement, and lease-safe retirement;
- editor/play/package/CLI/Null preflight parity and callback allocation, lock,
  logging, registry, scene/physics, file, and network guards.

## Consequences

Projects can deliberately use core stereo, prefer an optional spatial enhancement,
or require a capability/provider with a hard activation guarantee. Missing required
behavior becomes an actionable failure rather than an unnoticed mix change. Every
optional fallback is an explicit generation and durable observable state.

The cost is a typed profile and capability taxonomy, deterministic resolution,
transactional preflight, provider owner leases, and recovery/fallback generation
swaps. Device and content changes may make a previously valid required profile
unavailable, which is intentional contract enforcement.

## Rejected Alternatives

### Always fall back to the core stereo panner

Rejected because required HRTF, Ambisonic, object, layout, or provider-specific
content would launch with different semantics while appearing successful.

### Select the first registered compatible provider

Rejected because package/load/job order is not product policy and is not stable
across hosts. Profiles and versioned recommendation policy define deterministic
candidate order.

### Treat installation or manifest hints as availability

Rejected because ABI, device, backend, limits, preparation, and real-time
requirements can still fail. Complete current-snapshot preflight is authoritative.

### Let each bus or voice discover a provider at playback

Rejected because it introduces callback lookup, inconsistent mixes, unbounded
initialization, and provider lifetime races. One output profile resolves and
prepares the provider before publication.

### Switch to core stereo directly inside the callback after a fault

Rejected because selection, graph/layout preparation, allocation, policy, and
observability are control-owned. The callback emits a bounded fault and uses its
compiled silence behavior until control publishes a replacement.

### Make Null audio satisfy every spatial requirement

Rejected because deterministic validation is not evidence of hardware/native
capability. Tests use explicit simulated providers when they need that proof.
