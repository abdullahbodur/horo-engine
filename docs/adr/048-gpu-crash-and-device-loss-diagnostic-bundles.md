# ADR-048: GPU Crash and Device-Loss Diagnostic Bundles

- **Status**: Proposed
- **Date**: 2026-09-01
- **Supersedes**: None
- **Scope**: Renderer incident evidence capture, durable manifests, privacy and bundle lifecycle
- **Issue**: [RND-017.8](https://github.com/abdullahbodur/horo-engine/issues/440)
- **Jira**: [HORO-440](https://horo-engine.atlassian.net/browse/HORO-440)
- **Companion decisions**: [ADR-034](034-gpu-memory-and-residency-ownership.md), [ADR-041](041-backend-neutral-renderer-diagnostics-model.md), [ADR-042](042-cpu-gpu-timestamps-and-pipeline-statistics.md), [ADR-043](043-gpu-memory-and-resource-inspection.md), [ADR-044](044-render-markers-and-debug-labels.md), [ADR-045](045-backend-validation-and-debug-layer-integration.md), [ADR-046](046-gpu-driver-compatibility-and-workaround-registry.md), [ADR-047](047-renderdoc-pix-and-metal-capture-integration.md)
- **Normative documents**: [Platform Abstraction](../architecture/foundation/platform-abstraction.md), [Rendering Architecture](../architecture/runtime/rendering-architecture.md), [Observability Architecture](../architecture/observability/observability.md), [Logging, Context, And Diagnostics](../architecture/observability/observability-logging.md)

## Context

A GPU device loss can leave the process healthy enough to inspect immutable
renderer state and attempt recovery. A process crash, access violation, fatal
signal or driver termination cannot safely traverse graphs, allocate, lock normal
stores or format a bundle. Treating both as an ordinary “generate diagnostics”
operation either loses valuable device-loss context or performs unsafe work in a
compromised process.

Backend-native evidence also differs: Vulkan device-fault data, D3D12 DRED,
Metal command-buffer errors and OpenGL reset/debug state have different timing,
availability and privacy. Horo needs one incident identity and manifest without
pretending every backend supplies equivalent payloads or automatically invoking
external capture tools.

## Decision

### 1. One incident model has two capture paths

`RendererIncidentService` is an application-owned service composed over
Observability, Platform crash/filesystem capabilities and narrow generation-bound
renderer evidence ports. It owns incident identity, durable staging, manifests,
retention and export. Renderer/backends contribute bounded evidence; they do not
write bundles, upload files or call UI.

```cpp
struct RendererIncidentIdentity {
    RendererIncidentId id;
    RendererIncidentKind kind;
    SessionId session;
    RendererGeneration renderer;
    DeviceGeneration device;
    RendererIncidentSequence sequence;
};
```

Kinds distinguish `DeviceLost`, `DeviceHung`, `DeviceRemoved`,
`RendererFatal`, `ProcessCrashWithRendererContext` and `PreviousSessionCrash`.
The authoritative typed renderer/platform result chooses the kind; message text
does not. Duplicate callbacks for one loss generation coalesce into one incident
and occurrence count. A new device generation or distinct fatal process marker
gets a new identity.

There are two deliberately separate paths:

- **Live device-loss path**: the process closes old-generation admission, freezes
  pre-existing bounded evidence and performs allowed post-loss native queries at a
  renderer safe point before destroying their owning native objects.
- **Process-crash path**: the fault handler writes only a preallocated tombstone
  and signals an already-running out-of-process collector when available. Rich
  association/bundle assembly happens on next launch or in the collector from
  already shared/durable bounded data.

Neither path automatically starts ADR-047 graphics capture, waits for GPU idle,
replays work, retries a failed command or delays emergency termination beyond the
Platform crash-service contract.

### 2. Contributions are static, allowlisted and inert

Modules declare incident evidence before activation:

```cpp
struct RendererIncidentEvidenceDescriptor {
    RendererIncidentEvidenceId id;
    ModuleId owner;
    RendererIncidentEvidenceKind kind;
    RendererIncidentCapturePhase phases;
    PrivacyLevel privacy;
    uint64_t maxBytes;
    RendererIncidentCriticality criticality;
};
```

Descriptors are inert metadata. They do not register signal handlers, create
files, retain native objects or start collection. Composition rejects duplicate
IDs, foreign ownership, unknown phase/kind/privacy, zero/unbounded size and a
fault-time contribution that is not preallocated and signal-safe.

Allowed kinds include typed incident header; recent ADR-041 event window; graph
execution manifest; marker/correlation window; effective capability, validation
and compatibility-policy manifests; aggregate memory/residency snapshot; recent
timing availability summaries; surface/presentation state; platform crash marker;
and backend-native fault evidence. Resource contents, unrestricted shader source,
scene/project assets, credentials, complete environment, native pointer tables and
arbitrary filesystem walks are forbidden generic contributions.

An opaque backend-native fault payload is a separately declared
`RestrictedNativePayload`, disabled where product/profile or platform policy
forbids it. Its manifest identifies provider/schema/version and byte size without
requiring Horo to parse or redact unknown internals.

### 3. Normal execution maintains only bounded incident-ready evidence

Incident readiness cannot begin expensive traversal after a loss. During normal
renderer execution, enabled profiles maintain finite generation-scoped rings and
immutable manifests using data already produced by owning systems:

- ADR-041 retains admitted/aggregated renderer events in its existing stores plus
  a preallocated high-severity emergency window;
- graph execution publishes a compact manifest of pass IDs, queues, dependencies,
  admitted resources by typed identity/class, submission/completion state and
  marker IDs, never command/resource payloads;
- ADR-044 retains a bounded logical marker/correlation tail when incident capture
  is enabled, not native text or unbounded per-draw history;
- ADR-043 supplies the latest aggregate memory/ledger snapshot; incident capture
  never starts a full per-resource inspection after loss;
- capabilities, validation realization and compatibility policy are immutable
  generation manifests already needed for admission; and
- presentation retains the latest bounded surface/frame state.

The default window is the latest 8 real render frames, 2,048 logical marker
records, 1,024 graph/pass records and 2,048 renderer diagnostic events. Hard
maxima are 64 frames, 16,384 markers, 8,192 graph/pass records and the ADR-041
16,384-event maximum. Total incident-ready renderer CPU memory defaults to 8 MiB
and has a 64 MiB hard maximum per active device. Product profiles may lower or
disable optional detail, but required emergency header/high-severity capacity
remains preallocated before renderer activation.

Rings own bounded copied values and generation IDs. They do not hold resource,
graph, store or native-object leases that would prevent retirement. Updates use
preallocated/fixed-capacity storage, no file I/O and no frame-hot arbitrary heap
allocation. Saturation overwrites oldest optional evidence and increments an
explicit loss counter; required incident identity/high-severity slots are not
silently displaced.

### 4. Live device loss freezes before recovery mutates evidence

The first authoritative device-loss transition atomically closes new evidence for
the old generation and freezes ring revisions. `RenderFrontend` then executes one
bounded incident safe-point sequence on the render-capable owner thread:

1. publish incident identity and typed loss result through the preallocated path;
2. freeze immutable frontend manifests/rings without walking live registries;
3. invoke each admitted backend-native fault query exactly once while its required
   device/queue/context and validation objects remain alive;
4. copy returned bounded data into incident-owned staging or record unavailable,
   timed out, partial or unsafe-to-query explicitly;
5. release native evidence interfaces, then allow normal device teardown/recovery;
6. finalize non-native encoding/hash/publication on bounded cancellable workers.

Native query capability declares required affinity, maximum bytes/time, whether it
is legal after loss, and which parent object must remain alive. A query cannot call
device idle, submit new GPU work, map arbitrary resources, retry per callback or
extend the global recovery deadline. The default native-query budget is 100 ms and
8 MiB total; hard maxima are 2 seconds and 64 MiB in Qualification profiles.

A required evidence failure does not turn the already authoritative device-loss
result into another result or block safe teardown. It marks bundle coverage
partial with exact cause. Recovery may proceed after bounded native collection and
is never contingent on file encoding, compression, UI or upload.

### 5. Process crash writes only fault-safe evidence

Before risky renderer work, the service allocates a minimal fixed tombstone slot
and exposes signal/exception-safe scalar fields: incident/crash/session IDs,
renderer/device generations, last real frame/submission sequence, process/build
identity, fault class and pre-established collector/staging identifiers. Updating
these fields is bounded and uses the Platform crash-service synchronization model.

At process fault time the handler may only seal that slot, write/notify through
pre-opened Platform crash resources and flush the existing emergency sink where
safe. It performs no allocation, normal logging, mutex acquisition, graph/resource
traversal, native GPU query, compression, hashing, symbolization or directory
creation. Absence/failure of the collector falls back to Platform's minimal
tombstone and remains best effort.

An out-of-process collector may copy predeclared shared/emergency pages and a
platform dump under its own bounded policy. It cannot call into the failed
renderer, inspect arbitrary process memory beyond its approved dump contract or
claim that volatile evidence was complete. On next launch, Observability associates
the tombstone with the prior session and assembles a `PreviousSessionCrash` bundle
only from durable collector output, closed log segments and validated staging.

### 6. The manifest makes missing and partial evidence explicit

Every incident directory publishes a versioned `renderer-incident.json` manifest:

```cpp
struct RendererIncidentManifest {
    RendererIncidentIdentity incident;
    RendererIncidentStatus status;
    RendererIncidentCause cause;
    RendererIncidentEnvironment environment;
    RendererIncidentCoverage coverage;
    std::vector<RendererIncidentArtifactEntry> artifacts;
    RendererIncidentPrivacySummary privacy;
};
```

Published status is `Complete`, `Partial`, `Quarantined` or `Abandoned`; `Staging`
exists only in the private operation/journal and is never presented as a published
bundle. Each descriptor has `Captured`, `Unavailable`, `DisabledByProfile`, `NotSafeAfterLoss`,
`TimedOut`, `CapacityExceeded`, `Corrupt`, `CollectorUnavailable` or
`ProcessTerminatedBeforeCapture` coverage. Missing evidence is never encoded as an
empty successful file.

Environment records safe backend/module, physical adapter/device, normalized
OS/runtime/driver, effective capability revision, validation state, compatibility
policy/rule IDs, product/build profile and exact renderer/device generations.
Artifacts record schema/provider versions, bounded source time/revision, byte size,
digest, privacy, truncation and atomic-publication state. Native handles, addresses,
serial numbers and unrestricted labels/paths remain excluded.

Graph/marker records use ADR-044 stable identities. Memory totals preserve ADR-034
committed/reserved/retiring meanings. Timing values preserve ADR-042 unavailable,
disjoint and source-frame semantics. Bundles never reinterpret values or fabricate
cross-queue order after the incident.

### 7. Storage, privacy and export follow Observability

Incident staging lives under the previous/current Observability session crash
subtree in the platform user-state root with opaque generated names and private
permissions. Writers use same-filesystem temporary files, checked capacity,
durable flush policy and atomic publication where supported. The manifest is
published last. Startup validates and quarantines interrupted/corrupt staging
without executing or trusting native payload content.

Structured Horo evidence is classified field-by-field and redacted before durable
publication. Native dumps/fault blobs use an artifact-level
`RestrictedNativePayload` class: they may contain memory, shaders, resource/user
content or paths and cannot be promised redactable. This class is not a new
field-level Observability `PrivacyLevel`; generic bundle hooks treat it as
`Forbidden`, and only the owning incident export workflow may include it after
separate confirmation. It remains local, is never automatically uploaded and is
never included in a normal support bundle by default. Credentials/secrets remain
forbidden even when a native dump may incidentally contain process memory; UI warns
that opaque dumps cannot be fully inspected.

Default per-incident durable budget is 64 MiB structured plus 256 MiB native
payload, 60-second finalization and 8 retained incidents/2 GiB total. Hard maxima
are 256 MiB structured, 2 GiB native payload, 10-minute finalization, 64 incidents
and 16 GiB total. Product/platform crash-dump policy may impose lower limits.
Checked free-space reservation precedes live rich collection; fault-time tombstone
capacity is reserved at startup.

Retention never deletes an active/current staging directory, unexported user-pinned
incident or artifact being read/exported. Cleanup is bounded next-launch/background
work, oldest-completed first. Capacity pressure may omit optional native evidence
or reject a manual rich bundle, but cannot delete unrelated active evidence from a
fault callback.

Export is a user-initiated application operation showing incident/session IDs,
coverage, artifacts, sizes and privacy. It writes an atomic allowlisted archive to
a user-selected validated path. Normal diagnostics may export only structured
redacted evidence; adding opaque native payloads requires a separate explicit
confirmation. No backend, collector or crash handler performs network access.

### 8. Failure, concurrency and shutdown preserve incident evidence

Incident state is monotonic:

```text
Observed -> Frozen -> NativeCollecting -> Finalizing
         -> Complete | Partial | Quarantined | Abandoned
```

One old-generation collection owner exists per incident. Duplicate loss callbacks
increment bounded metadata and cannot start parallel native queries or writers.
Workers receive owned immutable pages and cancellation tokens; they never retain
references into destroyed frontend/backend/store state. Encoding queues are finite;
overflow preserves the incident header and records omitted optional evidence.

Cancellation applies to manual finalization/export, not the minimal tombstone.
Once native loss collection starts, shutdown/recovery requests bounded stop after
the current legal query and then continues teardown. Finalization may resume on
next launch from validated staging. No incident task owns renderer/device lifetime
beyond its explicitly bounded native safe-point phase.

Normal shutdown closes incident admission after renderer work stops, lets active
bounded collection finish or seal partial staging, releases evidence ports before
their native parents and then releases rings after workers/store leases. Repeated
shutdown is idempotent. A process-crash handler remains Platform-owned and follows
its independent process-lifetime teardown; renderer code never uninstalls it from
an unsafe context.

### 9. Backend parity and qualification are evidence-based

Every backend supplies the same incident identity, generation, manifest, coverage,
privacy and lifecycle semantics. Native evidence is optional capability:
OpenGL reset/debug status, Metal command-buffer/error evidence, Vulkan device-fault
data and D3D12 removal/DRED data remain private and may differ. Unsupported is
explicit; one backend's payload cannot be synthesized from logs or substituted for
another's.

Null injects deterministic loss/crash fixtures and validates freeze ordering,
rings, coverage, budgets, interrupted publication, next-launch association,
privacy and stale generations. It cannot prove native query safety, driver payload
accuracy, platform dump behavior or recovery timing.

Native qualification forces loss/removal/hang at each supported stage and proves
exact generation/cause, no unsafe post-loss calls, parent-object lifetime, bounded
recovery delay, complete/partial coverage, artifact readability, crash-handler
signal safety and next-launch recovery across qualified OS/runtime/driver profiles.

## Migration And Verification

Existing device-loss logs remain ADR-041 events and become references in incident
manifests; they are not duplicated through backend-owned files. Existing Platform
crash markers remain minimal fault authority and gain renderer scalar context only
through preallocated fields. Existing support bundles may select completed
structured incident evidence but do not include opaque native payloads by default.

Tests must cover duplicate/coalesced incidents, generation changes, every freeze/
query/finalization stage, native unavailable/timeout/overflow, recovery racing
finalization, cancellation/shutdown, hard budgets and checked arithmetic, ring
saturation, no leases blocking retirement, crash-handler forbidden operations,
collector absence, abrupt termination, corrupt staging, next-launch association,
atomic export/retention races, privacy/redaction/native confirmation, deterministic
Null fixtures and native OpenGL/Metal/Vulkan/D3D12 fault qualification.

## Consequences

Device-loss diagnosis retains bounded graph, marker, capability, memory and native
evidence without making recovery depend on bundle creation. Process crashes remain
signal-safe and gain rich association only from pre-established/durable data.
Missing evidence is explicit and sensitive native payloads stay local and
user-controlled. The cost is always-ready bounded rings, per-backend fault adapters,
out-of-process/next-launch assembly and substantial destructive qualification.

## Rejected Alternatives

| Option | Decision and reason |
|---|---|
| Build a full bundle inside a crash handler | Rejected: allocation, locking and traversal are unsafe in compromised state. |
| Use only normal logs for device loss | Rejected: loses graph/generation/native fault context and bounded coverage semantics. |
| Query every live resource after loss | Rejected: unsafe, unbounded and dependent on invalid native state. |
| Start RenderDoc/PIX/Metal capture automatically | Rejected: capture changes execution and may be unavailable after the fault. |
| Block recovery until compression/export completes | Rejected: durable diagnostics are secondary to bounded safe recovery. |
| Treat absent native evidence as an empty success | Rejected: hides unsupported, unsafe, timeout and abrupt-termination states. |
| Upload native dumps automatically | Rejected: opaque payloads may contain complete private process/resource data. |
| Hold device/resources alive until users export | Rejected: diagnostics cannot own renderer recovery or retirement. |
| Treat Null fault injection as native qualification | Rejected: it cannot prove driver/API fault behavior or signal safety. |
