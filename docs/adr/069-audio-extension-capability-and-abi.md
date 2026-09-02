# ADR-069: Audio Extension Capability and ABI

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: Audio capability identity, generic extension handoff, real-time in-process ABI, versioning, ownership, trust, transactional registration, and unload
- **Issue**: [AUD-011.1](https://github.com/abdullahbodur/horo-engine/issues/638)
- **Jira**: [HORO-638](https://horo-engine.atlassian.net/browse/HORO-638)
- **Parent**: [AUD-011](https://github.com/abdullahbodur/horo-engine/issues/637)
- **Related**: [ADR-054](054-extension-and-package-authority-boundary.md), [ADR-062](062-audio-runtime-ownership-and-update-order.md), [ADR-064](064-audio-asset-and-cook-boundary.md), [ADR-065](065-mixer-topology-and-constrained-dag.md), [ADR-066](066-spatial-provider-and-required-capability.md)
- **Normative documents**: [Audio Architecture](../architecture/runtime/audio-architecture.md), [Extension System](../architecture/extensions/plugin-system.md), [Horo Package System](../architecture/packages/package-system.md)

## Context

The generic Extension System defines verified package handoff, trust, a versioned
C bootstrap ABI, contribution registration, module leases, and conservative unload.
That boundary is sufficient for tools and control-plane capabilities, but not for
code executed inside an audio callback. A generic function pointer does not declare
sample/layout ABI, alignment, fixed memory, processing bounds, latency, tail,
denormal behavior, callback threading, fault reporting, or code/state retirement.

Audio needs codec, runtime decoder, DSP, spatializer, acoustic, and higher-level
service contributions without creating another package manager or trust store. It
also must prevent an extension from registering partially, unloading while a graph
or decoder uses its code, or claiming a required capability through an incompatible
version. The generic module ABI and the Audio real-time ABI therefore have related
but distinct responsibilities.

## Decision

### 1. EXT/PKG remains the discovery and trust authority

Every installable Audio extension is a Horo package under ADR-054. PackageService,
PackageLifecycleService, TrustService, and ExtensionHost retain ownership of exact
artifact resolution, signatures/files, install records, local/organization trust,
enablement, module loading, the generic C bootstrap ABI, activation generations,
and module leases. Audio does not scan directories, load arbitrary paths, resolve
dependencies, download codecs, or maintain a second trust database.

ExtensionHost hands Audio one immutable activation candidate bound to the verified
install record, selected module artifact, descriptor digest, approved permissions/
capabilities, module ABI, and live module lease. Audio validates only Audio-specific
descriptors and ABI tables and returns a candidate/commit result. It cannot mark a
package trusted, enabled, installed, updated, or uninstalled.

The generic extension ABI may bootstrap the module and return bounded Audio
contribution descriptors plus versioned Audio ABI entry tables. It is not itself
the processing contract. A generic `application.capability`, arbitrary callback,
`void*`, editor command, service locator entry, or generic runtime-system hook
cannot be cast or adapted directly into callback-executed DSP/spatial code.

### 2. Audio capability families are typed and closed by version

Audio defines stable capability-family IDs with independent schema/ABI versions:

| Family | Execution class | Examples |
|---|---|---|
| `audio.codec.import` | Offline/control/job or isolated helper | source probe, container parse, authoring decode, cook encode |
| `audio.decoder.runtime` | Bounded non-callback decode jobs; prepared output consumed by callback | stream/resident decode, seek/preroll |
| `audio.dsp.node` | Prepare/control plus real-time callback process | filters, dynamics, reverb, bus effects |
| `audio.spatial.provider` | Prepare/control plus real-time callback process | HRTF, Ambisonic, speaker/object rendering |
| `audio.acoustic.provider` | Non-RT query/update and optional prepared RT projection | occlusion, propagation, room data |
| `audio.system.service` | Application/control plane | adaptive-music or dialogue orchestration adapters |

Each contribution has a stable `AudioContributionId`, exact family, descriptor
schema version, Audio ABI range, provider semantic version, owner package/module/
artifact identity, declared capabilities/limits, execution class, permissions,
and compatibility fingerprint. Display names and module load order are not identity.
Unknown families, versions, flags, or duplicate IDs fail candidate validation.

Capability IDs are versioned semantic contracts, not free-form marketing strings.
A provider cannot claim an Audio capability merely because it exposes a similarly
named generic extension role. Required project/profile capabilities resolve only
against a committed compatible Audio registry snapshot.

### 3. Callback execution uses a separate Audio RT ABI

Third-party code that executes on the Horo callback must implement the versioned
Audio Real-Time ABI for its family. The ABI is a fixed-width C function-table
boundary with:

- `size`, major/minor version, reserved-zero fields, feature bits, and explicit
  calling convention;
- Horo-owned fixed-width IDs, enums, scalar encodings, frame/count types, error and
  bounded fault codes;
- exact ADR-063 sample representation, planar layout/view version, alignment,
  channel-role encoding, valid/capacity frame semantics, and sample-rate contract;
- separate owner-thread prepare/create/reset/flush/destroy and callback-thread
  process entry points;
- declared maximum state, scratch, input/output, channel, block, latency, tail,
  work, and nested-call requirements;
- no STL, RTTI, exceptions, compiler-specific classes/vtables, C++ ownership,
  allocator ownership, native API types, strings, or unbounded containers.

The generic module ABI and Audio RT ABI negotiate independently. A compatible
generic bootstrap cannot compensate for a missing/incompatible Audio RT table.
Major Audio RT versions must match exactly. Minor compatibility is admitted only
through size checks, reserved fields, feature negotiation, and a host-defined
backward-compatible range; unknown required features reject activation.

Internal first-party targets built in the same product may use private C++ adapters,
but they must satisfy the same semantic descriptor, validation, callback, lifetime,
and test contract. That build convenience is not a public third-party C++ ABI.

### 4. Real-time entry points are intentionally narrower

Audio RT `Process` receives only borrowed generation-scoped Horo block views,
preallocated instance/scratch state, immutable prepared parameters, and a bounded
fault/event sink. It must complete within declared work for the admitted block and
cannot:

- allocate/free general heap memory or resize storage;
- acquire contended locks, wait, sleep, spawn/join work, or use blocking atomics;
- throw, unwind across the ABI, format logs, access files/network, query clocks or
  configuration, discover services/packages, or call UI/ECS/physics/gameplay;
- retain borrowed block/parameter/sink pointers beyond the call;
- change layout/rate/capacity/ownership, call another bus/provider by discovery,
  publish graph topology, or unload code;
- invoke host callbacks except the exact bounded allocation-free operations present
  in the negotiated Audio RT table.

`Process` does not return an ordinary rich error requiring allocation or logging.
It writes a bounded stable fault code/counter associated with contribution,
instance, graph and callback generations. The callback applies the compiled bypass,
silence, or fault behavior; Audio control consumes records and owns recovery policy.
Host guards cannot make native memory corruption or an ABI violation safe.

### 5. Preparation proves ownership and bounded resources

All fallible discovery, descriptor parsing, schema migration, capability matching,
layout conversion planning, allocation, initialization, resource loading, and DSP
preparation occur outside the callback on declared owner/job threads. The host
passes versioned owned input values and bounded writers; no module retains borrowed
manifest, asset, registry, or editor memory.

Instance memory is either host-owned opaque storage sized/aligned by the validated
descriptor or module-owned storage created/destroyed by paired functions from the
same loaded module/allocator domain. Cross-module `new/delete`, exception objects,
STL values, and ownership transfer are forbidden. Host block and scratch memory
remains host-owned. Module-owned state remains charged to the Audio/package budget
and pins the exact module lease until paired destruction completes.

Offline import/cook codecs follow ADR-064's host-owned source/output transaction.
They may use an approved isolated helper when latency permits. Callback DSP and
spatial processing is in-process trusted native code; an IPC/sandbox call is not a
real-time processing path. Packages needing strong crash isolation cannot execute
third-party code directly on the callback.

### 6. Trust is contribution and execution-class aware

TrustService computes required trust from verified native code, family, execution
class, permissions, files, publisher, and requested capability set. In-process
Audio RT code requires explicit high-trust native-code approval for the exact
package/artifact/capability identity. Project configuration or a signed manifest
cannot grant local execution trust or lower this requirement.

Network, filesystem, process, microphone, telemetry, model/data download, or native
device access is absent unless a separate non-RT capability/permission contract
declares it and trust approves it. Even when approved, those operations remain
forbidden from RT entry points. A DSP capability does not imply codec, device,
capture, editor, or application-service permission.

Trust revocation closes new admission and schedules the same safe deactivation as
disable/update. It does not force-unmap code still referenced by callback/jobs.

### 7. Registration is transactional and snapshot based

Audio validates all contributions from one activation candidate before publishing
any of them. It checks package/module/descriptor binding, stable IDs, family/schema/
ABI versions, capabilities, permissions/trust, function-table sizes/pointers,
limits, formats/layouts, resource budgets, and owner lease. Duplicate/conflicting
claims resolve only through explicit project/product policy naming an exact ID;
registration or module order never wins.

Validation builds a private candidate catalog and provider factories. Only a fully
valid candidate set publishes one immutable Audio registry snapshot tied to the
ExtensionHost activation generation. Failure destroys candidate instances/state,
releases candidate leases, records typed diagnostics, and leaves the prior snapshot
and active graphs unchanged. Descriptor creation/validation is inert: it does not
open files/devices, register globally, start jobs/threads, or touch the callback.

Each import/cook/decode job, graph build, spatial preflight, live graph/voice, and
retiring tail pins the exact registry snapshot plus module/contribution lease it
uses. A newer snapshot does not mutate those users in place.

### 8. Graph admission performs Audio-specific RT validation

Registration proves structural compatibility, not that every instance can enter a
render graph. ADR-065 graph compilation pins a registry snapshot and validates each
DSP/spatial descriptor against actual layout, rate, block size, limits, latency,
tail, scratch/state, parameter schema, deterministic/order requirements, and active
profile budgets. It prepares private instances and publishes only a complete graph
generation at a callback boundary.

An extension cannot add hidden routes, callbacks, jobs, or dependencies from
`Prepare`/`Process`. Missing required capability or version fails graph/spatial/
runtime activation under ADR-066. Optional fallback must be explicit and observable;
the generic ExtensionHost cannot select an Audio fallback.

### 9. Live unload is conservative and lease gated

Disable, update, rollback, trust revocation, and removal first close new catalog
admission. By default, native Audio contribution changes become `PendingRestart`.
Live unload may be implemented for a specific family only when the host proves:

1. replacement or removal graphs/providers are fully prepared and published;
2. every callback epoch acknowledged quiescence/swap away from the old code/state;
3. declared DSP tails are drained, migrated, or cancelled by admitted policy;
4. decoder/import/cook/acoustic/application jobs are cancelled and joined;
5. command, completion, fault, meter, UI/diagnostic, registry and provider queues no
   longer contain old function pointers or borrowed/module-owned values;
6. every instance/state is destroyed through its paired owner function;
7. all graph, voice, resource, job, registry, and callback/module leases reach zero;
8. ExtensionHost then deactivates registrations and only afterward unloads code.

Timeout, callback/device loss, stuck job, missing acknowledgement, unknown tail,
or any surviving lease leaves the module loaded and reports pending restart/failure.
It never force-unloads executable code. Shutdown follows the same order and is
idempotent after partial activation.

### 10. Diagnostics and hostile behavior

Typed diagnostics include safe package/module/contribution/family/version identity,
activation/registry/graph generation, validation stage, stable reason, declared
versus admitted limits, and pending leases. They exclude function addresses, native
handles, proprietary state, source audio, credentials, and user content by default.
The callback emits only bounded codes/counters; formatting occurs on control paths.

Budget overrun, non-finite output, canary corruption where enabled, undeclared tail,
deadline faults, exception/ABI violation at an outer host boundary, or repeated
invalid fault records disable new admission and enter typed control recovery. Horo
does not claim in-process native code is sandboxed. Strong isolation uses a helper
for non-RT work or rejects the RT contribution.

### 11. Migration and verification

Audio Architecture gains this ABI boundary; Extension System references it as a
domain-specific stricter ABI layered after generic activation. Existing generic
DSP/spatial callbacks do not migrate by reinterpretation. They need explicit Audio
descriptors and ABI adapters or remain unavailable. Codec/service contributions use
their family contract and do not receive RT permission automatically.

AUD-011.2 through AUD-011.4 define codec/decoder, DSP, and spatial/acoustic tables
and tests. Middleware product integration remains AUD-016 and must select one of
these contribution shapes or a separately governed backend replacement.

Required contract coverage includes:

- generic module ABI success with missing/wrong Audio RT ABI rejection;
- size/major/minor/reserved/feature/calling-convention/function-pointer fixtures,
  unknown families/capabilities and duplicate/conflicting stable IDs;
- complete-candidate registration commit and rollback under randomized order;
- trust denied/revoked, permission mismatch, artifact/descriptor identity mismatch,
  and no project-granted native execution trust;
- allocator/ownership pairing, alignment, borrowed lifetime, no cross-module STL/
  exception/delete, and partial prepare unwind;
- callback allocation/lock/wait/log/I/O/network/registry/service discovery guards,
  bounded faults, non-finite output, deadline and canary fixtures;
- graph/profile/layout/rate/block/scratch/state/latency/tail/budget rejection before
  callback publication and required-capability failure;
- active graph/voice/tail, queued command/fault, decoder/import job, package update,
  trust revoke, callback loss and shutdown unload barriers with no force-unload;
- built-in private C++ and third-party C ABI implementations passing the same
  semantic RT contract suite.

## Consequences

Horo reuses one package, trust, bootstrap, and lifecycle system while applying the
strict domain contract callback audio requires. Generic extensions cannot smuggle
arbitrary code into the callback, and every graph/job pins compatible code and
state until safe retirement. Required capabilities and version failures are typed.

The cost is separate Audio family descriptors and ABI tables, strict validation,
high-trust in-process policy, owner-paired memory, immutable registry generations,
and conservative restart-first unload. This is necessary because native callback
code cannot be made safe by a generic plugin wrapper.

## Rejected Alternatives

### Use the generic extension C ABI directly for DSP callbacks

Rejected because it does not define Audio blocks, RT operations, limits, latency,
tail, scratch, fault, or callback lifetime and is therefore insufficient.

### Publish a stable third-party C++ Audio ABI

Rejected because compiler/STL/vtable/exception/allocator compatibility and ownership
cannot be promised across independent packages and toolchains.

### Give Audio its own package loader and trust store

Rejected because identity, dependency, update, signature, enablement, and trust
would diverge from EXT/PKG. Audio consumes exact activation candidates.

### Run callback DSP in an isolated helper process

Rejected as a baseline RT path because IPC scheduling and failure are not bounded to
the callback deadline. Helpers remain valid for offline/control work.

### Allow live unload after unregistering the contribution ID

Rejected because graphs, voices, tails, jobs, queues, state destructors, and function
pointers may still reference the module. Every lease and acknowledgement must drain.

### Catch exceptions and continue rendering

Rejected because exceptions cannot cross the C ABI and a catch cannot prove native
state or memory remains valid. RT functions use bounded fault records and control-
owned recovery; hostile native code is not sandboxed.
