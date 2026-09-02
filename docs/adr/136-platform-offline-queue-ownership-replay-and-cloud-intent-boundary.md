# ADR-136: Platform Offline Queue Ownership, Replay and Cloud Intent Boundary

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: Platform offline intent ownership, durable admission, ordering, coalescing, retry/reconciliation, expiry, per-subject partitioning, shutdown, and the caller-owned cloud intent boundary
- **Issue**: [PLS-007.1](https://github.com/abdullahbodur/horo-engine/issues/1892)
- **Jira**: [HORO-1848](https://horo-engine.atlassian.net/browse/HORO-1848)
- **Related**: [ADR-008](008-error-model-exception-boundary-and-registry.md), [ADR-010](010-job-waiting-and-operation-store-ownership.md), [ADR-113](113-local-storage-user-profile-and-slot-ownership.md), [ADR-115](115-cloud-save-authority-revision-and-conflict-policy.md), [ADR-130](130-platform-services-frontend-request-lifetime-timeout-null-and-error-semantics.md), [ADR-131](131-platform-services-closed-sdk-extension-abi-package-and-composition-boundary.md), [ADR-132](132-platform-services-project-salt-stable-id-tombstone-and-provider-mapping.md), [ADR-133](133-platform-progression-authority-trust-and-idempotency.md), [ADR-134](134-cloud-blob-transport-revision-precondition-and-offline-ownership.md), [ADR-135](135-platform-identity-session-generation-privacy-and-consent.md)
- **Normative documents**: [Platform Services Architecture](../architecture/runtime/platform-services-architecture.md), [Save Game and Persistence](../architecture/runtime/save-game-and-persistence.md), [Observability Logging](../architecture/observability/observability-logging.md)

## Context

Platform Services needs to preserve selected progression and presence intent while a
provider is offline, rate limited or temporarily unavailable. The existing architecture
identifies replay-safe progression algebra and reserves cloud upload/delete durability
for Save, but does not yet define the generic queue's durable acceptance boundary,
state machine, ordering, expiry, account partitioning or shutdown behavior.

Without one owner, a progression coordinator, frontend and provider adapter could each
persist the same mutation and replay it independently. Conversely, accepting an intent
before its queue record is durable creates an unreported crash-loss window. Removing a
record immediately after a provider callback creates the opposite ambiguity: the
remote effect may be committed while local completion was not durable.

Cloud synchronization has a different semantic owner. Save must retain archive bytes,
slot lineage, expected provider revision, conflict state and local publication leases.
Putting the same upload/delete in a generic FIFO would lose those invariants and could
publish an old generation after a newer local save or account switch.

This ADR assigns one durable owner per operation class and defines replay guarantees
and visible data-loss boundaries. PLS-007.2 will specify the physical queue schema and
atomic storage implementation; PLS-007.3 will implement mutation-ID allocation and
in-flight deduplication without changing the ownership decided here.

## Decision

### 1. Every durable intent has exactly one owner

| Operation class | Durable owner | Deliberate non-owner |
|---|---|---|
| Eligible achievement/stat/leaderboard logical mutation | Platform Services `PlatformOfflineQueue` | Gameplay, progression coordinator, frontend request store and provider adapter keep no second durable replay record |
| Opted-in presence desired state | `PlatformOfflineQueue`, as one expiring per-subject desired-state lane | UI, presence frontend and provider adapter do not persist a second copy |
| Cloud upload/delete and remote ambiguity | Save-owned `CloudSaveCoordinator` journal under ADR-115/134 | `PlatformOfflineQueue` never admits cloud mutation or archive bytes |
| Reads and queries, including cloud/friends/progression reads | No durable owner | They may receive bounded in-memory retry only |
| Provider-native request/callback | No Horo durable owner | ADR-130 frontend and provider package retain it only in memory through retirement |

The queue is hosted by Platform Services but stores service-owned logical intent, not
serialized public API calls or provider requests. The progression coordinator remains
the semantic authority for mutation algebra and logical outcome; the queue is its only
durable persistence/scheduling implementation. Presence policy owns desired-state
meaning; the same queue is its only optional durable store.

A component may retain an in-memory handle, cache or observer projection, but cannot
also journal a replayable copy. Sharing an atomic-file helper, retry calculator,
clock abstraction or transport adapter does not transfer ownership. Shared helpers are
stateless or operate on caller-owned storage and never discover or schedule records.

### 2. Durability is an explicit admission mode and receipt

Producers choose one declared delivery mode before the first provider attempt:

```cpp
enum class PlatformDeliveryMode : std::uint8_t {
    VolatileAttempt,
    DurableUntilTerminal,
};

enum class OfflineAdmissionState : std::uint8_t {
    NotQueued,
    DurableAccepted,
    JoinedExisting,
    Rejected,
};
```

`VolatileAttempt` uses ADR-130 in-memory request/retry only. Process exit, crash or
frontend shutdown may lose it, and no API/UI may label it queued or guaranteed.

`DurableUntilTerminal` first validates replay eligibility and atomically publishes the
queue record through the qualified durable storage adapter. Only after required file/
directory synchronization succeeds may it return `DurableAccepted` and schedule a
provider request. Storage failure, capacity exhaustion or unknown publication outcome
returns a typed admission failure and creates no provider attempt until storage truth
is reconciled. This write-before-submit rule closes the unreported crash window.

An exact duplicate canonical logical identity returns `JoinedExisting` and observes
the same durable outcome. A different payload reusing that identity fails conflict.
Dropping a receipt, closing UI or cancelling an ADR-130 attempt does not erase the
durable intent.

`DurableAccepted` promises only that Horo will retain and reconsider the exact intent
until a durable terminal disposition. It is not remote success, infinite retention,
availability, provider support or guaranteed eventual delivery.

### 3. Queue records contain canonical Horo intent, never live/native identity

The logical record contains a bounded versioned envelope equivalent to:

```cpp
struct PlatformOfflineIntent {
    PlatformOfflineIntentId identity;
    OfflineSubjectPartition partition;
    PlatformOfflineOperation operation;
    CanonicalOfflinePayload payload;
    PlatformStableRegistryFingerprint registry;
    ProgressionPolicyRevision progressionPolicy;
    PlatformConsentRequirements accessRequirements;
    PlatformReplayRequirements replayRequirements;
    OfflineOrderingKey ordering;
    OfflineExpiryPolicy expiry;
    OfflineAttemptPolicy attempts;
};
```

For progression, `identity` is the existing ADR-133 `ProgressionMutationId` and the
payload is the exact accepted mutation envelope. Presence uses a nonzero
`PresenceIntentId`, exact typed ADR-132 status/art IDs, bounded detail, desired
`Set`/`Clear` state and consent/retention policy. Queue insertion never allocates a new
progression mutation ID or changes a value/precondition.

`OfflineSubjectPartition` is a protected pseudonymous same-binding partition from
ADR-135. It is not a live `PlatformSubjectHandle`, raw provider account ID, display
name or provider request token. The record carries requirements needed to obtain a new
current subject capability and reauthorize later; it does not serialize credentials,
native objects, callbacks, archive bytes or unrestricted errors.

Records are provider-neutral. Provider mapping/capability generations are captured as
requirements/evidence, not native API payloads. Replay remaps the same Horo stable IDs
only after the current ADR-132 registry fingerprint and selected provider mapping are
proved compatible. A stale/unknown/tombstoned mapping blocks rather than rewriting the
durable envelope.

### 4. Replay eligibility is exhaustive by operation class

| Operation | Durable queue policy |
|---|---|
| `UnlockOnce` | Eligible only with qualified intrinsic repeated-unlock semantics |
| `SetProgressMaximum`, `SetStatMaximum`, `SetStatMinimum` | Eligible only with matching atomic monotonic provider/gateway semantics |
| `SubmitBestScore` | Eligible only with provider/gateway atomic best-score comparison using the registered ordering |
| `SetStatSnapshot`, `ReplaceScoreAtRevision` | Eligible with the exact conditional revision; precondition failure enters reconciliation, never unconditional retry |
| `AddStatOnce` or another non-intrinsic effect | Eligible only with durable provider/gateway deduplication of the original mutation ID |
| Presence `Set`/`Clear` desired state | Optionally eligible as one coalesced, expiring state lane when product purpose/retention consent and provider support permit |
| Progression, friends or cloud read/query | Never durable; stale reads are not future write intent |
| Cloud upload/delete | Never admitted; Save's coordinator journal owns it |

ADR-133 mutation algebra and qualified effective provider/gateway capability are the
authorities for progression eligibility. This queue cannot make an unsafe operation
safe, emulate atomicity with read-then-write or treat a locally persisted ID as remote
deduplication.

Presence durability is disabled by default. Enabling it requires an explicit
`PresencePublish` purpose, persistence/retention policy and current consent. It stores
only the latest desired state for that subject/policy lane and has a finite short
expiry. It is not presence history and cannot republish user text after consent,
session or product relevance expires.

### 5. One durable state machine survives every crash boundary

Queue state is exhaustive:

```cpp
enum class OfflineIntentState : std::uint8_t {
    Pending,
    Dispatching,
    Reconciling,
    Suspended,
    Succeeded,
    PermanentlyFailed,
    Expired,
    Superseded,
    Abandoned,
};
```

Only the queue storage transaction changes durable state. The ADR-130 request is an
in-memory attempt projection and cannot delete/change the row directly.

1. Admission durably writes `Pending` before any provider submit.
2. Scheduling captures a fresh subject/access/provider/policy snapshot and records the
   attempt generation before creating an ADR-130 request.
3. Known pre-commit transient failure returns to `Pending` with bounded backoff.
4. A possibly committed outcome becomes `Reconciling`; it never returns to a naïve
   “not attempted” state.
5. Confirmed remote success or a permanent/expired/superseded/abandoned disposition is
   committed durably before the live receipt reports terminal.
6. Checkpoint/compaction may later remove terminal rows only after required producer
   redelivery/deduplication retention and atomic checkpoint publication.

A crash before Step 1 produced no durable acceptance and no provider call. A crash
after Step 1 but before submit replays the pending record. A crash after remote commit
but before local terminal commit leaves the record eligible only for its declared
same-ID retry/reconciliation rule. A crash after durable terminal commit cannot replay
the effect even if physical compaction did not finish.

Corrupt, truncated, incompatible or publication-unknown queue storage is quarantined
and fails closed. Horo does not discard it, recreate an empty queue or report pending
work as success. Recovery exposes a bounded diagnostic and requires schema recovery or
explicit operator/product disposition.

### 6. Ordering is per subject and semantic lane, never global FIFO

There is no cross-subject or global provider completion order. Each record belongs to
one partition and `OfflineOrderingKey`:

- progression lanes use `(subject partition, definition ID, policy generation)`;
- presence uses `(subject partition, presence purpose/policy generation)`; and
- unrelated lanes may schedule concurrently within provider/service budgets.

Conditional progression operations serialize within their lane and reconcile the
exact expected revision before a successor can dispatch. Non-commutative operations
cannot overtake. Intrinsic monotonic/best operations may coalesce according to ADR-133
only when every individual mutation retains an outcome and the aggregate is
mathematically result preserving for every arrival order.

Presence is latest desired state, not an event log. A later `Set` replaces an older
unsent `Set`; `Clear` supersedes prior unsent publication; a later `Set` after `Clear`
is a new desired state. Coalescing is allowed only inside the exact subject/purpose/
access-policy lane. Once remote outcome is ambiguous, replacement cannot erase the
record until reconciliation establishes what was published.

Fair scheduling uses bounded per-partition and per-service concurrency plus a stable
round-robin/priority policy. One noisy account/definition cannot starve another. Queue
sequence proves only local lane order; it is not provider time, gameplay authority or
remote revision.

### 7. Replay always reauthorizes current capability and the same binding

Startup opens and validates queue storage without a provider call. Records remain
`Suspended` until the matching protected subject binding is active. Replay admission
then revalidates:

- exact same ADR-135 subject-binding partition and a new live subject handle;
- current session and `PlatformAccessPolicyRevision` with required purpose granted;
- product/profile/authority and ADR-132 registry/policy compatibility;
- selected provider mapping and effective operation capability; and
- payload lease/bounds, expiry, attempt budget and prior ambiguity state.

Sign-out/account switch first closes scheduling and invalidates the captured session.
The old partition remains suspended and cannot target the new current account.
Same-account reauthentication may resume only after binding proof and full
reauthorization. Consent revocation prevents dispatch/publication and applies the
service's retention/deletion policy; UI or debug tooling cannot resume it.

An `AuthorityServer` progression record is replayed only by a currently qualified
server authority/gateway with valid retained authority evidence. An offline client
cannot queue or later upgrade a locally manufactured server mutation.

### 8. Retry, backoff, expiry and capacity are bounded policy

Retry is driven by normalized provider state and engine scheduling events, not a busy
poll. Each record has finite attempt/backoff and expiry policy captured at admission.
Transient offline/network failure and a bounded `RateLimited` delay may reschedule;
forbidden, authority/access mismatch, invalid/tombstoned definition, unsupported
semantics, conflicting identity/payload and permanent provider failure do not.

Backoff uses jittered monotonic scheduling within a process. Durable checkpoints store
bounded next-eligible/attempt evidence; after restart, clock rollback or unavailable
trusted time never extends retention indefinitely or causes an immediate retry storm.
Provider retry hints are validated and clamped to product hard limits.

Expiry is an explicit terminal outcome, evaluated before every dispatch and after
recovery. Progression and presence have separately configured finite maximum ages;
presence is always shorter-lived. Expired work is durably marked `Expired`, observers
are notified and the semantic owner decides any user-visible remediation. The queue
never silently drops oldest records, resets expiry after retry/restart or reports
expiry as remote success.

Expiry applies only while the queue can prove no unresolved remote effect. A
`Reconciling` record does not become `Expired` when its ordinary delivery TTL elapses;
it remains retained until semantic reconciliation or an explicitly authorized,
audited `Abandoned` disposition. Attempt exhaustion follows the same rule: known
pre-commit failure may become `PermanentlyFailed`, while remote ambiguity cannot.

Record count, per-record bytes, total bytes, per-partition count, in-flight attempts
and terminal-retention storage are finite checked limits declared by the product
profile within engine hard maxima. Capacity may be recovered only by legal semantic
coalescing or terminal compaction. A full queue rejects new durable admission; it does
not evict another subject, cloud journal, ambiguous record or oldest mutation.

### 9. Remote ambiguity retains the original identity and strategy

Timeout, cancellation, transport loss or provider shutdown after submission may leave
remote outcome unknown. The ADR-130 request keeps its terminal state, while the queue
durably records `Reconciling` for the logical intent.

- intrinsic/deduplicated operations may resubmit the exact same identity/envelope;
- conditional operations query/reconcile the exact target/revision and retry only the
  still-valid precondition;
- unsafe non-deduplicated operations stop automatic dispatch and require explicit
  semantic reconciliation; and
- late provider evidence may inform reconciliation but cannot rewrite the original
  request result or bypass current session/access checks.

The queue never allocates a replacement mutation ID, changes provider target/payload,
marks ambiguity as failure eligible for a fresh attempt or lets compaction erase it.

### 10. Cloud intent remains fully caller-owned

`CloudSaveCoordinator` alone persists cloud upload/delete. Its ADR-115/134 journal
retains local slot generation and parent, archive hash/digest/size, immutable payload
lease, exact object key and provider precondition, `CloudMutationId`, subject binding,
supersession/conflict/recovery state and outcome ambiguity.

Platform Services receives one caller-owned immutable request/lease and owns only the
ADR-130 in-memory attempt/native lifetime. It may use the same stateless backoff,
deadline, jitter, error-normalization and transport helpers as the offline queue, but
those helpers neither persist nor schedule cloud work. The coordinator decides when
to reconcile/resubmit after request terminal state.

No API converts cloud intent into `PlatformOfflineIntent`; no queue row references or
copies an archive; and no Project Settings “persist offline queue” switch controls
cloud durability. A startup/reconnect service may wake both owners, but each opens and
reconciles its own state. Cross-owner ordering is expressed by typed coordinator
dependencies, never by inserting cloud work into a shared FIFO.

### 11. Cancellation, shutdown and deletion preserve truth

Producer cancellation may durably remove/supersede a `Pending` intent only when the
semantic owner proves no provider attempt crossed its commit boundary. Once dispatch
or ambiguity exists, cancellation stops new attempts but retains reconciliation; it
cannot claim remote rollback. Presence desired-state supersession follows Section 6.

Shutdown proceeds in bounded phases:

1. close queue admission and scheduling;
2. persist current state/attempt evidence and detach deferred observers;
3. request best-effort cancellation of in-memory provider work;
4. retain queue storage, provider/module/credential leases and completion sinks until
   possible callbacks retire under ADR-130/131; and
5. publish a typed shutdown result without deleting unresolved records.

Failure to durably checkpoint is a visible shutdown/storage failure. Forced process
termination may interrupt cleanup but the last atomically published state remains the
recovery authority. Startup never interprets an unclean marker as permission to clear
the queue.

Explicit user/admin deletion is a separate permissioned semantic operation. It shows
counts/classes, cannot target another subject through display name, refuses ambiguous
remote work unless policy records an acknowledged abandonment, and writes an audited
`Abandoned` terminal disposition before compaction. Ordinary cache cleanup cannot
erase intent.

### 12. Outcomes, diagnostics and privacy are bounded

Stable results include:

```text
platform.offline.ineligible
platform.offline.durable_unavailable
platform.offline.capacity_exceeded
platform.offline.storage_unknown
platform.offline.identity_conflict
platform.offline.suspended
platform.offline.expired
platform.offline.permanently_failed
platform.offline.reconciliation_required
platform.offline.abandoned
platform.offline.corrupt
```

Snapshots expose aggregate counts/bytes by service and safe state, oldest bounded age,
next eligible delay, capacity pressure, and normalized last failure. Metrics have no
subject, mutation, stable-definition, display or provider-native identity dimensions.
Logs exclude payload/presence text, raw account/handle values, credentials and private
binding locators. Safe correlation uses short-lived non-account-derived IDs.

Diagnostics distinguish `Pending`, `Suspended`, `Reconciling`, `Expired` and permanent
failure. “Queued” is never shown as success. Presence content requires the same
purpose/consent as publication and is redacted from ordinary diagnostics. Cloud queue
depth is reported from the Save coordinator separately, never added to the Platform
Offline Queue count.

### 13. Qualification covers storage, replay and owner boundaries

Required Mock/Null, fault-injection and integration evidence includes:

- ownership inventory proving every supported operation has exactly one or no durable
  owner and cloud upload/delete can never enter the platform queue;
- crash at every durable admission/attempt/terminal/checkpoint boundary, including
  remote commit before local completion, with no false success or duplicate unsafe
  effect;
- all ADR-133 operation classes against provider variants with intrinsic semantics,
  conditional revisions, durable dedupe and no safe primitive;
- exact duplicate join, conflicting identity/payload rejection, mutation identity
  preservation and individual outcomes after legal coalescing;
- per-lane ordering, conditional serialization, cross-lane concurrency, partition
  fairness and presence Set/Clear supersession permutations;
- offline/reconnect/rate-limit/backoff/clock-skew/expiry/attempt exhaustion without
  spin, retry storm, TTL reset or silent eviction;
- queue record/count/partition/byte/in-flight hard limits, disk-full, permission,
  sync/rename-unknown, corrupt/truncated/version-skewed storage and atomic recovery;
- Alice-to-Bob switch, sign-out, same-binding reauthentication, consent revocation,
  provider/mapping/policy replacement and stale callback evidence with no cross-subject
  dispatch/publication;
- AuthorityServer records never admitted/replayed by a client or stale world authority;
- cancellation before submit, during dispatch and after ambiguous commit plus bounded
  repeated/partial shutdown and late provider callback retirement;
- privacy/redaction/cardinality checks for records, logs, metrics, crash bundles,
  diagnostics and admin deletion; and
- concurrent Save cloud journal and Platform Offline Queue recovery proving shared
  helpers create no shared storage, scheduler, outcome or cross-owner FIFO.

## Consequences

- Eligible progression and opted-in presence have one durable queue with explicit
  acceptance, terminal and crash-recovery semantics.
- No operation can be replayed independently by multiple Horo queues, and cloud
  lineage/conflict intent remains entirely with Save.
- Durable acceptance is stronger than an in-memory request but intentionally weaker
  than remote success or infinite delivery; expiry/capacity/storage failure is visible.
- Account switch and consent changes suspend or terminate the old partition instead of
  retargeting whichever user is current.
- Providers with insufficient idempotency/atomicity expose narrower replay capability;
  persistence cannot manufacture remote safety.
- Implementation requires qualified atomic storage, finite scheduling, recovery,
  compaction, privacy-safe diagnostics and fault-injection coverage.

## Rejected Alternatives

### Persist every failed `PlatformRequest`

Rejected because requests contain attempt/provider/session lifetime and may represent
reads or unsafe mutations. The queue stores only qualified logical intent.

### Queue only after the first offline failure

Rejected for `DurableUntilTerminal` because a process may crash between remote submit
and persistence. Durable mode commits the record before the first attempt.

### Let each service keep its own durable retry file

Rejected because duplicate schedulers can replay one mutation more than once and make
capacity, shutdown, account partitioning and diagnostics inconsistent.

### Put cloud upload/delete into the generic queue

Rejected because a generic row cannot own slot lineage, archive lease, revision CAS,
conflict preservation or local publication. Save already owns the complete intent.

### Use one global FIFO across all subjects and services

Rejected because independent work would block, a noisy lane could starve others and
FIFO order is not semantic order for monotonic/coalescible operations.

### Replay against whichever account is currently signed in

Rejected because old-session work would mutate another user. Records are partitioned
by protected binding and reauthorized only for that same binding.

### Drop the oldest record when capacity is full

Rejected because age is not semantic priority and remote ambiguity or an achievement
could be silently lost. Admission fails unless legal coalescing frees capacity.

### Treat durable acceptance or queue expiry as remote success

Rejected because local persistence does not prove provider commit, while expiry is an
explicit delivery failure requiring a terminal outcome.

### Delete a record immediately after a provider callback

Rejected because a crash may lose local completion truth and later re-admit/replay the
same intent. Terminal disposition is durable before compaction.

### Persist presence by default

Rejected because presence can contain personal/user-authored data and quickly becomes
stale. Durable presence is explicit, consented, bounded and short-lived.
