# Platform Services Architecture

## Purpose

This document defines how Horo Engine integrates with platform-specific online
services such as achievements, leaderboards, cloud save, user presence, and
friends. These services are unlike audio, rendering, or physics: they are
asynchronous, network-bound, user-session dependent, and deeply tied to
closed platform SDKs and certification requirements.

The goal is a single backend-neutral frontend that gameplay code can use
without caring which platform the game is running on, while the actual
platform SDK implementations live in separate, optionally loaded backends.

[ADR-130](../../adr/130-platform-services-frontend-request-lifetime-timeout-null-and-error-semantics.md)
is the normative baseline for frontend-owned request records, admission, state and
result publication, deferred callback dispatch, capability truth, Null behavior,
timeout and provider-error normalization. Later service decisions specialize this
contract rather than creating another request lifecycle.

## Scope

Platform services covered here:

- Achievements / Trophies
- Leaderboards and persistent stats
- Cloud save (platform-managed user save sync)
- User presence / rich presence
- Friends and social graph read access
- Platform user identity and session

Deliberately out of scope:

- Audio backends (`audio-architecture.md`)
- Input haptics such as PS5 adaptive triggers (`input-architecture.md`)
- Local save files and serialization (game save system)
- In-app purchase and DLC entitlement validation (owned by release/DRM flow)
- Platform OS abstractions such as filesystem, window, or process
  (`platform-abstraction.md`)

## Core vs Modular

Platform services are **modular**, not part of the engine core.

The core engine must never link against Steamworks, PSN, Xbox Live, Nintendo
Online, or any other closed platform SDK. Those SDKs:

- are under NDA and cannot live in the public engine repository
- differ wildly in capability, size, and certification rules
- may not exist for headless, test, or prototype targets
- change on platform-holder timelines, not engine timelines

What the core provides:

- `IPlatformServicesBackend`: a narrow interface contract
- `PlatformServicesFrontend`: a backend-neutral, async API
- stable ID registries for achievements, leaderboards, and stats
- a null backend for tests and headless builds
- request queuing, offline buffering, and retry policy

Concrete backends live in separate platform packages such as
`horo-platform-steam`, `horo-platform-ps5`, `horo-platform-xbox`, and are
loaded by the runtime like any other extension package.

## High-Level Architecture

```text
Gameplay / UI code
        |
        v
PlatformServicesFrontend
  (Achievements, Leaderboards, CloudSave, Presence, Friends)
        |
        v
IPlatformServicesBackend (loaded extension)
        |
        +-- SteamBackend
        +-- PSNBackend
        +-- XboxLiveBackend
        +-- NintendoOnlineBackend
        +-- NullBackend
        +-- MockBackend (tests)
```

Gameplay code calls the frontend. The frontend routes the call to the active
backend, which is selected at process composition time based on the target
platform and available packages.

## Frontend Contract

All platform service calls are asynchronous. The frontend never blocks the
calling thread on network I/O.

```cpp
template <typename T>
class PlatformRequestHandle {
public:
    RequestId Id() const;
    FrontendGeneration Frontend() const;
};

class PlatformServicesFrontend {
public:
    Result<PlatformRequestHandle<AchievementUnlockResult>>
    UnlockAchievement(AchievementId id);

    Result<PlatformRequestHandle<LeaderboardEntries>>
    GetLeaderboardEntries(LeaderboardId id, const LeaderboardQuery& query);

    Result<PlatformRequestHandle<CloudReadResult>>
    ReadCloudSave(CloudSaveObjectKey key);

    Result<PlatformRequestHandle<CloudMutationResult>>
    WriteCloudSave(CloudWriteRequest request);

    Result<PlatformRequestHandle<void>>
    SetPresence(const PresenceState& state);

    template <typename T>
    Result<PlatformRequestSnapshot<T>> Query(PlatformRequestHandle<T> request) const;
    template <typename T>
    Result<PlatformRequestSubscription> Subscribe(
        PlatformRequestHandle<T> request,
        PlatformCompletionExecutor executor,
        PlatformTerminalObserver<T> observer);
    Result<void> Cancel(PlatformRequestId request);
};
```

Pre-admission validation, permission, lifecycle, capability, session and bounded-
capacity failure returns `Result` with no request record or provider call. Once admitted,
the frontend owns the request record independently of every handle and subscription.
Handles contain only typed request/frontend identity; they never own provider objects.

The authoritative snapshot state is exhaustive:

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
| `Succeeded`, `Failed`, `Cancelled`, `TimedOut` | none |

Exactly one terminal result publishes atomically with the terminal state. Success owns
one immutable value; Failed, Cancelled and TimedOut own one typed Error matching the
state. Non-terminal snapshots contain no terminal result.

### Request Lifetime

1. The frontend validates and either returns an admission error or atomically creates
   a `Queued` record with request, provider, session, policy and deadline generations.
2. Provider submission moves the record to `Running`; failure after admission commits
   `Failed` without inventing provider progress.
3. The provider performs asynchronous work and enqueues bounded generation-tagged
   evidence. SDK threads never mutate frontend state or invoke user code.
4. The frontend owner lane validates evidence and commits at most one terminal state/
   result before scheduling observer notification.
5. The record remains queryable under finite count/time retention after completion and
   until provider/callback retirement permits reclamation.

Dropping the last handle or subscription does not cancel an admitted request, erase a
write or free provider work. Explicit `Cancel(requestId)` or owner shutdown policy is
required. Querying expired or stale identity returns `platform.request.expired` or
`platform.request.stale`; it does not fabricate a replacement terminal result.

### Cancellation

`Cancel()` posts cancellation intent to the frontend owner. It never invokes the
provider or an observer inline. Cancellation is best-effort:

- An accepted queued/running request moves to `Cancelling` and closes new retries.
- Provider acknowledgement with no committed effect publishes `Cancelled` and
  `platform.request.cancelled`.
- A provider success already committed or completed despite cancellation may publish
  `Succeeded`; provider failure may publish `Failed`.
- Deadline expiry may still publish `TimedOut`. A terminal request cannot be cancelled
  retroactively.

Multiple `Cancel()` calls are idempotent. Cancellation intent is recorded separately
from the one final state; `Cancelled` does not promise rollback unless a service-
specific contract provides that guarantee.

### Threading

Completion callbacks run on an engine-controlled thread, never on a platform
SDK thread. Backends must not call frontend completion handlers directly; they
notify the frontend through an internal completion queue that the frontend
drains on an appropriate engine thread.

The authoritative observation API is snapshot query plus optional subscription.
Subscription returns a move-only RAII token and each token receives at most one
terminal notification. Destroying it prevents future delivery without cancelling the
request. Callbacks never run inline from submit, subscribe, cancel, provider callback,
queue drain/state mutation or while a frontend/provider lock is held. The frontend
commits terminal state first, then posts immutable request ID/terminal snapshot to the
declared engine executor for a later dispatch turn. Subscribing after retained terminal
completion schedules the same deferred delivery rather than invoking synchronously.

Gameplay code may submit from permitted threads through bounded ingress, but the
frontend serializes request mutation on its declared owner lane. Re-entering from a
later completion callback may enqueue new work; it cannot recursively drain completion
or change the published terminal record. Callback exceptions are caught at the engine
executor boundary and do not change the platform request result.

### Timeouts, Retry, And Throttling

The frontend captures one finite monotonic deadline at admission. Valid completion
evidence observed no later than the deadline is processed before deadline evaluation.
Otherwise the frontend commits `TimedOut` with `platform.request.timed_out`, requests
best-effort provider cancellation and closes caller-visible completion. Later native
completion is retirement/reconciliation evidence only and cannot replace the result.
Provider resources remain alive until acknowledged safe; timeout is not proof of
physical cancellation.

Retry is controlled by the owning frontend/service policy, not gameplay code, and all
attempts fit within the original request deadline:

- Idempotent writes such as stat updates may be retried a bounded number of
  times before surfacing an error.
- Reads and queries are not retried automatically unless the project policy
  explicitly allows it.
- When the backend reports normalized `Offline` or `NotSignedIn`, retriable writes move
  to the offline queue instead of being retried immediately.

High-frequency writes are throttled and coalesced:

- `SetStat` calls for the same stat are coalesced to the most recent value
  within a configured debounce window. The frontend emits one backend write per
  window, not one per gameplay call.
- `SubmitScore` calls are debounced, not coalesced: only the most recent score
  for a leaderboard is submitted after the debounce interval, but scores are
  not merged.
- Achievement unlock and progress updates are not throttled because they are
  infrequent, but duplicate unlocks for the same achievement are suppressed in
  memory before entering the queue.

Throttling metrics:

```text
platform.stats.coalesced_writes     -- stat writes merged by frontend
platform.leaderboards.deferred_submits -- submissions delayed by debounce
```

### Result And Errors

Submission and terminal snapshots use typed `Result`/`Error` contracts. Errors are
normalized so gameplay code does not need platform-specific error handling:

```text
platform.frontend.unavailable       -- frontend not active for admission
platform.capability.unavailable     -- selected real provider lacks the service
platform.provider.null              -- explicit Null composition; no operation accepted
platform.request.capacity_exceeded  -- bounded admission/observation capacity exhausted
platform.request.cancelled          -- acknowledged cancellation terminal outcome
platform.request.timed_out          -- frontend deadline terminal outcome
platform.request.expired            -- terminal observation retention ended
platform.request.stale              -- request/frontend/provider/session generation mismatch
platform.provider.failed            -- admitted provider operation failed
```

`platform.provider.failed` carries one normalized category: `Offline`, `NotSignedIn`,
`Forbidden`, `RateLimited`, `PreconditionFailed`, `QuotaExceeded`, `InvalidResponse`,
`TransientFailure` or `PermanentFailure`. Bounded redacted native codes may appear as
diagnostic/cause evidence but never as branching identity. Capability absence, Null,
cancellation and timeout are never remapped into provider failure.

### Ordering And Coalescing

Independent requests may complete out of order. Dependent operations are chained by
their owning coordinator. In particular, cloud mutation always carries the exact
provider revision returned by the reconciled read/list (or create-if-absent); generic
callers cannot turn a read completion into an unconditional write.

Presence updates are coalesced. If `SetPresence` is called many times before
the backend finishes the previous update, only the most recent state is sent.

## Service Categories

### Achievements

Achievements are authored once in project configuration and cooked into
platform-specific manifests.

```cpp
struct AchievementId { uint64_t value; };  // stable, numeric

struct AchievementDefinition {
    AchievementId id;
    std::string displayName;
    std::string description;
    bool hidden;
    uint32_t progressSteps;  // 0 or 1 for binary, >1 for progress
};

class IAchievementService {
public:
    virtual Result<PlatformRequestHandle<AchievementUnlockResult>>
    Unlock(AchievementId id) = 0;

    virtual Result<PlatformRequestHandle<AchievementProgressResult>>
    SetProgress(AchievementId id, uint32_t current, uint32_t total) = 0;

    virtual Result<PlatformRequestHandle<std::vector<AchievementState>>>
    QueryState() = 0;
};
```

The frontend validates that `current <= total` and that the achievement ID is
registered. Runtime code never passes raw strings or platform-specific IDs.

### Leaderboards And Stats

Leaderboards are also authored once and mapped to platform backends at cook
time.

```cpp
struct LeaderboardId { uint64_t value; };
struct StatName { StableString name; };

enum class LeaderboardSort {
    Ascending,
    Descending
};

struct LeaderboardQuery {
    uint32_t rangeStart;
    uint32_t rangeCount;
    bool friendsOnly;
};

class ILeaderboardService {
public:
    // The backend receives an idempotency key so duplicate submissions from
    // retry or offline replay do not create multiple leaderboard entries.
    virtual Result<PlatformRequestHandle<void>>
    SubmitScore(LeaderboardId id,
                int64_t score,
                IdempotencyKey key) = 0;

    virtual Result<PlatformRequestHandle<LeaderboardEntries>>
    GetEntries(LeaderboardId id, const LeaderboardQuery& query) = 0;

    virtual Result<PlatformRequestHandle<void>>
    SetStat(StatName name, int64_t value) = 0;

    virtual Result<PlatformRequestHandle<std::optional<int64_t>>>
    GetStat(StatName name) = 0;
};
```

`IdempotencyKey` is generated by the frontend from a monotonic write sequence
and the stable operation identity. The same key is reused across frontend
retries and offline queue replay. The backend must treat the second call with
an identical key as a no-op and return the same result.

Stats are server-authoritative where the platform allows. The frontend keeps a
read-through local cache for stats that gameplay reads frequently, but writes
always go to the backend.

### Cloud Save

Cloud save is platform-managed synchronization of save data blobs. It is not a
replacement for the local save system; it is an upload/download layer on top of
local save files.

```cpp
struct CloudSaveObjectKey { BoundedOpaqueBytes value; };
struct ProviderObjectRevision { BoundedOpaqueBytes value; };

enum class CloudConcurrencyCapability : uint8_t {
    ConditionalRevision,
    UncoordinatedBlob
};

struct CloudSaveMetadata {
    CloudSaveObjectKey key;
    ProviderObjectRevision revision;
    Timestamp modifiedTime; // presentation/provenance only, never causality
    uint64_t sizeBytes;
};

using CloudWritePrecondition =
    Variant<RequireObjectAbsent, MatchProviderRevision>;

struct CloudReadResult {
    CloudSaveMetadata metadata;
    OwnedImmutableBytes archive;
};

struct CloudWriteRequest {
    CloudSaveObjectKey key;
    OwnedImmutableBytes archive;
    CloudWritePrecondition precondition;
    IdempotencyKey retryIdentity;
};

struct CloudMutationResult {
    std::optional<ProviderObjectRevision> resultingRevision;
    std::optional<CloudSaveMetadata> metadata;
};

class ICloudSaveService {
public:
    virtual CloudConcurrencyCapability ConcurrencyCapability() const = 0;

    virtual Result<PlatformRequestHandle<std::vector<CloudSaveMetadata>>>
    List() = 0;

    virtual Result<PlatformRequestHandle<CloudReadResult>>
    Read(CloudSaveObjectKey key) = 0;

    virtual Result<PlatformRequestHandle<CloudMutationResult>>
    Write(CloudWriteRequest request) = 0;

    virtual Result<PlatformRequestHandle<CloudMutationResult>>
    Delete(CloudSaveObjectKey key, ProviderObjectRevision expected) = 0;
};
```

Under ADR-113, gameplay and UI never create `CloudSaveObjectKey` from a string. The
save/cloud coordinator derives it from the typed product/environment/profile/slot
address and authenticated provider-user context. Installation-local user IDs are not
serialized into it. It is not a local path, display name or `SaveGameSlotId` with
erased scope. The backend treats both key and finalized archive bytes as opaque and
never edits the local format or catalog.

Provider revisions and timestamps are returned as transport metadata. They do not
select a winning local/cloud generation automatically. Under ADR-115, automatic
mutation requires qualified `ConditionalRevision` semantics. `UncoordinatedBlob`
disables background write/delete while local saves remain available. The save/cloud
coordinator owns durable intent, lineage,
preconditions and divergent-generation resolution; UI may present a choice but never
becomes sync authority.

Provider authentication, TLS success, object ownership and a completed download do
not make archive bytes trusted. Under ADR-116, Platform Services enforces transport
byte/time bounds and returns operation-owned opaque bytes plus provider metadata. The
save/cloud coordinator then applies local framing, integrity, signature/scope,
compatibility, semantic and namespace policy before any local publication or load.
The backend cannot select a trust root, request a parser exception or reinterpret a
verification failure as an empty/older remote object.

### Presence

Presence is best-effort and non-critical. The frontend batches rapid presence
updates and coalesces them into the most recent state.

```cpp
struct PresenceState {
    std::string statusId;          // stable ID, mapped at cook time
    std::string detail;            // optional free text
    std::string largeImageKey;     // stable art key
    std::string smallImageKey;     // stable art key
};

class IPresenceService {
public:
    virtual Result<PlatformRequestHandle<void>>
    SetPresence(const PresenceState& state) = 0;

    virtual Result<PlatformRequestHandle<void>>
    ClearPresence() = 0;
};
```

### Friends

Friends access is read-only in core. Invites and friend management UI are
platform-owned.

```cpp
struct PlatformUserHandle {
    std::string platformUserId;
    std::string displayName;
};

class IFriendsService {
public:
    virtual Result<PlatformRequestHandle<std::vector<PlatformUserHandle>>>
    GetFriends() = 0;
};
```

Friends access requires explicit user consent and platform policy support.
Absence of the capability is typed, not a silent empty list.

## Stable ID Registries

Runtime code never passes strings for achievement, leaderboard, presence, or
stat IDs. All names are resolved at project cook or package build time into
stable numeric IDs.

Stable IDs are produced deterministically from the authoring name using a
project-scoped hash so that the same name always yields the same numeric ID
across clean builds and across developer machines:

```cpp
struct StableId64 { uint64_t value; };

AchievementId GenerateAchievementId(const std::string& projectSalt,
                                    const std::string& name);
LeaderboardId GenerateLeaderboardId(const std::string& projectSalt,
                                    const std::string& name);
```

The salt is stored in the project configuration. It must not change after the
first release that consumes platform IDs, because platform backends map stable
IDs to platform-specific manifests.

The registries enforce uniqueness and traceability:

```cpp
struct AchievementRegistry {
    enum class RegisterResult { Ok, DuplicateName, DuplicateId };

    Result<AchievementId, RegisterResult>
    Register(const std::string& name);

    std::optional<AchievementId> Find(const std::string& name) const;
    std::optional<std::string> FindName(AchievementId id) const;
};
```

- Registering the same name twice returns the existing ID and is treated as
  idempotent during cook, but emits a diagnostic so authors notice accidental
  reuse.
- A hash collision between two different names is a cook error. The author must
  rename one of the definitions.
- Removed achievements are marked `deprecated` rather than deleted from the
  registry. Their stable IDs remain reserved so they are never reused by a new
  definition.

The cook pipeline emits platform-specific manifest files:

```text
achievements.json          -- Horo authoring source (stable IDs, display text)
.deprecated_achievements   -- tombstone list for removed IDs
steam/achievements.vdf     -- Steamworks manifest
psn/trophy_pack/           -- PSN trophy pack
xbox/achievements.json     -- Xbox Live manifest
```

The same rules apply to leaderboards, stats, and presence status IDs.

This is the same pattern used by the audio parameter and event registries.

## Backend Interface

The backend is a capability bundle. A platform may implement all, some, or none
of the services.

```cpp
enum class PlatformServiceAvailability : uint8_t {
    Available,
    Unavailable
};

enum class PlatformServiceUnavailableReason : uint8_t {
    NoProviderSelected,
    NullProviderSelected,
    ServiceUnsupported,
    HostPolicyDenied,
    ProviderInitializationFailed
};

struct PlatformServiceCapability {
    PlatformServiceKind service;
    PlatformServiceAvailability availability;
    PlatformServiceLimits limits;
    std::optional<PlatformServiceBindingId> binding;
    std::optional<PlatformServiceUnavailableReason> unavailableReason;
};

struct PlatformServiceCapabilitySnapshot {
    ProviderGeneration providerGeneration;
    BoundedArray<PlatformServiceCapability> services;
};

class IPlatformServicesBackend {
public:
    virtual ~IPlatformServicesBackend() = default;

    virtual Result<PlatformServiceCapabilitySnapshot>
    Initialize(const PlatformServicesConfig& config) = 0;
    virtual Result<ProviderOperationId>
    Submit(const PlatformProviderRequest& request,
           PlatformProviderCompletionSink& completions) = 0;
    virtual Result<void> RequestCancel(ProviderOperationId operation) = 0;
    virtual Result<void> Shutdown() = 0;
};
```

The validated capability snapshot is the sole availability truth. `Available` has
exactly one compatible private binding; `Unavailable` has no binding and exactly one
reason. Missing, duplicate or mismatched bindings fail composition. Public callers do
not inspect provider pointers, backend names or SDK flags.

Connectivity, sign-in, consent and rate-limit state are request preconditions/session
state, not a second installed-capability flag. Provider reload or session replacement
increments generation; stale completion evidence cannot publish into the replacement.

## Offline And Degraded Behavior

The frontend maintains a persistent local queue for operations that can be
safely retried:

- achievement unlocks
- achievement progress updates
- leaderboard score submissions
- stat writes

Each queued operation carries an `IdempotencyKey`. The key is generated once
when the operation enters the frontend and is preserved across persistence,
retry, and replay. This prevents a retry + queue replay combination from
producing duplicate backend effects.

When the backend reports normalized `Offline` or `NotSignedIn`, the frontend:

1. assigns an idempotency key if the operation does not already have one
2. persists the operation to the local queue
3. deduplicates against the queue using the operation type, target ID, and key
4. emits an `offline_queued` metric
5. returns a result indicating queued state
6. replays the queue in order when the platform session becomes active

Operations that cannot be safely replayed, such as cloud save reads or
leaderboard queries, fail immediately with the appropriate error.

Cloud archive writes/deletes are deliberately absent from this generic queue. Their
immutable payload lease, slot lineage, expected provider revision, retry identity and
profile-session scope are durably owned by ADR-115's `CloudSaveCoordinator`. After
offline recovery it reconciles remote state before conditional mutation; it never
replays a stale FIFO upload under a later local generation or different user.

Replay rules:

- Queue order is preserved per user account.
- If a queued operation fails with a non-retriable error (for example,
  normalized `Forbidden`), it is removed from the queue and the failure is surfaced.
- If the backend accepts an idempotent operation, the queue advances.
- Duplicate keys are ignored: the frontend checks the queue file and the set of
  in-flight requests before submitting.

The Null backend is a valid explicit headless/test/product composition. It reports all
remote services unavailable with `NullProviderSelected`. Every read and write fails
admission with `platform.provider.null`; it creates no request, offline queue entry,
idempotency record or provider mutation. In particular, it never accepts or silently
discards achievements, stats, scores, cloud writes or presence. Optional
application intent may be explicitly suppressed before submission, but that is not a
successful Null operation. Tests needing success use the capability-accurate mock.

## Authentication And Session Boundary

Platform services do not own the game user account. They expose a platform user
handle that the game identity layer can map to its own player profile.

ADR-113 requires that mapping to produce a private provider-scoped
`LocalUserStorageId` and then select a product-owned `GameProfileId`. Raw platform
handles, gamertags, emails and display names are not save-directory or cloud-slot
identity. Backends explicitly report single-local-user, current-user-only or
multi-user capability; unsupported switching returns
`platform.capability.unavailable` rather than sharing a guessed user namespace.

```cpp
struct PlatformSessionState {
    bool signedIn;
    PlatformUserHandle currentUser;
};
```

The backend reports session changes through an observer interface:

```cpp
class IPlatformSessionObserver {
public:
    virtual void OnSessionStateChanged(const PlatformSessionState& state) = 0;
};
```

The frontend dispatches these changes to gameplay systems that care about sign
in/out. Cloud save sync and offline queue replay are triggered by sign-in
events.

## Shutdown And Late Completion

Shutdown closes frontend admission and increments generation before requesting
policy-permitted cancellation. It then drains accepted owner-lane evidence, commits at
most one terminal result per admitted request, stops subscriptions/executor delivery
and waits within the declared bound for provider operation and callback epochs. Safe
retirement, not terminal publication or dropped handles, permits provider/package and
borrowed-buffer release.

If native work does not retire within the bound, shutdown reports typed failure and
retains the required dependencies rather than unloading provider code early. Late
callbacks carry the closed provider/session/frontend generation, may complete private
retirement bookkeeping and cannot publish a new terminal result or invoke user code.
Shutdown is idempotent after partial initialization and repeated calls return the
recorded outcome without duplicating cancellation or native teardown.

## Security And Trust

- Closed platform SDK implementations live in private repositories. The public
  engine repository contains only interfaces, registries, the null backend, and
  the mock backend.
- Achievements and stats are unlocked/submitted through gameplay systems. For
  competitive or online games, critical stats should be validated by the game
  server before the client calls the platform backend, or the backend should be
  invoked from a trusted server path. See
  [Release Security](../release/release-security.md) for the broader trust and
  signing model that governs backend packages.
- Cloud save data is opaque untrusted input to the platform service layer. Transport
  authentication does not establish archive trust. Integrity, signature/scope,
  compatibility and optional confidentiality policy belong to Runtime Save and the
  host-selected save security profile under ADR-116.
- Provider tokens and account credentials remain inside the backend's short-lived
  credential lease. They never enter archive bytes, coordinator journals, tool
  results, logs or conflict metadata.
- Friends and presence data are subject to platform privacy policies and user
  consent. The frontend does not cache or expose this data beyond what the
  backend provides.

## Privacy And Compliance

- Friends and presence require explicit user consent where the platform
  requires it.
- Presence free text must not include PII or user-generated content that has
  not been reviewed.
- Cloud save retention follows platform policy; the engine does not control it.
- Children's accounts and restricted platforms may disable social features.
  The frontend surfaces this as normalized `Forbidden` provider failure or
  `platform.capability.unavailable`, according to whether a supported operation was
  denied dynamically or the capability was excluded at composition.

## Observability

Platform services emit bounded metrics:

```text
platform.request.pending_count           -- in-flight requests
platform.request.completed_count         -- completed by service and backend
platform.request.failed_count            -- by error category
platform.request.latency_ms              -- end-to-end latency
platform.request.offline_queued_count    -- operations deferred offline
platform.session.signed_in               -- 0/1 gauge
platform.capability.available            -- gauge per service per backend
```

No platform SDK logging or network callbacks run on the audio or render
threads.

## Editor And Runtime UI Surfaces

Platform services need authoring surfaces for project-level configuration and
small runtime/debugging panels for observing session and request state. Each
surface below includes an ASCII layout before implementation begins.

### Project Settings > Platform Services

Access: `Edit > Project Settings > Platform Services`

```text
+--------------------------------------------------------------+
| Project Settings                                      [Save] |
+--------------------------------------------------------------+
| General | Audio | Rendering | Physics | Platform Services |   |
+--------------------------------------------------------------+
| Active Backend                                               |
|   [ Steamworks                                     v ]       |
|                                                              |
| Achievements                                                 |
|   ID      Name                  Hidden  Progress Steps       |
|   --      ----                  ------  --------------       |
|   1       first_blood           [ ]     1                     |
|   2       kill_streak_10        [x]     10                    |
|   [+ Add]                                                    |
|                                                              |
| Leaderboards                                                 |
|   ID      Name                  Sort        Friends Only     |
|   10      high_score            Descending  [x]               |
|   [+ Add]                                                    |
|                                                              |
| Cloud Save                                                   |
|   Conflict resolution: [ Most Recent               v ]       |
|   [x] Persist offline queue across sessions                  |
|                                                              |
| Timeouts And Retry                                           |
|   Default timeout:  [ 30 ] seconds                           |
|   Retry attempts:   [ 3  ]                                   |
+--------------------------------------------------------------+
```

### Platform Diagnostics Panel

Access: `Window > Platform Diagnostics`

```text
+--------------------------------------------------------------+
| Platform Diagnostics                                   [x]   |
+--------------------------------------------------------------+
| Session: Signed In                                           |
| User:    PlayerOne#1234                                      |
| Backend: SteamBackend                                        |
+--------------------------------------------------------------+
| Service        | Available | Pending | Errors | Avg Latency |
|----------------|-----------|---------|------|-------------|
| Achievements   |    [x]    |    0    |   0  |    45 ms    |
| Leaderboards   |    [x]    |    1    |   0  |   120 ms    |
| Cloud Save     |    [x]    |    0    |   0  |   210 ms    |
| Presence       |    [ ]    |    0    |   0  |      -      |
| Friends        |    [x]    |    0    |   0  |    30 ms    |
+--------------------------------------------------------------+
| Offline Queue: 2 pending  |  Oldest: 4m 12s                |
+--------------------------------------------------------------+
```

### Social / Friends Panel (Optional)

Most friend management is platform-owned. Horo may expose a read-only runtime
overlay or editor debug panel.

```text
+--------------------------------------------------------------+
| Friends                                                [x]   |
+--------------------------------------------------------------+
| [ Online ] [ All ]                                           |
+--------------------------------------------------------------+
| [o] Alice        In Main Menu                                |
| [o] Bob          Playing Level 3                             |
| [ ] Carol        Offline                                     |
+--------------------------------------------------------------+
```

### Required Surface Checklist

| Surface | UI Placement | Access Pattern |
|---|---|---|
| Project Settings > Platform Services | Persistent settings panel | Menu: Edit → Project Settings → Platform Services |
| Achievements configuration | Persistent settings sub-panel | Inside Project Settings > Platform Services |
| Leaderboards configuration | Persistent settings sub-panel | Inside Project Settings > Platform Services |
| Cloud Save settings | Persistent settings sub-panel | Inside Project Settings > Platform Services |
| Presence status mapping | Persistent settings sub-panel | Inside Project Settings > Platform Services |
| Platform Diagnostics | Persistent panel | Menu: Window → Platform Diagnostics |
| Social/Friends panel | Runtime overlay or editor debug panel | Optional; most friend management is platform-owned |

Configuration authority lives in Project Settings:

- active platform backend selection
- achievement, leaderboard, stat, and presence stable ID tables
- cloud save capability, timeout and retry policy; conflict resolution authority is
  owned by the save/cloud coordinator
- per-service timeout and retry policy
- offline queue persistence settings

The Platform Diagnostics panel shows runtime state:

- signed-in user and session state
- backend capability availability (per service)
- pending request count and recent errors by category
- offline queue depth and oldest pending operation
- per-service latency and success/failure rates

Most platform-owned flows (friend invites, achievement toast notifications,
rich presence detail rendering) are handled by the platform overlay or OS, not
by Horo UI. Horo only authors the configuration and observes the state.

## Testing

Required tests cover:

- admission failure creates no request/provider call/callback, while dropping every
  handle/subscription leaves an admitted operation owned until terminal retirement
- every legal and illegal request-state transition, with terminal state/result atomic
  and exactly once under completion/cancel/timeout/shutdown races
- callbacks are never inline or under frontend/provider locks; late subscription is
  deferred, token destruction suppresses delivery and re-entry cannot recursively
  drain completion
- timeout uses one monotonic admission deadline across retries, wins only when no
  eligible pre-deadline completion exists and remains terminal after late provider work
- Null rejects every read and write with `platform.provider.null` and creates no
  request, offline entry or idempotency record
- mock backend replays scripted responses
- frontend routes calls to the correct backend service
- offline queue persists and replays in order
- request cancellation does not leak state
- stable ID registries reject unregistered names
- typed cloud-object addressing, provider revision propagation and no timestamp-based
  automatic winner selection
- conditional write/delete and precondition failure; uncoordinated providers never
  receive background mutations
- cloud archive writes/deletes never enter the generic offline queue
- authenticated-provider downloads remain untrusted until local bounded admission;
  malformed/oversized results cannot publish or replace a local generation
- cloud credential/provider metadata is redacted from save diagnostics and tools
- session sign-in/out triggers queue replay and state callbacks
- typed capability snapshot and private binding agree; missing/duplicate/mismatched
  bindings fail composition and unavailable services return
  `platform.capability.unavailable`
- Null, capability absence, cancellation, timeout and provider failure have distinct
  stable codes; provider categories/native evidence never replace frontend identity
- presence updates coalesce to the most recent state

### Mock Backend

The mock backend is a core test helper that exposes the same
`IPlatformServicesBackend` interface as real platform backends. It accepts a
scripted response table instead of calling an SDK:

```cpp
struct MockResponse {
    std::optional<Error> error;
    std::variant</* response types */> payload;
    std::chrono::milliseconds delay{0};
};

class MockPlatformServicesBackend : public IPlatformServicesBackend {
public:
    void SetResponse(ServiceType service,
                     OperationType operation,
                     MockResponse response);

    void ExpectSequence(std::vector<ExpectedCall> calls);
    bool AllExpectationsMet() const;
};
```

Tests use the mock backend to verify:

- correct request routing and error translation
- timeout handling when `delay` exceeds the policy
- offline queue behavior when the backend returns normalized `Offline`
- idempotency key stability across retries and replay
- stat coalescing and leaderboard debounce logic

Platform-specific backend tests live in the private platform repositories.

## Related Documents

- [ADR-130](../../adr/130-platform-services-frontend-request-lifetime-timeout-null-and-error-semantics.md):
  frontend request ownership, lifecycle, callback, capability, Null and error baseline.
- [Platform Services Config UI Reference](./platform-services-config.html): achievements, leaderboards, cloud saves, presence, and platform adapters panel.

- [Audio Architecture](./audio-architecture.md)
- [Input Architecture](./input-architecture.md)
- [Platform Abstraction Architecture](../foundation/platform-abstraction.md)
- [ADR-113](../../adr/113-local-storage-user-profile-and-slot-ownership.md): product,
  user/profile and logical-slot addressing across local and cloud boundaries.
- [ADR-115](../../adr/115-cloud-save-authority-revision-and-conflict-policy.md): local
  authority, provider preconditions, offline journal and conflict preservation.
- [ADR-116](../../adr/116-save-data-threat-model-and-trust-policy.md): untrusted cloud
  bytes, bounded local admission, credential isolation and save tool policy.
- [Extension System](../extensions/plugin-system.md)
- [Release Security](../release/release-security.md)
