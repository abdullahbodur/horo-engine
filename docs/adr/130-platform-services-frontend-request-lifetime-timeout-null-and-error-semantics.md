# ADR-130: Platform Services Frontend, Request Lifetime, Timeout, Null and Error Semantics

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: Platform-services frontend request ownership, admission, state machine, dropped handles, cancellation/timeout races, callback dispatch, capability absence, Null behavior and provider-error normalization
- **Issue**: [PLS-001.1](https://github.com/abdullahbodur/horo-engine/issues/1874)
- **Jira**: [HORO-1830](https://horo-engine.atlassian.net/browse/HORO-1830)
- **Related**: [ADR-008](008-error-model-exception-boundary-and-registry.md), [ADR-018](018-command-registration-permissions-threading-and-packaged-build-policy.md), [ADR-056](056-external-editor-ui-boundary.md), [ADR-060](060-release-domain-model-and-state-machine.md), [ADR-113](113-local-storage-user-profile-and-slot-ownership.md), [ADR-115](115-cloud-save-authority-revision-and-conflict-policy.md), [ADR-116](116-save-data-threat-model-and-trust-policy.md)
- **Normative documents**: [Platform Services Architecture](../architecture/runtime/platform-services-architecture.md), [Error and Diagnostics Architecture](../architecture/foundation/error-and-diagnostics.md), [Internal Module Descriptor](../architecture/foundation/internal-module-descriptor.md)

## Context

The Platform Services architecture intends one backend-neutral asynchronous frontend,
but its examples define contradictory semantics. `OnComplete` may run synchronously
for an already-finished request even though provider completions must be dispatched on
an engine-controlled thread. It does not say whether dropping a returned handle
cancels, detaches or destroys provider work, nor which owner retains terminal results.

Timeout is described as generic `platform_error` while the error list also exposes a
distinct `timeout`. Capability availability has two competing sources of truth: bool
fields and nullable service pointers. The Null backend reports no capabilities and
returns `not_supported` for reads, yet accepts and silently discards writes. A caller
therefore cannot distinguish a real accepted mutation from deliberate absence.

Later achievement, stat, leaderboard, cloud, identity and offline-queue decisions need
one request lifecycle and error rule. This decision establishes that foundation. It
does not decide durable offline queue ownership, service-specific idempotency, user
identity or proprietary SDK packaging except where they constrain request semantics.

## Decision

### 1. The frontend owns every admitted request record

`PlatformServicesFrontend` is the sole owner of frontend request identity, mutable
state, deadline, cancellation source, terminal result, observer list and bounded
retention. A provider owns only its native operation and returns completion evidence
through a generation-checked adapter queue. A `PlatformRequestHandle<T>` is a cheap
typed reference to `PlatformRequestId` plus the frontend/session generation; it never
owns or exposes a provider object.

Submission has an explicit admission result:

```cpp
template <typename T>
using PlatformSubmitResult = Result<PlatformRequestHandle<T>>;

PlatformSubmitResult<AchievementUnlockResult>
PlatformServicesFrontend::UnlockAchievement(AchievementId id);
```

Validation, permission, frontend lifecycle, capability, session and bounded-capacity
checks happen before admission. Failure returns `Result` with no request ID, provider
call, callback or partial queue record. Success atomically creates the request record,
captures policy/provider/session generations and deadline, then returns its handle.

Dropping or destroying the last handle does not cancel, detach provider ownership or
erase an admitted write. The frontend continues the operation to a terminal record and
normal provider retirement. This prevents UI lifetime and temporary C++ scope from
changing remote side effects. Cancellation requires an explicit `Cancel(requestId)` or
owner shutdown policy.

Terminal records are retained under a finite count/time policy so late query or
subscription can observe the outcome. Expiry removes only a terminal observation
record after provider/callback retirement; it never rewrites the remote outcome.
Querying an expired or wrong-generation ID returns `platform.request.expired` or
`platform.request.stale`, not a fabricated failure result for that old operation.

### 2. One exhaustive state machine governs every service

```cpp
enum class PlatformRequestState : uint8_t {
    Queued,
    Running,
    Cancelling,
    Succeeded,
    Failed,
    Cancelled,
    TimedOut
};
```

| From | Legal next states |
|---|---|
| `Queued` | `Running`, `Cancelling`, `Failed`, `TimedOut` |
| `Running` | `Cancelling`, `Succeeded`, `Failed`, `TimedOut` |
| `Cancelling` | `Succeeded`, `Failed`, `Cancelled`, `TimedOut` |
| terminal states | none |

`Succeeded`, `Failed`, `Cancelled` and `TimedOut` are terminal. A successful terminal
record contains exactly one immutable `T`; each other terminal record contains exactly
one typed `Error` whose code agrees with the state. Non-terminal records contain no
terminal result. State and result publish atomically once.

The frontend serializes state mutations on its declared owner lane. Provider SDK
threads may only enqueue bounded completion evidence carrying request, provider,
session and operation generations. They never mutate records, invoke user callbacks or
hold frontend locks while entering engine code.

Admission failure is not a synthetic `Failed` request because no request was accepted.
An accepted request may move directly from `Queued` to `Failed` if provider submission
fails after the record is published. Coalescing or durable replay may have a separate
operation identity, but each caller-visible request still owns one terminal outcome.

### 3. Cancellation is explicit, best-effort and idempotent

`Cancel()` posts one cancellation intent to the frontend owner and never invokes a
provider or callback inline. Repeated intents for the same current request are
idempotent. An accepted intent moves `Queued` or `Running` to `Cancelling`, closes new
retries and asks the provider adapter to abort when supported.

Cancellation intent is not terminal acknowledgement. From `Cancelling`:

- provider acknowledgement with no committed operation publishes `Cancelled` and
  `platform.request.cancelled`;
- an eligible provider success already committed or completing despite best-effort
  cancellation may publish `Succeeded`;
- a provider-reported failure may publish `Failed`; and
- deadline expiry publishes `TimedOut` if no eligible completion won first.

The immutable terminal record includes whether cancellation was requested and the
provider's bounded cancellation/commit evidence. `Cancelled` never promises that a
remote mutation was rolled back unless the service-specific contract provides that
guarantee. Callers determine behavior from terminal state/code, not a separate
`WasCancelled` boolean that can contradict the result.

### 4. Timeout is a frontend-owned terminal result

Every admitted request captures a finite monotonic deadline from the selected service
policy. At the owner boundary, valid provider completion evidence observed no later
than that deadline is processed before deadline evaluation. If no such evidence can
publish, the frontend atomically commits `TimedOut` with
`platform.request.timed_out`, requests best-effort provider cancellation and closes
caller-visible completion.

Timeout is not `platform.provider.failed`, `offline` or `Cancelled`. Later provider
completion cannot replace the terminal result. It is consumed only for resource/
idempotency reconciliation and reported as late evidence. Native provider work and
borrowed buffers remain alive until the adapter acknowledges retirement; terminal
publication does not prove physical cancellation.

Retry attempts and backoff fit inside the original request deadline unless a later
service-specific durable operation explicitly creates a new request. A retry does not
reset the deadline, request ID or exactly-once terminal slot. A zero, infinite,
overflowing or wall-clock-derived deadline is invalid policy.

### 5. Completion callbacks are deferred observations

The authoritative API is immutable request snapshot query plus optional subscription.
Registering an observer returns a move-only RAII `PlatformRequestSubscription`. Each
subscription receives at most one terminal notification for its request. Destroying
the subscription prevents future invocation but does not cancel the request. Dropping
the request handle has the same non-cancelling behavior.

Callbacks never run inline from submission, subscription registration, `Cancel`,
provider callbacks, queue drain, state mutation or while a frontend/provider lock is
held. Terminal state is committed first; the frontend posts notification to the
request's declared engine executor for a later dispatch turn. Registering after a
retained terminal result schedules the same deferred delivery rather than invoking
synchronously.

A callback receives immutable request ID and terminal snapshot. Re-entering the
frontend from that later callback may enqueue a new request/cancel/subscription, but it
cannot mutate the callback's terminal record or recursively drain completion. Callback
exceptions are caught at the engine executor boundary under ADR-008, converted to the
subscriber owner's error/diagnostic path and never alter the platform request outcome.

Progress/session events are non-authoritative bounded observations with revision/gap
semantics. Consumers query the current snapshot after a gap. They cannot stand in for
the terminal record.

### 6. Capability truth has one typed source

Composition validates one immutable `PlatformServiceCapabilitySnapshot` for the
selected provider generation. Each service has a typed availability entry containing
support level, limits, required session/consent conditions and provider binding ID.
There is no independent bool plus nullable-interface combination.

Provider binding lookup is private to the validated frontend composition. A capability
marked available must have exactly one compatible bound service; missing, duplicate or
mismatched binding fails composition. A capability marked unavailable has no callable
binding and carries one reason such as `NoProviderSelected`, `NullProviderSelected`,
`ServiceUnsupported`, `HostPolicyDenied` or `ProviderInitializationFailed`.

Dynamic sign-in, connectivity, consent and rate state are operation preconditions or
session snapshots, not changes that forge installed capability. Provider reload or
session replacement creates a new generation. Requests retain the generation captured
at admission; stale completion evidence cannot publish into a replacement frontend.

Public callers see only frontend capabilities and Horo types. They never test provider
pointers, backend names or SDK flags and never route around the frontend.

### 7. Null is fail-closed and never acknowledges a write

`PlatformServicesNull` is an explicit valid composition for headless, tests and
products without remote platform services. Its capability snapshot marks every remote
service unavailable with `NullProviderSelected`. Every read and write fails admission
with `platform.provider.null`; no request record, offline entry, idempotency record or
provider mutation is created.

Null never accepts and silently discards achievements, stats, scores, cloud mutations
or presence. Such behavior would make success mean two incompatible things and could
hide missing product composition. Tests that require successful or scripted async
behavior use `PlatformServicesMock`, which advertises exactly the capabilities it
implements and follows the same state/callback/deadline contract.

An application may explicitly suppress an optional cosmetic presence intent before
submission. That application policy is not a successful Null write and remains
observable as suppression at its owner boundary.

### 8. Errors preserve frontend and provider responsibility

All failures use ADR-008 `ErrorCode`/`Result`; message or native SDK text is never the
branching contract. Stable frontend codes include:

| Code | Owner/meaning |
|---|---|
| `platform.frontend.unavailable` | Frontend absent, starting, stopping or generation closed before admission |
| `platform.capability.unavailable` | Selected real provider/host policy does not expose the required service |
| `platform.provider.null` | Explicit Null composition; read/write not accepted |
| `platform.request.capacity_exceeded` | Bounded request/result/subscription capacity unavailable before admission |
| `platform.request.cancelled` | Accepted request reached acknowledged cancellation terminal state |
| `platform.request.timed_out` | Frontend deadline reached before eligible completion |
| `platform.request.expired` | Terminal observation retention ended |
| `platform.request.stale` | Request/frontend/session/provider generation mismatch |
| `platform.provider.failed` | Valid provider operation failed after admission; normalized category and safe cause retained |

`platform.provider.failed` carries a typed normalized category: `Offline`,
`NotSignedIn`, `Forbidden`, `RateLimited`, `PreconditionFailed`, `QuotaExceeded`,
`InvalidResponse`, `TransientFailure` or `PermanentFailure`. Service-specific decisions
may refine categories with stable codes, but cannot remap cancellation, timeout, Null
or capability absence into provider failure.

The provider adapter maps native results once. It retains service/operation, request,
provider/session generation, retryability, retry-after or revision evidence where
applicable, plus bounded redacted native code as diagnostics/cause. It never leaks SDK
types, credentials, raw response bodies or user identifiers. GUI/CLI/MCP/gameplay
adapters translate the same Horo error rather than inventing local business outcomes.

### 9. Shutdown closes admission before draining requests

Frontend shutdown is explicit and idempotent:

1. close admission and publish a new frontend generation;
2. reject new submissions with `platform.frontend.unavailable`;
3. post cancellation for requests whose service policy permits it;
4. drain owner-queued completions and commit one terminal result per admitted request;
5. stop/cancel subscriptions and executor deliveries;
6. wait within the declared bound for provider acknowledgements and callback epochs;
7. retain dependencies when work cannot retire safely, reporting shutdown failure
   rather than freeing provider code or borrowed memory early; and
8. release terminal stores, bindings and provider generation in reverse ownership
   order after all leases retire.

Dropping application/UI handles during shutdown does not shorten this sequence. A late
native callback carries the closed generation, is consumed for adapter retirement and
cannot publish or invoke a user callback.

### 10. Existing examples migrate to this contract

The Platform Services architecture examples change as follows:

- `PlatformServiceRequest<T>` becomes an admitted `PlatformRequestHandle<T>` returned
  by `Result`; the frontend, not the handle, owns the record.
- `IsPending`/`IsComplete`/`WasCancelled` are replaced by one snapshot state and
  terminal variant.
- `OnComplete` never invokes synchronously; it becomes deferred subscription with a
  move-only token and immutable terminal snapshot.
- bool capabilities and nullable service getters become one validated typed capability
  snapshot plus private binding.
- frontend timeout maps only to `platform.request.timed_out`.
- Null rejects both reads and writes with `platform.provider.null`.
- provider-native failures map to `platform.provider.failed` plus normalized category;
  cancellation, timeout, capability absence and Null remain separate.

Later platform-service ADRs must reuse this ownership/state/error contract and may only
specialize payload, authority, trust, retry/idempotency and durable-queue behavior.

## Consequences

- Callers can distinguish admission failure, Null, capability absence, cancellation,
  timeout and provider failure without inspecting text or contradictory flags.
- UI/handle lifetime cannot cancel or falsely complete remote side effects.
- Terminal publication and callback behavior are exactly once, non-reentrant and safe
  across SDK threads, reload and shutdown.
- Null/headless builds fail closed, making missing platform-service composition visible.
- Frontend implementation needs a bounded request/result/subscription store, owner
  executor, generation fencing and provider-retirement bookkeeping.
- Dropped handles may leave useful work running; owners that no longer want it must
  explicitly cancel, and retained result limits must be sized and observed.
- Existing architecture examples and any prototypes using synchronous `OnComplete`,
  nullable services or successful Null writes require migration.

## Rejected Alternatives

### Let the request handle own and cancel provider work on destruction

Rejected because temporary UI/C++ lifetime would silently change remote mutations and
provider callbacks could outlive freed state. The frontend record owns the operation.

### Invoke late-registered completion callbacks synchronously

Rejected because behavior would depend on timing, permit recursion under caller locks
and violate the engine-controlled dispatch boundary.

### Represent timeout as cancellation or generic provider failure

Rejected because the frontend deadline, caller cancellation and provider failure have
different owners, retry evidence and diagnostics.

### Keep both capability flags and nullable service pointers

Rejected because disagreement is representable and forces callers to invent routing.
Composition validates one typed availability snapshot and private binding.

### Treat Null writes as successful no-ops

Rejected because achievements, stats, scores, cloud and presence would appear accepted
without any provider effect. Optional intent may be suppressed explicitly by its owner;
the frontend cannot fabricate success.
