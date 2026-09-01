# ADR-041: Backend-Neutral Renderer Diagnostics Model

- **Status**: Proposed
- **Date**: 2026-09-01
- **Supersedes**: None
- **Scope**: Renderer diagnostic identity, context, ingestion, correlation and retention
- **Issue**: [RND-017.1](https://github.com/abdullahbodur/horo-engine/issues/433)
- **Jira**: [HORO-433](https://horo-engine.atlassian.net/browse/HORO-433)
- **Normative documents**: [Error And Diagnostics](../architecture/foundation/error-and-diagnostics.md), [Observability Architecture](../architecture/observability/observability.md), [Logging, Context, And Diagnostics](../architecture/observability/observability-logging.md), [Rendering Architecture](../architecture/runtime/rendering-architecture.md)

## Context

Renderer failures currently use typed results and general structured logs, while
capability, backend validation, graph execution, presentation, driver and optional
provider decisions each describe diagnostic context independently. A native API
callback may also arrive on an API-owned thread with native severity, source and
message identifiers that do not match Horo's error or logging model.

Without one renderer projection, backends can leak native enums or handles,
frontend code can branch on message strings, equivalent failures can differ by
API, and repeated per-frame warnings can produce unbounded work. Conversely, a
second renderer-owned logging store would duplicate `ObservabilityRuntime`, split
retention/privacy policy and make editor panels own collection accidentally.

This ADR defines the backend-neutral event and ingestion seam. It does not define
timestamp queries, pipeline statistics, memory inspection, native debug-layer
setup, driver workarounds, external graphics capture, crash bundles, inspector UI,
reference images, benchmark thresholds or release qualification. Those consumers
and producers use this model in later RND-017 deliveries.

## Decision

### 1. Results, events and observability signals remain distinct

Horo uses four related renderer signals:

| Signal | Authority |
|---|---|
| `Result<T, Error>` | The only caller-visible success/failure contract for a renderer operation. |
| `RendererDiagnosticEvent` | An immutable structured account of one renderer finding, decision, degradation, failure or recovery. It cannot change control flow. |
| Structured log | The retained/queryable projection owned by `ObservabilityRuntime` when admitted by bounded delivery policy. |
| Metric or profiler event | Bounded numeric history or explicitly armed timing/detail capture; never encoded as a diagnostic event per frame. |

A failed public operation returns an error in the `horo.render` or owning provider
domain. The owning boundary may submit exactly one correlated diagnostic event and
log projection; callers propagate the error without logging it repeatedly. A
consumer never parses an event, native message or log text to determine success.

Expected unsupported requests return a stable typed error such as
`render.operation.unsupported`. An optional route may follow only a declared
fallback edge. A recorded normal preference selection is `Info`; a fallback
caused by a failed preferred route is `Warning` and
records both route identities and the cause code. No failure becomes success
merely because an event was emitted.

### 2. One canonical event schema crosses renderer boundaries

The conceptual public value is:

```cpp
struct RendererDiagnosticEvent {
    uint32_t schemaVersion{1};
    DiagnosticCode code;
    std::optional<ErrorCode> relatedErrorCode;
    DiagnosticSeverity severity;
    RendererDiagnosticKind kind;
    RendererSubsystem subsystem;
    std::chrono::system_clock::time_point occurredAtUtc;
    std::chrono::steady_clock::time_point occurredAtMonotonic;
    RendererDiagnosticProducerSequence producerSequence;
    RendererDiagnosticContext context;
    RendererDiagnosticFields fields;
    RendererDiagnosticMessage message;
    std::optional<NativeDiagnosticIdentity> nativeIdentity;
};
```

`DiagnosticCode` is the existing Foundation identity registered in the owning
module's validated renderer diagnostic contribution. Its meaning cannot change
within a schema version. `relatedErrorCode` carries the exact existing Foundation
`ErrorCode` when the event supports a failed operation; it is absent for
non-failing findings.
Runtime sites use validated/interned handles rather than inventing strings. The
codes, not `message`, drive filtering, tests, deduplication and host presentation.

The emitter samples UTC and process monotonic occurrence time as one pair at the
source. UTC supports correlation with persistent/system logs across sessions;
monotonic time supplies in-session ordering, intervals and aggregate windows
without wall-clock jumps. The log projection preserves both values and never
substitutes later drain time for event occurrence time.

Each module declares its diagnostic identities before activation:

```cpp
struct RendererDiagnosticCodeDescriptor {
    DiagnosticCode code;
    ModuleId owner;
    DiagnosticSeverity defaultSeverity;
    RendererDiagnosticKind kind;
    RendererSubsystem subsystem;
    RendererDiagnosticRepetition repetition;
    RendererDiagnosticNativeAggregation nativeAggregation;
    std::span<const RendererDiagnosticFieldDescriptor> fields;
    DiagnosticPrivacyClass privacy;
    std::span<const ErrorCode> allowedRelatedErrors;
};
```

Composition rejects duplicate codes, foreign ownership, an invalid default
severity, unregistered field keys/types, an unbounded repetition policy or an
unregistered related error. It also rejects a native-message family without a
bounded native aggregation policy. Descriptor creation is inert metadata and
performs no sink registration, native callback setup or runtime mutation. Runtime
emission uses the validated handle derived during composition.

Canonical severities are `Info`, `Warning`, `Error` and `Fatal`, with these
meanings:

| Severity | Renderer meaning | Log projection |
|---|---|---|
| `Info` | Non-failing lifecycle, selection or recovery fact useful for diagnosis. | `Info` |
| `Warning` | Operation continued through an explicit degraded condition or recovery. | `Warn` |
| `Error` | The correlated requested operation failed, but the process or isolated runtime may continue. | `Error` |
| `Fatal` | The renderer instance or process cannot continue safely under its admitted contract. | `Critical` plus the existing emergency path. |

Severity does not terminate the process. Lifecycle ownership decides whether a
renderer instance is suspended, lost, shut down or the host terminates. Backend
native severities map through a validated adapter table; adapters cannot downgrade
Horo invariant or fatal codes. Unknown native severities map to the descriptor's
safe default and retain `unknown` as native metadata.

`RendererDiagnosticKind` is a small extensible classification: `Validation`,
`Availability`, `Selection`, `Lifecycle`, `Degradation`, `Failure`, `Recovery`,
`Compatibility` or `DataQuality`. `RendererSubsystem` identifies a stable Horo
owner such as frontend, backend, device, surface, presentation, graph, resource,
memory, shader, pipeline, raster, ray tracing or image provider. Neither field is
a native API enum, source filename or UI category.

The message is bounded developer-facing fallback text. It is never identity and
may be absent in compact hot-path submissions when the descriptor supplies a
summary. Fields are a bounded typed allowlist of booleans, integers, finite
numbers, durations, stable IDs and bounded safe strings. Version 1 admits at most
16 fields, a 1,024-byte message, 1,024 bytes for any one evidence string and 4 KiB
for the complete encoded event. Oversized native evidence is safely truncated and
marked; invalid producer-authored values reject submission. Collections, resource
contents, shader source, user input, unrestricted paths, environment dumps and
native pointer values are forbidden.

### 3. Context is complete, backend-neutral and generation-aware

`RendererDiagnosticContext` carries applicable values from these groups:

| Group | Context |
|---|---|
| Process | session, process role, build/configuration and product profile IDs inherited from observability context |
| Renderer | frontend instance, backend ID/version, renderer generation and effective capability snapshot revision |
| Device | stable Horo device identity, safe vendor/device/driver projection and device generation |
| Work | operation/job/request IDs, simulation tick, real render frame, real or synthetic presentation frame and graph execution IDs |
| Scope | optional surface, view, queue class, graph pass, resource, pipeline, provider, plan and history identities with generations |

Only applicable fields are present. Absence is explicit and never encoded as a
zero ID. A native callback that cannot be correlated to a frame/pass still carries
the renderer/device generation and native identity. A late callback retains the
generation that produced it; the router never relabels it as current.

Backend/device/profile fields describe evidence, not policy. Consumers cannot
infer a capability from GPU vendor, backend name or diagnostic presence. Safe
device identity follows renderer distribution/privacy policy: stable Horo IDs and
bounded product/driver facts are allowed; serial numbers, native handles and
unrestricted native object labels are not.

Logical operation context is captured at submission and follows queued work.
Backend worker/callback threads do not inherit stale thread-local context. Render
frame/pass/resource scopes derive immutable child context and restore the prior
binding after work completes. Context cardinality is acceptable for logs and
profiler correlation but does not automatically become metric dimensions.

### 4. The host supplies one bounded ingestion port

The process composition root creates `ObservabilityRuntime` before renderer
services and supplies a `RendererDiagnosticIngestPort` while composing
`RenderFrontend`. The frontend derives renderer/device/work context and lends
narrow generation-bound emitters to the selected backend and private providers.
Backends and providers do not create sinks, stores, files, telemetry clients or
process-global callbacks.

Submission may occur from the render owner, worker, driver callback or platform
callback thread. The port contract is:

- bounded non-blocking admission with no formatting, file I/O or arbitrary heap
  allocation on producer threads;
- fixed schema and field-count/string-size limits validated before admission;
- monotonically sequenced ingestion after acceptance, while producer-local order
  is retained through a producer sequence when global races exist;
- category/code gates before expensive message construction;
- no renderer lock acquisition by the observability drain path; and
- generation checks so shutdown or replacement rejects stale emitters safely.

The host drains accepted events into immutable structured log records at bounded
safe points or on its asynchronous observability worker. The port is not a data
bus. Editor/runtime views receive only coalesced store revision notifications and
query immutable ranges from the existing `StructuredLogStore` capability.

The default renderer ingestion queue holds 2,048 events and is configurable from
256 through a hard maximum of 16,384 before renderer composition. The default
aggregate table holds 512 fingerprints with a hard maximum of 4,096. The existing
emergency sink provides at least 64 preallocated renderer high-severity slots for
an active renderer composition. Product profiles may choose smaller values within
these ranges only when required high-severity capacity and startup/device-loss
tests still pass; runtime resizing is forbidden.

If normal admission is saturated, `Info` events may be dropped and `Warning`
events may be aggregated according to declared policy. Dropped counts, first/last
sequence and affected severity are summarized when capacity recovers. `Error` and
`Fatal` use the existing bounded emergency path and are never silently discarded.
Ingestion failure never converts a successful renderer operation to failure unless
an explicit audit contract requires delivery.

### 5. Repetition is aggregated without hiding state changes

Each code descriptor declares whether it is `OncePerGeneration`,
`AggregateWindow`, `EveryOccurrence` or `StateTransition`. An aggregate window
defaults to one second and must be between 100 milliseconds and 10 seconds. The
router computes a bounded fingerprint from code, renderer/device generation and
descriptor-approved scope keys. When a descriptor admits native evidence, its
policy declares whether normalized API family, source/type and stable numeric
message ID join the fingerprint. The generic
`render.backend.native_message_unknown` family must include API family and native
message ID so unrelated driver findings never collapse together. Raw message text
and other high-cardinality fields cannot enter the fingerprint. The fixed
aggregate-table budget still bounds adversarial native ID cardinality.

An aggregate stores first/latest monotonic time, first/latest sequence, occurrence
count and suppressed count. Capacity is fixed; overflow evicts the lowest-severity
oldest aggregate and increments an explicit overflow summary. State transitions
such as lost then recovered are separate events and cannot collapse into one
count. `Error` and `Fatal` operation results retain every authoritative error even
when supporting repeated native messages are aggregated.

Per-draw, per-resource and per-frame normal activity is not a diagnostic event.
It uses counters, histograms, sampled summaries or an explicitly armed profiler
channel. Debug labels and native marker scopes are bounded instrumentation owned by
RND-017.4, not event messages emitted on every command.

### 6. Native diagnostics are translated at private adapters

Vulkan validation, D3D12 debug/diagnostic queues, Metal validation, OpenGL debug
callbacks and third-party provider callbacks remain private to their owning
targets. An adapter maps native source/type/severity/message ID into a registered
Horo code, kind, subsystem and severity. `NativeDiagnosticIdentity` may retain a
bounded API family, numeric/stable native message ID and normalized source/type
for engineering correlation.

Raw native text is optional bounded evidence after redaction and path/handle
normalization. Unknown native messages use one registered
`render.backend.native_message_unknown` family with the native ID; they do not
dynamically register one Horo code per driver string. A native callback cannot
call UI code, mutate renderer policy, trigger fallback or synchronously flush a
file. Validation/debug layer enablement and product-profile availability remain
RND-017.5 policy.

Backend-originated events and equivalent frontend/Null validation use the same
Horo codes and semantic fields where the condition is equivalent. API-specific
evidence may differ, but host consumers do not need native enums to understand
unsupported, invalid, degraded, lost or recovered states.

### 7. Retention, privacy and export stay with observability

Renderer events have no independent persistent store. Their structured log
projection uses `renderer`, `renderer.frontend`, `renderer.<backend>`,
`renderer.graph`, `renderer.presentation` and similarly registered categories,
plus the process-wide sequence, session, rotation, retention and redaction rules.
The editor's bounded process-wide `StructuredLogStore` remains the query source;
renderer code cannot reserve unbounded rows or alter its overwrite policy.

Metrics retain bounded numeric renderer history. Profiler captures retain detailed
CPU/GPU timelines only during explicit sessions. Crash/device-loss bundles may
snapshot approved recent renderer events and manifests under RND-017.8, but do not
change normal store ownership or automatically upload data.

Shipping records omit source locations and private labels according to product
policy. Shader/resource contents, screenshots, capture payloads and raw driver
dumps require separately approved capture/export contracts. Diagnostic export is
user initiated, allowlisted, size-bounded, previewable and redacted. Baseline
renderer diagnostics perform no network access or vendor telemetry.

### 8. Lifecycle and failure behavior are explicit

Renderer composition validates diagnostic code/category descriptors, native
mapping tables, field schemas, queue/aggregate budgets and host availability
before backend initialization. Duplicate identities, invalid ownership or an
undersized required emergency path fail composition with typed errors. Optional
verbose native integration may be unavailable without weakening core typed
results or lifecycle diagnostics.

Backend/device replacement closes old emitters, stops new native callbacks,
drains or generation-tags already accepted events, unregisters native callbacks
on their owner thread, then destroys backend-native state. A late stale submission
is rejected and counted without dereferencing old renderer state. Shutdown is
idempotent after partial initialization and waits only within the observability
runtime's bounded flush policy; normal renderer threads never wait for all sinks.

Device loss first records the authoritative typed result and generation. Further
native chatter is aggregated. Recovery emits a distinct event linked to the old
and new device generations. Failure to persist or display diagnostics never causes
a hidden backend switch, retry, quality reduction or fabricated recovery.

## Rejected Alternatives

| Option | Decision and reason |
|---|---|
| Let each backend log native strings directly | Rejected: leaks API conventions, defeats stable filtering/testing and encourages control flow from text. |
| Create a renderer-only log file and event store | Rejected: duplicates process observability ownership, retention, privacy and UI query paths. |
| Use `RendererDiagnosticEvent` as the operation result | Rejected: asynchronous evidence and caller-visible success/failure have different ownership. |
| Publish every event on `EngineDataBus` | Rejected: creates per-record fan-out, hidden throughput coupling and UI-owned collection pressure. |
| Emit every draw, resource and frame as a diagnostic | Rejected: unbounded hot-path work belongs in metrics or explicit profiler captures. |
| Select fallback or recovery from severity/message text | Rejected: typed errors, lifecycle states and declared policy edges own behavior. |
| Store native handles, raw paths or unrestricted driver dumps | Rejected: unstable identity and privacy/security risk; adapters publish bounded safe evidence. |
| Drop high-severity events when the normal queue is full | Rejected: `Error` and `Fatal` require the bounded emergency path and explicit delivery status. |

## Migration And Verification

Existing renderer logs migrate by registering stable categories/codes and moving
searchable backend, device, profile, operation and generation values out of message
text into typed context/fields. Existing typed errors retain their identity; the
owning boundary adds one correlated event instead of duplicate logs at every
layer. Native callback adapters migrate only when their RND-017 implementation
ticket lands; until then absence is reported as unavailable diagnostics, not as
renderer capability loss.

| Delivery | Required implementation evidence |
|---|---|
| RND-017.2 / #434 | CPU/GPU timestamps and pipeline-statistic availability, calibration and frame/queue correlation use metrics/profiler signals rather than per-frame events. |
| RND-017.3 / #435 | Memory/resource snapshots use stable Horo identities, bounded queries and diagnostic correlation without native handles. |
| RND-017.4 / #436 | Command/pass markers and labels use bounded registered identities and profile-gated native adapters. |
| RND-017.5 / #437 | Native validation/debug callbacks map to registered codes/severity/context under explicit product policy. |
| RND-017.6 / #438 | Driver workaround decisions identify rule/evidence/effective capability generations without branching on messages. |
| RND-017.7 / #439 | External capture operations are explicit, profile-gated and correlated; tools do not become normal sinks. |
| RND-017.8 / #440 | Device-loss/crash bundles snapshot allowlisted bounded events with generation and privacy manifests. |
| RND-017.9 / #441 | Inspector UI queries immutable stores/snapshots and never owns collection, retention or backend callbacks. |
| RND-017.10 / #442 | Reference-image failures return typed results and correlate artifacts without embedding image data in events. |
| RND-017.11 / #443 | Benchmark/regression outcomes use metrics/artifacts plus stable summary diagnostics, not log parsing. |
| RND-017.12 / #444 | Packaged qualification records exact backend/device/profile/build evidence and unsupported paths through this model. |

Tests must cover:

- descriptor duplicate/domain/schema rejection and stable serialization;
- equivalent frontend, Null and native-backend conditions producing equal Horo
  codes, severity, subsystem and semantic fields;
- typed unsupported/failure results remaining authoritative with exactly one
  owning correlated event and no message parsing;
- complete optional context, explicit absence, generation retention and late
  stale callback rejection;
- multi-thread producer ordering, context capture/restoration, cancellation and
  no renderer-lock or sink wait on producer paths;
- category gates before formatting, field/string/count bounds, invalid finite
  values, redaction and native handle/path normalization;
- paired UTC/monotonic occurrence capture, persistent wall-clock correlation,
  monotonic ordering and no substitution of drain time;
- once, aggregate, every-occurrence and state-transition policies, including
  descriptor-selected native identity, separation of unknown native IDs, bounded
  aggregate overflow and recovery summaries;
- queue saturation with low-severity drops, warning aggregation and emergency
  delivery of error/fatal records;
- initialization failure, partial shutdown, callback unregistration, device loss,
  replacement, recovery and repeated shutdown;
- StructuredLogStore range/filter/revision behavior without per-record data-bus
  events, plus persistent JSONL schema/rotation compatibility; and
- shipping/profile gates, support-bundle allowlists and no baseline network or
  vendor telemetry.

## Consequences

Renderer findings now have stable backend-neutral identity, bounded context and
one retention path while typed results remain authoritative. Native callbacks can
be correlated without exposing SDK types, and repeated driver chatter cannot turn
into unbounded frame work. The cost is descriptor/mapping maintenance, bounded
ingestion and aggregation state, generation-aware emitters and explicit adapters
for each native diagnostics source. This ADR enables no native debug layer,
profiler, capture tool, inspector or telemetry upload by itself.
