# ADR-134: Cloud Blob Transport, Revision Preconditions and Offline Ownership

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: Authenticated opaque cloud-object transport, object/revision/address types, capability limits, conditional atomic write/delete, whole-blob integrity, partial transfer, cancellation/timeout ambiguity, retry and caller-owned durable intent
- **Issue**: [PLS-005.1](https://github.com/abdullahbodur/horo-engine/issues/1890)
- **Jira**: [HORO-1846](https://horo-engine.atlassian.net/browse/HORO-1846)
- **Related**: [ADR-008](008-error-model-exception-boundary-and-registry.md), [ADR-112](112-save-archive-container-and-compatibility-policy.md), [ADR-113](113-local-storage-user-profile-and-slot-ownership.md), [ADR-115](115-cloud-save-authority-revision-and-conflict-policy.md), [ADR-116](116-save-data-threat-model-and-trust-policy.md), [ADR-130](130-platform-services-frontend-request-lifetime-timeout-null-and-error-semantics.md), [ADR-131](131-platform-services-closed-sdk-extension-abi-package-and-composition-boundary.md), [ADR-133](133-platform-progression-authority-trust-and-idempotency.md)
- **Normative documents**: [Platform Services Architecture](../architecture/runtime/platform-services-architecture.md), [Save Game and Persistence](../architecture/runtime/save-game-and-persistence.md), [Application Security](../architecture/security/application-security.md)

## Context

ADR-115 establishes local Runtime Save as gameplay authority, the application/profile-
owned `CloudSaveCoordinator` as the single durable sync-intent owner and the provider
revision as an opaque compare-and-swap token. Platform Services is deliberately only
authenticated blob transport. Its current interface still leaves important transport
behavior implicit: whether a read is revision-consistent, whether partial multipart
uploads can become visible, how byte integrity is checked, which quotas are known, and
what retry means after a timeout with an unknown remote commit.

Those gaps could recreate policy in the provider frontend. A generic offline queue
could retain a second copy of an upload, an adapter could expose a partially written
object, or a completed download could be mistaken for a trusted save. An unconditional
write emulated by read-then-write would also violate ADR-115 when two devices race.

This ADR ratifies the existing ownership rather than revising it. It defines a narrow
whole-blob transport contract, exact conditional operations, limits, integrity and
partial-transfer semantics. Save lineage, local generations, conflict classification,
winner/merge policy, durable upload/delete scheduling and archive trust stay with
ADR-112 through ADR-116.

## Decision

### 1. The provider is an authenticated opaque-object transport

The Platform Services cloud capability exposes only bounded operations over one
authenticated provider-user/product namespace:

- list bounded object heads;
- read one complete object revision;
- conditionally create or replace one complete object;
- conditionally delete one object revision; and
- report an advisory quota/capability snapshot.

It treats `CloudSaveObjectKey`, bytes and any caller envelope as opaque. It does not
parse `.horosave`, enumerate participants, inspect `SlotGenerationId`/lineage, compute
gameplay merge, choose local versus remote, migrate state, edit a local catalog, derive
a filesystem path or decide which object should be deleted. A provider SDK's “latest
save,” conflict resolver or automatic sync feature is not used as a parallel semantic
authority.

Authentication proves only which provider session/namespace accepted the transport
request. TLS, SDK success, provider ownership and a matching transport digest do not
make downloaded bytes structurally safe, authentic, current or authorized for local
publication. ADR-116 admission remains mandatory after download.

### 2. Transport identity and save identity remain distinct

```cpp
struct CloudSaveObjectKey {
    BoundedOpaqueBytes value;
};

struct ProviderObjectRevision {
    BoundedOpaqueBytes value;
};

struct CloudBlobDigest {
    Sha256Digest value;
};

struct CloudMutationId {
    UInt128 value;
};

struct CloudObjectHead {
    CloudSaveObjectKey key;
    ProviderObjectRevision revision;
    std::uint64_t sizeBytes;
    std::optional<CloudBlobDigest> transportDigest;
    OptionalProviderTimestamp modifiedForPresentation;
};
```

`CloudSaveObjectKey` is derived by the coordinator under ADR-113 from typed product,
environment, profile, logical-slot and private authenticated-provider scope. It is not
a path, display name, raw platform account ID or `SaveGameSlotId` with erased scope.
Only the adapter translates it to a provider-native key.

`ProviderObjectRevision` is nonempty, bounded opaque bytes scoped to the exact provider
package/generation, authenticated subject namespace and object key. It is compared for
exact byte equality only. It has no numeric/lexicographic ordering, cross-provider
meaning, time semantics or gameplay visibility. Provider timestamp is optional
presentation/provenance and never a precondition or winner signal.

`CloudBlobDigest` is SHA-256 of exactly the complete opaque object bytes transferred by
Horo. It detects truncation/substitution between caller staging and returned complete
transport, but is not a signature, provider revision, save `ArchiveContentHash`,
lineage proof or trust grant. A provider-native checksum may be retained as private
diagnostic evidence; it does not replace the Horo digest.

List metadata may omit a digest when the provider cannot return one without reading
the object. Absence is explicit. Every successful Horo read computes and returns the
digest of its complete received bytes, and reconciliation that needs content identity
reads the object rather than treating missing list metadata as equality.

`CloudMutationId` is allocated once and durably owned by the coordinator's ADR-115
journal for one exact write/delete intent. It is not a frontend request ID, provider
operation token or authentication proof.

### 3. One immutable capability snapshot defines exact transport support

Before admission, the adapter publishes a validated generation-scoped
`CloudBlobTransportCapability` containing:

```cpp
enum class CloudMutationAtomicity : std::uint8_t {
    ConditionalAtomicObject,
    UncoordinatedBlob,
};

struct CloudBlobLimits {
    std::uint64_t maxObjectBytes;
    std::uint64_t maxNamespaceBytes;
    std::uint32_t maxObjectCount;
    std::uint32_t maxKeyBytes;
    std::uint32_t maxRevisionBytes;
    std::uint32_t maxConcurrentReads;
    std::uint32_t maxConcurrentMutations;
    std::uint32_t transferChunkBytes;
    std::uint32_t maxListPageEntries;
};
```

The snapshot also declares list/read consistency, `CreateIfAbsent`, revision-matched
replace/delete, mutation-ID dedupe, digest availability, progress support, cancellation
strength and whether temporary multipart bytes count against quota. Every limit is
finite and within Horo hard ceilings. Zero/contradictory limits or a claimed atomic
mode missing either create or revision CAS fail provider composition.

`ConditionalAtomicObject` is required for automatic cloud synchronization. It means a
successful create/replace/delete is indivisible at one object key and all competing
operations observe precondition ordering. `UncoordinatedBlob` permits bounded read and
explicit import/export where product policy allows, but automatic background write or
delete is unavailable. Horo never emulates CAS with a process mutex, advisory lease or
read-then-unconditional-write.

Quota/usage observations are advisory and generation-scoped. Another device can
consume space immediately after the query. They support preflight/UI but never promise
a later mutation; provider `QuotaExceeded` remains a normal typed terminal outcome.

### 4. List and read return one bounded revision-consistent view

List is cursor-based and bounded. A page carries an opaque continuation token scoped
to provider/session/query generation, entries sorted or normalized by Horo key bytes,
and a completeness flag. Duplicate keys/revisions, invalid/oversized fields, cursor
loops or more entries than declared limits fail the candidate page. A list need not be
a namespace transaction unless the capability explicitly guarantees snapshot listing;
the coordinator therefore re-reads the exact selected key before mutation/conflict
decisions.

A successful read returns bytes and `CloudObjectHead` from the same object revision.
If the provider SDK retrieves metadata separately, the adapter must revision-check
before and after transfer or use an atomic provider primitive. A change during read is
`platform.cloud.revision_changed`, not a mixed success. `NotFound` is typed absence at
that observation; it is not deletion intent or authority to erase a local save.

The frontend admits the declared/object hard byte limit before allocation. Download
streams only through a host-owned bounded sink associated with the request/provider/
session generation. Checked arithmetic enforces declared length, aggregate bytes and
chunk count. Success publishes one immutable complete byte owner only after exact
length, transport digest and final revision validation. No partial span escapes.

The coordinator then independently validates framing, `ArchiveContentHash`, optional
signature/scope, product/profile/slot identity, compatibility, semantic content and
local generation/lease before local publication. Transport cannot turn a failed save
validation into `NotFound`, an empty save or an older winner.

### 5. Writes and deletes require exact atomic preconditions

```cpp
using CloudWritePrecondition =
    Variant<CreateIfAbsent, MatchProviderRevision>;

struct CloudWriteRequest {
    CloudSaveObjectKey key;
    CloudBlobReadSource source;
    std::uint64_t exactSizeBytes;
    CloudBlobDigest expectedDigest;
    CloudWritePrecondition precondition;
    CloudMutationId mutation;
};

struct CloudDeleteRequest {
    CloudSaveObjectKey key;
    ProviderObjectRevision expectedRevision;
    CloudMutationId mutation;
};
```

There is no unconditional automatic-sync variant. `CreateIfAbsent` succeeds only when
the key has no object at the provider's atomic commit point. `MatchProviderRevision`
and delete succeed only when the exact captured revision is current at commit. A stale
or concurrently replaced object returns `PreconditionFailed` without visible mutation.
The coordinator refetches/reclassifies; it never retries without the precondition.

Write success means the complete expected bytes became atomically visible at the key
and returns a nonempty new `ProviderObjectRevision`, exact size and matching Horo
transport digest. Delete success means the exact matched revision is no longer the
visible object and returns bounded deletion evidence/new namespace observation when
available. Success without required revision/digest evidence is an invalid provider
response.

The request source is an immutable operation-owned lease created by the coordinator
over one finalized local archive/generation. The ABI adapter pulls fixed bounded chunks
through call-borrowed spans; provider code may copy during the call but cannot retain
the span or source callback after revocation. A newer local save does not mutate the
old source. Only the coordinator may decide that a newer journal intent supersedes it.

### 6. Native multipart work cannot expose partial Horo objects

An adapter may internally use native multipart/temp-object APIs, but staging identities
and native parts stay private. Before final provider commit, the public key must still
resolve to the complete previous revision or absence required by the precondition.
After success it resolves to exactly the new complete bytes/revision. No list/read may
observe a partial Horo object as current.

Upload/read failure, cancellation, timeout, process/provider shutdown or short transfer
produces no partial success/result. The adapter requests best-effort abort/cleanup of
native staging. A provider may report bounded cleanup-pending evidence and temporary
quota impact, but Horo never publishes staging keys or claims remote rollback until
confirmed. Cleanup lifetime pins the provider module/generation under ADR-131.

Download staging belongs to the frontend request until complete publication or discard.
It cannot overwrite a local archive, catalog or conflict recovery record. Disk/memory
budget failure stops the transfer and removes only operation-owned staging through its
owner; the prior local/remote committed objects remain untouched.

Progress callbacks are optional bounded observations containing transferred/total
bytes and request identity. They are monotonic within one attempt, deferred not to
exceed exact size, dispatched on the engine observer executor and never imply commit.
Gameplay/UI cannot use progress to read partial bytes or decide a conflict.

### 7. Exactly one durable owner retains upload and delete intent

`CloudSaveCoordinator`, scoped to the application/profile namespace, remains the sole
durable owner of upload/delete intent under ADR-115. Its journal owns:

- `CloudMutationId`, operation kind and exact typed object key;
- local generation/parent, immutable archive hash/digest/size and payload lease or
  recoverable locator;
- expected provider revision or create-if-absent;
- provider subject/session requirements, retry/reconciliation state and typed outcome;
  and
- supersession, conflict, deletion and recovery-retention policy.

Platform Services owns only an admitted in-memory ADR-130 request and native operation
lifetime. It does not write a cloud mutation into the generic PLS-007 queue, copy the
archive into another durable queue, select the next local generation or persist a
second backoff/success authority. PLS-007 progression/presence storage explicitly
excludes cloud write/delete.

Frontend retry, if allowed, is bounded by the original request deadline and repeats
the exact request/precondition/mutation ID while the coordinator lease remains valid.
After request terminal state, only the coordinator decides durable reschedule,
supersession or reconciliation. Dropping a request handle, closing UI or shutting down
the frontend does not erase the journal intent or imply remote rollback.

### 8. Retry is conditional and timeout preserves ambiguity

An in-memory retry is eligible only for a normalized transient failure known to occur
before provider commit, or when the adapter's durable mutation-ID dedupe guarantees the
same result for the exact canonical request. Attempt count/backoff fits the original
ADR-130 deadline and revalidates provider/session/source lease.

If timeout, cancellation, transport loss or provider crash occurs after commit may
have begun, the request retains its ADR-130 terminal result and the coordinator marks
the durable intent `RemoteOutcomeUnknown`. It does not allocate a new mutation ID,
advance the local synced head or report success.

Reconciliation reads the exact key:

- matching expected content digest/size and compatible returned head recognizes the
  write as committed and records the observed revision;
- old expected revision/absence means the original precondition may be attempted again
  with the same mutation ID after complete revalidation;
- another revision/content is a normal precondition conflict handled by ADR-115; and
- delete sees absence only as evidence for this coordinator-owned exact delete intent;
  unrelated absence without journal history remains Unknown/Conflict.

A repeated request whose mutation ID is known by a deduplicating provider must return
the same semantic outcome/evidence. The same ID with another key, digest, size,
precondition or operation fails `platform.cloud.idempotency_conflict`. Mutation-ID
dedupe does not authorize an operation or weaken revision CAS.

### 9. Quota policy is bounded and non-destructive

Admission checks exact object length, Horo hard limits, provider `maxObjectBytes`,
declared object count/namespace usage where known, staging budget and concurrent
transfer slots with overflow-safe arithmetic. Zero-length objects are allowed only if
the product cloud-envelope schema explicitly uses them; absence/deletion is never
encoded as an empty blob.

Provider quota state may include bounded total/used/available bytes, object count,
per-object and temporary/multipart overhead plus an observation generation/time. It
contains no unrestricted native metadata or account identity. Unknown usage is
explicit, not zero or unlimited.

Quota failure returns `platform.provider.failed` with normalized `QuotaExceeded` and
safe limit/request evidence. Platform Services never deletes old objects, compresses,
truncates, splits across undeclared keys, chooses another slot, increases limits or
falls back to a local path. The coordinator/product UI owns any explicit cleanup or
retention command under revision/recovery policy.

### 10. Errors distinguish absence, conflict, integrity and partial work

Stable cloud transport outcomes include:

```text
platform.cloud.not_found
platform.cloud.already_exists
platform.cloud.precondition_failed
platform.cloud.revision_changed
platform.cloud.object_too_large
platform.cloud.quota_exceeded
platform.cloud.integrity_mismatch
platform.cloud.partial_transfer
platform.cloud.invalid_provider_response
platform.cloud.idempotency_conflict
platform.cloud.cleanup_pending
```

Frontend unavailable, Null, capability absence, cancellation, timeout and generic
provider failure retain ADR-130 identities. Native errors map to normalized category
plus bounded redacted cause evidence; adapters never branch on provider message text.
`NotFound`, `PreconditionFailed`, `QuotaExceeded`, `PartialTransfer` and `TimedOut` are
not interchangeable and never become an empty successful blob.

Diagnostics contain safe Horo object/mutation correlation, operation, byte counts,
capability/provider/session generations, normalized phase and revision/digest prefixes
only where policy permits. They exclude object bytes, archive state, local paths,
credentials, raw account/native key/revision/error data and provider staging names.

### 11. Lifecycle pins bytes, sinks and provider code through retirement

An admitted request owns or leases all source/sink/staging state independently of
handles/subscriptions. Provider callbacks carry provider, session, request and native-
operation generations and copy through host-owned bounded sinks. Late/stale callbacks
may retire native work but cannot publish bytes or results into a replacement session.

Sign-out/profile switch closes new scheduling and invalidates subject admission. It
does not retarget a request or journal intent. Cancellation requests provider abort but
does not free source/sink/module state until callback/operation retirement confirms it
is safe. Shutdown stops admission, drains/cancels under a finite policy, preserves the
coordinator journal and retains the provider module lease on unknown callback tail
rather than force-unloading code.

### 12. Qualification proves transport semantics independently of save policy

Public Mock/Null/ABI fixtures and private provider qualification cover:

- bounded list pagination, unstable/non-snapshot lists, duplicate/changed entries,
  cursor loops and exact-key re-read;
- complete read at one revision plus revision-change, short/extra/oversized chunks,
  digest mismatch, sink exhaustion and no partial result escape;
- conditional create/replace/delete racing two devices, exact new revision/digest and
  no read/list visibility of multipart staging;
- cancellation/timeout/crash before transfer, during parts, before native commit and
  after commit but before callback, including cleanup/module lease behavior;
- same mutation replay, conflicting mutation ID payload and providers with/without
  durable dedupe;
- ambiguity reconciliation for committed/not-committed/conflicting write and delete;
- object/count/namespace/staging/concurrency quota at below/exact/above boundaries,
  changing advisory quota and no automatic destructive cleanup;
- provider/TLS-authenticated hostile blobs still entering ADR-116 as untrusted and
  leaving local generations/catalogs untouched on validation failure;
- sign-out/profile/provider replacement and stale callback isolation; and
- proof that exactly the ADR-115 coordinator journal owns durable cloud upload/delete
  intent and the generic PLS-007 queue contains none.

Provider conformance records native API/version, atomic visibility/CAS evidence,
dedupe behavior, documented quota/staging behavior and cancellation limitations.
Certification freezes that capability evidence with the selected provider package.

## Consequences

- Platform Services gains a complete testable blob API without inheriting save format,
  lineage, merge, conflict or local-publication authority.
- Automatic cloud mutation requires real provider atomic CAS; weak blob stores remain
  useful for bounded read/manual import-export without risking background overwrite.
- Downloads and uploads publish only complete revision-consistent bytes with explicit
  integrity and partial-transfer outcomes.
- Timeout/late commit ambiguity is reconciled by the one durable coordinator rather
  than hidden by a new request or duplicate queue.
- Quota observations improve preflight/UI but cannot trigger silent deletion or imply
  reservation.
- Adapters may need staging/multipart cleanup, digest calculation and revision checks,
  and providers lacking those guarantees expose narrower capability.

## Rejected Alternatives

### Let Platform Services own a durable cloud retry queue

Rejected because ADR-115 already assigns the journal, lineage, payload lease,
supersession and conflict outcome to `CloudSaveCoordinator`. A second queue can replay
an older archive or mark the wrong generation synced.

### Expose provider automatic-sync or conflict-resolution APIs directly

Rejected because they interpret “latest” and conflict outside Horo lineage, trust,
local transaction and recovery policy.

### Treat an authenticated or checksum-valid download as a trusted save

Rejected because transport identity/integrity does not prove framing, signature scope,
compatibility, semantic validity or freshness.

### Support unconditional write/delete for automatic synchronization

Rejected because concurrent devices can overwrite/delete unseen work. Only provider-
enforced create-if-absent and exact revision CAS satisfy the baseline.

### Emulate CAS with read-then-write or a process mutex

Rejected because another device/provider actor can mutate between calls and does not
participate in the local lock.

### Return partial download bytes on failure or timeout

Rejected because callers could parse/publish a truncated archive and request lifetime
would no longer own all staging. Partial bytes remain operation-owned and are discarded.

### Infer successful mutation from timeout or retry with a new ID

Rejected because provider commit may already have happened. The exact durable intent
remains ambiguous until read/revision reconciliation.

### Delete old provider objects automatically when quota is exceeded

Rejected because transport does not own save retention, conflict recovery or user
intent and could irreversibly destroy the only valid generation.
