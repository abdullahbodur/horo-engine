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
class PlatformServiceRequest {
public:
    RequestId Id() const;
    RequestState State() const;
    bool IsPending() const;
    bool IsComplete() const;
    bool WasCancelled() const;

    void OnComplete(std::function<void(Result<T>)> callback);
    void Cancel();
};

class PlatformServicesFrontend {
public:
    PlatformServiceRequest<AchievementUnlockResult>
    UnlockAchievement(AchievementId id);

    PlatformServiceRequest<LeaderboardEntries>
    GetLeaderboardEntries(LeaderboardId id, const LeaderboardQuery& query);

    PlatformServiceRequest<CloudSaveMetadata>
    ReadCloudSave(SaveSlotId slot);

    PlatformServiceRequest<void>
    WriteCloudSave(SaveSlotId slot, std::span<const std::byte> data);

    PlatformServiceRequest<void>
    SetPresence(const PresenceState& state);
};
```

### Request Lifetime

1. Caller invokes a frontend method and receives a `PlatformServiceRequest`.
2. The frontend assigns a unique `RequestId`, validates inputs, and routes the
   call to the active backend service.
3. The backend performs the platform call asynchronously. It may complete
   immediately, queue work on an SDK thread, or schedule a network operation.
4. When the backend reports completion, the frontend transitions the request to
   `Complete` and dispatches the caller's callback on an engine job or main
   thread.
5. The request handle remains valid after completion; `OnComplete` may be
   invoked synchronously if the request has already finished.

### Cancellation

`Cancel()` asks the frontend to abort the operation. Cancellation is best-effort:

- If the request has not left the frontend queue, it is removed and the caller
  receives `request_cancelled`.
- If the backend is already executing, the frontend forwards the cancellation.
  The backend may ignore it if the SDK does not support cancellation.
- A completed request cannot be cancelled retroactively.

Multiple `Cancel()` calls are idempotent. Only one final state is published.

### Threading

Completion callbacks run on an engine-controlled thread, never on a platform
SDK thread. Backends must not call frontend completion handlers directly; they
notify the frontend through an internal completion queue that the frontend
drains on an appropriate engine thread.

Gameplay code may call frontend methods from any thread, but request handles
and callbacks should be owned by systems that understand the engine's threading
rules. The frontend itself is thread-safe.

### Timeouts, Retry, And Throttling

The frontend applies a per-service timeout policy. If a backend does not
complete a request in time, the frontend treats it as `platform_error` and
cancels the underlying operation.

Retry is controlled by the frontend, not by gameplay code:

- Idempotent writes such as stat updates may be retried a bounded number of
  times before surfacing an error.
- Reads and queries are not retried automatically unless the project policy
  explicitly allows it.
- When the backend reports `offline` or `not_signed_in`, retriable writes move
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

Requests return typed `Result<T>`. Errors are normalized so gameplay code does
not need platform-specific error handling:

```text
offline           -- no network or platform session
not_signed_in     -- no authenticated user
forbidden         -- platform policy or user consent blocks the call
not_supported     -- the active backend does not implement this service
rate_limited      -- call must be retried later
platform_error    -- backend-specific error, diagnostic detail available
request_cancelled -- caller cancelled
timeout           -- frontend timeout before backend completion
```

### Ordering And Coalescing

Independent requests may complete out of order. Dependent operations must be
chained explicitly by the caller:

```cpp
frontend.ReadCloudSave(slot)
    .OnComplete([&](Result<CloudSaveMetadata> meta) {
        if (meta) {
            frontend.WriteCloudSave(slot, newData);
        }
    });
```

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
    virtual PlatformServiceRequest<AchievementUnlockResult>
    Unlock(AchievementId id) = 0;

    virtual PlatformServiceRequest<AchievementProgressResult>
    SetProgress(AchievementId id, uint32_t current, uint32_t total) = 0;

    virtual PlatformServiceRequest<std::vector<AchievementState>>
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
    virtual PlatformServiceRequest<void>
    SubmitScore(LeaderboardId id,
                int64_t score,
                IdempotencyKey key) = 0;

    virtual PlatformServiceRequest<LeaderboardEntries>
    GetEntries(LeaderboardId id, const LeaderboardQuery& query) = 0;

    virtual PlatformServiceRequest<void>
    SetStat(StatName name, int64_t value) = 0;

    virtual PlatformServiceRequest<std::optional<int64_t>>
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
struct SaveSlotId { StableString value; };

struct CloudSaveMetadata {
    SaveSlotId slot;
    Timestamp modifiedTime;
    uint64_t sizeBytes;
};

class ICloudSaveService {
public:
    virtual PlatformServiceRequest<CloudSaveMetadata>
    List() = 0;

    virtual PlatformServiceRequest<std::vector<std::byte>>
    Read(SaveSlotId slot) = 0;

    virtual PlatformServiceRequest<void>
    Write(SaveSlotId slot, std::span<const std::byte> data) = 0;

    virtual PlatformServiceRequest<void>
    Delete(SaveSlotId slot) = 0;
};
```

Conflict resolution is configured per project:

```text
prefer_local   -- local save wins, upload overwrites cloud
prefer_cloud   -- cloud save wins, download overwrites local
most_recent    -- compare timestamps, newer wins
manual         -- prompt the player (UI owns the choice)
```

The engine calls `Read` at game start and `Write` after the local save system
commits a new save. Cloud save never directly edits the local save format.

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
    virtual PlatformServiceRequest<void>
    SetPresence(const PresenceState& state) = 0;

    virtual PlatformServiceRequest<void>
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
    virtual PlatformServiceRequest<std::vector<PlatformUserHandle>>
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
struct PlatformServiceCapabilities {
    bool achievements;
    bool leaderboards;
    bool cloudSave;
    bool presence;
    bool friends;
};

class IPlatformServicesBackend {
public:
    virtual ~IPlatformServicesBackend() = default;

    virtual PlatformServiceCapabilities Capabilities() const = 0;

    virtual void Initialize(const PlatformServicesConfig& config) = 0;
    virtual void Shutdown() = 0;

    virtual IAchievementService* Achievements() = 0;
    virtual ILeaderboardService* Leaderboards() = 0;
    virtual ICloudSaveService* CloudSave() = 0;
    virtual IPresenceService* Presence() = 0;
    virtual IFriendsService* Friends() = 0;
};
```

Backends return `nullptr` for services they do not implement. The frontend
returns `not_supported` for calls to unavailable services.

## Offline And Degraded Behavior

The frontend maintains a persistent local queue for operations that can be
safely retried:

- achievement unlocks
- achievement progress updates
- leaderboard score submissions
- stat writes
- cloud save writes

Each queued operation carries an `IdempotencyKey`. The key is generated once
when the operation enters the frontend and is preserved across persistence,
retry, and replay. This prevents a retry + queue replay combination from
producing duplicate backend effects.

When the backend reports `offline` or `not_signed_in`, the frontend:

1. assigns an idempotency key if the operation does not already have one
2. persists the operation to the local queue
3. deduplicates against the queue using the operation type, target ID, and key
4. emits an `offline_queued` metric
5. returns a result indicating queued state
6. replays the queue in order when the platform session becomes active

Operations that cannot be safely replayed, such as cloud save reads or
leaderboard queries, fail immediately with the appropriate error.

Replay rules:

- Queue order is preserved per user account.
- If a queued operation fails with a non-retriable error (for example,
  `forbidden`), it is removed from the queue and the failure is surfaced.
- If the backend accepts an idempotent operation, the queue advances.
- Duplicate keys are ignored: the frontend checks the queue file and the set of
  in-flight requests before submitting.

The null backend is always available. It:

- reports no capabilities
- returns `not_supported` for all reads
- accepts and silently discards all writes after logging a metric
- never leaves the local queue in an inconsistent state

## Authentication And Session Boundary

Platform services do not own the game user account. They expose a platform user
handle that the game identity layer can map to its own player profile.

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
- Cloud save data is opaque to the platform service layer. Encryption and
  tamper detection are the responsibility of the local save system.
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
  The frontend surfaces this as `forbidden` or `not_supported`.

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
- cloud save conflict resolution policy
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

- null backend accepts writes without crashing
- mock backend replays scripted responses
- frontend routes calls to the correct backend service
- offline queue persists and replays in order
- request cancellation does not leak state
- stable ID registries reject unregistered names
- cloud save conflict resolution strategies
- session sign-in/out triggers queue replay and state callbacks
- unavailable services return `not_supported`
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
- offline queue behavior when the backend returns `offline`
- idempotency key stability across retries and replay
- stat coalescing and leaderboard debounce logic

Platform-specific backend tests live in the private platform repositories.

## Related Documents

- [Platform Services Config UI Reference](./platform-services-config.html): achievements, leaderboards, cloud saves, presence, and platform adapters panel.

- [Audio Architecture](./audio-architecture.md)
- [Input Architecture](./input-architecture.md)
- [Platform Abstraction Architecture](../foundation/platform-abstraction.md)
- [Extension System](../extensions/plugin-system.md)
- [Release Security](../release/release-security.md)
