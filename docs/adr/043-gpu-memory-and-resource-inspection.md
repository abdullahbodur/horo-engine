# ADR-043: GPU Memory and Resource Inspection

- **Status**: Proposed
- **Date**: 2026-09-01
- **Supersedes**: None
- **Scope**: Backend-neutral GPU memory, allocation, residency and resource inspection
- **Issue**: [RND-017.3](https://github.com/abdullahbodur/horo-engine/issues/435)
- **Jira**: [HORO-435](https://horo-engine.atlassian.net/browse/HORO-435)
- **Related**: [ADR-012](012-world-streaming-partition-authority-and-subsystem-boundaries.md),
  [ADR-018](018-command-registration-permissions-threading-and-packaged-build-policy.md),
  [ADR-038](038-gpu-scene-and-instance-data-model.md),
  [ADR-039](039-ray-tracing-capability-and-abstraction.md),
  [ADR-042](042-cpu-gpu-timestamps-and-pipeline-statistics.md)
- **Companion decisions**: [ADR-027: Renderer Resource Identity and Descriptors](027-renderer-resource-identity-and-descriptors.md), [ADR-034: GPU Memory and Residency Ownership](034-gpu-memory-and-residency-ownership.md), [ADR-041: Backend-Neutral Renderer Diagnostics Model](041-backend-neutral-renderer-diagnostics-model.md)
- **Normative documents**: [Metrics And Profiling](../architecture/observability/observability-performance.md), [Rendering Architecture](../architecture/runtime/rendering-architecture.md), [Render Backend Parity](../architecture/runtime/render-backend-parity-contract.md)

## Context

ADR-027 gives every resident renderer resource a typed owner/slot/generation
identity and an explicit lifecycle. ADR-034 gives GPU backing one reservation
ledger, owner scopes, accounting domains, native observations and pressure policy.
Neither decision defines how diagnostics tools inspect those models without
reaching into a backend, walking mutable registries during frame execution or
turning native pointers and driver labels into public identity.

The existing Observability contract has aggregate process and renderer memory
metrics. Aggregates cannot answer which logical resources retain one charged
block, whether bytes are live payload, reusable slack or pending retirement, or
which generation and owner scope produced pressure. Conversely, publishing one
metric series or diagnostic event per resource would create unbounded cardinality
and duplicate renderer ownership in a monitoring subsystem.

Native APIs expose materially different memory facts. Some report physical heap
budgets, some expose approximate working-set guidance, some expose only extension-
specific estimates, and Null has no hardware memory. A useful inspector must keep
ledger facts separate from native observations and must represent unavailable,
estimated, overlapping and truncated data explicitly.

This ADR defines aggregate publication and explicit immutable inspection
snapshots. It does not define the inspector UI, allocator algorithm, pressure or
eviction policy, external capture tooling, resource contents, shader source,
texture previews, native object naming, benchmark thresholds or remote telemetry.

## Decision

### 1. Inspection observes the existing authorities

The renderer exposes three related but non-interchangeable views:

| View | Authority | Use |
|---|---|---|
| Memory/resource operation result | ADR-027 registry or ADR-034 ledger | Caller control flow and exact success/failure. |
| Aggregate metrics and availability | Process `MetricsStore` | Bounded trends for charged bytes, pressure and resource counts. |
| `RendererMemoryInspectionSnapshot` | Render frontend inspection service | Explicit point-in-time engineering detail with stable Horo identities. |

The inspection service does not own a second registry, allocator, budget or
pressure state machine. It projects ADR-027 registry and ADR-034 ledger revisions.
An inspector cannot release, evict, resize, rename, reconstruct or reprioritize a
resource through the read contract. Future mutation commands must use the owning
typed operation and permission/safe-point model; they cannot mutate a snapshot.

ADR-041 events report snapshot request failure, availability transitions,
truncation policy changes and collection recovery. They do not carry one event per
allocation or resource. Metrics and snapshots never determine admission,
residency, quality fallback or eviction.

### 2. Aggregate metrics preserve accounting boundaries

The frontend publishes pre-registered low-cardinality instruments from ledger
revisions. The canonical set includes:

| Metric | Kind/unit | Meaning |
|---|---|---|
| `gpu.memory.reserved` | Gauge/bytes | Admitted but not yet committed renderer backing. |
| `gpu.memory.committed` | Gauge/bytes | Whole charged backing capacity. |
| `gpu.memory.live_payload` | Gauge/bytes | Logical live payload within committed backing. |
| `gpu.memory.retiring` | Gauge/bytes | Committed capacity still retained by retirement. |
| `gpu.memory.reusable_slack` | Gauge/bytes | Charged reusable space, not released device capacity. |
| `gpu.memory.budget` | Gauge/bytes | Effective configured ledger cap for the accounting domain. |
| `gpu.memory.native_usage` | Gauge/bytes | Optional native observation with explicit provenance. |
| `gpu.memory.native_budget` | Gauge/bytes | Optional native budget or working-set observation, not an admission grant. |
| `gpu.memory.pressure` | Gauge/ratio | `(committed + unallocated reservations) / configured hard cap`; it may exceed `1.0` after a cap reduction. |
| `renderer.resource.count` | Gauge/count | Current logical generations by finite class and state. |

`committed` is not added to `live_payload`, `retiring` or `reusable_slack`; those
are overlapping breakdowns of charged capacity as defined by ADR-034. A resource
referencing a suballocation does not add its payload to the whole block again.
UMA mappings are not counted once as CPU and again as GPU physical memory. Process
resident/committed memory, renderer CPU metadata and GPU backing remain separately
labeled accounting boundaries.

Dimensions are fixed enumerations such as accounting domain, pool class, resource
class, lifecycle state, observation provenance and availability. Owner IDs,
resource generations, allocation IDs, asset IDs, labels, paths and native heap
indices are forbidden metric dimensions. If a backend cannot produce a qualified
native observation, the corresponding metric availability is unsupported or
temporarily unavailable; its value is not zero and the ledger metrics remain
available.

Metric publication is coalesced from immutable revisions and never scans every
resource per sample. Pressure metrics describe ADR-034 state after the decision;
no renderer path reads `MetricsStore` to make a pressure decision.

### 3. Snapshot identity and provenance are backend-neutral

One successful request returns this conceptual envelope:

```cpp
struct RendererMemoryInspectionSnapshot {
    RendererMemoryInspectionSnapshotId id;
    RendererMemoryInspectionSchemaVersion schema;
    RendererInstanceId renderer;
    RendererGeneration rendererGeneration;
    DeviceGeneration device;
    EffectiveCapabilitiesRevision capabilities;
    RenderResourceRegistryRevision registryRevision;
    GpuMemoryLedgerRevision ledgerRevision;
    SystemTimePoint capturedAtUtc;
    MonotonicTimePoint capturedAtMonotonic;
    RendererMemoryInspectionCoverage coverage;
    RendererMemoryAggregateSnapshot aggregates;
    RendererMemoryInspectionPageManifest pages;
};

struct RendererMemoryInspectionPage {
    RendererMemoryInspectionSnapshotId snapshot;
    RendererMemoryInspectionRecordClass recordClass;
    RendererMemoryInspectionPageCursor next;
    OwnedImmutableRecordArray records;
};
```

The snapshot header contains no borrowed range into service storage. `ReadPage`
returns an owned immutable page value whose bounded record array keeps its backing
alive independently of projection-page recycling. A page cursor is identity and
position only; it is never a pointer. Snapshot expiry rejects new page reads but
cannot invalidate a page value already returned to a consumer. Page-result storage
is capacity-admitted and released by its owning value. The page is move-only and
its internal ownership token returns storage on destruction; copying records
requires an explicit caller-owned copy outside this service. One consumer may hold
four page values by default and at most 16. Further reads receive backpressure.
No public raw span, pointer or iterator may outlive its owning page value.

The snapshot carries one internally consistent registry/ledger revision pair or
fails with `InspectionRevisionUnavailable`. UTC and process-monotonic capture time
are sampled as one pair at the safe point; monotonic time owns in-session ordering,
while UTC supports an explicitly exported snapshot's session correlation. The
service never combines a current resource table with an older allocation table
and labels the result complete. Native observations retain their own sample
revision/time and maximum age; their presence does not change the snapshot's
ledger revision.

`coverage` states `Complete` or `Partial`, requested and available detail levels,
total-known and returned counts for every record class, applied filters, omitted
stable-identity intervals, truncation reason and encoded bytes. `Complete` is
valid only when every record matching the request at the captured revision is in
the materialized snapshot. Native-observation availability remains a field of the
relevant pool/aggregate record and does not by itself make ledger coverage partial.

Public inspection identities are generation-scoped Horo values:

- `GpuMemoryPoolId` identifies one charged logical pool in one device generation;
- `GpuAllocationId` identifies one charged backing block or dedicated allocation;
- typed ADR-027 owner/slot/generation values identify resident resources,
  including [ADR-039](039-ray-tracing-capability-and-abstraction.md)
  `BottomLevelAsHandle` and `TopLevelAsHandle`;
- ADR-039 AS scratch, result, compaction-overlap and shader-table allocations
  appear as `GpuAllocationSnapshot` records in the same ADR-034 reservation
  classes (they are not omitted because they are ray work); and
- `RenderResourceOwnerScopeId` identifies the admitted host, world, editor or
  service scope without becoming resource ownership itself.

Stable identity order is the lexicographic unsigned tuple
`(resourceClassOrdinal, ownerId, slot, generation)` within each record class.
Omitted coverage intervals are contiguous ranges in that order. ADR-039 AS
handles use the same owner/slot/generation comparison inside their class. The
order is fixed for a renderer generation and does not use native addresses.

None is serialized as durable asset identity or reused after renderer/device
replacement. Native pointers, object handles, GPU virtual addresses, Vulkan
memory-type indices, D3D12 heap pointers, Metal object identities and OpenGL names
never cross the adapter. A backend may provide bounded normalized facts such as
memory locality, mapping/access class, dedicated/suballocated realization and
estimated versus measured provenance.

### 4. Allocation and resource records remain distinct

`GpuMemoryPoolSnapshot` reports accounting domain, pool class, configured cap,
reserved/committed/live/retiring/slack totals, pressure state, allocation count,
fragmentation information when qualified and optional native observations.
Fragmentation carries a named method and semantic revision; unknown or
incomparable native allocator values are unavailable rather than normalized into
a fabricated common percentage.

`GpuAllocationSnapshot` reports its Horo allocation ID, pool ID, owner scope,
whole charged bytes, used payload bytes, reusable slack, retiring bytes,
dedicated/suballocated/aliasable class, creation sequence, last accepted-use
sequence and retirement eligibility. It may contain bounded resource-region
references expressed as offsets and sizes relative to the Horo allocation. It
does not expose a native address or imply that offset is stable outside the
snapshot. Payload, slack and retiring values are labeled potentially overlapping
views of the whole charge and are never presented as independently summable bytes.

`RenderResourceSnapshot` reports:

- the exact typed ADR-027 identity, resource class and lifecycle state;
- an immutable bounded structural descriptor summary and descriptor fingerprint;
- owner scope and optional recovery-policy/residency-key presence, never source
  paths or retained contents;
- logical payload bytes and allocation-region references where the backend can
  establish them;
- creation, ready, last accepted-use and retirement-request sequences when
  applicable;
- direct dependency/pin counts and bounded exact dependency identities when the
  requested detail level admits them; and
- bounded registered diagnostic label IDs or redacted bounded fallback labels
  when build/privacy policy permits them.

Descriptor fingerprints help correlate equal structures but are not resource
identity. A resource may reference several allocation regions, and one allocation
may retain several resources. Transient aliases may share one region only when
their graph lifetime proof permits it; records include the graph/plan generation
and non-overlapping lifetime ranges needed to explain that sharing. The inspector
does not add every resource payload to derive committed bytes.

Missing sequence/time fields mean the transition has not happened or was not
retained by the selected detail policy. A resource absent from a partial snapshot
is not inferred to be retired. Failed generations follow ADR-027 operation-result
ownership and may appear only while their bounded completion/retirement record is
still retained.

### 5. Requests, materialization and paging are finite

Inspection is an explicit operation, not an always-on per-frame dump:

```cpp
struct RendererMemoryInspectionRequest {
    RendererMemoryInspectionDetail detail;
    RendererMemoryInspectionFilters filters;
    std::uint32_t maximumPools;
    std::uint32_t maximumAllocations;
    std::uint32_t maximumResources;
    ByteCount maximumEncodedBytes;
    Duration maximumRetention;
};
```

Filters use finite typed sets for pool class, owner category, resource class and
lifecycle state plus optional exact Horo identities. Arbitrary text predicates,
regexes, source paths and native values are not evaluated on the renderer owner
thread. `Summary`, `Allocations`, `Resources` and `Dependencies` are explicit
detail levels; unavailable detail returns a typed result or a partial coverage
record according to the request's declared policy.

Defaults admit 64 pools, 1,024 allocations, 4,096 resources and an 8 MiB encoded
snapshot. Hard limits are 256 pools, 16,384 allocations, 65,536 resources and
32 MiB. Snapshot retention defaults to 30 seconds and cannot exceed five minutes.
Product profiles may lower these values. Checked arithmetic validates every count,
offset and byte estimate before materialization. Limits do not grow at runtime.

Aggregate metrics consume already-maintained ADR-034 ledger revisions and require
no per-resource inspection mirror. Detailed projection is completely inactive
until a profile-allowed inspection request successfully reserves a session. A
Shipping profile that disables detail maintains no detailed inspection records or
mutation journal.

An admitted session records its starting registry/ledger revisions, incrementally
copies entries in stable Horo identity order at subsequent
[ADR-018](018-command-registration-permissions-threading-and-packaged-build-policy.md)
`CommandThreadPolicy::RenderSafePoint`s and
journals only the mutations that occur while that session is arming. Each safe
point visits at most 512 records by default and 4,096 at the hard limit; there is
no catch-up burst. At a final safe point it applies the bounded journal and seals
one current consistent revision pair. The journal defaults to 8,192 mutations and
has a hard limit of 65,536.

Byte/count limits on the **request** (`maximumResources`, `maximumEncodedBytes`,
page size) may return `Partial` when the caller allowed partial results: the
revision pair is consistent, some identity ranges are omitted. Journal/churn
overflow during **arming** always fails with `InspectionCapacityExceeded`. The
service cannot prove a consistent revision pair, so it must not emit Partial of
an inconsistent capture. Rendering and authoritative resource operations continue
unchanged. No mutation fails merely to satisfy an inspector.

Session projection pages, mutation journal, snapshot storage, owned page results,
page cursors and worker queues are charged to the **inspection** slice of the host
diagnostics CPU-memory envelope before admission.
[ADR-042](042-cpu-gpu-timestamps-and-pipeline-statistics.md) query/readback
storage is a sibling slice of the same envelope, independently admitted. One
session cannot steal the other's reserved bytes. Combined occupancy still cannot
exceed the host diagnostics CPU envelope; if remaining room is insufficient, the
new session is rejected with `InspectionCapacityExceeded` and the armed
instrumentation plan is left unchanged. Failure to reserve inspection storage
rejects the request without weakening rendering or timestamp budgets. Optional
dependency/lifetime detail is armed with the session and capacity-admitted before
scanning; unavailable detail returns `InspectionUnsupportedDetail` rather than
introducing permanent shipping cost.

The final safe point seals a revision token and immutable projection pages, then
queues bounded materialization; it does not copy the complete registry in one
owner-thread refresh. The path does not call device idle, wait for GPU completion,
map resource contents or synchronously ask a driver to enumerate live objects.
Backend observations arrive through their existing bounded sampler/callback path
and are joined by revision/provenance. Encoding, filtering that does not affect
consistency, sorting for presentation and serialization may run on a cancellable
worker over owned immutable records.

A materialized snapshot may be read in bounded pages without touching live
renderer state. Page cursors contain snapshot ID, schema and position, expire with
the snapshot and reject foreign/stale use. Paging returns owned immutable page
values and does not recapture later state.
One page defaults to at most 256 records and 256 KiB encoded data; callers may
request less, and the hard limits are 1,024 records and 1 MiB. A single record that
cannot fit the hard encoded-page limit makes the request invalid rather than
splitting an identity across pages.
If a request exceeds a limit, the service returns `Partial` only when the caller
allowed partial results and reports total-known/returned counts, omitted classes,
byte limit and the omitted stable-identity intervals. Otherwise it returns
`InspectionCapacityExceeded`; it never silently truncates a complete result.

Only one detailed snapshot per `RenderFrontendId` (one frontend instance) is
materialized by default and at most two at the hard limit. That is not
process-wide, not per device, and not per view or GPU Scene. Multiple views
sharing one ADR-038 GPU Scene share one frontend and therefore one snapshot
that includes their GPU Scene and ADR-039 AS resources. A second admitted
preview/world frontend ([ADR-012](012-world-streaming-partition-authority-and-subsystem-boundaries.md)
non-overlapping host envelopes) has its own snapshot slots. Device generation is
a snapshot field, not the concurrency key. Further requests on the same frontend
receive bounded backpressure. Summary metrics continue independently and no
catch-up burst follows a skipped request.

### 6. Threading, cancellation and lifecycle are explicit

Registry/ledger revision capture occurs on the host-declared render-capable owner
thread. Producers submit owned bounded requests and receive an operation result;
they do not lock the registry or backend directly. Worker jobs follow ADR-010
cancellation and completion rules and capture no borrowed renderer references.

Cancellation before capture removes the request and journal. Cancellation after
capture suppresses publication and releases service-owned immutable storage; page
values already returned remain valid under their own bounded ownership. It does
not cancel GPU work or alter resource lifetime. Result queues and concurrently
owned pages are bounded, so a forgotten inspector causes backpressure rather than
unbounded service retention.

Device/backend replacement closes admission, completes or cancels pending captures,
and preserves already-published snapshots only as explicitly stale historical
records carrying their source generations. They can never be queried as current.
Shutdown removes callbacks on their owner threads, rejects new requests, cancels
workers, expires page leases and releases snapshot storage. Repeated shutdown and
partial initialization are safe. No shutdown path waits for a UI consumer.

### 7. Failure and availability remain typed

Configuration/request failures distinguish at least `InspectionDisabledByProfile`,
`InspectionUnsupportedDetail`, `InspectionInvalidFilter`,
`InspectionCapacityExceeded`, `InspectionBusy` and
`InspectionRevisionUnavailable`. Runtime results distinguish `Valid`, `Partial`,
`Cancelled`, `StaleGeneration`, `DeviceLost` and `EncodingFailed`.

Metric sampler availability additionally distinguishes missing native telemetry,
stale observation and sampler failure. A native OOM, budget denial, resource
creation failure and device loss retain the ADR-027/034 operation errors; an
inspection failure cannot replace or reinterpret them. Optional snapshot failure
does not change rendering. Required developer tooling reports failure to its
caller without selecting another backend or weakening detail silently.

ADR-041 emits one correlated bounded event for request failure or availability
state transition and aggregates repetition. Per-resource records, labels and byte
counts are snapshot payload, not diagnostic fields. Snapshot IDs may correlate a
result with logs/profiler captures but never become metric dimensions.

### 8. Privacy, export and backend parity are enforced

Snapshots are local, ephemeral and developer-facing by default. They contain no
resource contents, screenshots, shader source, upload/readback bytes, unrestricted
paths, asset names, native labels, pointers or driver dumps. Registered labels are
subject to build-profile allowlists, redaction and bounded length. Shipping may
disable detailed snapshots while retaining required aggregate ledger metrics.

Export is an application/Observability operation with explicit user intent,
allowlisted fields, preview, size limit and atomic finalization. Backends perform
no file or network export. External tools cannot attach through this contract.

OpenGL, Metal, Vulkan and D3D12 adapters must project the same ledger/resource
semantics and mark native facts with provenance. Lack of native budget or
fragmentation telemetry does not disable registry/ledger inspection. Null executes
the same request, limit, paging, cancellation, generation and truncation contracts
using injected allocations; it reports synthetic provenance and makes no hardware
capacity, fragmentation or performance claim.

## Migration And Verification

The old generic `gpu.memory.*` wording in the Observability contract migrates to
the canonical ledger-versus-native metric split above. Existing allocator debug
dumps remain backend-private until adapted; they are not parsed into Horo records.
RND-017.9 consumes immutable snapshots and pages and owns no collection loop.
ADR-034 remains the only budget/pressure authority and ADR-027 remains the only
resident identity/lifecycle authority.

Tests must cover:

- aggregate accounting without committed/payload/slack/retiring double counting,
  UMA overlap and separate discrete accounting domains;
- exact-cap and checked-arithmetic request validation, default/hard record and byte
  limits, deterministic partial coverage, journal-churn always failing rather than
  Partial, and complete-result rejection;
- ADR-039 BLAS/TLAS identities and AS scratch/result allocations in the same
  snapshot;
- lexicographic `(class, owner, slot, generation)` omitted intervals;
- inspection vs ADR-042 sibling CPU-memory slices under one envelope;
- stable owner/slot/generation, pool/allocation identities, stale/foreign cursors,
  device replacement and no native handle/address leakage;
- dedicated, suballocated, shared, retiring and transient-aliased backing with
  resource-to-region relationships and qualified fragmentation provenance;
- immutable consistent revision pairs under concurrent creation/release, delayed
  native samples, bounded arming journals and no recapture during page traversal;
- no detailed projection or mutation-journal cost before an allowed inspection
  session, including Shipping-disabled detail;
- no GPU wait, device-idle, content mapping, live-driver enumeration or unbounded
  render-thread filtering during capture;
- queue saturation, cancellation before/after capture, snapshot expiry with an
  already-returned owned page, bounded retention, partial initialization and
  repeated shutdown;
- typed unsupported/busy/capacity/revision/device-loss outcomes, ADR-041 bounded
  aggregation and no missing-to-zero conversion;
- metric descriptor cardinality, availability revisions, redaction, export size
  and atomic publication; and
- deterministic Null fixtures plus native backend parity fixtures that separate
  ledger truth from observed/estimated hardware facts.

## Consequences

Tools can explain renderer memory pressure and resource lifetime through stable
Horo identities without owning collection or exposing native objects. Aggregate
metrics stay cheap and bounded, while detailed records are explicit, immutable,
generation-aware and honest about partial or unavailable data. The cost is
revisioned projection storage, finite snapshot materialization, per-backend
provenance adapters and extra registry/ledger bookkeeping for qualified
relationships and lifetime sequences.

## Rejected Alternatives

| Option | Decision and reason |
|---|---|
| Let the inspector enumerate native objects directly | Rejected: leaks API identity, races backend lifetime and cannot provide parity. |
| Emit one metric or diagnostic event per resource | Rejected: creates unbounded cardinality/work and duplicates snapshot retention. |
| Treat resource payload totals as physical GPU usage | Rejected: double-counts pooled, shared and aliased backing and ignores whole-block charges. |
| Expose pointers, GPU addresses or native heap indices for correlation | Rejected: unsafe, backend-specific and invalid across recreation. |
| Build every UI refresh from the live registry | Rejected: introduces owner-thread scans, inconsistent revisions and UI-controlled retention. |
| Silently truncate a requested complete snapshot | Rejected: absence could be misread as retirement; partial coverage must be explicit. |
| Return Partial after journal churn overflow | Rejected: Partial requires a consistent revision pair; arming overflow cannot prove that. |
| Omit ADR-039 AS from resource/allocation snapshots | Rejected: AS result/scratch is ADR-034 charged backing and must be inspectable. |
| Share one mutable diagnostics CPU pool between ADR-042 queries and inspection | Rejected: sibling slices of one envelope; independently admitted, jointly capped. |
| Keep snapshots forever for historical comparison | Rejected: resource cardinality and labels require bounded retention; explicit export owns persistence. |
| Use observed native budget as the renderer cap | Rejected: observation may be stale, estimated or unavailable; ADR-034 ledger admission remains authoritative. |
| Make Null report zero hardware use as real support | Rejected: zero is a valid measurement and Null cannot qualify hardware behavior. |
